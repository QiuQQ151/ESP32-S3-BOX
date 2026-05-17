#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "event_loop_service.h"


#define MAX_SERVICES 50  // 最大支持的服务数量
typedef struct {
    bool  used;           // 条目是否被占用
    int   source_id;      // 服务ID
    char  name[16];       // 服务名称（调试）
    QueueHandle_t   queue;          // 该服务的请求队列
} service_entry_t;
static service_entry_t s_service_table[MAX_SERVICES];

QueueHandle_t main_event_queue = NULL; // 系统请求接收队列
static TaskHandle_t   event_loop_task_handle;  

static const char *TAG = "event_loop_service";


static void event_loop_task(void *pvParameters);

// 获取全局事件队列，供其他任务发送事件用
QueueHandle_t get_main_event_queue(void) {
    return main_event_queue;
}


// 生成新的唯一 source ID（简单递增）
static int generate_source_id(void) {
    static int next_id = 1;   // 0 保留为无效 ID
    return next_id++;
}

// 注册服务
int event_loop_register_service(const char *name, QueueHandle_t service_queue) {
    if (service_queue == NULL) {
        ESP_LOGE(TAG, "Invalid service queue for %s", name);
        return 0;
    }
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (!s_service_table[i].used) {
            // 找出未使用的服务id
            int new_id = generate_source_id();
            s_service_table[i].used = true;
            s_service_table[i].source_id = new_id;
            strncpy(s_service_table[i].name, name, sizeof(s_service_table[i].name) - 1);
            s_service_table[i].name[sizeof(s_service_table[i].name) - 1] = '\0';
            s_service_table[i].queue = service_queue;
            ESP_LOGI(TAG, "Registered service '%s' with ID %lu", name, (unsigned long)new_id);
            return new_id;
        }
    }
    ESP_LOGE(TAG, "Service table full, cannot register %s", name);
    return 0;
}

// 根据 source ID 查找对应的服务队列
static QueueHandle_t find_service_queue(int source) {
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (s_service_table[i].used && s_service_table[i].source_id == source) {
            return s_service_table[i].queue;
        }
    }
    return NULL;
}


// 事件循环任务
static void event_loop_task(void *pvParameters) {
    ESP_LOGI(TAG, "Event loop task started");
    app_event_t *evt;
    while (1) {
        if (xQueueReceive(main_event_queue, &evt, portMAX_DELAY)) {
            ESP_LOGD(TAG, "Received event from source ID %lu", (unsigned long)evt->source);
            QueueHandle_t target_queue = find_service_queue(evt->source);
            if(target_queue == 0){
                ESP_LOGW(TAG, "handle by event loop: ID %lu", (unsigned long)evt->source);
                if (evt->payload) free(evt->payload);
            }
            else if (target_queue) {
                // 将 payload 发送给目标服务队列（注意：这里发送的是 payload 指针，而不是 evt 本身）
                if (xQueueSend(target_queue, &evt->payload, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Target queue full, dropping event from source %lu", (unsigned long)evt->source);
                    // 若发送失败，需要释放 payload，避免内存泄漏
                    if (evt->payload) free(evt->payload);
                }
            } else {
                ESP_LOGW(TAG, "No service registered for source ID %lu", (unsigned long)evt->source);
                // 无人认领则释放 payload
                if (evt->payload) free(evt->payload);
            }
            // 释放事件外壳
            free(evt);
        }
    }
}


void event_loop_service_init(void){
    // 系统请求队列
    main_event_queue = xQueueCreate(20, sizeof(app_event_t*)); // 指针
    if (main_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create main event queue");
        return;
    }   
   // 创建转发任务
    xTaskCreate(event_loop_task, "event_loop_task", 4096, NULL, 10, &event_loop_task_handle);
    ESP_LOGI(TAG, "Initialized");
}

