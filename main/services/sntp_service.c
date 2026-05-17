#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "system_config.h"
#include "services/system_event.h"
#include "services/wifi_service.h"
#include "services/sntp_service.h"

static const char *TAG = "sntp_service";

static sntp_service_send_data_t sntp_time; // 记录的时间
static void sntp_service_task(void *arg); // sntp服务任务
static TaskHandle_t sntp_service_check_wifi_task_handle = NULL; // 检查WiFi连接任务句柄
static void sntp_service_check_wifi_task(void *arg); // 检查WiFi连接任务
static void handle_get_time(sntp_service_receive_data_t *payload); 
static void handle_reply(QueueHandle_t reply_queue);

static sntp_service_state_t sntp_service_state = SNTP_SERVICE_ERR_WIFI; // 
static QueueHandle_t   sntp_service_request_queue; // 

// ===============api==========================
esp_err_t sntp_service_init(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    // 创建外部请求队列
    sntp_service_request_queue = xQueueCreate(8, sizeof(event_data_t*));
    if (! sntp_service_request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return ESP_ERR_NO_MEM;
    }
    // 启动任务
    xTaskCreate(sntp_service_task, "sntp_service_task", 4096, NULL, 5, NULL);  
    return ESP_OK;
}

QueueHandle_t get_sntp_service_queue(void){
    return sntp_service_request_queue;
}

void time_sync_notification_cb(struct timeval *tv)
{
    //ESP_LOGI(TAG, "Time synchronized!");
    sntp_time.service_stata = SNTP_SERVICE_OK;
}

// =======================================内部函数=======================================


static void sntp_service_check_wifi_task(void *arg){    
    while (sntp_service_state == SNTP_SERVICE_ERR_WIFI) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        // 等待WiFi连接
        // 请求一次WiFi连接状态
        wifi_service_receive_data_t *wifi_payload = (wifi_service_receive_data_t*)malloc(sizeof(wifi_service_receive_data_t));
        if(wifi_payload){
            // 分配WiFi信息
            wifi_payload->cmd = WIFI_CMD_CHEACK_STATA;
            event_data_t *evt_data = malloc(sizeof(event_data_t));
            if(evt_data){
                evt_data->service_id = SNTP_SERVICE; //
                evt_data->event_type = REQUEST;
                evt_data->reply_queue = sntp_service_request_queue; // 需要回复
                evt_data->data = wifi_payload;
                ESP_LOGI(TAG,"req wifi check stata");
                if (xQueueSend(get_wifi_service_queue(), &evt_data, 0) != pdPASS) {
                    free(wifi_payload);
                    free(evt_data);
                }
            } else{
                ESP_LOGE(TAG,"malloc event_data_t err");
                free(wifi_payload);
            }
        }

    }
    ESP_LOGI(TAG,"wifi is connected");
    // 进行时间同步配置
    // 设置时间同步回调
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb); // 必须设置时间回调函数
    // 配置SNTP服务器
    esp_sntp_setservername(0, PRIMARY_SNTP_SERVER);
    esp_sntp_setservername(1, SECONDARY_SNTP_SERVER);
    // 设置SNTP操作模式
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_set_sync_interval(60000); // 60s
    // 初始化SNTP服务
    esp_sntp_init();
    setenv("TZ", "CST-8", 1); 
    tzset();    // 生效时区配置
    ESP_LOGI(TAG, "Initializing");    
    vTaskDelete(NULL);
}

// sntp服务任务
static void sntp_service_task(void *arg){    

    // 启动检查WiFi连接任务
    xTaskCreate(sntp_service_check_wifi_task, "sntp_service_check_wifi_task", 4096, NULL, 2, &sntp_service_check_wifi_task_handle); // 

    // 分发服务请求
    event_data_t *evt_data = NULL;
    while (1) {
        if (xQueueReceive( sntp_service_request_queue, &evt_data, portMAX_DELAY) == pdTRUE) {
            if(evt_data->event_type == REQUEST){
                // 请求本服务
                sntp_service_receive_data_t *req_payload = (sntp_service_receive_data_t *)evt_data->data;
                switch (req_payload->cmd) {
                    case SNTP_CMD_GET_TIME: {
                        // 请求最新时间
                        handle_get_time(req_payload);
                        break;
                    }           
                    default:
                        ESP_LOGW(TAG, "Unknown cmd: 0x%d", req_payload->cmd);
                        break;
                }

            } else if(evt_data->event_type == NOTIFICATION){
                // 来自其它服务的通知
                switch (evt_data->service_id) {
                    case WIFI_SERVICE:
                        // 处理WiFi服务通知
                        ESP_LOGI(TAG,"wifi service notification");
                        wifi_service_send_data_t *wifi_payload = (wifi_service_send_data_t *)evt_data->data;
                        if(wifi_payload){
                            sntp_service_state = (wifi_payload->service_stata == WIFI_SRV_STATE_CONNECTED) ? SNTP_SERVICE_OK_WIFI : SNTP_SERVICE_ERR_WIFI;
                            ESP_LOGI(TAG,"wifi service notification: STATA %d", sntp_service_state);
                        }
                        break;
                    default:
                        ESP_LOGW(TAG, "Unknown service_id: 0x%d", evt_data->service_id);
                        break;
                }

            } else{
                ESP_LOGW(TAG, "Unknown event_type: 0x%d", evt_data->event_type);
            }
            if(evt_data->reply_queue){
                handle_reply(evt_data->reply_queue);
            }            
            // 释放内存空间
            if(evt_data->data){
                free(evt_data->data);
                evt_data->data = NULL;
            }
            if(evt_data){
                free(evt_data);
                evt_data = NULL;
            }
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
    //ESP_LOGI(TAG,"get time: %s", sntp_time.current_time);
}

// 处理回复
static void handle_reply(QueueHandle_t reply_queue){
    sntp_service_send_data_t *payload = malloc(sizeof(sntp_service_send_data_t));
    if(!payload){
        ESP_LOGE(TAG,"malloc sntp_service_send_data_t err");
        return;
    } else{
        // 拷贝数据
        payload->service_id = SNTP_SERVICE; // 标识服务来源
        payload->year = sntp_time.year;
        payload->month = sntp_time.month;
        payload->day = sntp_time.day;
        payload->weekday = sntp_time.weekday;
        payload->hour = sntp_time.hour;
        payload->min = sntp_time.min;
        payload->sec = sntp_time.sec;
        strcpy(payload->current_time,sntp_time.current_time);
        payload->service_stata = sntp_time.service_stata; // 服务状态

        // 回复事件
        event_data_t *evt_data = malloc(sizeof(event_data_t));
        if(!evt_data){
            ESP_LOGE(TAG,"malloc event_data_t err");
            free(payload);
            return;
        } else{
            evt_data->event_type = NOTIFICATION;
            evt_data->service_id = SNTP_SERVICE;
            evt_data->reply_queue = NULL;
            evt_data->data = payload;
            if (xQueueSend(reply_queue, &evt_data, 0) != pdPASS) {
                ESP_LOGE(TAG,"xQueueSend err");
                free(payload);
                free(evt_data);
            }            
        }
    }
}