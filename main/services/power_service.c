#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "system_config.h"
#include "event_loop_service.h"
#include "hal/power_io_hal.h"
#include "power_service.h"

static const char *TAG = "power_service";
static int power_service_ID = 0; // 非0才是有效注册

static void power_service_task(void *arg); // 服务任务
// static void handle_(xxxxx_service_receive_data_t *payload); 
// static void handle_reply(QueueHandle_t reply_queue);
static QueueHandle_t  power_service_request_queue; // 接收来自事件循环的 app_event_t*

// ===============api==========================
esp_err_t power_service_init(void)
{
    ESP_LOGI(TAG, "Initializing");
    // 初始化内容


    // 创建外部请求队列
    power_service_request_queue = xQueueCreate(8, sizeof(app_event_t*));
    if (! power_service_request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return ESP_ERR_NO_MEM;
    }

    // 向事件循环注册
    power_service_ID = event_loop_register_service("power_service_task", power_service_request_queue);
    // 启动服务任务
    xTaskCreate(power_service_task, "power_service_task", 2048, NULL, 5, NULL);  // 对外服务
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

int get_power_service_ID(void){
    return power_service_ID;
}



// 服务任务
static void power_service_task(void *arg){
   
    // 分发服务请求
    power_service_receive_data_t *payload;
    while (1) {
        if (xQueueReceive( power_service_request_queue, &payload, portMAX_DELAY) == pdTRUE) {
            switch (payload->cmd) {
                case 0: {
                    // 
                    //handle_(payload);
                    break;
                }           
                default:
                    ESP_LOGW(TAG, "Unknown cmd: 0x%lx", payload->cmd);
                    break;
            }
            free(payload); // 释放服务请求数据的内存
        }
    }
} 

// 设置LCD背光
static void hanle_LCD_ON(int light){


}

// void handle_(xxx_service_receive_data_t *payload)
// {
//     // 具体操作

//     // 回复请求
//     if( payload->reply_queue){
//         handle_reply( payload->reply_queue);
//     }
// }

// // 处理回复
// static void handle_reply(QueueHandle_t reply_queue){
//     xxx_service_send_data_t *payload = malloc(sizeof(_service_send_data_t));
//     // 拷贝数据
//     // ...

//     xQueueSend(reply_queue, &payload, 0);
// }

