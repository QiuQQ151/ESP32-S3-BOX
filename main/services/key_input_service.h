#ifndef SERVICES_EVENT_KEY_INPUT_SERVICE_H
#define SERVICES_EVENT_KEY_INPUT_SERVICE_H


// =========================服务对外接口======================================================

// ===============请求服务
typedef enum {
    KEY_INPUT_CMD = 0,  // 通用按键输入处理
    ENC_INPUT_CMD,      //编码器输入
} key_intput_service_cmd_t;
typedef struct {
    uint32_t cmd;  // 请求服务
    union {
        uint8_t  key_id;        // 按键编号
        int8_t   enc_dir;       // 编码器方向 +1/-1
    } data;    
    QueueHandle_t reply_queue;   // 结果通知队列 (可为 NULL)
} key_input_service_receive_data_t;


// ==================服务回复
typedef enum {
    // 当前状态
    KEY_INPUT_SRV_OK = 0,    
    KEY_INPUT_SRV_STATE_ERROR, // 响应错误
} key_input_service_state_t;

// 服务发给reply_queue的数据结构
typedef struct {
    // ......
    // 不需要回复
    key_input_service_state_t service_stata;    // 服务回复
} key_input_service_send_data_t;


esp_err_t key_input_service_init(void);  // 初始化  服务（由系统启动）   
int get_key_input_service_ID(void);

#endif