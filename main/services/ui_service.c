#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"

#include "system_config.h"
#include "system_event.h"
#include "ui_service.h"
#include "sntp_service.h"
#include "lvgl_hal.h"
#include "keys_hal.h"

// 应用注册头文件（根据实际项目添加）
#include "desktop_app.h"
#include "radio_app.h"

static const char *TAG = "ui_service";

// UI 服务自身的请求队列
static QueueHandle_t ui_service_request_queue = NULL;

// 当前前台应用（无任何常驻，关闭即销毁）
static ui_app_t *current_app = NULL;

// 上一次从桌面启动的应用名（桌面应用通过 ui_service_get_last_launched_app 获取）
static char *last_launched_app = NULL;

// 应用注册表
#define MAX_APPS 20
static ui_app_t *app_list[MAX_APPS];
static int app_count = 0;

// LVGL 定时器
static esp_timer_handle_t lvgl_tick_timer = NULL;

// ---------- 内部函数声明 ----------
static void ui_service_task(void *arg);
static void ui_service_process_request(event_data_t *evt);
static ui_app_t *find_app(const char *name);
static void lvgl_tick_callback(void *arg);

// =============== API 实现 ===============
esp_err_t ui_service_init(void)
{
    // 1. 初始化 LVGL 硬件
    lvgl_hal_init();
    key_hal_init();
    
    // 2. 创建 LVGL 心跳定时器（每 5ms）
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

    // 4. 注册应用
    desktop_app_register();
    radio_app_register();

    // 5. 创建 UI 服务任务
    BaseType_t ret = xTaskCreate(ui_service_task, "ui_service_task", 8192, NULL, 4, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_ERR_NO_MEM;
    }

    // 6. 发送打开桌面的命令（启动时默认进入桌面）
    ui_service_receive_data_t *open_evt = malloc(sizeof(ui_service_receive_data_t));
    if (open_evt) {
        open_evt->cmd = UI_CMD_OPEN_APP;
        open_evt->data = strdup("desktop_app");
        open_evt->data_len = strlen("desktop_app") + 1;

        event_data_t *evt = malloc(sizeof(event_data_t));
        if (evt) {
            evt->service_id = UI_SERVICE;
            evt->event_type = REQUEST;
            evt->reply_queue = NULL;
            evt->data = open_evt;
            evt->data_len = sizeof(ui_service_receive_data_t);

            ESP_LOGI(TAG, "Request event sent: open desktop_app");
            xQueueSend(ui_service_request_queue, &evt, 0);
        } else {
            free(open_evt->data);
            free(open_evt);
        }
    }

    ESP_LOGI(TAG, "UI Service initialized");
    return ESP_OK;
}

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

void ui_service_open_app(const char *name)
{
    ui_app_t *app = find_app(name);
    if (!app) {
        ESP_LOGE(TAG, "App not found: %s", name);
        return;
    }

    if (current_app == app) {
        ESP_LOGI(TAG, "App '%s' already in foreground", name);
        return;
    }

    // 1. 准备目标应用屏幕（若未创建则调用 on_create）
    if (!app->screen) {
        if (app->on_create) {
            app->on_create(app);
        }
        if (!app->screen) {
            ESP_LOGE(TAG, "App '%s' did not create a screen in on_create", name);
            return;
        }
    }

    // 2. 通知目标应用即将打开（在屏幕切换之前，以便应用准备数据）
    if (app->on_open) {
        app->on_open(app);
    }

    // 3. 先切换屏幕（直接加载，不带动画，避免引用旧屏幕）
    lv_scr_load(app->screen);

    // 4. 更新最后启动的应用记录（桌面不计入）
    if (strcmp(name, "desktop_app") != 0) {
        free(last_launched_app);
        last_launched_app = strdup(name);
        ESP_LOGI(TAG, "Last launched app updated: %s", name);
    }

    // 5. 关闭并销毁旧应用（此时旧屏幕已不是活动屏幕，可安全删除）
    ui_app_t *old_app = current_app;
    if (old_app) {
        if (old_app->on_close) {
            old_app->on_close(old_app);
        }
        if (old_app->on_destroy) {
            old_app->on_destroy(old_app);
        }
        old_app->screen = NULL;   // 强制置空，避免应用遗忘
    }

    // 6. 更新当前应用指针
    current_app = app;
    ESP_LOGI(TAG, "Opened app: %s", name);
}

