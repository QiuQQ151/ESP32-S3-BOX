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
#include "music_app.h"

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
    music_app_register();

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
