#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_service.h"
#include "radio_app.h"
#include "system_event.h"
#include "keys_hal.h"
#include "sntp_service.h"
#include "audio_service.h"
#include "led_service.h"

static const char *TAG = "radio_app";
static ui_app_t s_radio_app;

// ---------- 图片资源声明 ----------
LV_IMG_DECLARE(icon_next_40);
LV_IMG_DECLARE(icon_play_40);
LV_IMG_DECLARE(icon_prev_40);
LV_IMG_DECLARE(icon_reflash_28);
LV_IMG_DECLARE(icon_stop_40);
LV_IMG_DECLARE(icon_wifion_15);
LV_IMG_DECLARE(icon_wifioff_15);

// ---------- 内部 UI 句柄 ----------
static struct {
    lv_obj_t *time_label;
    lv_obj_t *wifi_icon;
    lv_obj_t *channel_label;
    lv_obj_t *play_btn;
    lv_obj_t *play_icon;
    lv_obj_t *volume_slider;   // 新增音量滑块
    bool playing;
} s_radio_ui = {0};

// ---------- 任务句柄 ----------
static TaskHandle_t radio_update_status_task_handle = NULL;

// ---------- 电台定义 ----------
typedef struct {
    const char *name;
    const char *url;
    const audio_service_stream_type_t type;
} radio_station_t;

static const radio_station_t stations[] = {
   {"两广之声音乐台", "http://lhttp.qingting.fm/live/20500149/64k.mp3",mp3_dec},
    {"怀集音乐之声", "http://lhttp.qingting.fm/live/4804/64k.mp3",mp3_dec},
    {"河北音乐广播", "http://lhttp.qingting.fm/live/1649/64k.mp3",mp3_dec},
    {"清晨音乐台", "http://lhttp.qingting.fm/live/4915/64k.mp3",mp3_dec},
    {"江苏经典流行音乐", "http://lhttp.qingting.fm/live/4938/64k.mp3",mp3_dec},
    {"上海流行音乐LoveRadio", "http://lhttp.qingting.fm/live/273/64k.mp3",mp3_dec},
    {"广东音乐之声", "http://lhttp.qingting.fm/live/1260/64k.mp3",mp3_dec},
    {"AsiaFM 亚洲粤语台", "http://lhttp.qingting.fm/live/15318569/64k.mp3",mp3_dec},
    {"上海动感101", "http://lhttp.qingting.fm/live/274/64k.mp3",mp3_dec},
    {"苏州音乐广播", "http://lhttp.qingting.fm/live/2803/64k.mp3",mp3_dec},
    {"哈尔滨音乐广播", "http://lhttp.qingting.fm/live/839/64k.mp3",mp3_dec},
    {"959年代音乐怀旧好声音", "http://lhttp.qingting.fm/live/5021381/64k.mp3",mp3_dec},
    {"怀旧好声音", "http://lhttp.qingting.fm/live/1223/64k.mp3",mp3_dec},
    {"顺德音乐之声", "http://lhttp.qingting.fm/live/20500150/64k.mp3",mp3_dec},
    {"南京音乐广播", "http://lhttp.qingting.fm/live/4963/64k.mp3",mp3_dec},
    {"FM950广西音乐台", "http://lhttp.qingting.fm/live/4875/64k.mp3",mp3_dec},
    {"山西音乐广播", "http://lhttp.qingting.fm/live/4932/64k.mp3",mp3_dec},
    {"浙江音乐调频", "http://lhttp.qingting.fm/live/4866/64k.mp3",mp3_dec},
    {"动听音乐台", "http://lhttp.qingting.fm/live/5022107/64k.mp3",mp3_dec},
    {"成都年代音乐怀旧好声音", "http://lhttp.qingting.fm/live/20211686/64k.mp3",mp3_dec},
    {"北京音乐广播", "http://lhttp.qingting.fm/live/332/64k.mp3",mp3_dec},
    {"深圳飞扬971", "http://lhttp.qingting.fm/live/1271/64k.mp3",mp3_dec},
    {"FM954汽车音乐广播", "http://lhttp.qingting.fm/live/1936/64k.mp3",mp3_dec},
    {"厦门音乐广播", "http://lhttp.qingting.fm/live/1739/64k.mp3",mp3_dec},
    {"山东经典音乐广播", "http://lhttp.qingting.fm/live/20240/64k.mp3",mp3_dec},
    {"FM88.6长沙音乐广播", "http://lhttp.qingting.fm/live/20847/64k.mp3",mp3_dec},
    {"上海经典947", "http://lhttp.qingting.fm/live/267/64k.mp3",mp3_dec},
    {"广东广播电视台珠江之声", "http://lhttp.qingting.fm/live/470/64k.mp3",mp3_dec},
    {"长春广播电视台 FM88.0", "http://lhttp.qingting.fm/live/4850/64k.mp3",mp3_dec},
    {"年代音乐1022", "http://lhttp.qingting.fm/live/20500066/64k.mp3",mp3_dec},
    {"崂山921", "http://lhttp.qingting.fm/live/20212426/64k.mp3",mp3_dec},
    {"AsiaFM HD音乐台", "http://lhttp.qingting.fm/live/15318341/64k.mp3",mp3_dec},
    {"安徽音乐广播", "http://lhttp.qingting.fm/live/1947/64k.mp3",mp3_dec},
    {"武汉经典音乐广播", "http://lhttp.qingting.fm/live/1297/64k.mp3",mp3_dec},
    {"江西音乐广播", "http://lhttp.qingting.fm/live/1802/64k.mp3",mp3_dec},
    {"天津滨海100.5", "http://lhttp.qingting.fm/live/20003/64k.mp3",mp3_dec},
    {"江苏音乐广播PlayFM897", "http://lhttp.qingting.fm/live/4936/64k.mp3",mp3_dec},
    {"江门旅游之声", "http://lhttp.qingting.fm/live/1283/64k.mp3",mp3_dec},
    {"星河音乐", "http://lhttp.qingting.fm/live/20210755/64k.mp3",mp3_dec}, 
    {"西安音乐广播", "http://lhttp.qingting.fm/live/1612/64k.mp3",mp3_dec},
    {"无锡音乐广播", "http://lhttp.qingting.fm/live/2779/64k.mp3",mp3_dec},
    {"长沙FM101.7城市之声", "http://lhttp.qingting.fm/live/4237/64k.mp3",mp3_dec},
    {"惠州音乐广播", "http://lhttp.qingting.fm/live/5021523/64k.mp3",mp3_dec},
    {"杭州FM90.7", "http://lhttp.qingting.fm/live/15318146/64k.mp3",mp3_dec},
};
static const int station_count = sizeof(stations) / sizeof(stations[0]);

