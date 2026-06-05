#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "ui_service.h"
#include "wifi_service.h"
#include "system_event.h"
#include "soundcard_app.h"
#include "system_event.h"
#include "keys_hal.h"
#include "sntp_service.h"
#include "audio_service.h"
#include "led_service.h"

static const char *TAG = "soundcard_app";

typedef struct {
    lv_obj_t *time_label;
    lv_obj_t *mode_label;
    lv_obj_t *status_dot;
    lv_obj_t *volume_bar;
    lv_timer_t *update_timer;
    bool dot_visible;
    int volume;
} soundcard_ui_t;

static ui_app_t s_soundcard_app;
static soundcard_ui_t s_soundcard_ui;
static int s_volume = 60;

static void soundcard_on_create(ui_app_t *app);
static void soundcard_on_open(ui_app_t *app);
static void soundcard_on_close(ui_app_t *app);
static void soundcard_on_destroy(ui_app_t *app);
static void soundcard_on_event(ui_app_t *app, event_data_t *event);
static void soundcard_update_dis_cb(lv_timer_t *timer);
static void soundcard_update_time(void);
static void soundcard_handle_change_to_audio(audio_service_cmd_t cmd);
static void soundcard_increase_volume(void);
static void soundcard_decrease_volume(void);
static void soundcard_app_led_control(led_mode_t mode, uint32_t arg);

void soundcard_app_register(void)
{
    memset(&s_soundcard_app, 0, sizeof(s_soundcard_app));
    s_soundcard_app.name = "soundcard_app";
    s_soundcard_app.screen = NULL;
    s_soundcard_app.on_create = soundcard_on_create;
    s_soundcard_app.on_open = soundcard_on_open;
    s_soundcard_app.on_close = soundcard_on_close;
    s_soundcard_app.on_destroy = soundcard_on_destroy;
    s_soundcard_app.on_event = soundcard_on_event;
    ui_service_register_app(&s_soundcard_app);
    ESP_LOGI(TAG, "Sound card app registered");
}

