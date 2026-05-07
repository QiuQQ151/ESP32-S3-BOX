// main/services/wifi_service.h
#ifndef SERVICES_WIFI_SERVICE_H
#define SERVICES_WIFI_SERVICE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "hal/wifi_hal.h"
#include "config/events.h"

#ifdef __cplusplus
extern "C" {
#endif

// 服务内部状态
typedef enum {
    WIFI_SRV_STATE_IDLE = 0,
    WIFI_SRV_STATE_CONNECTING,
    WIFI_SRV_STATE_CONNECTED,
    WIFI_SRV_STATE_AP_MODE,
    WIFI_SRV_STATE_ERROR,
} wifi_srv_state_t;

// 回调函数类型：收到不同事件时会调用，携带 wifi_event_data_t 数据
typedef void (*wifi_event_callback_t)(app_wifi_event_id_t event, wifi_event_data_t *data);

esp_err_t wifi_service_init(void);
esp_err_t wifi_service_start(void);
esp_err_t wifi_service_connect_saved(void);
esp_err_t wifi_service_connect(const char *ssid, const char *password, bool save);
esp_err_t wifi_service_disconnect(void);
esp_err_t wifi_service_start_provisioning(void);
wifi_srv_state_t wifi_service_get_state(void);
esp_err_t wifi_service_get_ip_str(char *ip_buf, size_t buf_len);
int8_t wifi_service_get_rssi(void);
void wifi_service_register_callback(wifi_event_callback_t callback);
bool wifi_service_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif