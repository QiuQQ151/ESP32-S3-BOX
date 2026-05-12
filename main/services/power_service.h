#ifndef SERVICES_EVENT_POWER_SERVICE_H
#define SERVICES_EVENT_POWER_SERVICE_H


// =========================服务对外接口======================================================

// ===============请求服务
typedef enum {
    LCD_ON_CMD = 0,             // 开启LCD背光
    LCD_OFF_CMD, // 关闭LCD背光

} power_service_cmd_t;
typedef struct {
    uint32_t cmd;  // 请求服务
    QueueHandle_t reply_queue;   // 结果通知队列 (可为 NULL)
} power_service_receive_data_t;


// ==================服务回复
typedef enum {
    // 当前状态
    POWER_SRV_OK = 0,    
    POWER_SRV_ERROR, // 响应错误
} power_service_state_t;

// 服务发给reply_queue的数据结构
typedef struct {
    // ......

    power_service_state_t service_stata;    // 服务回复
} power_service_send_data_t;


esp_err_t power_service_init(void);  // 初始化  服务（由系统启动）   
int get_power_service_ID(void);

#endif