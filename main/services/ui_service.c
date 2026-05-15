// services/ui_service.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
//
#include "system_config.h"
#include "ui_service.h"

 //应用注册头文件
#include "desktop_app.h"     
#include "radio_app.h"

static const char *TAG = "ui_service";
static int ui_service_ID = 0;
static QueueHandle_t ui_service_request_queue;
static ui_app_t *current_app = NULL;  // 活动的app编号，用于事件转发

// app栈(存活的app)
#define MAX_APP_STACK 10
static ui_app_t *app_stack[MAX_APP_STACK];
static int stack_top = -1;

// app数量
#define MAX_APPS 20 
static ui_app_t *app_list[MAX_APPS];
static int app_count = 0;

// 界面定时更新
static esp_timer_handle_t lvgl_tick_timer = NULL;

// 内部函数声明
static void ui_service_task(void *arg);
static void handle_key_event(key_event_data_t *data);
static void default_app_key_handler(key_event_data_t *data);
static void ui_show_screen(ui_app_t *app);
static ui_app_t *find_app(const char *name);
static void lvgl_tick_callback(void *arg);  // 界面定时更新回调
static void ui_show_booting(const char* path);   // 开机画面

// =============== API 实现 ===============
esp_err_t ui_service_init(void)
{
    // 1. 初始化显示和触摸硬件
    lvgl_hal_init();

    // 2. 创建 LVGL 计时器
    const esp_timer_create_args_t tick_timer_args = {
        .callback = &lvgl_tick_callback,
        .name = "lvgl_tick"
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 5000));

    // 3. 创建 UI 请求队列
    ui_service_request_queue = xQueueCreate(16, sizeof(void *));
    if (!ui_service_request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return ESP_ERR_NO_MEM;
    }

    // 4. 注册服务
    ui_service_ID = event_loop_register_service("ui_service", ui_service_request_queue);
    if (ui_service_ID == 0) {
        ESP_LOGE(TAG, "Failed to register service");
        vQueueDelete(ui_service_request_queue);
        return ESP_FAIL;
    }

    // 5. 注册应用
    desktop_app_register(); // 桌面
    radio_app_register();

    // 6. 创建 UI 服务任务
    BaseType_t task_ret = xTaskCreate(ui_service_task, "ui_service_task", 8192, NULL, 4, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_ERR_NO_MEM;
    }

    // 7. 显示开机动画
    // ui_show_booting("S:/system/icon/icon_booting1.gif");

    // 8. 发送打开桌面的命令
    // vTaskDelay(1000 / portTICK_PERIOD_MS);
    ui_service_receive_data_t *open_evt = malloc(sizeof(ui_service_receive_data_t));
    if (open_evt) {
        open_evt->cmd = UI_CMD_OPEN_APP;
        open_evt->reply_queue = NULL;
        open_evt->data = strdup("desktop_app");
        open_evt->data_len = strlen("desktop_app") + 1;
        xQueueSend(ui_service_request_queue, &open_evt, 0);
    }

    ESP_LOGI(TAG, "UI Service initialized with ID %d", ui_service_ID);
    return ESP_OK;
}


// 获取服务注册的ID
int get_ui_service_ID(void) {
    return ui_service_ID;
}

// 注册函数
void ui_service_register_app(ui_app_t *app)
{
    if (!app || !app->name) return;
    if (app_count >= MAX_APPS) {
        ESP_LOGE(TAG, "App registry full!");
        return;
    }
    if (find_app(app->name)) {
        ESP_LOGW(TAG, "App '%s' already registered", app->name);
        return;
    }
    app_list[app_count++] = app;
    ESP_LOGI(TAG, "Registered app: %s", app->name);
}

// 
void ui_service_open_app(const char *name)
{
    ui_app_t *app = find_app(name);
    if (!app) {
        ESP_LOGE(TAG, "App not found: %s", name);
        return;
    }
    if (!app->screen) {
        if (app->on_create) {
            app->on_create(app);
        }
        if (!app->screen) {
            // 应用未创建屏幕，自动创建默认屏幕
            app->screen = lv_obj_create(NULL);
            if (app->screen == NULL) {
                ESP_LOGE(TAG, "Failed to create screen for app %s", name);
                return;
            }
            lv_obj_t *label = lv_label_create(app->screen);
            lv_label_set_text(label, app->name);
            lv_obj_center(label);
        }
    }
    if (current_app && current_app != app && stack_top < MAX_APP_STACK - 1) {
        app_stack[++stack_top] = current_app;
    }
    if (current_app && current_app != app && current_app->on_pause) {
        current_app->on_pause(current_app);
    }
    ui_show_screen(app);
    if (app->on_resume) {
        app->on_resume(app);
    }
    current_app = app;
    ESP_LOGI(TAG, "Opened app: %s", name);
}