static void soundcard_on_create(ui_app_t *app)
{
    ESP_LOGI(TAG, "soundcard_on_create");

    ESP_LOGI(TAG, "stop wifi");

    // 发送 WiFi 断开请求，避免影响接收USB 音频数据
    wifi_service_receive_data_t *wifi_payload = malloc(sizeof(wifi_service_receive_data_t));
    if (wifi_payload) {
        wifi_payload->cmd = WIFI_CMD_DISCONNECT;
    }
    event_data_t *evt_data = malloc(sizeof(event_data_t));
    if (evt_data) {
        evt_data->service_id = HAL;
        evt_data->event_type = REQUEST;
        evt_data->reply_queue = NULL;
        evt_data->data = wifi_payload;
        xQueueSend(get_wifi_service_queue(), &evt_data, 0);
    }


    app->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(app->screen, lv_color_black(), 0);
    lv_obj_clear_flag(app->screen, LV_OBJ_FLAG_SCROLLABLE);

    // 时间
    s_soundcard_ui.time_label = lv_label_create(app->screen);
    lv_obj_set_style_text_color(s_soundcard_ui.time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_soundcard_ui.time_label, &lv_font_montserrat_20, 0);
    lv_label_set_text(s_soundcard_ui.time_label, "--:--:--");
    lv_obj_align(s_soundcard_ui.time_label, LV_ALIGN_TOP_MID, 0, 10);

    // 模式文字
    s_soundcard_ui.mode_label = lv_label_create(app->screen);
    lv_obj_set_style_text_color(s_soundcard_ui.mode_label, lv_color_white(), 0);
    LV_FONT_DECLARE(font_alipuhui20);
    lv_obj_set_style_text_font(s_soundcard_ui.mode_label, &font_alipuhui20, 0);
    lv_label_set_text(s_soundcard_ui.mode_label, "声卡工作中");
    lv_obj_align(s_soundcard_ui.mode_label, LV_ALIGN_CENTER, -20, 0);

    // 状态指示灯
    s_soundcard_ui.status_dot = lv_obj_create(app->screen);
    lv_obj_set_size(s_soundcard_ui.status_dot, 30, 30);
    lv_obj_set_style_radius(s_soundcard_ui.status_dot, 15, 0);
    lv_obj_set_style_bg_color(s_soundcard_ui.status_dot, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_soundcard_ui.status_dot, 0, 0);
    lv_obj_align_to(s_soundcard_ui.status_dot, s_soundcard_ui.mode_label, LV_ALIGN_OUT_RIGHT_MID, 15, 0);
    s_soundcard_ui.dot_visible = false;

    // 音量进度条
    s_soundcard_ui.volume_bar = lv_bar_create(app->screen);
    lv_obj_set_size(s_soundcard_ui.volume_bar, 150, 15);
    lv_obj_align(s_soundcard_ui.volume_bar, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_bar_set_range(s_soundcard_ui.volume_bar, 0, 100);
    lv_bar_set_value(s_soundcard_ui.volume_bar, get_audio_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_soundcard_ui.volume_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_soundcard_ui.volume_bar, lv_color_make(0, 255, 0), LV_PART_INDICATOR);

    // 定时器
    s_soundcard_ui.update_timer = lv_timer_create(soundcard_update_dis_cb, 500, NULL);

    // 启动 USB 声卡
    soundcard_handle_change_to_audio(AUDIO_CMD_CONNECT);
    soundcard_app_led_control(LED_MODE_MUSIC, 100);

    ESP_LOGI(TAG, "Sound card UI created");
}

static void soundcard_on_open(ui_app_t *app)
{
    ESP_LOGI(TAG, "soundcard_on_open");
}

static void soundcard_on_close(ui_app_t *app)
{
    ESP_LOGI(TAG, "soundcard_on_close");
}

static void soundcard_on_destroy(ui_app_t *app)
{
    ESP_LOGI(TAG, "soundcard_on_destroy");

    ESP_LOGI(TAG, "restore wifi");
    // 发送 WiFi 连接请求
    wifi_service_receive_data_t *wifi_payload = malloc(sizeof(wifi_service_receive_data_t));
    if (wifi_payload) {
        wifi_payload->cmd = WIFI_CMD_CONNECT_SAVED;
    }
    event_data_t *evt_data = malloc(sizeof(event_data_t));
    if (evt_data) {
        evt_data->service_id = HAL;
        evt_data->event_type = REQUEST;
        evt_data->reply_queue = NULL;
        evt_data->data = wifi_payload;
        xQueueSend(get_wifi_service_queue(), &evt_data, 0);
    }

    // 断开 USB 音频
    soundcard_handle_change_to_audio(AUDIO_CMD_DISCONNECT);
    ESP_LOGI(TAG, "Sound card disconnected");

    if (s_soundcard_ui.update_timer) {
        lv_timer_del(s_soundcard_ui.update_timer);
        s_soundcard_ui.update_timer = NULL;
    }
    // 清空指针
    s_soundcard_ui.time_label  = NULL;
    s_soundcard_ui.mode_label  = NULL;
    s_soundcard_ui.status_dot  = NULL;
    s_soundcard_ui.volume_bar  = NULL;
    app->screen = NULL;
    ESP_LOGI(TAG, "Sound card UI destroyed");
}

static void soundcard_on_event(ui_app_t *app, event_data_t *event)
{
    if (!event) return;

    // 处理按键事件
    if (event->event_type == NOTIFICATION) {
        if (event->service_id == KEYHAL_SERVICE) {
            key_event_data_t *key_data = (key_event_data_t *)event->data;
            if (key_data) {
                if (key_data->event == KEY_EVENT_PRESS) {
                    if (key_data->key_id == KEY_ID_BACK) {
                        ui_service_receive_data_t *cmd = malloc(sizeof(ui_service_receive_data_t));
                        if (cmd) {
                            cmd->cmd = UI_CMD_GO_HOME;
                            cmd->data = NULL;
                            cmd->data_len = 0;
                            event_data_t *evt = malloc(sizeof(event_data_t));
                            if (evt) {
                                evt->service_id = UI_SERVICE;
                                evt->event_type = REQUEST;
                                evt->reply_queue = NULL;
                                evt->data = cmd;
                                evt->data_len = sizeof(ui_service_receive_data_t);
                                xQueueSend(get_ui_service_queue(), &evt, 0);
                                ESP_LOGI(TAG, "send go home event");
                            } else {
                                free(cmd);
                            }
                        }
                    }
                } else if (key_data->event == KEY_EVENT_ROTATE_CW) {
                    soundcard_increase_volume();
                } else if (key_data->event == KEY_EVENT_ROTATE_CCW) {
                    soundcard_decrease_volume();
                }
            }
        }

        // 处理音频服务通知
        if (event->service_id == AUDIO_SERVICE) {
            audio_service_send_data_t *audio_ntf = (audio_service_send_data_t *)event->data;
            if (audio_ntf) {
                switch (audio_ntf->cmd) {
                case AUDIO_CMD_END:
                    ESP_LOGI(TAG, "Audio playback ended");
                    // 可恢复指示灯为熄灭状态
                    break;
                case AUDIO_CMD_ERROR:
                    ESP_LOGE(TAG, "Audio error occurred");
                    break;
                case AUDIO_CMD_CONNECT:
                    ESP_LOGI(TAG, "Audio connected");
                    break;
                default:
                    break;
                }
            }
        }

        if (event->service_id == SNTP_SERVICE) {
            if (event->data) {
                sntp_service_send_data_t *sntp = (sntp_service_send_data_t *)event->data;
                if (s_soundcard_ui.time_label) {
                    lv_label_set_text(s_soundcard_ui.time_label, sntp->current_time);
                }
            }
        }

    }

    if (event->data) free(event->data);
    free(event);
}

static void soundcard_update_dis_cb(lv_timer_t *timer)
{
    // 时间
    soundcard_update_time();
    s_soundcard_ui.dot_visible = !s_soundcard_ui.dot_visible;
    lv_color_t color = s_soundcard_ui.dot_visible ? lv_color_make(0, 255, 0) : lv_color_black();
    lv_obj_set_style_bg_color(s_soundcard_ui.status_dot, color, 0);
    // 音量条
    if (s_soundcard_ui.volume_bar) {
        lv_bar_set_value(s_soundcard_ui.volume_bar, get_audio_volume(), LV_ANIM_ON); // 同步显示实际音量
    }
}

static void soundcard_update_time(void)
{
    //SNTP 时间更新
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
    //ESP_LOGI(TAG, "Requested time update from SNTP service");
}

static void soundcard_handle_change_to_audio(audio_service_cmd_t cmd)
{   
    // uac播放
    audio_service_receive_data_t *payload = malloc(sizeof(audio_service_receive_data_t));
    if (!payload) return;
    memset(payload, 0, sizeof(audio_service_receive_data_t));
    payload->cmd = cmd;
    if (cmd == AUDIO_CMD_CONNECT) {
        // 声卡管线：USB UAC 输入 → 无解码 → I2S 输出
        payload->prv_type = usb_uac;          // 音频来源：USB 主机
        payload->midle_type = raw_hal; // 无需解码
        payload->back_type = i2s_hal;          // 输出到板载扬声器
        payload->volume = s_volume;
        payload->start_after_connect = true;
    }
    event_data_t *evt = malloc(sizeof(event_data_t));
    if (!evt) {
        free(payload);
        return;
    }
    evt->service_id = UI_SERVICE;
    evt->event_type = REQUEST;
    evt->reply_queue = NULL;   // 本应用无需同步回复
    evt->data = payload;
    evt->data_len = sizeof(audio_service_receive_data_t);
    xQueueSend(get_audio_service_queue(), &evt, 0);
    ESP_LOGI(TAG, "Sent audio service request: cmd=%d", cmd);

    // uac录音
    payload = malloc(sizeof(audio_service_receive_data_t));
    if (!payload) return;
    memset(payload, 0, sizeof(audio_service_receive_data_t));
    payload->cmd = cmd;
    if (cmd == AUDIO_CMD_CONNECT) {
        payload->prv_type = i2s_hal;          // 
        payload->midle_type = raw_hal; // 无需解码
        payload->back_type = usb_uac;          // 
        payload->volume = s_volume;
        payload->start_after_connect = true;
    }
    evt = malloc(sizeof(event_data_t));
    if (!evt) {
        free(payload);
        return;
    }
    evt->service_id = UI_SERVICE;
    evt->event_type = REQUEST;
    evt->reply_queue = NULL;   // 本应用无需同步回复
    evt->data = payload;
    evt->data_len = sizeof(audio_service_receive_data_t);
    xQueueSend(get_audio_service_queue(), &evt, 0); 
    ESP_LOGI(TAG, "Sent audio service request: cmd=%d", cmd);   
}

static void soundcard_increase_volume(void)
{
    if (s_volume >= 100) return;
    s_volume += 2;
    if (s_volume > 100) s_volume = 100;
    set_audio_volume(s_volume);    // 使用框架提供的音量设置函数
    ESP_LOGI(TAG, "Volume increased to %d", s_volume);
}

static void soundcard_decrease_volume(void)
{
    if (s_volume <= 0) return;
    s_volume -= 2;
    if (s_volume < 0) s_volume = 0;

    set_audio_volume(s_volume);
    ESP_LOGI(TAG, "Volume decreased to %d", s_volume);
}

static void soundcard_app_led_control(led_mode_t mode, uint32_t arg)
{
    // TODO: 对接 LED 服务实现实际的灯光控制
    ESP_LOGI(TAG, "LED control: mode=%d, arg=%" PRIu32, mode, arg);
}