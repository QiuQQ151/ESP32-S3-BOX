#pragma once
// system_event.h
#include "esp_err.h"
#include "freertos/queue.h"

// 定义来源
typedef enum{
  HAL = 0,  // driver HAL层事件
  UI_SERVICE = 1,
  SNTP_SERVICE,
  WIFI_SERVICE,
  POWER_SERVICE,
  LED_SERVICE,
  AUDIO_SERVICE,
  KEYHAL_SERVICE,
} service_id_t;

// 定义事件类型
typedef enum{
  REQUEST = 1, // 请求服务
  NOTIFICATION, // 服务主动通知
} event_type_t;

// 定义事件数据结构
typedef struct {
    int service_id;             // 标识服务来源
    event_type_t event_type;    // 标识事件类型
    QueueHandle_t reply_queue;  // 事件处理的回复队列（NULL表示不需要回复）
    void *data;                 // 携带数据（结构通过事件类型确定：服务请求使用目标服务的请求结构，服务主动通知使用本服务的通知结构）
    size_t data_len;
} event_data_t;

