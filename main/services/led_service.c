#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_hal.h"
#include "services/system_event.h"
#include "services/led_service.h"

static const char *TAG = "led_service";

/* ========== 可调节参数 ========== */
#define LED_FRONT_NUM           30
#define LED_EXTENSION_NUM       10

#define VOLUME_DISPLAY_MS       2000      // 音量通知显示时长（毫秒）
#define VOLUME_OFF_BRIGHT       20        // 未点亮灯珠的微亮亮度
#define VOLUME_LOW_R            0
#define VOLUME_LOW_G            255
#define VOLUME_LOW_B            0
#define VOLUME_HIGH_R           255
#define VOLUME_HIGH_G           0
#define VOLUME_HIGH_B           0

#define ALERT_BLINK_PERIOD_MS   500       // 警告闪烁周期（一次亮+灭）

/* ========== 设备上下文 ========== */
typedef struct {
    led_mode_t mode;                // 当前模式
    uint8_t    brightness;          // 当前亮度
    led_mode_t ambient_mode;        // 被通知/倒计时打断前的氛围灯模式

    // 通知状态
    bool       in_notification;     // 是否处于通知状态（音量或警告）
    uint8_t    notify_volume;       // 当前通知音量值
    uint8_t    alert_remaining;     // 警告剩余闪烁次数
    bool       alert_led_on;        // 警告当前亮灭状态
    TickType_t last_blink_tick;     // 上次闪烁切换时刻
    TickType_t notify_start_tick;   // 通知开始时刻（用于音量超时）

    // 番茄时钟倒计时
    uint32_t   countdown_secs;      // 剩余秒数
    uint32_t   last_countdown_tick; // 上次减一秒的时刻
    led_mode_t pre_clock_mode;      // 进入时钟前的氛围模式（用于倒计时结束恢复）
} dev_ctx_t;

static QueueHandle_t led_service_request_queue = NULL;
static dev_ctx_t dev_ctx[LED_DEVICE_MAX];

/* ========== 前向声明 ========== */
static void led_service_task(void *arg);
static void led_service_update_task(void *arg);
static void handle_set_mode(const led_service_receive_data_t *payload);
static void handle_set_brightness(const led_service_receive_data_t *payload);
static void handle_get_status(const led_service_receive_data_t *payload, QueueHandle_t reply_queue);
static void update_notification_animation(led_device_t dev);
static void update_clock_animation(led_device_t dev);
static led_hal_mode_t mode_to_hal(led_mode_t mode);
static uint16_t get_led_count(led_device_t dev);

/* ========== 初始化 ========== */
esp_err_t led_service_init(void) {
    led_hal_init();

    for (int i = 0; i < LED_DEVICE_MAX; i++) {
        dev_ctx[i].mode            = LED_MODE_BREATH;
        dev_ctx[i].brightness      = 50;
        dev_ctx[i].ambient_mode    = LED_MODE_BREATH;
        dev_ctx[i].in_notification = false;
        dev_ctx[i].alert_remaining = 0;
        dev_ctx[i].alert_led_on    = false;
        dev_ctx[i].last_blink_tick = 0;
        dev_ctx[i].countdown_secs  = 0;
        dev_ctx[i].last_countdown_tick = 0;
    }

    led_hal_set_mode(LED_HAL_DEVICE_FRONT, LED_HAL_MODE_BREATH);
    led_hal_set_brightness(LED_HAL_DEVICE_FRONT, 50);
    led_hal_set_mode(LED_HAL_DEVICE_EXTENSION, LED_HAL_MODE_BREATH);
    led_hal_set_brightness(LED_HAL_DEVICE_EXTENSION, 50);

    led_service_request_queue = xQueueCreate(10, sizeof(event_data_t *));
    if (!led_service_request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return ESP_ERR_NO_MEM;
    }

    xTaskCreate(led_service_task, "led_srv", 4*1024, NULL, 5, NULL);
    xTaskCreate(led_service_update_task, "led_srv_upd", 4*1024, NULL, 5, NULL);
    return ESP_OK;
}

