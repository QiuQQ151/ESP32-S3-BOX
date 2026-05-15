#ifndef SERVICES_EVENT_SNTP_SERVICE_H
#define SERVICES_EVENT_SNTP_SERVICE_H


// =========================服务对外接口======================================================

// ===============请求服务
typedef enum {
    SNTP_CMD_GET_TIME = 0,             // 查询sntp时间
} sntp_service_cmd_t;
typedef struct {
    uint32_t cmd;  // 请求服务
    QueueHandle_t reply_queue;   // 结果通知队列 (可为 NULL)
} sntp_service_receive_data_t;


// ==================服务回复
typedef enum {
    // 当前状态
    SNTP_SRV_OK = 0,    
    SNTP_SRV_STATE_ERROR, // 响应错误
    SNTP_SRV_ERR_WIFI,    
    SNTP_SRV_ERR_INT,
} sntp_service_state_t;

// 服务发给reply_queue的数据结构
typedef struct {
    int service_id;   // 标识服务来源
    int year;
    int month;
    int day;
    int weekday;
    int hour;
    int min;
    int sec;
    char current_time[20]; // 当前时间字符串（格式：HH:MM:SS）(24h)
    sntp_service_state_t service_stata;    // 服务回复
} sntp_service_send_data_t;


esp_err_t sntp_service_init(void);  // 初始化sntp服务（由系统启动）   
int get_sntp_service_ID(void);

#endif