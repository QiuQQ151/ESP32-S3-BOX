// main.c
#include <stdio.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "services/wifi_service.h"
#include "config/events.h"

static const char *TAG = "app";

// WiFi 事件回调
static void on_wifi_event(app_wifi_event_id_t event, wifi_event_data_t *data)
{
    switch (event) {
        case APP_WIFI_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WiFi Connected! SSID: %s, IP: %s",
                     data->ssid, data->ip_address);
            break;
        case APP_WIFI_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi Disconnected");
            break;
        case APP_WIFI_EVENT_AP_STARTED:
            ESP_LOGI(TAG, "AP Mode Started");
            break;
        case APP_WIFI_EVENT_CONNECTION_FAILED:
            ESP_LOGE(TAG, "WiFi Connection Failed: %d", data->error_code);
            break;
        default:
            break;
    }
}

void app_main(void)
{
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化 WiFi 服务
    wifi_service_init();
    wifi_service_register_callback(on_wifi_event);
    wifi_service_start();

    // 后续任务...
    wifi_service_connect("ZTE_49A720","1234567890",true);
}