QueueHandle_t get_led_service_queue(void) {
    return led_service_request_queue;
}

/* ========== 主任务：分发请求 ========== */
static void led_service_task(void *arg) {
    event_data_t *evt_data;
    while (1) {
        if (xQueueReceive(led_service_request_queue, &evt_data, portMAX_DELAY) == pdTRUE) {
            if (evt_data->event_type == REQUEST) {
                led_service_receive_data_t *payload = (led_service_receive_data_t *)evt_data->data;
                if (!payload) {
                    free(evt_data);
                    continue;
                }
                ESP_LOGI(TAG, "cmd:%d dev:%d mode:%d", payload->cmd, payload->device, payload->mode);
                switch (payload->cmd) {
                    case LED_CMD_SET_MODE:
                        handle_set_mode(payload);
                        break;
                    case LED_CMD_SET_BRIGHTNESS:
                        handle_set_brightness(payload);
                        break;
                    case LED_CMD_GET_STATUS:
                        handle_get_status(payload, evt_data->reply_queue);
                        break;
                    default:
                        ESP_LOGE(TAG, "unknown cmd %d", payload->cmd);
                        break;
                }
                free(payload);
                evt_data->data = NULL;
            } else {
                if (evt_data->data) free(evt_data->data);
            }
            free(evt_data);
        }
    }
}

/* ========== 更新任务：动画与定时处理 ========== */
static void led_service_update_task(void *arg) {
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        led_hal_tick();   // 驱动氛围灯（非通知/非倒计时模式）

        for (int dev = 0; dev < LED_DEVICE_MAX; dev++) {
            if (dev_ctx[dev].in_notification) {
                update_notification_animation((led_device_t)dev);
            } else if (dev_ctx[dev].mode == LED_MODE_CLOCK) {
                update_clock_animation((led_device_t)dev);
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(15));
    }
}

/* ========== 命令处理 ========== */
static led_hal_mode_t mode_to_hal(led_mode_t mode) {
    switch (mode) {
        case LED_MODE_OFF:     return LED_HAL_MODE_OFF;
        case LED_MODE_BREATH:  return LED_HAL_MODE_BREATH;
        case LED_MODE_RAINBOW: return LED_HAL_MODE_RAINBOW;
        case LED_MODE_CLOCK:   return LED_HAL_MODE_CLOCK;
        case LED_MODE_BULB:    return LED_HAL_MODE_BULB;
        case LED_MODE_MUSIC:   return LED_HAL_MODE_MUSIC;
        default:               return LED_HAL_MODE_OFF;
    }
}

static void handle_set_mode(const led_service_receive_data_t *payload) {
    led_device_t dev  = payload->device;
    led_mode_t   mode = payload->mode;

    // 如果携带了亮度值（非0），则更新亮度
    if (payload->brightness != 0) {
        dev_ctx[dev].brightness = payload->brightness;
        led_hal_set_brightness((led_hal_device_t)dev, payload->brightness);
    }

    // ---------- 通知类模式 ----------
    if (mode == LED_MODE_VOLUME || mode == LED_MODE_ALERT) {
        if (!dev_ctx[dev].in_notification && dev_ctx[dev].mode != LED_MODE_CLOCK) {
            // 保存当前氛围模式（若当前不在通知/倒计时中）
            dev_ctx[dev].ambient_mode = dev_ctx[dev].mode;
        }

        dev_ctx[dev].in_notification = true;
        dev_ctx[dev].mode            = mode;

        if (mode == LED_MODE_VOLUME) {
            dev_ctx[dev].notify_volume  = payload->volume;
            dev_ctx[dev].notify_start_tick = xTaskGetTickCount();
        } else { // ALERT
            dev_ctx[dev].alert_remaining = payload->alert_count;
            dev_ctx[dev].alert_led_on    = true;
            dev_ctx[dev].last_blink_tick = xTaskGetTickCount();
        }

        // 硬件切换到通知模式（停止 hal 自动刷新）
        led_hal_set_mode((led_hal_device_t)dev, LED_HAL_MODE_NOTIFICATION);
        return;
    }

    // ---------- 番茄时钟模式 ----------
    if (mode == LED_MODE_CLOCK) {
        // 保存进入时钟前的氛围模式（用于倒计时结束恢复）
        // 注意：不希望在倒计时中再次切换时钟模式导致丢失原始氛围模式
        if (dev_ctx[dev].mode != LED_MODE_CLOCK) {
            dev_ctx[dev].pre_clock_mode = dev_ctx[dev].mode;
        }

        dev_ctx[dev].mode = LED_MODE_CLOCK;
        dev_ctx[dev].in_notification = false;

        // 将时分秒转换为总秒数
        uint32_t total_secs = payload->clock_hour * 3600UL +
                              payload->clock_minute * 60UL +
                              payload->clock_second;
        dev_ctx[dev].countdown_secs = total_secs;
        dev_ctx[dev].last_countdown_tick = xTaskGetTickCount();

        // 将剩余时间写入 hal（hal 会立即显示指针位置）
        uint8_t h = (uint8_t)(total_secs / 3600);
        uint8_t m = (uint8_t)((total_secs % 3600) / 60);
        uint8_t s = (uint8_t)(total_secs % 60);
        led_hal_set_mode((led_hal_device_t)dev, LED_HAL_MODE_CLOCK);
        led_hal_set_clock((led_hal_device_t)dev, h, m, s);
        return;
    }

    // ---------- 普通氛围灯模式 ----------
    dev_ctx[dev].mode            = mode;
    dev_ctx[dev].in_notification = false;
    dev_ctx[dev].countdown_secs  = 0;   // 取消倒计时
    dev_ctx[dev].ambient_mode    = mode;

    led_hal_set_mode((led_hal_device_t)dev, mode_to_hal(mode));
}

static void handle_set_brightness(const led_service_receive_data_t *payload) {
    led_device_t dev = payload->device;
    dev_ctx[dev].brightness = payload->brightness;
    led_hal_set_brightness((led_hal_device_t)dev, payload->brightness);
}

static void handle_get_status(const led_service_receive_data_t *payload, QueueHandle_t reply_queue) {
    if (!reply_queue) return;
    led_service_send_data_t reply = {
        .service_state      = LED_SERVICE_OK,
        .device             = payload->device,
        .current_mode       = dev_ctx[payload->device].mode,
        .current_brightness = dev_ctx[payload->device].brightness,
    };
    xQueueSend(reply_queue, &reply, 0);
}

/* ========== 动画更新函数 ========== */
static uint16_t get_led_count(led_device_t dev) {
    return (dev == LED_FRONT) ? LED_FRONT_NUM : LED_EXTENSION_NUM;
}

static void restore_ambient_mode(led_device_t dev) {
    led_hal_device_t hal_dev = (dev == LED_FRONT) ? LED_HAL_DEVICE_FRONT : LED_HAL_DEVICE_EXTENSION;
    dev_ctx[dev].mode = dev_ctx[dev].ambient_mode;
    dev_ctx[dev].in_notification = false;
    dev_ctx[dev].countdown_secs = 0;
    led_hal_set_mode(hal_dev, mode_to_hal(dev_ctx[dev].ambient_mode));
    ESP_LOGI(TAG, "dev%d restored to ambient mode %d", dev, dev_ctx[dev].ambient_mode);
}

