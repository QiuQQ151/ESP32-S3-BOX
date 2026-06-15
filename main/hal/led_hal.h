// led_hal.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** LED 设备枚举 */
typedef enum {
    LED_HAL_DEVICE_FRONT     = 0,
    LED_HAL_DEVICE_EXTENSION = 1,
    LED_HAL_DEVICE_MAX       = 2,
} led_hal_device_t;

/**
 * @brief 初始化指定 LED 设备
 * @param dev      设备索引
 * @param gpio     GPIO 引脚号
 * @param max_leds 最大 LED 数量
 * @return ESP_OK 成功，否则失败
 */
esp_err_t led_hal_init(led_hal_device_t dev, uint8_t gpio, uint16_t max_leds);

/**
 * @brief 获取设备 LED 数量
 * @param dev 设备索引
 * @return LED 数量（若未初始化返回 0）
 */
uint16_t led_hal_get_count(led_hal_device_t dev);

/**
 * @brief 设置单个像素颜色
 * @param dev   设备索引
 * @param index LED 索引
 * @param r     红色分量 (0-255)
 * @param g     绿色分量 (0-255)
 * @param b     蓝色分量 (0-255)
 */
void led_hal_set_pixel(led_hal_device_t dev, uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 刷新设备输出
 * @param dev 设备索引
 */
void led_hal_refresh(led_hal_device_t dev);

/**
 * @brief 清除所有像素并刷新
 * @param dev 设备索引
 */
void led_hal_clear(led_hal_device_t dev);

/**
 * @brief 检查设备是否已成功初始化
 * @param dev 设备索引
 * @return true 可用，false 不可用
 */
bool led_hal_is_ready(led_hal_device_t dev);

#ifdef __cplusplus
}
#endif