void ui_service_go_home(void)
{
    ui_service_open_app("desktop_app");
}

const char* ui_service_get_last_launched_app(void)
{
    return last_launched_app;
}

QueueHandle_t get_ui_service_queue(void)
{
    return ui_service_request_queue;
}

// =============== UI 服务任务 ===============
static void lvgl_tick_callback(void *arg)
{
    lv_tick_inc(5);
}

static void ui_service_task(void *arg)
{
    const TickType_t lvgl_period = pdMS_TO_TICKS(5);
    event_data_t *evt = NULL;

    while (1) {
        if (xQueueReceive(ui_service_request_queue, &evt, lvgl_period) == pdTRUE) {
            if (evt->event_type == REQUEST) {
                // 处理 UI 服务自身的请求命令
                ui_service_process_request(evt);
            } else if (evt->event_type == NOTIFICATION) {
                // 所有通知（包括按键）原样转发给当前应用
                if (current_app && current_app->on_event) {
                    current_app->on_event(current_app, evt);
                } else {
                    // 无应用处理，释放资源
                    if (evt->data) free(evt->data);
                    free(evt);
                }
            } else {
                ESP_LOGE(TAG, "Unknown event type: %d", evt->event_type);
                if (evt->data) free(evt->data);
                free(evt);
            }
        }
        lv_timer_handler();
        vTaskDelay(1);
    }
}

// 处理请求命令（UI 服务自身负责的命令）
static void ui_service_process_request(event_data_t *evt)
{
    ui_service_receive_data_t *payload = (ui_service_receive_data_t *)evt->data;
    if (!payload) {
        ESP_LOGE(TAG, "Null payload in request");
        free(evt);
        return;
    }

    bool handled = true;  // 标记是否由 UI 服务直接处理
    switch (payload->cmd) {
        case UI_CMD_OPEN_APP: {
            const char *app_name = (const char *)payload->data;
            if (app_name) {
                ui_service_open_app(app_name);
            }
            break;
        }
        case UI_CMD_GO_HOME:
            ui_service_go_home();
            break;
        default:
            // 未识别的命令，转交给当前应用处理
            handled = false;
            break;
    }

    if (handled) {
        // 服务已处理，释放 payload 及外壳
        free(payload->data);
        free(payload);
        free(evt);
    } else {
        // 转发给当前应用，所有权转移
        if (current_app && current_app->on_event) {
            current_app->on_event(current_app, evt);
        } else {
            // 无应用接收，释放资源
            free(payload->data);
            free(payload);
            free(evt);
        }
    }
}

// =============== 辅助函数 ===============
static ui_app_t *find_app(const char *name)
{
    for (int i = 0; i < app_count; i++) {
        if (strcmp(app_list[i]->name, name) == 0) {
            return app_list[i];
        }
    }
    return NULL;
}


// // services/ui_service.c
// #include <stdio.h>
// #include <string.h>
// #include <stdlib.h>
// #include <sys/stat.h>
// #include <errno.h>
// #include <dirent.h>
// #include "esp_err.h"
// #include "esp_log.h"
// #include "esp_timer.h"
// #include "esp_psram.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/queue.h"
// #include "lvgl.h"
// //
// #include "system_config.h"
// #include "system_event.h"
// #include "ui_service.h"
// #include "sntp_service.h"
// #include "keys_hal.h"
// #include "lvgl_hal.h"

//  //应用注册头文件
// #include "desktop_app.h"     
// #include "radio_app.h"

// static const char *TAG = "ui_service";
// static QueueHandle_t ui_service_request_queue;
// static ui_app_t *current_app = NULL;  // 活动的app编号，用于事件转发

// // app栈(存活的app)
// #define MAX_APP_STACK 10
// static ui_app_t *app_stack[MAX_APP_STACK];
// static int stack_top = -1;

// // app数量
// #define MAX_APPS 20 
// static ui_app_t *app_list[MAX_APPS];
// static int app_count = 0;

// // 界面定时更新
// static esp_timer_handle_t lvgl_tick_timer = NULL;