// 通知动画（音量/警告）
static void update_notification_animation(led_device_t dev) {
    led_hal_device_t hal_dev = (dev == LED_FRONT) ? LED_HAL_DEVICE_FRONT : LED_HAL_DEVICE_EXTENSION;
    uint16_t total = get_led_count(dev);

    if (dev_ctx[dev].mode == LED_MODE_VOLUME) {
        // 检查是否超时
        TickType_t now = xTaskGetTickCount();
        if ((now - dev_ctx[dev].notify_start_tick) >= pdMS_TO_TICKS(VOLUME_DISPLAY_MS)) {
            restore_ambient_mode(dev);
            return;
        }

        // 绘制音量条
        uint8_t vol = dev_ctx[dev].notify_volume;
        uint16_t lit = (vol * total) / 100;
        for (uint16_t i = 0; i < total; i++) {
            if (i < lit) {
                float ratio = (float)i / (total - 1);
                uint8_t r = (uint8_t)(VOLUME_LOW_R + ratio * (VOLUME_HIGH_R - VOLUME_LOW_R));
                uint8_t g = (uint8_t)(VOLUME_LOW_G + ratio * (VOLUME_HIGH_G - VOLUME_LOW_G));
                uint8_t b = (uint8_t)(VOLUME_LOW_B + ratio * (VOLUME_HIGH_B - VOLUME_LOW_B));
                led_hal_set_pixel(hal_dev, i, r, g, b);
            } else {
                led_hal_set_pixel(hal_dev, i, VOLUME_OFF_BRIGHT, VOLUME_OFF_BRIGHT, VOLUME_OFF_BRIGHT);
            }
        }
        led_hal_refresh(hal_dev);

    } else if (dev_ctx[dev].mode == LED_MODE_ALERT) {
        // 闪烁次数用完则恢复
        if (dev_ctx[dev].alert_remaining == 0) {
            restore_ambient_mode(dev);
            return;
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - dev_ctx[dev].last_blink_tick) >= pdMS_TO_TICKS(ALERT_BLINK_PERIOD_MS)) {
            // 切换亮灭状态
            dev_ctx[dev].alert_led_on = !dev_ctx[dev].alert_led_on;
            dev_ctx[dev].last_blink_tick = now;
            // 从灭转为亮时，表示完成一次完整的亮灭周期，次数减一
            if (dev_ctx[dev].alert_led_on) {
                if (dev_ctx[dev].alert_remaining > 0) {
                    dev_ctx[dev].alert_remaining--;
                }
            }
        }

        uint8_t r = dev_ctx[dev].alert_led_on ? 255 : 0;
        for (uint16_t i = 0; i < total; i++) {
            led_hal_set_pixel(hal_dev, i, r, 0, 0);
        }
        led_hal_refresh(hal_dev);
    }
}

// 番茄时钟动画（倒计时，每秒更新 hal 时间）
static void update_clock_animation(led_device_t dev) {
    led_hal_device_t hal_dev = (dev == LED_FRONT) ? LED_HAL_DEVICE_FRONT : LED_HAL_DEVICE_EXTENSION;
    TickType_t now = xTaskGetTickCount();

    // 检查是否经过了 1 秒
    if ((now - dev_ctx[dev].last_countdown_tick) >= pdMS_TO_TICKS(1000)) {
        if (dev_ctx[dev].countdown_secs > 0) {
            dev_ctx[dev].countdown_secs--;
            dev_ctx[dev].last_countdown_tick = now;

            uint32_t secs = dev_ctx[dev].countdown_secs;
            uint8_t h = secs / 3600;
            uint8_t m = (secs % 3600) / 60;
            uint8_t s = secs % 60;
            led_hal_set_clock(hal_dev, h, m, s);

            if (secs == 0) {
                // 倒计时结束，恢复进入时钟前的氛围模式
                dev_ctx[dev].mode = dev_ctx[dev].pre_clock_mode;
                led_hal_set_mode(hal_dev, mode_to_hal(dev_ctx[dev].mode));
                ESP_LOGI(TAG, "dev%d countdown finished, restore mode %d", dev, dev_ctx[dev].mode);
            }
        }
    }
}
// #include <math.h> 
// #include <string.h>
// #include "esp_log.h"

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// #include "led_hal.h"
// #include "services/system_event.h"
// #include "services/led_service.h"


// static const char* TAG = "led_service";

// // 内部函数
// static QueueHandle_t led_service_request_queue = NULL;
// static void led_service_task(void *arg);
// static void led_service_update_task(void *arg);
// static void handle_set_mode(led_service_receive_data_t *payload);
// static void handle_set_brightness(led_service_receive_data_t *payload);
// static void handle_get_status(led_service_receive_data_t *payload);
// static void handle_reply(led_service_receive_data_t *payload);      // 回复请求

