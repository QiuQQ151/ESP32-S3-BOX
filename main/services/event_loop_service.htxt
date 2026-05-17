// main/services/event_loop_service.h
#ifndef SERVICES_EVENT_LOOP_SERVICE_H
#define SERVICES_EVENT_LOOP_SERVICE_H

/* 事件结构 */
// 要通信的任务要在.h中提供自己的cmd、收发数据结构，以供其它任务使用
typedef struct {
    int source;   // 来源
    void *payload;   // 携带数据（由发方创建，收方释放）
} app_event_t;

void event_loop_service_init(void);
QueueHandle_t get_main_event_queue(void); // 获取event队列
int event_loop_register_service(const char *name, QueueHandle_t service_queue); //注册服务

#endif