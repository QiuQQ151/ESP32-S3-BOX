// main.c
#include <stdio.h>
#include<string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"

// 
#include "hal/lvgl_hal.h"

// 
#include "services/wifi_service.h"
#include "services/power_service.h"
#include "services/sntp_service.h"
#include "services/event_loop_service.h"
#include "services/ui_service.h"


static const char *TAG = "app_main";

extern  QueueHandle_t main_event_queue; // 系统请求接收队列

void app_main(void)
{
    // ================系统hal
    //  NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // 
    //lvgl_hal_init();



    // ==================系统service
    // ui
    ui_service_init();

    // 电源管理
    power_service_init();
    //  WiFi
    wifi_service_init();
    // sntp
    sntp_service_init();
    // 事件转发
    event_loop_service_init();



    // 请求一次WiFi服务
    wifi_service_receive_data_t *payload = (wifi_service_receive_data_t*)malloc(sizeof(wifi_service_receive_data_t));
    if(payload){
        // 分配WiFi信息
        payload->cmd = WIFI_CMD_CONNECT;
        strcpy(payload->ssid,"ZTE_49A720");
        strcpy(payload->password,"1234567890");
        payload->save = true;
        payload->reply_queue = NULL; // 
    }
    app_event_t *evt = malloc(sizeof(app_event_t));
    if(evt){
        // 构造事件外壳
        evt-> source = get_wifi_service_ID();
        evt->payload = payload;
        ESP_LOGI(TAG,"req wifi conect");
        xQueueSend(main_event_queue, &evt, 0);
    }


    QueueHandle_t app_main_queue = xQueueCreate(20, sizeof(wifi_service_send_data_t*));; // 系统请求接收队列

    while(1){
        // 请求一次WiFi服务
        wifi_service_receive_data_t *payload = (wifi_service_receive_data_t*)malloc(sizeof(wifi_service_receive_data_t));
        if(payload){
            // 分配WiFi信息
            payload->cmd = WIFI_CMD_CHEACK_STATA;
            payload->reply_queue = app_main_queue; // 
        }
        app_event_t *evt = malloc(sizeof(app_event_t));
        if(evt){
            // 构造事件外壳
            evt-> source = get_wifi_service_ID();
            evt->payload = payload;
            ESP_LOGI(TAG,"req wifi stata");
            xQueueSend(main_event_queue, &evt, 0);
        }

        wifi_service_send_data_t* wifi_data;
        if (xQueueReceive(app_main_queue, &wifi_data, portMAX_DELAY)){
            ESP_LOGI(TAG,"rec wifi ser data");
            ESP_LOGI(TAG,"ssid:%s,pass:%s,rssi:%d,ip:%s",wifi_data->ssid,wifi_data->password,wifi_data->rssi,wifi_data->ip_address);
            free(wifi_data);
        }
        vTaskDelay(8000 / portTICK_PERIOD_MS); 

        // 请求一次sntp服务
        sntp_service_receive_data_t *sntp_payload = (sntp_service_receive_data_t*)malloc(sizeof(sntp_service_receive_data_t));
        if(sntp_payload){
            // 分配WiFi信息
            sntp_payload->cmd = SNTP_CMD_GET_TIME;
            sntp_payload->reply_queue = app_main_queue; // 
        }
        evt = malloc(sizeof(app_event_t));
        if(evt){
            // 构造事件外壳
            evt-> source = get_sntp_service_ID();
            evt->payload = sntp_payload;
            ESP_LOGI(TAG,"req wifi stata");
            xQueueSend(main_event_queue, &evt, 0);
        }

        sntp_service_send_data_t* sntp_data;
        if (xQueueReceive(app_main_queue, &sntp_data, portMAX_DELAY)){
            ESP_LOGI(TAG,"rec sntp ser data");
            ESP_LOGI(TAG,"time:%s",sntp_data->current_time);
            free(sntp_data);
        }
        vTaskDelay(8000 / portTICK_PERIOD_MS); 
    }
    }