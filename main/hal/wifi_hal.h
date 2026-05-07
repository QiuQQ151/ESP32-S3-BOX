// main/hal/wifi_hal.h
#ifndef HAL_WIFI_HAL_H
#define HAL_WIFI_HAL_H

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi 状态枚举
 */
typedef enum {
    WIFI_STATE_UNINIT = 0,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE,
    WIFI_STATE_ERROR,
} wifi_state_t;

/**
 * @brief WiFi 配置结构体
 */
typedef struct {
    char ssid[33];
    char password[65];
    bool auto_connect;
    bool enable_smart_config;
} wifi_config_user_t;

/**
 * @brief 初始化 WiFi HAL
 * @return ESP_OK 成功，其他失败
 */
esp_err_t wifi_hal_init(void);

/**
 * @brief 启动 WiFi STA 模式并连接
 * @param ssid WiFi SSID
 * @param password WiFi 密码
 * @return ESP_OK 成功，其他失败
 */
esp_err_t wifi_hal_connect(const char *ssid, const char *password);

/**
 * @brief 主动断开 WiFi 连接
 * @return ESP_OK 成功，其他失败
 */
esp_err_t wifi_hal_disconnect(void);

/**
 * @brief 启动 AP 模式（用于配网）
 * @param ssid AP 的 SSID
 * @param password AP 密码（至少8位）
 * @return ESP_OK 成功，其他失败
 */
esp_err_t wifi_hal_start_ap(const char *ssid, const char *password);

/**
 * @brief 停止 AP 模式
 * @return ESP_OK 成功，其他失败
 */
esp_err_t wifi_hal_stop_ap(void);


/**
 * @brief 扫描可用 WiFi 网络
 * @return ESP_OK 成功，其他失败
 */
esp_err_t wifi_hal_scan(void);

/**
 * @brief 获取当前 WiFi 状态
 * @return WiFi 状态枚举值
 */
wifi_state_t wifi_hal_get_state(void);

/**
 * @brief 获取当前连接的 RSSI
 * @return RSSI 值 (dBm)
 */
int8_t wifi_hal_get_rssi(void);

#ifdef __cplusplus
}
#endif

#endif