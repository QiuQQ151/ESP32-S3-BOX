// main/services/wifi_event_data.h
#ifndef SERVICES_WIFI_EVENT_DATA_H
#define SERVICES_WIFI_EVENT_DATA_H

/**
 * @brief WiFi 事件附加数据
 */
typedef struct {
    const char *ssid;
    const char *ip_address;
    int error_code;
} wifi_event_data_t;

#endif