// // ==================================api==================================================================

// esp_err_t led_service_init(void){

//     //  初始化led
//     led_hal_init();
//     // 设置前置为
//     led_hal_set_mode(LED_HAL_DEVICE_FRONT, LED_HAL_MODE_BREATH);
//      led_hal_set_brightness(LED_HAL_DEVICE_FRONT, 50);
//     // 设置扩展板为呼吸模式，亮度 128
//     led_hal_set_mode(LED_HAL_DEVICE_EXTENSION, LED_HAL_MODE_BREATH);
//     led_hal_set_brightness(LED_HAL_DEVICE_EXTENSION, 50);

//     // 初始化LED服务队列
//     led_service_request_queue = xQueueCreate(10, sizeof(led_service_receive_data_t));
//     if(led_service_request_queue == NULL){
//         ESP_LOGE(TAG, "Failed to create led_service_request_queue");
//         vTaskDelete(NULL);
//         return ESP_ERR_NO_MEM;
//     }
//     // 创建LED任务
//     xTaskCreate(led_service_task, "led_service_task", 5*1024, NULL, 5, NULL);  
//     xTaskCreate(led_service_update_task, "led_service_update_task", 5*1024, NULL, 5, NULL);  
//     return ESP_OK;
// }

// QueueHandle_t get_led_service_queue(void){
//     return led_service_request_queue;
// }


// // =======================================内部函数==================================================================
// static void led_service_update_task(void *arg) // LED服务更新任务
// {
//     while (1) {
//         led_hal_tick();
//         vTaskDelay(pdMS_TO_TICKS(15));
//     }
// }



// static void led_service_task(void *arg) // LED服务任务
// {
//     // 分发LED服务请求
//     event_data_t *evt_data;
//     while (1) {
//         if (xQueueReceive(led_service_request_queue, &evt_data, portMAX_DELAY) == pdTRUE) {
//             if(evt_data->event_type == REQUEST){
//                 // 处理请求
//                 led_service_receive_data_t *payload = (led_service_receive_data_t *)evt_data->data;
//                 if(payload){
//                     ESP_LOGI(TAG, "Received led service request: cmd %d", payload->cmd);
//                     switch(payload->cmd){
//                         case LED_CMD_SET_MODE:
//                             ESP_LOGI(TAG, "Received led service request: cmd %d, device %d, mode %d", payload->cmd, payload->device, payload->mode);
//                             handle_set_mode(payload);
//                             break;
//                         case LED_CMD_SET_BRIGHTNESS:
//                             ESP_LOGI(TAG, "Received led service request: cmd %d, device %d, brightness %d", payload->cmd, payload->device, payload->brightness);
//                             handle_set_brightness(payload);
//                             break;
//                         case LED_CMD_GET_STATUS:
//                             ESP_LOGI(TAG, "Received led service request: cmd %d, device %d", payload->cmd, payload->device);
//                             handle_get_status(payload);
//                             break;
//                         default:
//                             ESP_LOGE(TAG, "Unknown led service cmd %d", payload->cmd);
//                             break;
//                     }
//                     if(payload){
//                         free(payload);
//                     }
//                 }
//             } else{
//                 // 不支持的服务类型
//                 ESP_LOGE(TAG, "led service request fail: event_type %d", evt_data->event_type);
//                 if(evt_data->data){
//                     free(evt_data->data);
//                 }
//             }

//             // 释放内存
//             if(evt_data){
//                 free(evt_data);
//             }
//         }
//     }
// }

// static void handle_set_mode(led_service_receive_data_t *payload){
//     // 设置LED模式

// }

// static void handle_set_brightness(led_service_receive_data_t *payload){
//     // 设置LED亮度

// }

// static void handle_get_status(led_service_receive_data_t *payload){
//     // 获取LED状态

// }

// static void handle_reply(led_service_receive_data_t *payload){
//     // 回复请求

// }