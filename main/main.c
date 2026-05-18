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
// #include "hal/led_hal.h"
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
        // led_service_receive_data_t* led_payload = (led_service_receive_data_t*)malloc(sizeof(led_service_receive_data_t));
        // if(led_payload){
        //     led_payload->device = LED_HAL_DEVICE_FRONT;
        //     led_payload->mode = LED_MODE_ALERT;
        //     led_payload->brightness = 80;
        //     led_payload->arg = 0;
            
        //     // 
        //     event_data_t* led_event = (event_data_t*)malloc(sizeof(event_data_t));
        //     if(led_event){
        //         led_event->service_id = HAL;
        //         led_event->event_type = REQUEST;
        //         led_event->data = led_payload;
        //         led_event->data_len = sizeof(led_service_receive_data_t);
        //         xQueueSend(get_led_service_queue(), &led_event, 0);
        //     } else{
        //         ESP_LOGE(TAG, "malloc led_event failed");
        //         free(led_payload);
        //     }
        // }
        // ESP_LOGI(TAG, "test_task");
        // // 切换到呼吸模式
        // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_BREATH, 80, 0);
        // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
        // // 启动番茄时钟
        // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_CLOCK, 80, 10);
        // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
        // // 模拟音量变化
        // int i = 50;
        // while( i < 100){
        //     led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_VOLUME, 80, i);
        //     i++;
        //     vTaskDelay(100 / portTICK_PERIOD_MS); // 0.1s
        // }
        // // 切回音乐模式
        // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_MUSIC, 250, 0);
        // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s        
        // // 触发警报
        // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_ALERT, 80, 50);
        // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
        // // 切回音乐模式
        // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_MUSIC, 250, 0);
        // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
        // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_BULB, 250, 0);
        // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
        // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_RUN, 80, 0);
        // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
        // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_OFF, 80, 0);
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

    led_service_init(); // 初始化LED服务
    vTaskDelay(1000 / portTICK_PERIOD_MS); // 1s
    wifi_service_init();
    vTaskDelay(1000 / portTICK_PERIOD_MS); // 1s
    audio_service_init(); // 初始化了IIC（I2C_NUM_0）和IIS，并初始化了tca9535 IO扩展芯片（IIC通信）
    vTaskDelay(1000 / portTICK_PERIOD_MS); // 1s
    sntp_service_init(); // 初始化了NTP客户端(需要WiFi连接) 
    vTaskDelay(1000 / portTICK_PERIOD_MS); // 1s
    ui_service_init(); // 注释里面含iic初始化，里面初始化通用按键key_hal_init();
//  power_service_init();

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