static int selected_station_index = 0;   // 当前选中的电台索引
static int volume = 50; // 输出音量 0-100

// ---------- 内部函数声明 ----------
static void radio_on_create(ui_app_t *app);
static void radio_on_open(ui_app_t *app);
static void radio_on_close(ui_app_t *app);
static void radio_on_destroy(ui_app_t *app);
static void radio_on_event(ui_app_t *app, event_data_t *event);
static bool radio_handle_key_event(key_event_data_t *key);
static void radio_app_led_control(led_mode_t mode, uint32_t arg);
static void radio_handle_change_to_audio(audio_service_cmd_t cmd);
static void radio_update_status_task(void *arg);
static void radio_update_time(void);

// 按钮操作
static void radio_play_pause(void);
static void radio_prev_channel(void);
static void radio_next_channel(void);
static void radio_refresh(void);
static void radio_increase_volume(void);
static void radio_decrease_volume(void);

// 按钮事件回调
static void play_btn_event_cb(lv_event_t *e)   { radio_play_pause(); }
static void prev_btn_event_cb(lv_event_t *e)   { radio_prev_channel(); }
static void next_btn_event_cb(lv_event_t *e)   { radio_next_channel(); }
static void refresh_btn_event_cb(lv_event_t *e){ radio_refresh(); }
static void volume_slider_event_cb(lv_event_t *e); // 新增滑块回调

// ========== 注册函数 ==========
void radio_app_register(void)
{
    memset(&s_radio_app, 0, sizeof(s_radio_app));
    s_radio_app.name = "radio_app";
    s_radio_app.screen = NULL;
    s_radio_app.on_create = radio_on_create;
    s_radio_app.on_open = radio_on_open;
    s_radio_app.on_close = radio_on_close;
    s_radio_app.on_destroy = radio_on_destroy;
    s_radio_app.on_event = radio_on_event;
    ui_service_register_app(&s_radio_app);
    ESP_LOGI(TAG, "Radio app registered");
}

