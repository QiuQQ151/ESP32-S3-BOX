#pragma once
// =========================服务对外接口======================================================

// ===============请求服务
typedef enum {
    _CMD_ = 0,             // xxxx
} ui_service_cmd_t;
typedef struct {
    uint32_t cmd;  // 请求服务
    QueueHandle_t reply_queue;   // 结果通知队列 (可为 NULL)
} ui_service_receive_data_t;


// ==================服务回复
typedef enum {
    // 当前状态
    UI_SRV_OK = 0,    
    UI_SRV_STATE_ERROR, // 响应错误
} ui_service_state_t;

// 服务发给reply_queue的数据结构
typedef struct {
    // ......

    ui_service_state_t service_stata;    // 服务回复
} ui_service_send_data_t;


esp_err_t ui_service_init(void);  // 初始化  服务（由系统启动）   
int get_ui_service_ID(void);
