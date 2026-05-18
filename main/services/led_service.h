#ifndef LED_HAL_H
#define LED_HAL_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_HAL_DEVICE_FRONT     = 0,
    LED_HAL_DEVICE_EXTENSION = 1,
    LED_HAL_DEVICE_MAX       = 2,
} led_hal_device_t;

typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_BREATH,
    LED_MODE_CLOCK,
    LED_MODE_BULB,
    LED_MODE_MUSIC,
    LED_MODE_RUN,
    LED_MODE_VOLUME,         // 临时模式（3秒后恢复）
    LED_MODE_ALERT,          // 临时模式（闪3次后恢复）
} led_mode_t;

/* ========== 扁平服务请求结构 ========== */
typedef struct {
    led_hal_device_t    device;         // 设备
    led_mode_t          mode;           // 模式（关灯/氛围灯/通知）
    uint8_t             brightness;     // 亮度 0-255 （每次设置都有效）
    uint32_t            arg;            // 附加参数：
} led_service_receive_data_t;

/* ========== 状态回复 ========== */
typedef enum {
    LED_SERVICE_OK = 0,
    LED_SERVICE_ERROR,
} led_service_state_t;

typedef struct {
    led_service_state_t service_state;
    led_hal_device_t        device;
    led_mode_t          current_mode;
    uint8_t             current_brightness;
} led_service_send_data_t;



esp_err_t led_service_init(void);
QueueHandle_t get_led_service_queue(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_HAL_H */

// #ifndef LED_HAL_H
// #define LED_HAL_H

// #include "esp_err.h"
// #include <stdint.h>

// #ifdef __cplusplus
// extern "C" {
// #endif

// typedef enum {
//     LED_HAL_DEVICE_FRONT     = 0,
//     LED_HAL_DEVICE_EXTENSION = 1,
//     LED_HAL_DEVICE_MAX       = 2,
// } led_hal_device_t;

// typedef enum {
//     LED_MODE_OFF = 0,
//     LED_MODE_BREATH,         // 白色呼吸
//     LED_MODE_CLOCK,          // 番茄时钟（需先调用 led_hal_set_clock）
//     LED_MODE_BULB,           // 白色常亮
//     LED_MODE_MUSIC,          // 有机渐变
//     LED_MODE_RUN,            // 跑马灯
//     LED_MODE_VOLUME,         // 音量指示（临时）
//     LED_MODE_ALERT,          // 警告闪烁（临时）
// } led_mode_t;

// /**
//  * @brief 初始化 LED 硬件及效果任务
//  */
// esp_err_t led_hal_init(void);

// /**
//  * @brief 设置指定面板的永久模式与最大亮度
//  * @param dev        面板
//  * @param mode       模式（不可为 LED_MODE_VOLUME / LED_MODE_ALERT）
//  * @param brightness 最大亮度 (1~255)
//  */
// esp_err_t led_hal_set_panel_mode(led_hal_device_t dev, led_mode_t mode, uint8_t brightness);

// /**
//  * @brief 启动番茄时钟（仅前面板）
//  * @param total_seconds 倒计时总秒数 (1~3600)
//  */
// esp_err_t led_hal_set_clock(uint32_t total_seconds);

// /**
//  * @brief 触发音量指示（仅前面板，3秒后自动恢复原模式）
//  * @param volume     音量值 0~255
//  * @param brightness 最大亮度 (1~255)
//  */
// esp_err_t led_hal_set_volume(uint8_t volume, uint8_t brightness);

// /**
//  * @brief 触发警告闪烁（仅前面板，全红闪3次后自动恢复原模式）
//  * @param brightness 最大亮度 (1~255)
//  */
// esp_err_t led_hal_alert(uint8_t brightness);

// #ifdef __cplusplus
// }
// #endif

// #endif

// #pragma once
// #include <stdint.h>
// #include "esp_err.h"

// #ifdef __cplusplus
// extern "C" {
// #endif

// typedef enum {
//     LED_HAL_DEVICE_FRONT = 0,
//     LED_HAL_DEVICE_EXTENSION,
// } led_hal_device_t;

// typedef enum {
//     LED_HAL_MODE_OFF = 0,
//     LED_HAL_MODE_CLOCK,
//     LED_HAL_MODE_BREATH,
//     LED_HAL_MODE_RAINBOW,
//     LED_HAL_MODE_BULB,
//     LED_HAL_MODE_MUSIC,
// } led_hal_mode_t;

// esp_err_t led_hal_init(void);

// // 设置模式（立即生效，重置动画相位）
// void led_hal_set_mode(led_hal_device_t dev, led_hal_mode_t mode);

// // 设置亮度 0-255（下次 tick 生效）
// void led_hal_set_brightness(led_hal_device_t dev, uint8_t brightness);

// // 设置时钟时间（仅 CLOCK 模式有效）
// void led_hal_set_clock(led_hal_device_t dev, uint8_t hour, uint8_t minute, uint8_t second);

// // 驱动函数，需由服务层每 15~20ms 调用一次
// void led_hal_tick(void);

// #ifdef __cplusplus
// }
// #endif

