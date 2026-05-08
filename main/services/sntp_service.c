#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sntp_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "system_config.h"
#include "event_loop_service.h"


static const char *TAG = "sntp_service";
static int sntp_service_ID = 0; // 非0才是有效注册
static sntp_service_send_data_t sntp_time; // 记录的时间
static void sntp_service_task(void *arg); // sntp服务任务
static void handle_get_time(sntp_service_receive_data_t *payload); 
static void handle_reply(QueueHandle_t reply_queue);
static void time_sync_notification_cb(struct timeval *tv);
static QueueHandle_t   sntp_service_request_queue; // 接收来自事件循环的 app_event_t*

// ===============api==========================
esp_err_t sntp_service_init(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    // 设置时间同步回调
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb); // 必须设置时间回调函数
    // 配置SNTP服务器
    esp_sntp_setservername(0, PRIMARY_SNTP_SERVER);
    esp_sntp_setservername(1, SECONDARY_SNTP_SERVER);
    // 设置SNTP操作模式
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_set_sync_interval(60000); // 1s
    // 初始化SNTP服务
    esp_sntp_init();
    setenv("TZ", "CST-8", 1); 
    tzset();    // 生效时区配置

    // 创建外部请求队列
    sntp_service_request_queue = xQueueCreate(8, sizeof(app_event_t*));
    if (! sntp_service_request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return ESP_ERR_NO_MEM;
    }

    // 向事件循环注册
    sntp_service_ID = event_loop_register_service("sntp_service_task", sntp_service_request_queue);
    // 启动服务任务
    xTaskCreate(sntp_service_task, "sntp_service_task", 2048, NULL, 5, NULL);  // 对外服务
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

int get_sntp_service_ID(void){
    return sntp_service_ID;
}

// 
void time_sync_notification_cb(struct timeval *tv)
{
    //ESP_LOGI(TAG, "Time synchronized!");
    sntp_time.service_stata = SNTP_SRV_OK;
}


// sntp服务任务
static void sntp_service_task(void *arg){
   
    // 分发服务请求
    sntp_service_receive_data_t *payload;
    while (1) {
        if (xQueueReceive( sntp_service_request_queue, &payload, portMAX_DELAY) == pdTRUE) {
            switch (payload->cmd) {
                case SNTP_CMD_GET_TIME: {
                    // 请求最新时间
                    handle_get_time(payload);
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
    
void handle_get_time(sntp_service_receive_data_t *payload)
{
    // 直接从系统获取当前时间(硬件时间)
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    sntp_time.year = timeinfo.tm_year + 1900;
    sntp_time.month = timeinfo.tm_mon + 1;
    sntp_time.day = timeinfo.tm_mday;
    sntp_time.weekday = timeinfo.tm_wday;
    sntp_time.hour = timeinfo.tm_hour;
    sntp_time.min = timeinfo.tm_min;
    sntp_time.sec = timeinfo.tm_sec;
    strftime(sntp_time.current_time, sizeof(sntp_time.current_time), "%H:%M:%S", &timeinfo);
    //sntp_time.service_stata = SNTP_SRV_OK;

    // 回复请求
    if( payload->reply_queue){
        handle_reply( payload->reply_queue);
    }
}

// 处理回复
static void handle_reply(QueueHandle_t reply_queue){
    sntp_service_send_data_t *payload = malloc(sizeof(sntp_service_send_data_t));
    // 拷贝数据
    payload->year = sntp_time.year;
    payload->month = sntp_time.month;
    payload->day = sntp_time.day;
    payload->weekday = sntp_time.weekday;
    payload->hour = sntp_time.hour;
    payload->min = sntp_time.min;
    payload->sec = sntp_time.sec;
    strcpy(payload->current_time,sntp_time.current_time);
    payload->service_stata = sntp_time.service_stata; // 服务状态
    xQueueSend(reply_queue, &payload, 0);

}