void ui_service_go_back(void)
{
    if (stack_top >= 0) {
        ui_app_t *prev = app_stack[stack_top--];
        if (current_app && current_app->on_pause) {
            current_app->on_pause(current_app);
        }
        ui_show_screen(prev);
        if (prev->on_resume) {
            prev->on_resume(prev);
        }
        current_app = prev;
        ESP_LOGI(TAG, "Back to app: %s", prev->name);
    } else {
        ESP_LOGI(TAG, "Already at root, cannot go back");
    }
}

// 回到desktop
void ui_service_go_home(void)
{
    while (stack_top >= 0) {
        ui_app_t *app = app_stack[stack_top--];
        if (app->on_destroy) {
            app->on_destroy(app);
        }
    }
    ui_app_t *home = find_app("desktop_app");
    if (home) {
        if (current_app && current_app != home && current_app->on_pause) {
            current_app->on_pause(current_app);
        }
        ui_show_screen(home);
        if (home->on_resume) {
            home->on_resume(home);
        }
        current_app = home;
    }
}

QueueHandle_t get_ui_service_queue(void) {
    return ui_service_request_queue;
}

// =============== 任务与 LVGL 驱动 ===============
static void lvgl_tick_callback(void *arg)
{
    lv_tick_inc(5);
}

// 等待接收事件
static void ui_service_task(void *arg)
{
    const TickType_t lvgl_period = pdMS_TO_TICKS(5);
    void *received;

    while (1) {
        if (xQueueReceive(ui_service_request_queue, &received, lvgl_period) == pdTRUE) {

            ui_service_receive_data_t *payload = (ui_service_receive_data_t *)received;
            // 处理事件
            switch (payload->cmd) {
                // 按键事件
                case UI_CMD_KEY_EVENT: {
                    // 提取按键事件有效
                    ESP_LOGI(TAG,"rec event from key_hal");
                    key_event_data_t *key = (key_event_data_t *)payload->data;
                    if (key) {
                        handle_key_event(key);
                        free(key);
                    }
                    break;
                }
                // 打开应用
                case UI_CMD_OPEN_APP: {
                    const char *app_name = (const char *)payload->data;
                    if (app_name) {
                        ui_service_open_app(app_name);
                        free(payload->data);
                    }
                    break;
                }
                // 返回
                case UI_CMD_GO_BACK:
                    ui_service_go_back();
                    break;
                case UI_CMD_TO_HOME:
                    ui_service_go_home();
                    break;
                case UI_CMD_UPDATE_WIDGET:
                default:
                   // 转发给当前应用处理
                    if (current_app && current_app->on_receive_data) {
                        current_app->on_receive_data(current_app, payload);
                    }
                    if (payload->data) free(payload->data);
                    break;
            }            

            free(payload);
        }
        lv_timer_handler();
        vTaskDelay(1);
    }
}

// =============== 按键处理 ===============
static void handle_key_event(key_event_data_t *data)
{
    if (current_app && current_app->on_key_event) {
        if (current_app->on_key_event(current_app, data)) {
            return;
        }
    }
    default_app_key_handler(data);
}

static void default_app_key_handler(key_event_data_t *data)
{
    switch (data->event) {
        case KEY_EVENT_PRESS:
            if (data->key_id == KEY_ID_BACK) {
                ui_service_go_back();
            } else if (data->key_id == KEY_ID_ENTER) {
                // 默认回车无操作，由应用处理
            }
            break;
        default:
            break;
    }
}

// =============== 界面切换辅助 ===============
static void ui_show_screen(ui_app_t *app)
{
    if (!app || !app->screen) return;
    if (current_app && current_app->screen && current_app->screen != app->screen) {
        lv_obj_add_flag(current_app->screen, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(app->screen, LV_OBJ_FLAG_HIDDEN);
    lv_scr_load_anim(app->screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

static ui_app_t *find_app(const char *name)
{
    for (int i = 0; i < app_count; i++) {
        if (strcmp(app_list[i]->name, name) == 0) {
            return app_list[i];
        }
    }
    return NULL;
}

// =============== 开机画面 ===============
static lv_obj_t* gif_obj = NULL;

static void ui_show_booting(const char* file_path) {
    if (!file_path) {
        ESP_LOGE(TAG, "Invalid file path (NULL)");
        return;
    }

    sd_hal_list_content("/sdcard/system/icon");

    gif_obj = lv_gif_create(lv_scr_act());
    if (!gif_obj) {
        ESP_LOGE(TAG, "Failed to create lv_gif object");
        return;
    }

    // lv_gif_set_src(gif_obj, file_path);
    lv_obj_center(gif_obj);
}