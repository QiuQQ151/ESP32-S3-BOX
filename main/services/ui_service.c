#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "system_config.h"
#include "hal/lvgl_hal.h"
#include "lvgl.h"
#include "services/event_loop_service.h"
#include "ui_service.h"

static const char *TAG = "ui_service";
static int ui_service_ID = 0; // 非0才是有效注册
static lv_disp_t *disp = NULL;
static void ui_service_update_task(void *arg); //页面更新
static void ui_service_task(void *arg); // 服务任务
// static void handle_(xxxxx_service_receive_data_t *payload); 
// static void handle_reply(QueueHandle_t reply_queue);
static QueueHandle_t  ui_service_request_queue; // 接收来自事件循环的 app_event_t*

// ===============api==========================
esp_err_t ui_service_init(void)
{
    ESP_LOGI(TAG, "Initializing xxxx");
    // 初始化内容
    disp = lvgl_hal_init();

    // 创建外部请求队列
    ui_service_request_queue = xQueueCreate(8, sizeof(app_event_t*));
    if (! ui_service_request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return ESP_ERR_NO_MEM;
    }

    // 向事件循环注册
    ui_service_ID = event_loop_register_service("ui_service_task", ui_service_request_queue);
    // 启动服务任务
    xTaskCreate(ui_service_update_task, "ui_service_update_task", 10*1024, NULL, 5, NULL); 
    xTaskCreate(ui_service_task, "ui_service_task", 30*1024, NULL, 5, NULL);  // 对外服务
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

int get_ui_service_ID(void){
    return ui_service_ID;
}

static void ui_service_update_task(void *arg){
    while (1)
    {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(20));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
    }
} 

// 按钮点击回调函数
static void btn_click_event_cb(lv_event_t *e)
{
    // 获取用户数据（即计数器标签对象指针）
    lv_obj_t *counter_label = (lv_obj_t *)lv_event_get_user_data(e);
    if (counter_label) {
        // 读取当前数字并增加
        const char *text = lv_label_get_text(counter_label);
        int count = 0;
        if (text && sscanf(text, "Count: %d", &count) == 1) {
            count++;
        } else {
            count = 1;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "Count: %d", count);
        lv_label_set_text(counter_label, buf);
    }
}

// 服务任务
static void ui_service_task(void *arg){
   
    // 创建测试界面
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_clean(scr);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "LVGL Ready");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *counter_label = lv_label_create(scr);
    lv_obj_align(counter_label, LV_ALIGN_CENTER, 0, 30);
    lv_label_set_text(counter_label, "Count: 0");
    //lv_obj_set_style_text_font(counter_label, &font_alipuhui20, 0); 
    lv_obj_set_style_text_color(counter_label, lv_color_hex(0x00FF00), 0);

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 80);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click Me");
    lv_obj_center(btn_label);
    lv_obj_add_event_cb(btn, btn_click_event_cb, LV_EVENT_CLICKED, counter_label);


    // 分发服务请求
    ui_service_receive_data_t *payload;
    while (1) {
        if (xQueueReceive( ui_service_request_queue, &payload, portMAX_DELAY) == pdTRUE) {
            switch (payload->cmd) {
                case 0: {
                    // 
                    //handle_(payload);
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
    
// void handle_(xxx_service_receive_data_t *payload)
// {
//     // 具体操作

//     // 回复请求
//     if( payload->reply_queue){
//         handle_reply( payload->reply_queue);
//     }
// }

// // 处理回复
// static void handle_reply(QueueHandle_t reply_queue){
//     xxx_service_send_data_t *payload = malloc(sizeof(_service_send_data_t));
//     // 拷贝数据
//     // ...

//     xQueueSend(reply_queue, &payload, 0);
// }