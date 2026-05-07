// main/config/events.h
#ifndef CONFIG_EVENTS_H
#define CONFIG_EVENTS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 应用层 WiFi 事件 ====================
typedef enum {
    APP_WIFI_EVENT_CONNECTED = 0x1000,      // 已连接
    APP_WIFI_EVENT_DISCONNECTED,            // 断开
    APP_WIFI_EVENT_GOT_IP,                  // 获得 IP
    APP_WIFI_EVENT_AP_STARTED,              // AP 启动
    APP_WIFI_EVENT_AP_STOPPED,              // AP 停止
    APP_WIFI_EVENT_CONNECTION_FAILED,       // 连接失败
    APP_WIFI_EVENT_SMART_CONFIG_DONE,       // （保留，但不使用）
} app_wifi_event_id_t;

// ==================== WiFi 事件携带的数据 ====================
typedef struct {
    const char *ssid;           // 连接的 SSID
    const char *ip_address;     // IP 地址字符串
    int error_code;             // 错误码（失败时使用）
} wifi_event_data_t;

// ==================== 其他系统事件可继续添加 ====================

#ifdef __cplusplus
}
#endif

#endif // CONFIG_EVENTS_H