// // 内部函数声明
// static void ui_service_task(void *arg);
// static void handle_key_event(key_event_data_t *data);
// static void default_app_key_handler(key_event_data_t *data);
// static void ui_show_screen(ui_app_t *app);
// static ui_app_t *find_app(const char *name);
// static void lvgl_tick_callback(void *arg);  // 界面定时更新回调
// static void ui_show_booting(const char* path);   // 开机画面
// static void ui_service_close_current_app(void);
// // =============== API 实现 ===============
// esp_err_t ui_service_init(void)
// {
//     // 1. 初始化显示和触摸硬件
//     lvgl_hal_init();
//     key_hal_init();

//     // 2. 创建 LVGL 计时器
//     const esp_timer_create_args_t tick_timer_args = {
//         .callback = &lvgl_tick_callback,
//         .name = "lvgl_tick"
//     };
//     ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &lvgl_tick_timer));
//     ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 5000));

//     // 3. 创建 UI 请求队列
//     ui_service_request_queue = xQueueCreate(16, sizeof(void *));
//     if (!ui_service_request_queue) {
//         ESP_LOGE(TAG, "Failed to create request queue");
//         return ESP_ERR_NO_MEM;
//     }


//     // 5. 注册应用
//     desktop_app_register(); // 桌面
//     radio_app_register();

//     // 6. 创建 UI 服务任务
//     BaseType_t task_ret = xTaskCreate(ui_service_task, "ui_service_task", 8192, NULL, 4, NULL);
//     if (task_ret != pdPASS) {
//         ESP_LOGE(TAG, "Failed to create UI task");
//         return ESP_ERR_NO_MEM;
//     }

//     // 7. 显示开机动画
//     // ui_show_booting("S:/system/icon/icon_booting1.gif");

//     // 8. 发送打开桌面的命令
//     ui_service_receive_data_t *open_evt = malloc(sizeof(ui_service_receive_data_t));
//     if (open_evt) {
//         open_evt->cmd = UI_CMD_OPEN_APP;
//         open_evt->data = strdup("desktop_app");
//         open_evt->data_len = strlen("desktop_app") + 1;

//         event_data_t *evt = malloc(sizeof(event_data_t));
//         if (evt) {
//             evt->service_id = UI_SERVICE;
//             evt->event_type = REQUEST;
//             evt->reply_queue = NULL;
//             evt->data = open_evt;           // 内含的负载
//             evt->data_len = sizeof(ui_service_receive_data_t);

//             ESP_LOGI(TAG, "Request event sent: open desktop_app");
//             xQueueSend(ui_service_request_queue, &evt, 0);   // 传递 event_data_t* 指针
//         } else {
//             free(open_evt->data);
//             free(open_evt);
//         }
//     }
//     ESP_LOGI(TAG, "UI Service initialized");
//     return ESP_OK;
// }


// // 注册函数
// void ui_service_register_app(ui_app_t *app)
// {
//     if (!app || !app->name) return;
//     if (app_count >= MAX_APPS) {
//         ESP_LOGE(TAG, "App registry full!");
//         return;
//     }
//     if (find_app(app->name)) {
//         ESP_LOGW(TAG, "App '%s' already registered", app->name);
//         return;
//     }
//     app_list[app_count++] = app;
//     ESP_LOGI(TAG, "Registered app: %s", app->name);
// }

// // 
// void ui_service_open_app(const char *name)
// {
//     ui_app_t *app = find_app(name);
//     if (!app) {
//         ESP_LOGE(TAG, "App not found: %s", name);
//         return;
//     }
//     if (!app->screen) {
//         if (app->on_create) {
//             app->on_create(app);
//         }
//         if (!app->screen) {
//             // 应用未创建屏幕，自动创建默认屏幕
//             app->screen = lv_obj_create(NULL);
//             if (app->screen == NULL) {
//                 ESP_LOGE(TAG, "Failed to create screen for app %s", name);
//                 return;
//             }
//             lv_obj_t *label = lv_label_create(app->screen);
//             lv_label_set_text(label, app->name);
//             lv_obj_center(label);
//         }
//     }
//     if (current_app && current_app != app && stack_top < MAX_APP_STACK - 1) {
//         app_stack[++stack_top] = current_app;
//     }
//     if (current_app && current_app != app && current_app->on_pause) {
//         current_app->on_pause(current_app);
//     }
//     ui_show_screen(app);
//     if (app->on_resume) {
//         app->on_resume(app);
//     }
//     current_app = app;
//     ESP_LOGI(TAG, "Opened app: %s", name);
// }

// void ui_service_go_back(void)
// {
//     if (stack_top >= 0) {
//         ui_app_t *prev = app_stack[stack_top--];
//         if (current_app && current_app->on_pause) {
//             current_app->on_pause(current_app);
//         }
//         ui_show_screen(prev);
//         if (prev->on_resume) {
//             prev->on_resume(prev);
//         }
//         current_app = prev;
//         ESP_LOGI(TAG, "Back to app: %s", prev->name);
//     } else {
//         ESP_LOGI(TAG, "Already at root, cannot go back");
//     }
// }

// // 回到desktop
// void ui_service_go_home(void)
// {
//     ESP_LOGI(TAG, "going home");
//     // 
//     while (stack_top >= 0) {
//         ui_app_t *app = app_stack[stack_top--];
//         if (app->on_destroy) {
//             app->on_destroy(app);
//         } else{
//             ESP_LOGW(TAG, "App %s has no on_destroy callback", app->name);
//         }
//     }
//     ui_app_t *home = find_app("desktop_app");
//     if (home) {
//         if (current_app && current_app != home && current_app->on_pause) {
//             current_app->on_pause(current_app);
//         }
//         ui_show_screen(home);
//         if (home->on_resume) {
//             home->on_resume(home);
//         }
//         current_app = home;
//     }
// }



// QueueHandle_t get_ui_service_queue(void) {
//     return ui_service_request_queue;
// }

// // =============== 任务与 LVGL 驱动 ===============
// static void lvgl_tick_callback(void *arg)
// {
//     lv_tick_inc(5);
// }

// // 等待接收事件
// static void ui_service_task(void *arg) {
//     const TickType_t lvgl_period = pdMS_TO_TICKS(5);
//     event_data_t *evt = NULL;

//     while (1) {
//         if (xQueueReceive(ui_service_request_queue, &evt, lvgl_period) == pdTRUE) {
//             // ========== 处理请求 ==========
//             if (evt->event_type == REQUEST) {
//                 ui_service_receive_data_t *payload = (ui_service_receive_data_t *)evt->data;
//                 if (!payload) {
//                     ESP_LOGE(TAG, "Null payload in request");
//                 } else {
//                     switch (payload->cmd) {
//                         case UI_CMD_OPEN_APP: {
//                             const char *app_name = (const char *)payload->data;
//                             if (app_name) {
//                                 ui_service_open_app(app_name);
//                             }
//                             // 释放 payload 中 data 和 payload 自身
//                             free(payload->data);
//                             free(payload);
//                             break;
//                         }
//                         case UI_CMD_GO_BACK:
//                             ui_service_go_back();
//                             free(payload->data); // data 可能为 NULL，free(NULL) 安全
//                             free(payload);
//                             break;
//                         case UI_CMD_TO_HOME:
//                             ui_service_go_home();
//                             free(payload->data);
//                             free(payload);
//                             break;
//                         case UI_CMD_CLOSE_APP:
//                             ui_service_close_current_app();
//                             free(payload->data);   // data 可能为 NULL
//                             free(payload);
//                             break;
//                         case UI_CMD_UPDATE_WIDGET:
//                         case UI_CMD_CUSTOM:
//                         default:
//                             // 转发给当前应用处理
//                             if (current_app && current_app->on_receive_data) {
//                                 current_app->on_receive_data(current_app, payload->cmd,
//                                                              payload->data, payload->data_len);
//                             }
//                             free(payload->data);
//                             free(payload);
//                             break;
//                     }
//                 }
//             }
//             // ========== 处理通知 ==========
//             else if (evt->event_type == NOTIFICATION) {
//                 switch (evt->service_id) {
//                     case KEYHAL_SERVICE: {
//                         // 处理按键事件
//                         ESP_LOGI(TAG, "Key event received");
//                         key_event_data_t *key_data = (key_event_data_t *)evt->data;
//                         if (key_data) {
//                             handle_key_event(key_data);
//                             free(key_data);
//                         }
//                         break;
//                     }
//                     default:
//                             // 转发给当前应用处理
//                             if (current_app && current_app->on_receive_data) {
//                                 current_app->on_receive_data(current_app, evt->service_id,
//                                                              evt->data, evt->data_len);
//                             }
//                             if (evt->data) free(evt->data);
//                             break;  
//                 }
//             } else {
//                 ESP_LOGE(TAG, "Unknown event type: %d", evt->event_type);
//                 if (evt->data) free(evt->data);
//             }
//             // 释放外壳
//             free(evt);
//         }
//         lv_timer_handler();
//         vTaskDelay(1);
//     }
// }

