#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ========== 通用 API ==========
esp_err_t led_hal_init(void);


#ifdef __cplusplus
}
#endif


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

