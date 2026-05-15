#include "ui_service.h"
#include "esp_log.h"
#include "lvgl.h"
#include "radio_app.h"
#include <stdio.h>
#include "event_loop_service.h"
#include "sntp_service.h" // 获取时间更新


static const char *TAG = "radio_app";
static ui_app_t s_radio_app;

// ---------- 图标图片声明 ----------
LV_IMG_DECLARE(icon_next_40);
LV_IMG_DECLARE(icon_play_40);
LV_IMG_DECLARE(icon_prev_40);
LV_IMG_DECLARE(icon_reflash_28);
LV_IMG_DECLARE(icon_stop_40);
LV_IMG_DECLARE(icon_wifion_15);
LV_IMG_DECLARE(icon_wifioff_15);

// ---------- 预留接口 ----------
static void radio_update_status_task(void *arg);
static void radio_play_pause(void);
static void radio_prev_channel(void);
static void radio_next_channel(void);
static void radio_refresh(void);
static void radio_update_time(void); // 请求更新时间（从sntp_service获取）

// ---------- 内部 UI 句柄 ----------
static struct {
    lv_obj_t *time_label;
    lv_obj_t *wifi_icon;
    lv_obj_t *channel_label;
    lv_obj_t *play_btn;
    lv_obj_t *play_icon;
    bool playing;
} s_radio_ui = {0};

// ---------- 回调声明 ----------
static void radio_on_create(ui_app_t *app);
static void radio_on_resume(ui_app_t *app);
static void radio_on_pause(ui_app_t *app);
static void radio_on_destroy(ui_app_t *app);
static bool radio_on_key_event(ui_app_t *app, void *key_event);
static void radio_on_receive_data(ui_app_t *app, uint32_t cmd, void *data, size_t len);

// ---------- 按钮事件 ----------
static void play_btn_event_cb(lv_event_t *e) { radio_play_pause(); }
static void prev_btn_event_cb(lv_event_t *e) { radio_prev_channel(); }
static void next_btn_event_cb(lv_event_t *e) { radio_next_channel(); }
static void refresh_btn_event_cb(lv_event_t *e) { radio_refresh(); }


void radio_app_register(void)
{
    s_radio_app.name = "radio_app";
    s_radio_app.screen = NULL;
    s_radio_app.on_create = radio_on_create;
    s_radio_app.on_resume = radio_on_resume;
    s_radio_app.on_pause = radio_on_pause;
    s_radio_app.on_destroy = radio_on_destroy;
    s_radio_app.on_key_event = radio_on_key_event;
    s_radio_app.on_receive_data = radio_on_receive_data;

    ui_service_register_app(&s_radio_app);
    ESP_LOGI(TAG, "Radio app registered");
}