// // =============== 按键处理 ===============
// static void handle_key_event(key_event_data_t *data)
// {
//     if (current_app && current_app->on_key_event) {
//         if (current_app->on_key_event(current_app, data)) {
//             return;
//         }
//     }
//     default_app_key_handler(data);
// }

// static void default_app_key_handler(key_event_data_t *data)
// {
//     switch (data->event) {
//         case KEY_EVENT_PRESS:
//             if (data->key_id == KEY_ID_BACK) {
//                 ui_service_go_back();
//             } else if (data->key_id == KEY_ID_ENTER) {
//                 // 默认回车无操作，由应用处理
//             }
//             break;
//         default:
//             break;
//     }
// }

// // =============== 界面切换辅助 ===============
// static void ui_show_screen(ui_app_t *app)
// {
//     if (!app || !app->screen) return;
//     if (current_app && current_app->screen && current_app->screen != app->screen) {
//         lv_obj_add_flag(current_app->screen, LV_OBJ_FLAG_HIDDEN);
//     }
//     lv_obj_clear_flag(app->screen, LV_OBJ_FLAG_HIDDEN);
//     lv_scr_load_anim(app->screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
// }

// static ui_app_t *find_app(const char *name)
// {
//     for (int i = 0; i < app_count; i++) {
//         if (strcmp(app_list[i]->name, name) == 0) {
//             return app_list[i];
//         }
//     }
//     return NULL;
// }

// // =============== 开机画面 ===============
// static lv_obj_t* gif_obj = NULL;

// static void ui_show_booting(const char* file_path) {
//     if (!file_path) {
//         ESP_LOGE(TAG, "Invalid file path (NULL)");
//         return;
//     }

//     sd_hal_list_content("/sdcard/system/icon");

//     gif_obj = lv_gif_create(lv_scr_act());
//     if (!gif_obj) {
//         ESP_LOGE(TAG, "Failed to create lv_gif object");
//         return;
//     }

//     // lv_gif_set_src(gif_obj, file_path);
//     lv_obj_center(gif_obj);
// }

// /**
//  * @brief 关闭当前前台应用，并切换到上一个应用或桌面
//  * @note 该函数必须在 UI 服务任务上下文中调用
//  */
// static void ui_service_close_current_app(void) {
//     if (!current_app) return;

//     // 1. 暂停并销毁当前应用
//     if (current_app->on_pause) {
//         current_app->on_pause(current_app);
//     }
//     if (current_app->on_destroy) {
//         current_app->on_destroy(current_app);
//     }

//     // 2. 从返回栈中取出上一个应用（如果有）
//     if (stack_top >= 0) {
//         ui_app_t *prev = app_stack[stack_top--];
//         ui_show_screen(prev);
//         if (prev->on_resume) {
//             prev->on_resume(prev);
//         }
//         current_app = prev;
//         ESP_LOGI(TAG, "Closed current app, back to: %s", prev->name);
//     } else {
//         // 3. 没有上一个应用，回到桌面
//         ui_app_t *home = find_app("desktop_app");
//         if (home) {
//             if (home != current_app) { // 理论上 current_app 已被销毁，这里 home 不可能是它
//                 ui_show_screen(home);
//                 if (home->on_resume) {
//                     home->on_resume(home);
//                 }
//             }
//             current_app = home;
//             ESP_LOGI(TAG, "Closed current app, back to home");
//         } else {
//             current_app = NULL;
//             ESP_LOGW(TAG, "No home app, no app active");
//         }
//     }
// }