// ========== 生命周期回调 ==========
static void radio_on_create(ui_app_t *app)
{
    ESP_LOGI(TAG, "radio_on_create");
    app->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(app->screen, lv_color_black(), 0);

    // ---------- 顶部状态栏 ----------
    lv_obj_t *top_bar = lv_obj_create(app->screen);
    lv_obj_set_size(top_bar, 160, 32);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);

    s_radio_ui.wifi_icon = lv_img_create(top_bar);
    lv_img_set_src(s_radio_ui.wifi_icon, &icon_wifion_15);
    lv_obj_align(s_radio_ui.wifi_icon, LV_ALIGN_LEFT_MID, 0, 0);

    s_radio_ui.time_label = lv_label_create(top_bar);
    lv_label_set_text(s_radio_ui.time_label, "12:00");
    lv_obj_set_style_text_color(s_radio_ui.time_label, lv_color_white(), 0);
    lv_obj_align(s_radio_ui.time_label, LV_ALIGN_RIGHT_MID, 0, 0);

    // ---------- 频道滚动 + 刷新按钮 ----------
    lv_obj_t *channel_area = lv_obj_create(app->screen);
    lv_obj_set_size(channel_area, 180, 36);
    lv_obj_align(channel_area, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_bg_opa(channel_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(channel_area, 0, 0);
    lv_obj_set_style_pad_all(channel_area, 0, 0);

    s_radio_ui.channel_label = lv_label_create(channel_area);
    lv_label_set_text(s_radio_ui.channel_label,
                      (station_count > 0) ? stations[selected_station_index].name : "无电台");
    lv_obj_set_style_text_color(s_radio_ui.channel_label, lv_color_white(), 0);
    LV_FONT_DECLARE(font_alipuhui20);
    lv_obj_set_style_text_font(s_radio_ui.channel_label, &font_alipuhui20, 0);
    lv_obj_set_width(s_radio_ui.channel_label, 135);
    lv_label_set_long_mode(s_radio_ui.channel_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_radio_ui.channel_label, LV_ALIGN_LEFT_MID, 0, 0);

    // 刷新按钮
    lv_obj_t *refresh_btn = lv_btn_create(channel_area);
    lv_obj_set_size(refresh_btn, 30, 30);
    lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(refresh_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_style_bg_opa(refresh_btn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(refresh_btn, refresh_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *refresh_img = lv_img_create(refresh_btn);
    lv_img_set_src(refresh_img, &icon_reflash_28);
    lv_obj_center(refresh_img);

    // ---------- 音量滑块（频道区域与底部控制栏之间）----------
    s_radio_ui.volume_slider = lv_slider_create(app->screen);
    lv_obj_set_size(s_radio_ui.volume_slider, 160, 10);
    lv_obj_align_to(s_radio_ui.volume_slider, channel_area, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_slider_set_range(s_radio_ui.volume_slider, 0, 100);
    lv_slider_set_value(s_radio_ui.volume_slider, volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_radio_ui.volume_slider, volume_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // ---------- 底部控制栏 ----------
    lv_obj_t *control_bar = lv_obj_create(app->screen);
    lv_obj_set_size(control_bar, 180, 55);
    lv_obj_align(control_bar, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_opa(control_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(control_bar, 0, 0);
    lv_obj_set_style_pad_all(control_bar, 0, 0);

    // 上一曲按钮
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

    // 下一曲按钮
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

    // 播放/暂停按钮
    s_radio_ui.play_btn = lv_btn_create(control_bar);
    lv_obj_set_size(s_radio_ui.play_btn, 43, 43);
    lv_obj_set_style_radius(s_radio_ui.play_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_radio_ui.play_btn, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_bg_opa(s_radio_ui.play_btn, LV_OPA_COVER, 0);
    lv_obj_align(s_radio_ui.play_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(s_radio_ui.play_btn, play_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_radio_ui.play_icon = lv_img_create(s_radio_ui.play_btn);
    lv_img_set_src(s_radio_ui.play_icon, &icon_stop_40);
    lv_obj_center(s_radio_ui.play_icon);
    s_radio_ui.playing = false;
    
    volume = 50;
    radio_handle_change_to_audio(AUDIO_CMD_CONNECT);// 首次连接
       // 启动状态更新任务
    xTaskCreate(radio_update_status_task, "radio_update_status", 4096, NULL, 5,
                &radio_update_status_task_handle);
    radio_app_led_control(LED_MODE_MUSIC, 100);
    ESP_LOGI(TAG, "Radio UI created");
}

static void radio_on_open(ui_app_t *app)
{
    // 恢复显示当前电台
    if (s_radio_ui.channel_label && station_count > 0) {
        lv_label_set_text(s_radio_ui.channel_label, stations[selected_station_index].name);
    }
    ESP_LOGI(TAG, "radio_on_open, current station: %s",
             (station_count > 0) ? stations[selected_station_index].name : "none");
}

static void radio_on_close(ui_app_t *app)
{
    ESP_LOGI(TAG, "radio_on_close");
}

static void radio_on_destroy(ui_app_t *app)
{
    ESP_LOGI(TAG, "radio_on_destroy");
    // 删除状态更新任务
    if (radio_update_status_task_handle) {
        vTaskDelete(radio_update_status_task_handle);
        radio_update_status_task_handle = NULL;
    }
    // 通知音频服务关闭连接
    radio_handle_change_to_audio(AUDIO_CMD_DISCONNECT);

    // 释放内存
    memset(&s_radio_ui, 0, sizeof(s_radio_ui));
    app->screen = NULL;
}

static void radio_on_event(ui_app_t *app, event_data_t *event)
{
    if (!event) return;

    if (event->event_type == NOTIFICATION) {
        if (event->service_id == KEYHAL_SERVICE) {
            key_event_data_t *key_data = (key_event_data_t *)event->data;
            if (key_data) radio_handle_key_event(key_data);
        } else if (event->service_id == SNTP_SERVICE) {
            if (event->data) {
                sntp_service_send_data_t *sntp = (sntp_service_send_data_t *)event->data;
                if (s_radio_ui.time_label) {
                    lv_label_set_text(s_radio_ui.time_label, sntp->current_time);
                }
                //ESP_LOGI(TAG, "Time update: %s", sntp->current_time);
            }
        }
    }

    if (event->data) {
        free(event->data);
        event->data = NULL;
    }
    free(event);
}

// ========== 按键处理 ==========
static bool radio_handle_key_event(key_event_data_t *key)
{
    if (key->event == KEY_EVENT_PRESS) {
        switch (key->key_id) {
            case KEY_ID_ENTER:
                radio_play_pause();
                return true;
            case KEY_ID_BACK: {
                ui_service_receive_data_t *cmd = malloc(sizeof(ui_service_receive_data_t));
                if (!cmd) return true;
                cmd->cmd = UI_CMD_GO_HOME;
                cmd->data = NULL;
                cmd->data_len = 0;
                event_data_t *evt = malloc(sizeof(event_data_t));
                if (!evt) { free(cmd); return true; }
                evt->service_id = UI_SERVICE;
                evt->event_type = REQUEST;
                evt->reply_queue = NULL;
                evt->data = cmd;
                evt->data_len = sizeof(ui_service_receive_data_t);
                xQueueSend(get_ui_service_queue(), &evt, 0);
                ESP_LOGI(TAG, "send ui_event to ui_service_queue");
                radio_app_led_control(LED_MODE_BREATH, 100);
                return true;
            }
            default: return true;
        }
    } else if (key->event == KEY_EVENT_ROTATE_CW) {
        radio_increase_volume();
        return true;
    } else if (key->event == KEY_EVENT_ROTATE_CCW) {
        radio_decrease_volume();
        return true;
    }
    return false;
}

// ========== 状态更新任务 ==========
static void radio_update_status_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        radio_update_time();
    }
}

static void radio_update_time(void)
{
    sntp_service_receive_data_t *sntp_payload = malloc(sizeof(sntp_service_receive_data_t));
    if (!sntp_payload) return;
    sntp_payload->cmd = SNTP_CMD_GET_TIME;

    event_data_t *evt = malloc(sizeof(event_data_t));
    if (!evt) { free(sntp_payload); return; }
    evt->service_id = UI_SERVICE;
    evt->event_type = REQUEST;
    evt->reply_queue = get_ui_service_queue();
    evt->data = sntp_payload;
    evt->data_len = sizeof(sntp_service_receive_data_t);

    if (xQueueSend(get_sntp_service_queue(), &evt, 0) != pdPASS) {
        free(sntp_payload);
        free(evt);
    }
}

// ========== 控制功能 ==========
static void radio_play_pause(void)
{
    if (!s_radio_ui.play_icon) return;
    if (s_radio_ui.playing) {
        ESP_LOGI(TAG, "pause");
        lv_img_set_src(s_radio_ui.play_icon, &icon_stop_40);
        s_radio_ui.playing = false;
        radio_handle_change_to_audio(AUDIO_CMD_STOP);
    } else {
        ESP_LOGI(TAG, "play");
        lv_img_set_src(s_radio_ui.play_icon, &icon_play_40);
        s_radio_ui.playing = true;
        radio_handle_change_to_audio(AUDIO_CMD_PLAY);
    }
    radio_app_led_control(LED_MODE_ALERT, 0);
}

static void radio_prev_channel(void)
{
    if (station_count == 0) return;
    selected_station_index = (selected_station_index - 1 + station_count) % station_count;
    if (s_radio_ui.channel_label) {
        lv_label_set_text(s_radio_ui.channel_label, stations[selected_station_index].name);
    }
    ESP_LOGI(TAG, "prev channel: %s", stations[selected_station_index].name);
    radio_handle_change_to_audio(AUDIO_CMD_CONNECT);
}

static void radio_next_channel(void)
{
    if (station_count == 0) return;
    selected_station_index = (selected_station_index + 1) % station_count;
    if (s_radio_ui.channel_label) {
        lv_label_set_text(s_radio_ui.channel_label, stations[selected_station_index].name);
    }
    ESP_LOGI(TAG, "next channel: %s", stations[selected_station_index].name);
    radio_handle_change_to_audio(AUDIO_CMD_CONNECT);
}

static void radio_handle_change_to_audio(audio_service_cmd_t cmd){
    audio_service_receive_data_t* audio_payload = malloc(sizeof(audio_service_receive_data_t));
    if(!audio_payload){
        ESP_LOGE(TAG,"malloc audio_service_receive_data_t err");
    } else{
        audio_payload->cmd = cmd;
        strcpy(audio_payload->url, stations[selected_station_index].url);
        audio_payload->prv_type = http_str;
        audio_payload->midle_type = stations[selected_station_index].type;
        audio_payload->back_type = i2s_hal;
        audio_payload->volume = volume;
        audio_payload->start_after_connect = s_radio_ui.playing;

        event_data_t *evt_data = malloc(sizeof(event_data_t));
        if(!evt_data){
            ESP_LOGE(TAG,"malloc event_data_t err");
            free(audio_payload);
        } else{
            evt_data->service_id = UI_SERVICE;
            evt_data->event_type = REQUEST;
            evt_data->reply_queue = NULL;
            evt_data->data = audio_payload;
            evt_data->data_len = sizeof(audio_service_receive_data_t);
            xQueueSend(get_audio_service_queue(), &evt_data, 0);
        }
    }
}

// 新增滑块回调：拖动滑块时更新音量
static void volume_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    if (val != volume) {
        volume = val;
        radio_handle_change_to_audio(AUDIO_CMD_VOLUME);
        radio_app_led_control(LED_MODE_VOLUME, volume);
        ESP_LOGI(TAG, "Volume slider set to %d", volume);
    }
}

static void radio_increase_volume(void) { 
    ESP_LOGI(TAG, "increase volume"); 
    volume += 1;
    if(volume > 100) volume = 100;
    if(s_radio_ui.volume_slider) {
        lv_slider_set_value(s_radio_ui.volume_slider, volume, LV_ANIM_OFF);
    }
    radio_handle_change_to_audio(AUDIO_CMD_VOLUME);
    radio_app_led_control(LED_MODE_VOLUME, volume);
}

static void radio_decrease_volume(void) { 
    ESP_LOGI(TAG, "decrease volume"); 
    volume -= 1;
    if(volume < 0) volume = 0;
    if(s_radio_ui.volume_slider) {
        lv_slider_set_value(s_radio_ui.volume_slider, volume, LV_ANIM_OFF);
    }
    radio_handle_change_to_audio(AUDIO_CMD_VOLUME);
    radio_app_led_control(LED_MODE_VOLUME, volume);
}

static void radio_refresh(void)
{
    ESP_LOGI(TAG, "refresh, current: %s", stations[selected_station_index].name);
}

static void radio_app_led_control(led_mode_t mode, uint32_t arg) {
    led_service_receive_data_t* led_payload = (led_service_receive_data_t*)malloc(sizeof(led_service_receive_data_t));
    if(led_payload){
        led_payload->device = LED_HAL_DEVICE_FRONT;
        led_payload->mode = mode;
        led_payload->brightness = 40;
        led_payload->arg = arg;
        
        event_data_t* led_event = (event_data_t*)malloc(sizeof(event_data_t));
        if(led_event){
            led_event->service_id = HAL;
            led_event->event_type = REQUEST;
            led_event->data = led_payload;
            led_event->data_len = sizeof(led_service_receive_data_t);
            xQueueSend(get_led_service_queue(), &led_event, 0);
            ESP_LOGI(TAG, "send led_event to led_service_queue");
        } else{
            ESP_LOGE(TAG, "malloc led_event failed");
            free(led_payload);
        }
    } else {
        ESP_LOGE(TAG, "malloc led_service_receive_data_t err");
    }
}