static void radio_on_create(ui_app_t *app)
{
    ESP_LOGI(TAG, "radio_on_create");
    app->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(app->screen, lv_color_black(), 0);

    // ========== 顶部状态栏 ==========
    lv_obj_t *top_bar = lv_obj_create(app->screen);
    lv_obj_set_size(top_bar, 160, 32);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 28);   // 下移避免裁切
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);

    // WiFi 图标
    lv_obj_t *wifi_icon = lv_img_create(top_bar);
    lv_img_set_src(wifi_icon, &icon_wifion_15);
    lv_obj_align(wifi_icon, LV_ALIGN_LEFT_MID, 0, 0);
    s_radio_ui.wifi_icon = wifi_icon;

    // 时间
    lv_obj_t *time_label = lv_label_create(top_bar);
    lv_label_set_text(time_label, "12:00");
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_obj_align(time_label, LV_ALIGN_RIGHT_MID, 0, 0);
    s_radio_ui.time_label = time_label;

    // ========== 频道滚动 + 刷新按钮 ==========
    lv_obj_t *channel_area = lv_obj_create(app->screen);
    lv_obj_set_size(channel_area, 180, 36);
    lv_obj_align(channel_area, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(channel_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(channel_area, 0, 0);
    lv_obj_set_style_pad_all(channel_area, 0, 0);

    lv_obj_t *channel_label = lv_label_create(channel_area);
    lv_label_set_text(channel_label, "87.5 MHz");
    lv_obj_set_style_text_color(channel_label, lv_color_white(), 0);
    LV_FONT_DECLARE(font_alipuhui20);
    lv_obj_set_style_text_font(channel_label, &font_alipuhui20, 0);
    lv_obj_set_width(channel_label, 135);
    lv_label_set_long_mode(channel_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(channel_label, LV_ALIGN_LEFT_MID, 0, 0);
    s_radio_ui.channel_label = channel_label;

    // 刷新按钮：直径 30 圆形，浅蓝色背景
    lv_obj_t *refresh_btn = lv_btn_create(channel_area);
    lv_obj_set_size(refresh_btn, 30, 30);
    lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(refresh_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x4FC3F7), 0);   // 浅蓝
    lv_obj_set_style_bg_opa(refresh_btn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(refresh_btn, refresh_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *refresh_img = lv_img_create(refresh_btn);
    lv_img_set_src(refresh_img, &icon_reflash_28);
    lv_obj_center(refresh_img);

    // ========== 底部控制栏 ==========
    lv_obj_t *control_bar = lv_obj_create(app->screen);
    lv_obj_set_size(control_bar, 180, 55);
    lv_obj_align(control_bar, LV_ALIGN_BOTTOM_MID, 0, -40);   
    lv_obj_set_style_bg_opa(control_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(control_bar, 0, 0);
    lv_obj_set_style_pad_all(control_bar, 0, 0);

    // 上一曲按钮：深蓝灰背景
    lv_obj_t *prev_btn = lv_btn_create(control_bar);
    lv_obj_set_size(prev_btn, 43, 43);
    lv_obj_set_style_radius(prev_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(prev_btn, lv_color_hex(0x546E7A), 0);
    lv_obj_set_style_bg_opa(prev_btn, LV_OPA_COVER, 0);
    lv_obj_align(prev_btn, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(prev_btn, prev_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *prev_img = lv_img_create(prev_btn);
    lv_img_set_src(prev_img, &icon_prev_40);
    lv_obj_center(prev_img);

    // 下一曲按钮：深蓝灰背景
    lv_obj_t *next_btn = lv_btn_create(control_bar);
    lv_obj_set_size(next_btn, 43, 43);
    lv_obj_set_style_radius(next_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(next_btn, lv_color_hex(0x546E7A), 0);
    lv_obj_set_style_bg_opa(next_btn, LV_OPA_COVER, 0);
    lv_obj_align(next_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_event_cb(next_btn, next_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *next_img = lv_img_create(next_btn);
    lv_img_set_src(next_img, &icon_next_40);
    lv_obj_center(next_img);

    // 播放/暂停按钮：绿色背景
    lv_obj_t *play_btn = lv_btn_create(control_bar);
    lv_obj_set_size(play_btn, 43, 43);
    lv_obj_set_style_radius(play_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x4CAF50), 0);   // 绿色
    lv_obj_set_style_bg_opa(play_btn, LV_OPA_COVER, 0);
    lv_obj_align(play_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(play_btn, play_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *play_img = lv_img_create(play_btn);
    lv_img_set_src(play_img, &icon_play_40);
    lv_obj_center(play_img);
    s_radio_ui.play_btn = play_btn;
    s_radio_ui.play_icon = play_img;
    s_radio_ui.playing = false;

    // 启动一个任务定期更新时间和WiFi状态
    xTaskCreate(radio_update_status_task, "radio_update_status_task", 2*1024, NULL, 5, NULL);                        
    ESP_LOGI(TAG, "Radio UI created");
}

// ---------- 其余回调 ----------
static void radio_update_status_task(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // 每1秒请求查询一次
        radio_update_time(); // 发送时间更新请求
                             // 发送wifi状态更新请求
    }
}

static void radio_on_resume(ui_app_t *app) {
    ESP_LOGI(TAG, "radio_on_resume");
}
static void radio_on_pause(ui_app_t *app) {
    ESP_LOGI(TAG, "radio_on_pause");
}
static void radio_on_destroy(ui_app_t *app) {
    ESP_LOGI(TAG, "radio_on_destroy");
    if (app->screen) {
        lv_obj_del(app->screen);
        app->screen = NULL;
    }
    memset(&s_radio_ui, 0, sizeof(s_radio_ui));
}

// 处理按键事件，返回 true 表示已处理，不需要默认处理
static bool radio_on_key_event(ui_app_t *app, void *key_event) {
    key_event_data_t *key = (key_event_data_t *)key_event;
    if (key->event == KEY_EVENT_PRESS) {
        ESP_LOGI(TAG, "Key press: id=%d", key->key_id);
        if (key->key_id == KEY_ID_ENTER) {
            radio_play_pause();
            return true;
        } else if (key->key_id == KEY_ID_BACK) {
            ui_service_go_home();
            return true;
        }
    } else if (key->event == KEY_EVENT_ROTATE_CW) {
        ESP_LOGI(TAG, "Rotate CW");
       
        return true;
    } else if (key->event == KEY_EVENT_ROTATE_CCW) {
        ESP_LOGI(TAG, "Rotate CCW");
        
        return true;
    }
    return false;
}

// 处理其他服务发送的自定义数据
static void radio_on_receive_data(ui_app_t *app, uint32_t cmd, void *data, size_t len) {
    ESP_LOGI(TAG, "Received custom command %" PRIu32, cmd);
    
}

// ========== 接口实现 ==========
static void radio_play_pause(void) {
    if (s_radio_ui.play_icon == NULL) return;
    if (s_radio_ui.playing) {
        ESP_LOGI(TAG, "pause");
        lv_img_set_src(s_radio_ui.play_icon, &icon_play_40);
        s_radio_ui.playing = false;
    } else {
        ESP_LOGI(TAG, "play");
        lv_img_set_src(s_radio_ui.play_icon, &icon_stop_40);
        s_radio_ui.playing = true;
    }
}
static void radio_prev_channel(void) {
    ESP_LOGI(TAG, "prev channel");
}
static void radio_next_channel(void) {
    ESP_LOGI(TAG, "next channel");
}
static void radio_refresh(void) {
    ESP_LOGI(TAG, "refresh");
}

// 提高音量
static void radio_increase_volume(void) {
    ESP_LOGI(TAG, "increase volume");
    
}

// 降低音量
static void radio_decrease_volume(void) {
    ESP_LOGI(TAG, "decrease volume");

}


// // ===============请求服务
// typedef enum {
//     SNTP_CMD_GET_TIME = 0,             // 查询sntp时间
// } sntp_service_cmd_t;
// typedef struct {
//     uint32_t cmd;  // 请求服务
//     QueueHandle_t reply_queue;   // 结果通知队列 (可为 NULL)
// } sntp_service_receive_data_t;


// 获取时间
static void radio_update_time(void) {
    // 请求一次sntp服务
    sntp_service_receive_data_t *payload = (sntp_service_receive_data_t*)malloc(sizeof(sntp_service_receive_data_t));
    if(payload){
        // 分配SNTP信息
        payload->cmd = SNTP_CMD_GET_TIME;
        payload->reply_queue = NULL;
    }
    
    QueueHandle_t main_event_queue = get_main_event_queue();
    app_event_t *evt = malloc(sizeof(app_event_t));
    if(evt){
        // 构造事件外壳
        evt-> source = get_sntp_service_ID();
        evt->payload = payload;
        ESP_LOGI(TAG,"req sntp time");
        xQueueSend(main_event_queue, &evt, 0);
        payload->reply_queue = get_ui_service_queue(); 
    }

}