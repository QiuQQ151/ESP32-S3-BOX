// main/services/wifi_service.h
#ifndef SERVICES_WIFI_SERVICE_H
#define SERVICES_WIFI_SERVICE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "hal/wifi_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// =========================服务对外接口======================================================

// ===============请求服务
typedef enum {
    WIFI_CMD_CONNECT        = 0x2000,  // 连接指定 AP
    WIFI_CMD_DISCONNECT,               // 断开当前连接
    WIFI_CMD_CONNECT_SAVED,            // 连接已保存的网络
    WIFI_CMD_CHEACK_STATA,             // 查询WiFi状态
} wifi_service_cmd_t;
// 服务接收reply_queue的数据结构
typedef struct {
    uint32_t cmd;  // 请求服务
    char ssid[33];
    char password[65];
    bool save;                   // 是否保存至 NVS
    QueueHandle_t reply_queue;   // 结果通知队列 (可为 NULL)
} wifi_service_receive_data_t;


// ==================服务通知
typedef enum {
    // 当前状态
    WIFI_SRV_STATE_IDLE = 0, //空闲
    WIFI_SRV_STATE_CONNECTING, // 连接中
    WIFI_SRV_STATE_CONNECTED,  // 已连接
    WIFI_SRV_STATE_DISCONNECTED, // 已断开连接
    WIFI_SRV_STATE_AP_MODE,  // AP模式
    WIFI_SRV_STATE_ERROR, // 响应错误
    
    // 执行结果
    WIFI_SRV_OK,
    WIFI_SRV_ERR_NVS,    
    WIFI_SRV_ERR_SSID,
    WIFI_SRV_ERR_PASS,
} wifi_service_state_t;

// 服务发给reply_queue的数据结构
typedef struct {
    char ssid[33];          // 动态分配，接收方负责 free
    char password[65];    // 动态分配，接收方负责 free
    char ip_address[20];
    int8_t rssi;
    wifi_service_state_t service_stata;    // 服务通知
} wifi_service_send_data_t;

/**
 * @brief 初始化 WiFi 服务
 *        1. 初始化底层 HAL
 *        2. 加载 NVS 凭证
 *        3. 创建服务任务及内部请求队列
 *        4. 向全局事件循环注册 EVENT_SRC_WIFI
 */
esp_err_t wifi_service_init(void);  // 初始化wifi服务（由系统启动）   
QueueHandle_t get_wifi_service_queue(void); // 获取wifi服务的请求队列

#ifdef __cplusplus
}
#endif

#endif