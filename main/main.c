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

extern  QueueHandle_t main_event_queue; // 系统请求接收队列

void app_main(void)
{
   //
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << 4),   // 选中 GPIO4
        .mode = GPIO_MODE_OUTPUT,      // 输出模式
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(4, 1);             // 输出高电平



    // ================系统初始化
    //  NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret); 

    wifi_service_init();


//     // audio
//     audio_service_init(); // 初始化了IIC（I2C_NUM_0）和IIS

//     // ====================hal====================
//     // IO扩展口
//     tca9535_hal_init(I2C_NUM_0);
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
//     sntp_service_init();
//     // ui
//     ui_service_init(); // 注释里面含iic初始化
//     key_hal_init(); // 依赖ui_service_ID

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
    event_data_t *evt_data = malloc(sizeof(event_data_t));
    if(evt_data){
        evt_data->service_id = HAL; //
        evt_data->event_type = REQUEST;
        evt_data->reply_queue = NULL; // 不需要回复
        evt_data->data = payload;
        ESP_LOGI(TAG,"req wifi conect");
        xQueueSend(get_wifi_service_queue(), &evt_data, 0);
    }

    // 删除main任务
    vTaskDelete(NULL);
}