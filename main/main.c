// main.c
#include <stdio.h>
#include<string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
// 
#include "hal/tca9535_hal.h"
#include "hal/lvgl_hal.h"
#include "hal/sd_hal.h"
#include "hal/led_hal.h"
#include "hal/keys_hal.h"
// 
#include "services/system_event.h"
#include "services/wifi_service.h"
#include "services/power_service.h"
#include "services/sntp_service.h"
#include "services/audio_service.h"
#include "services/ui_service.h"
#include "services/led_service.h"

static const char *TAG = "app_main";

// 测试任务
static void test_task(void *arg)
{
    while(1){
        vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
        ESP_LOGI(TAG, "test_task");
    }
}

void app_main(void)
{
    // ================系统初始化
    //  NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret); 

    wifi_service_init();
    audio_service_init(); // 初始化了IIC（I2C_NUM_0）和IIS，并初始化了tca9535 IO扩展芯片（IIC通信）
    sntp_service_init(); // 初始化了NTP客户端(需要WiFi连接)  存在栈溢出，待查
    led_hal_init();
//  led_service_init(); // 初始化LED服务
    ui_service_init(); // 注释里面含iic初始化，里面初始化通用按键key_hal_init();

//     // audio
//     

//     // ====================hal====================
//     // IO扩展口
//    
//     // led
//     led_hal_init();
//     // sd
//     sd_hal_init();
   

//     // ==================系统service
//    // 事件转发
//     event_loop_service_init();
//     // 电源管理
//     power_service_init();

//     // sntp
//     
//     // ui
//     


    // 请求一次WiFi服务
    wifi_service_receive_data_t *wifi_payload = (wifi_service_receive_data_t*)malloc(sizeof(wifi_service_receive_data_t));
    if(wifi_payload){
        // 分配WiFi信息
        wifi_payload->cmd = WIFI_CMD_CONNECT;
        strcpy(wifi_payload->ssid,"ZTE_49A720");
        strcpy(wifi_payload->password,"1234567890");
        wifi_payload->save = true;
    }
    event_data_t *evt_data = malloc(sizeof(event_data_t));
    if(evt_data){
        evt_data->service_id = HAL; //
        evt_data->event_type = REQUEST;
        evt_data->reply_queue = NULL; // 不需要回复
        evt_data->data = wifi_payload;
        ESP_LOGI(TAG,"req wifi conect");
        xQueueSend(get_wifi_service_queue(), &evt_data, 0);
    }

    // // 请求一次audio服务
    // audio_service_receive_data_t *audio_payload = (audio_service_receive_data_t*)malloc(sizeof(audio_service_receive_data_t));
    // if(audio_payload){
    //     // 分配音频信息
    //     audio_payload->cmd = AUDIO_CMD_CONNECT;
    // }
    // event_data_t* evt_audio_data = malloc(sizeof(event_data_t));
    // if(evt_audio_data){
    //     evt_audio_data->service_id = HAL; //
    //     evt_audio_data->event_type = REQUEST;
    //     evt_audio_data->reply_queue = NULL; // 不需要回复
    //     evt_audio_data->data = audio_payload;
    //     ESP_LOGI(TAG,"req audio conect");
    //     xQueueSend(get_audio_service_queue(), &evt_audio_data, 0);
    // }   

   
    // 启动test
    xTaskCreate(test_task, "test_task", 4096, NULL,2, NULL);


    // 删除main任务
    vTaskDelete(NULL);
}