#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "usb_device_uac.h"

// HAL
#include "hal/tca9535_hal.h"
#include "hal/lvgl_hal.h"
#include "hal/sd_hal.h"
#include "hal/keys_hal.h"

// Services
#include "services/system_event.h"
#include "services/wifi_service.h"
#include "services/sntp_service.h"
#include "services/audio_service.h"
#include "services/ui_service.h"
#include "services/led_service.h"

// ADF elements
#include "audio_element.h"
#include "i2s_stream.h"
#include "raw_stream.h"          // 如果确实需要 raw_stream，用于接收USB数据
#include "usb_device_uac.h"      // USB UAC 组件
#include "esp_codec_dev.h"       // 编解码器设备（ES8311）
#include "driver/i2s_std.h"
#include "board.h"
#include "audio_pipeline.h"

static const char *TAG = "app_main";

/* ---------- 测试任务 ---------- */
static void test_task(void *arg)
{
    while (1) {
        uint32_t light_dac = lvgl_hal_get_light_adc();
        lvgl_hal_set_brightness( (uint8_t)(light_dac) );
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    // 基础初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    audio_service_init(); //i2c->tca9535->power_hal
    led_service_init();
    wifi_service_init();
    sntp_service_init();
    ui_service_init();

    // 发送 WiFi 连接请求（按需修改 SSID/密码）
    wifi_service_receive_data_t *wifi_payload = malloc(sizeof(wifi_service_receive_data_t));
    if (wifi_payload) {
        wifi_payload->cmd = WIFI_CMD_CONNECT;
        strcpy(wifi_payload->ssid, "WIN10-HFMP");
        strcpy(wifi_payload->password, "1234567890");
        wifi_payload->save = true;
    }
    event_data_t *evt_data = malloc(sizeof(event_data_t));
    if (evt_data) {
        evt_data->service_id = HAL;
        evt_data->event_type = REQUEST;
        evt_data->reply_queue = NULL;
        evt_data->data = wifi_payload;
        xQueueSend(get_wifi_service_queue(), &evt_data, 0);
    }

    xTaskCreate(test_task, "test_task", 4096, NULL, 2, NULL);
    vTaskDelete(NULL);
}