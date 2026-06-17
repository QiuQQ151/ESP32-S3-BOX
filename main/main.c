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

static const char *TAG = "app_main";

/* ---------- 测试任务 ---------- */
static void test_task(void *arg)
{
    while (1) {
        // uint32_t light_dac = lvgl_hal_get_light_adc();
        // lvgl_hal_set_brightness( (uint8_t)(light_dac) );
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

    vTaskDelay(10000 / portTICK_PERIOD_MS);
    // 播放音频
    audio_service_receive_data_t* audio_payload = malloc(sizeof(audio_service_receive_data_t));
    audio_payload->cmd = AUDIO_CMD_CONNECT;
    strcpy(audio_payload->url, "http://lhttp.qingting.fm/live/4915/64k.mp3");
    audio_payload->prv_type = http_str;
    audio_payload->midle_type = mp3_dec;
    audio_payload->back_type = i2s_hal;
    audio_payload->volume = 50;
    audio_payload->start_after_connect = true;

    event_data_t *audio_evt_data = malloc(sizeof(event_data_t));
    audio_evt_data->service_id = UI_SERVICE;
    audio_evt_data->event_type = REQUEST;
    audio_evt_data->reply_queue = NULL;
    audio_evt_data->data = audio_payload;
    audio_evt_data->data_len = sizeof(audio_service_receive_data_t);
    xQueueSend(get_audio_service_queue(), &audio_evt_data, 0);

    xTaskCreate(test_task, "test_task", 4096, NULL, 2, NULL);
    vTaskDelete(NULL);
}