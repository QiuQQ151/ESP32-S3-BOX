// desktop_app.c
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "lvgl.h"
#include "desktop_app.h"
#include "led_service.h"         // led_service_set_panel_mode
#include "ui_service.h"          // ui_service_get_last_launched_app
#include "system_event.h"        // event_data_t, NOTIFICATION, KEYHAL_SERVICE
#include "keys_hal.h"            // key_event_data_t, KEY_EVENT_*, KEY_ID_*

// ---------- 动画类型定义 ----------
typedef enum {
    DESKTOP_ANIM_NONE  = 0,
    DESKTOP_ANIM_FADE  = 1,
    DESKTOP_ANIM_SLIDE = 2,
    DESKTOP_ANIM_ZOOM  = 3,
    DESKTOP_ANIM_MAX
} desktop_anim_type_t;

// 字体
LV_FONT_DECLARE(font_alipuhui20);
// 图片资源声明
LV_IMG_DECLARE(icon_booting);
LV_IMG_DECLARE(icon_deepseek);
LV_IMG_DECLARE(icon_folder);
LV_IMG_DECLARE(icon_light);
LV_IMG_DECLARE(icon_music);
LV_IMG_DECLARE(icon_record);
LV_IMG_DECLARE(icon_clock);
LV_IMG_DECLARE(icon_radio);
LV_IMG_DECLARE(icon_weather);
LV_IMG_DECLARE(icon_setting);
LV_IMG_DECLARE(icon_usb);
LV_IMG_DECLARE(icon_tomato);

// 桌面项定义
typedef struct {
    const char *app_name;
    const lv_img_dsc_t *icon;
    const char *label;
} desktop_item_t;

static const desktop_item_t desktop_items[] = {
    { "music_app",    &icon_music,     "音乐" },
    { "radio_app",    &icon_radio,     "网络收音机" },
    { "tomato_app",   &icon_tomato,    "番茄时钟" },
    { "deepseek_app", &icon_deepseek,  "DeepSeek" },
    { "weather_app",  &icon_weather,   "天气" },
    { "clock_app",    &icon_clock,     "闹钟设置" },
    { "folder_app",   &icon_folder,    "文件" },
    { "record_app",   &icon_record,    "录音机" },
    { "lght_app",     &icon_light,     "手电筒" },
    { "usb_app",      &icon_usb,       "U盘模式" },
    { "setting_app",  &icon_setting,   "设置" },
};
static const int desktop_item_count = sizeof(desktop_items) / sizeof(desktop_items[0]);

// 桌面应用实例
static ui_app_t desktop_app;

// 桌面 UI 元素（静态变量，在 on_destroy 时重置）
static lv_obj_t *desktop_screen = NULL;
static lv_obj_t *icon_img = NULL;
static lv_obj_t *name_label = NULL;
static int current_item_index = 0;

static const char *TAG = "desktop_app";

// 动画状态
static bool anim_in_progress = false;
static desktop_anim_type_t current_anim_type = DESKTOP_ANIM_FADE;

// 滑动方向临时变量（-1: prev, +1: next）
static int g_slide_dir = 1;

// ========== 内部函数前置声明 ==========
static void desktop_on_create(ui_app_t *app);
static void desktop_on_open(ui_app_t *app);
static void desktop_on_close(ui_app_t *app);
static void desktop_on_destroy(ui_app_t *app);
static void desktop_on_event(ui_app_t *app, event_data_t *event);

static bool desktop_handle_key_event(key_event_data_t *data);
static void desktop_update_display(void);
static void desktop_next_item(void);
static void desktop_prev_item(void);
static void desktop_open_current(void);
static void desktop_gesture_handler(lv_event_t *e);
static void desktop_icon_click_handler(lv_event_t *e);

static void desktop_animate_switch(int direction);
static void set_opa_cb(void *var, int32_t v);
static void set_x_cb(void *var, int32_t v);
static void set_zoom_cb(void *var, int32_t v);

static void start_fade_anim(void);
static void start_slide_anim(void);
static void start_zoom_anim(void);

static void anim_fade_out_ready_cb(lv_anim_t *a);
static void anim_fade_in_ready_cb(lv_anim_t *a);
static void anim_slide_out_ready_cb(lv_anim_t *a);
static void anim_slide_in_ready_cb(lv_anim_t *a);
static void anim_zoom_out_ready_cb(lv_anim_t *a);
static void anim_zoom_in_ready_cb(lv_anim_t *a);

// ---------- 生命周期实现 ----------
static void desktop_on_create(ui_app_t *app)
{
    desktop_screen = lv_obj_create(NULL);
    if (!desktop_screen) {
        ESP_LOGE(TAG, "Failed to create desktop screen");
        return;
    }
    lv_obj_set_style_bg_color(desktop_screen, lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(desktop_screen, desktop_gesture_handler, LV_EVENT_GESTURE, NULL);

    icon_img = lv_img_create(desktop_screen);
    lv_obj_align(icon_img, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_flag(icon_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(icon_img, desktop_icon_click_handler, LV_EVENT_CLICKED, NULL);

    name_label = lv_label_create(desktop_screen);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(name_label, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_text_font(name_label, &font_alipuhui20, LV_STATE_DEFAULT);

    // 初始透明，等待 on_open 设置正确后显示
    lv_obj_set_style_opa(icon_img, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(name_label, LV_OPA_TRANSP, 0);

    current_item_index = 0;
    desktop_update_display();
    app->screen = desktop_screen;

    // 请求LED服务设置模式
    led_service_receive_data_t* led_payload = (led_service_receive_data_t*)malloc(sizeof(led_service_receive_data_t));
    if(led_payload){
        led_payload->device = LED_HAL_DEVICE_FRONT;
        led_payload->mode = LED_MODE_BREATH;
        led_payload->brightness = 100;
        led_payload->arg = 0;
        
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

static void desktop_on_open(ui_app_t *app)
{
    // 根据上次启动的应用设定高亮图标
    const char *last_app = ui_service_get_last_launched_app();
    if (last_app) {
        for (int i = 0; i < desktop_item_count; i++) {
            if (strcmp(desktop_items[i].app_name, last_app) == 0) {
                current_item_index = i;
                break;
            }
        }
    }
    desktop_update_display();
    // 设置为完全不透明并确保显示
    lv_obj_set_style_opa(icon_img, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(name_label, LV_OPA_COVER, 0);
    lv_obj_clear_flag(app->screen, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Desktop opened, highlight index %d", current_item_index);
}

static void desktop_on_close(ui_app_t *app)
{
    anim_in_progress = false;   // 停止可能正在运行的动画
    ESP_LOGI(TAG, "Desktop closed");
}

static void desktop_on_destroy(ui_app_t *app)
{
    if (desktop_screen) {
        lv_obj_del(desktop_screen);
        desktop_screen = NULL;
    }
    icon_img = NULL;
    name_label = NULL;
    anim_in_progress = false;

    app->screen = NULL;   // ★ 必须置空，否则 UI 服务不会重建屏幕

    ESP_LOGI(TAG, "Desktop destroyed");
}

// ---------- 通用事件回调（接收所有通知） ----------
static void desktop_on_event(ui_app_t *app, event_data_t *event)
{
    if (!event) return;

    if (event->event_type == NOTIFICATION) {
        if (event->service_id == KEYHAL_SERVICE) {
            key_event_data_t *key_data = (key_event_data_t *)event->data;
            if (key_data) {
                desktop_handle_key_event(key_data);
            }
        }
        // 其他通知可在此扩展
    }

    // 释放通知资源（按键数据也在内）
    if (event->data) {
        free(event->data);
        event->data = NULL;
    }
    free(event);
}

// ---------- 按键处理（由 on_event 调用） ----------
static bool desktop_handle_key_event(key_event_data_t *data)
{
    if (data->event == KEY_EVENT_ROTATE_CW) {
        desktop_next_item();
        return true;
    } else if (data->event == KEY_EVENT_ROTATE_CCW) {
        desktop_prev_item();
        return true;
    } else if (data->event == KEY_EVENT_PRESS && data->key_id == KEY_ID_ENTER) {
        desktop_open_current();
        return true;
    }
    return false;
}

// ---------- 显示更新 ----------
static void desktop_update_display(void)
{
    if (current_item_index < 0 || current_item_index >= desktop_item_count) return;
    lv_img_set_src(icon_img, desktop_items[current_item_index].icon);
    lv_label_set_text(name_label, desktop_items[current_item_index].label);
}

// ---------- 图标切换 ----------
static void desktop_next_item(void)
{
    if (desktop_item_count == 0 || anim_in_progress) return;
    current_item_index = (current_item_index + 1) % desktop_item_count;
    if (current_anim_type == DESKTOP_ANIM_NONE) {
        desktop_update_display();
    } else {
        desktop_animate_switch(1);
    }
}

static void desktop_prev_item(void)
{
    if (desktop_item_count == 0 || anim_in_progress) return;
    current_item_index = (current_item_index - 1 + desktop_item_count) % desktop_item_count;
    if (current_anim_type == DESKTOP_ANIM_NONE) {
        desktop_update_display();
    } else {
        desktop_animate_switch(-1);
    }
}

static void desktop_open_current(void)
{
    if (current_item_index < 0 || current_item_index >= desktop_item_count) return;

    // 通过队列异步请求打开应用，避免在 LVGL 回调中直接切换屏幕
    ui_service_receive_data_t *cmd = malloc(sizeof(ui_service_receive_data_t));
    if (!cmd) return;

    cmd->cmd = UI_CMD_OPEN_APP;
    cmd->data = strdup(desktop_items[current_item_index].app_name);
    if (!cmd->data) {
        free(cmd);
        return;
    }
    cmd->data_len = strlen(desktop_items[current_item_index].app_name) + 1;

    event_data_t *evt = malloc(sizeof(event_data_t));
    if (!evt) {
        free(cmd->data);
        free(cmd);
        return;
    }

    evt->service_id = UI_SERVICE;
    evt->event_type = REQUEST;
    evt->reply_queue = NULL;
    evt->data = cmd;
    evt->data_len = sizeof(ui_service_receive_data_t);

    if (xQueueSend(get_ui_service_queue(), &evt, 0) != pdPASS) {
        free(cmd->data);
        free(cmd);
        free(evt);
    }
}

// ---------- 手势与点击 ----------
static void desktop_gesture_handler(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT) {
        desktop_next_item();
    } else if (dir == LV_DIR_RIGHT) {
        desktop_prev_item();
    }
}

static void desktop_icon_click_handler(lv_event_t *e)
{
    desktop_open_current();
}

// ========== 动画部分 ==========
static void desktop_animate_switch(int direction)
{
    anim_in_progress = true;
    switch (current_anim_type) {
        case DESKTOP_ANIM_FADE:
            start_fade_anim();
            break;
        case DESKTOP_ANIM_SLIDE:
            g_slide_dir = direction;
            start_slide_anim();
            break;
        case DESKTOP_ANIM_ZOOM:
            start_zoom_anim();
            break;
        default:
            anim_in_progress = false;
            desktop_update_display();
            break;
    }
}

static void set_opa_cb(void *var, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    if (lv_obj_is_valid(obj)) {
        lv_obj_set_style_opa(obj, (lv_opa_t)v, 0);
    }
}

static void set_x_cb(void *var, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    if (lv_obj_is_valid(obj)) {
        lv_obj_set_x(obj, v);
    }
}

static void set_zoom_cb(void *var, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    if (lv_obj_is_valid(obj) && obj == icon_img) {
        lv_img_set_zoom(obj, (uint16_t)v);
    }
}

// ---------- 1. 淡入淡出动画 ----------
static void start_fade_anim(void)
{
    // 淡出图标
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, icon_img);
    lv_anim_set_exec_cb(&anim, set_opa_cb);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&anim, 200);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, anim_fade_out_ready_cb);
    lv_anim_start(&anim);

    // 淡出文字
    lv_anim_t anim2;
    lv_anim_init(&anim2);
    lv_anim_set_var(&anim2, name_label);
    lv_anim_set_exec_cb(&anim2, set_opa_cb);
    lv_anim_set_values(&anim2, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&anim2, 200);
    lv_anim_set_path_cb(&anim2, lv_anim_path_ease_in_out);
    lv_anim_start(&anim2);
}

static void anim_fade_out_ready_cb(lv_anim_t *a)
{
    desktop_update_display();

    // 淡入图标
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, icon_img);
    lv_anim_set_exec_cb(&anim, set_opa_cb);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&anim, 200);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, anim_fade_in_ready_cb);
    lv_anim_start(&anim);

    // 淡入文字
    lv_anim_t anim2;
    lv_anim_init(&anim2);
    lv_anim_set_var(&anim2, name_label);
    lv_anim_set_exec_cb(&anim2, set_opa_cb);
    lv_anim_set_values(&anim2, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&anim2, 200);
    lv_anim_set_path_cb(&anim2, lv_anim_path_ease_in_out);
    lv_anim_start(&anim2);
}

static void anim_fade_in_ready_cb(lv_anim_t *a)
{
    anim_in_progress = false;
}

// ---------- 2. 横向滑动动画 ----------
static void start_slide_anim(void)
{
    lv_coord_t screen_w = lv_obj_get_width(desktop_screen);
    lv_coord_t slide_dist = screen_w * 8 / 10;

    lv_coord_t icon_x = lv_obj_get_x(icon_img);
    lv_coord_t label_x = lv_obj_get_x(name_label);
    lv_obj_align(icon_img, LV_ALIGN_TOP_LEFT, icon_x, lv_obj_get_y(icon_img));
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, label_x, lv_obj_get_y(name_label));

    lv_coord_t out_icon_target = icon_x - g_slide_dir * slide_dist;
    lv_coord_t out_label_target = label_x - g_slide_dir * slide_dist;

    // 滑出图标
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, icon_img);
    lv_anim_set_exec_cb(&anim, set_x_cb);
    lv_anim_set_values(&anim, icon_x, out_icon_target);
    lv_anim_set_time(&anim, 250);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, anim_slide_out_ready_cb);
    lv_anim_start(&anim);

    // 滑出文字
    lv_anim_t anim2;
    lv_anim_init(&anim2);
    lv_anim_set_var(&anim2, name_label);
    lv_anim_set_exec_cb(&anim2, set_x_cb);
    lv_anim_set_values(&anim2, label_x, out_label_target);
    lv_anim_set_time(&anim2, 250);
    lv_anim_set_path_cb(&anim2, lv_anim_path_ease_in_out);
    lv_anim_start(&anim2);

    // 透明度配合
    lv_anim_t anim_opa;
    lv_anim_init(&anim_opa);
    lv_anim_set_var(&anim_opa, icon_img);
    lv_anim_set_exec_cb(&anim_opa, set_opa_cb);
    lv_anim_set_values(&anim_opa, LV_OPA_COVER, LV_OPA_50);
    lv_anim_set_time(&anim_opa, 250);
    lv_anim_start(&anim_opa);

    lv_anim_t anim_opa2;
    lv_anim_init(&anim_opa2);
    lv_anim_set_var(&anim_opa2, name_label);
    lv_anim_set_exec_cb(&anim_opa2, set_opa_cb);
    lv_anim_set_values(&anim_opa2, LV_OPA_COVER, LV_OPA_50);
    lv_anim_set_time(&anim_opa2, 250);
    lv_anim_start(&anim_opa2);
}

static void anim_slide_out_ready_cb(lv_anim_t *a)
{
    desktop_update_display();

    lv_coord_t screen_w = lv_obj_get_width(desktop_screen);
    lv_coord_t slide_dist = screen_w * 8 / 10;

    lv_coord_t icon_target_x = (screen_w - lv_obj_get_width(icon_img)) / 2;
    lv_coord_t label_target_x = (screen_w - lv_obj_get_width(name_label)) / 2;

    lv_coord_t start_icon_x = icon_target_x + g_slide_dir * slide_dist;
    lv_coord_t start_label_x = label_target_x + g_slide_dir * slide_dist;

    lv_obj_set_x(icon_img, start_icon_x);
    lv_obj_set_x(name_label, start_label_x);
    lv_obj_set_style_opa(icon_img, LV_OPA_50, 0);
    lv_obj_set_style_opa(name_label, LV_OPA_50, 0);

    // 滑入图标
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, icon_img);
    lv_anim_set_exec_cb(&anim, set_x_cb);
    lv_anim_set_values(&anim, start_icon_x, icon_target_x);
    lv_anim_set_time(&anim, 250);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, anim_slide_in_ready_cb);
    lv_anim_start(&anim);

    // 滑入文字
    lv_anim_t anim2;
    lv_anim_init(&anim2);
    lv_anim_set_var(&anim2, name_label);
    lv_anim_set_exec_cb(&anim2, set_x_cb);
    lv_anim_set_values(&anim2, start_label_x, label_target_x);
    lv_anim_set_time(&anim2, 250);
    lv_anim_set_path_cb(&anim2, lv_anim_path_ease_in_out);
    lv_anim_start(&anim2);

    // 淡入透明度
    lv_anim_t anim_opa;
    lv_anim_init(&anim_opa);
    lv_anim_set_var(&anim_opa, icon_img);
    lv_anim_set_exec_cb(&anim_opa, set_opa_cb);
    lv_anim_set_values(&anim_opa, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_time(&anim_opa, 250);
    lv_anim_start(&anim_opa);

    lv_anim_t anim_opa2;
    lv_anim_init(&anim_opa2);
    lv_anim_set_var(&anim_opa2, name_label);
    lv_anim_set_exec_cb(&anim_opa2, set_opa_cb);
    lv_anim_set_values(&anim_opa2, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_time(&anim_opa2, 250);
    lv_anim_start(&anim_opa2);
}

static void anim_slide_in_ready_cb(lv_anim_t *a)
{
    anim_in_progress = false;
    lv_obj_align(icon_img, LV_ALIGN_CENTER, 0, -20);
    lv_obj_align(name_label, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_opa(icon_img, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(name_label, LV_OPA_COVER, 0);
}

// ---------- 3. 缩放动画 ----------
static void start_zoom_anim(void)
{
    // 缩小 + 淡出图标
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, icon_img);
    lv_anim_set_exec_cb(&anim, set_zoom_cb);
    lv_anim_set_values(&anim, LV_IMG_ZOOM_NONE, 0);
    lv_anim_set_time(&anim, 250);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, anim_zoom_out_ready_cb);
    lv_anim_start(&anim);

    lv_anim_t anim_opa;
    lv_anim_init(&anim_opa);
    lv_anim_set_var(&anim_opa, icon_img);
    lv_anim_set_exec_cb(&anim_opa, set_opa_cb);
    lv_anim_set_values(&anim_opa, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&anim_opa, 250);
    lv_anim_start(&anim_opa);

    // 文字淡出
    lv_anim_t anim2;
    lv_anim_init(&anim2);
    lv_anim_set_var(&anim2, name_label);
    lv_anim_set_exec_cb(&anim2, set_opa_cb);
    lv_anim_set_values(&anim2, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&anim2, 250);
    lv_anim_set_path_cb(&anim2, lv_anim_path_ease_in_out);
    lv_anim_start(&anim2);
}

static void anim_zoom_out_ready_cb(lv_anim_t *a)
{
    desktop_update_display();

    lv_img_set_zoom(icon_img, 0);
    lv_obj_set_style_opa(icon_img, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(name_label, LV_OPA_TRANSP, 0);

    // 放大 + 淡入图标
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, icon_img);
    lv_anim_set_exec_cb(&anim, set_zoom_cb);
    lv_anim_set_values(&anim, 0, LV_IMG_ZOOM_NONE);
    lv_anim_set_time(&anim, 250);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, anim_zoom_in_ready_cb);
    lv_anim_start(&anim);

    lv_anim_t anim_opa;
    lv_anim_init(&anim_opa);
    lv_anim_set_var(&anim_opa, icon_img);
    lv_anim_set_exec_cb(&anim_opa, set_opa_cb);
    lv_anim_set_values(&anim_opa, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&anim_opa, 250);
    lv_anim_start(&anim_opa);

    // 文字淡入
    lv_anim_t anim2;
    lv_anim_init(&anim2);
    lv_anim_set_var(&anim2, name_label);
    lv_anim_set_exec_cb(&anim2, set_opa_cb);
    lv_anim_set_values(&anim2, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&anim2, 250);
    lv_anim_set_path_cb(&anim2, lv_anim_path_ease_in_out);
    lv_anim_start(&anim2);
}

static void anim_zoom_in_ready_cb(lv_anim_t *a)
{
    anim_in_progress = false;
    lv_img_set_zoom(icon_img, LV_IMG_ZOOM_NONE);
    lv_obj_set_style_opa(icon_img, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(name_label, LV_OPA_COVER, 0);
}

// ========== 注册函数 ==========
void desktop_app_register(void)
{
    memset(&desktop_app, 0, sizeof(desktop_app));
    desktop_app.name = "desktop_app";
    desktop_app.on_create = desktop_on_create;
    desktop_app.on_open = desktop_on_open;
    desktop_app.on_close = desktop_on_close;
    desktop_app.on_destroy = desktop_on_destroy;
    desktop_app.on_event = desktop_on_event;   // 接收所有通知事件
    // 不再注册 on_key_event，UI 服务不会调用它
    ui_service_register_app(&desktop_app);
    ESP_LOGI(TAG, "Desktop app registered");
}

// // desktop_app.c
// #include <string.h>
// #include <stdlib.h>
// #include "esp_log.h"
// #include "lvgl.h"
// #include "desktop_app.h"

// // ---------- 动画类型定义 ----------
// typedef enum {
//     DESKTOP_ANIM_NONE  = 0,   // 无动画
//     DESKTOP_ANIM_FADE  = 1,   // 淡入淡出
//     DESKTOP_ANIM_SLIDE = 2,   // 横向滑动
//     DESKTOP_ANIM_ZOOM  = 3,   // 缩放（图标缩放 + 文字淡入淡出）
//     DESKTOP_ANIM_MAX
// } desktop_anim_type_t;

// // 字体
// LV_FONT_DECLARE(font_alipuhui20);
// // 图片资源声明
// LV_IMG_DECLARE(icon_booting);
// LV_IMG_DECLARE(icon_deepseek);
// LV_IMG_DECLARE(icon_folder);
// LV_IMG_DECLARE(icon_light);
// LV_IMG_DECLARE(icon_music);
// LV_IMG_DECLARE(icon_record);
// LV_IMG_DECLARE(icon_clock);
// LV_IMG_DECLARE(icon_radio);
// LV_IMG_DECLARE(icon_weather);
// LV_IMG_DECLARE(icon_setting);
// LV_IMG_DECLARE(icon_usb);
// LV_IMG_DECLARE(icon_tomato);

// // 桌面项定义（图标改为 const 指针）
// typedef struct {
//     const char *app_name;
//     const lv_img_dsc_t *icon;
//     const char *label;
// } desktop_item_t;

// static const desktop_item_t desktop_items[] = {
//     { "music_app",    &icon_music,     "音乐" },
//     { "radio_app",    &icon_radio,     "网络收音机" },
//     { "tomato_app",   &icon_tomato,    "番茄时钟" },
//     { "deepseek_app", &icon_deepseek,  "DeepSeek" },
//     { "weather_app",  &icon_weather,   "天气" },
//     { "clock_app",    &icon_clock,     "闹钟设置" },
//     { "folder_app",   &icon_folder,    "文件" },
//     { "record_app",   &icon_record,    "录音机" },
//     { "lght_app",     &icon_light,     "手电筒" },
//     { "usb_app",      &icon_usb,       "U盘模式" },
//     { "setting_app",  &icon_setting,   "设置" },
// };
// static const int desktop_item_count = sizeof(desktop_items) / sizeof(desktop_items[0]);

// // 桌面应用实例
// static ui_app_t desktop_app;

// // 桌面 UI 元素
// static lv_obj_t *desktop_screen = NULL;
// static lv_obj_t *icon_img = NULL;
// static lv_obj_t *name_label = NULL;
// static int current_item_index = 0;

// static const char *TAG = "desktop_app";

// // 动画状态
// static bool anim_in_progress = false;
// // 当前动画类型（运行时可变）
// static desktop_anim_type_t current_anim_type = DESKTOP_ANIM_FADE;

// // 滑动方向临时变量（-1: prev, +1: next）
// static int g_slide_dir = 1;

// // ========== 内部函数前置声明 ==========
// static void desktop_on_create(ui_app_t *app);
// static bool desktop_on_key_event(ui_app_t *app, void *key_event);
// static void desktop_update_display(void);
// static void desktop_next_item(void);
// static void desktop_prev_item(void);
// static void desktop_open_current(void);
// static void desktop_gesture_handler(lv_event_t *e);
// static void desktop_icon_click_handler(lv_event_t *e);

// static void desktop_animate_switch(int direction);
// static void set_opa_cb(void *var, int32_t v);
// static void set_x_cb(void *var, int32_t v);
// static void set_zoom_cb(void *var, int32_t v);

// // 启动各种动画的函数
// static void start_fade_anim(void);
// static void start_slide_anim(void);
// static void start_zoom_anim(void);

// // 各动画的就绪回调
// static void anim_fade_out_ready_cb(lv_anim_t *a);
// static void anim_fade_in_ready_cb(lv_anim_t *a);
// static void anim_slide_out_ready_cb(lv_anim_t *a);
// static void anim_slide_in_ready_cb(lv_anim_t *a);
// static void anim_zoom_out_ready_cb(lv_anim_t *a);
// static void anim_zoom_in_ready_cb(lv_anim_t *a);

// // ========== 公共 API：设置动画类型 ==========
// void desktop_app_set_animation(desktop_anim_type_t type)
// {
//     if (type < DESKTOP_ANIM_NONE || type >= DESKTOP_ANIM_MAX) {
//         ESP_LOGW(TAG, "Invalid animation type %d, ignoring", type);
//         return;
//     }
//     current_anim_type = type;
//     ESP_LOGI(TAG, "Animation type set to %d", type);
// }

// // ========== 桌面应用实现 ==========
// static void desktop_on_create(ui_app_t *app)
// {
//     desktop_screen = lv_obj_create(NULL);
//     if (desktop_screen == NULL) {
//         ESP_LOGE(TAG, "Failed to create desktop screen");
//         return;
//     }
//     lv_obj_set_style_bg_color(desktop_screen, lv_color_hex(0x000000), 0);
//     lv_obj_add_event_cb(desktop_screen, desktop_gesture_handler, LV_EVENT_GESTURE, NULL);

//     icon_img = lv_img_create(desktop_screen);
//     lv_obj_align(icon_img, LV_ALIGN_CENTER, 0, -20);
//     lv_obj_add_flag(icon_img, LV_OBJ_FLAG_CLICKABLE);
//     lv_obj_add_event_cb(icon_img, desktop_icon_click_handler, LV_EVENT_CLICKED, NULL);

//     name_label = lv_label_create(desktop_screen);
//     lv_obj_set_style_text_color(name_label, lv_color_hex(0xFFFFFF), 0);
//     lv_obj_align(name_label, LV_ALIGN_CENTER, 0, 60);
//     lv_label_set_text(name_label, "");

//     // 确保初始完全不透明
//     lv_obj_set_style_opa(icon_img, LV_OPA_COVER, 0);
//     lv_obj_set_style_opa(name_label, LV_OPA_COVER, 0);

//     current_item_index = 0;
//     desktop_update_display();

//     app->screen = desktop_screen;
// }

// static bool desktop_on_key_event(ui_app_t *app, void *key_event)
// {
//     static int i = 0;
//     key_event_data_t *data = (key_event_data_t *)key_event;
//     if (data->event == KEY_EVENT_ROTATE_CW) {
//         desktop_next_item();
//         return true;
//     } else if (data->event == KEY_EVENT_ROTATE_CCW) {
//         desktop_prev_item();
//         return true;
//     } else if (data->event == KEY_EVENT_PRESS && data->key_id == KEY_ID_ENTER) {
//         desktop_open_current();
//         return true;
//     }
//     return false;
// }

// static void desktop_update_display(void)
// {
//     if (current_item_index < 0 || current_item_index >= desktop_item_count) return;
//     lv_img_set_src(icon_img, desktop_items[current_item_index].icon);
//     lv_label_set_text(name_label, desktop_items[current_item_index].label);
//     lv_obj_set_style_text_font(name_label, &font_alipuhui20, LV_STATE_DEFAULT);
// }

// static void desktop_next_item(void)
// {
//     if (desktop_item_count == 0) return;
//     if (anim_in_progress) return;  // 动画进行中，丢弃按键

//     current_item_index = (current_item_index + 1) % desktop_item_count;

//     if (current_anim_type == DESKTOP_ANIM_NONE) {
//         desktop_update_display();
//     } else {
//         desktop_animate_switch(1); // 方向：正（下一项）
//     }
// }

// static void desktop_prev_item(void)
// {
//     if (desktop_item_count == 0) return;
//     if (anim_in_progress) return;

//     current_item_index = (current_item_index - 1 + desktop_item_count) % desktop_item_count;

//     if (current_anim_type == DESKTOP_ANIM_NONE) {
//         desktop_update_display();
//     } else {
//         desktop_animate_switch(-1); // 方向：反（上一项）
//     }
// }

// static void desktop_open_current(void)
// {
//     if (current_item_index < 0 || current_item_index >= desktop_item_count) return;
//     ui_service_open_app(desktop_items[current_item_index].app_name);
// }

// static void desktop_gesture_handler(lv_event_t *e)
// {
//     lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
//     if (dir == LV_DIR_LEFT) {
//         desktop_next_item();
//     } else if (dir == LV_DIR_RIGHT) {
//         desktop_prev_item();
//     }
// }

// static void desktop_icon_click_handler(lv_event_t *e)
// {
//     desktop_open_current();
// }

// // ========== 动画调度 ==========
// static void desktop_animate_switch(int direction)
// {
//     anim_in_progress = true;

//     switch (current_anim_type) {
//         case DESKTOP_ANIM_FADE:
//             start_fade_anim();
//             break;
//         case DESKTOP_ANIM_SLIDE:
//             g_slide_dir = direction;
//             start_slide_anim();
//             break;
//         case DESKTOP_ANIM_ZOOM:
//             start_zoom_anim();
//             break;
//         default:
//             anim_in_progress = false;
//             desktop_update_display();
//             break;
//     }
// }

// // ---------- 通用动画回调函数 ----------
// static void set_opa_cb(void *var, int32_t v)
// {
//     lv_obj_t *obj = (lv_obj_t *)var;
//     if (lv_obj_is_valid(obj)) {
//         lv_obj_set_style_opa(obj, (lv_opa_t)v, 0);
//     }
// }

// static void set_x_cb(void *var, int32_t v)
// {
//     lv_obj_t *obj = (lv_obj_t *)var;
//     if (lv_obj_is_valid(obj)) {
//         lv_obj_set_x(obj, v);
//     }
// }

// static void set_zoom_cb(void *var, int32_t v)
// {
//     lv_obj_t *obj = (lv_obj_t *)var;
//     if (lv_obj_is_valid(obj) && obj == icon_img) {
//         lv_img_set_zoom(obj, (uint16_t)v);
//     }
// }

// // ---------- 1. 淡入淡出动画 ----------
// static void start_fade_anim(void)
// {
//     // 淡出图标
//     lv_anim_t anim;
//     lv_anim_init(&anim);
//     lv_anim_set_var(&anim, icon_img);
//     lv_anim_set_exec_cb(&anim, set_opa_cb);
//     lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
//     lv_anim_set_time(&anim, 200);
//     lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
//     lv_anim_set_ready_cb(&anim, anim_fade_out_ready_cb);
//     lv_anim_start(&anim);

//     // 淡出文字
//     lv_anim_t anim2;
//     lv_anim_init(&anim2);
//     lv_anim_set_var(&anim2, name_label);
//     lv_anim_set_exec_cb(&anim2, set_opa_cb);
//     lv_anim_set_values(&anim2, LV_OPA_COVER, LV_OPA_TRANSP);
//     lv_anim_set_time(&anim2, 200);
//     lv_anim_set_path_cb(&anim2, lv_anim_path_ease_in_out);
//     lv_anim_start(&anim2);
// }

// static void anim_fade_out_ready_cb(lv_anim_t *a)
// {
//     desktop_update_display();

//     // 淡入图标
//     lv_anim_t anim;
//     lv_anim_init(&anim);
//     lv_anim_set_var(&anim, icon_img);
//     lv_anim_set_exec_cb(&anim, set_opa_cb);
//     lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
//     lv_anim_set_time(&anim, 200);
//     lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
//     lv_anim_set_ready_cb(&anim, anim_fade_in_ready_cb);
//     lv_anim_start(&anim);

//     // 淡入文字
//     lv_anim_t anim2;
//     lv_anim_init(&anim2);
//     lv_anim_set_var(&anim2, name_label);
//     lv_anim_set_exec_cb(&anim2, set_opa_cb);
//     lv_anim_set_values(&anim2, LV_OPA_TRANSP, LV_OPA_COVER);
//     lv_anim_set_time(&anim2, 200);
//     lv_anim_set_path_cb(&anim2, lv_anim_path_ease_in_out);
//     lv_anim_start(&anim2);
// }

// static void anim_fade_in_ready_cb(lv_anim_t *a)
// {
//     anim_in_progress = false;
// }

// // ---------- 2. 横向滑动动画 ----------
// static void start_slide_anim(void)
// {
//     lv_coord_t screen_w = lv_obj_get_width(desktop_screen);
//     lv_coord_t slide_dist = screen_w * 8 / 10;

//     // 记录当前位置并解除对齐
//     lv_coord_t icon_x = lv_obj_get_x(icon_img);
//     lv_coord_t label_x = lv_obj_get_x(name_label);
//     lv_obj_align(icon_img, LV_ALIGN_TOP_LEFT, icon_x, lv_obj_get_y(icon_img));
//     lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, label_x, lv_obj_get_y(name_label));

//     // 滑出方向：g_slide_dir = 1 时图标向左滑，= -1 时向右滑
//     lv_coord_t out_icon_target = icon_x - g_slide_dir * slide_dist;
//     lv_coord_t out_label_target = label_x - g_slide_dir * slide_dist;

//     // 滑出动画（图标）
//     lv_anim_t anim;
//     lv_anim_init(&anim);
//     lv_anim_set_var(&anim, icon_img);
//     lv_anim_set_exec_cb(&anim, set_x_cb);
//     lv_anim_set_values(&anim, icon_x, out_icon_target);
//     lv_anim_set_time(&anim, 250);
//     lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
//     lv_anim_set_ready_cb(&anim, anim_slide_out_ready_cb);
//     lv_anim_start(&anim);

//     // 滑出动画（文字）
//     lv_anim_t anim2;
//     lv_anim_init(&anim2);
//     lv_anim_set_var(&anim2, name_label);
//     lv_anim_set_exec_cb(&anim2, set_x_cb);
//     lv_anim_set_values(&anim2, label_x, out_label_target);
//     lv_anim_set_time(&anim2, 250);
//     lv_anim_set_path_cb(&anim2, lv_anim_path_ease_in_out);
//     lv_anim_start(&anim2);

//     // 同时半透明化
//     lv_anim_t anim_opa;
//     lv_anim_init(&anim_opa);
//     lv_anim_set_var(&anim_opa, icon_img);
//     lv_anim_set_exec_cb(&anim_opa, set_opa_cb);
//     lv_anim_set_values(&anim_opa, LV_OPA_COVER, LV_OPA_50);
//     lv_anim_set_time(&anim_opa, 250);
//     lv_anim_start(&anim_opa);

//     lv_anim_t anim_opa2;
//     lv_anim_init(&anim_opa2);
//     lv_anim_set_var(&anim_opa2, name_label);
//     lv_anim_set_exec_cb(&anim_opa2, set_opa_cb);
//     lv_anim_set_values(&anim_opa2, LV_OPA_COVER, LV_OPA_50);
//     lv_anim_set_time(&anim_opa2, 250);
//     lv_anim_start(&anim_opa2);
// }

// static void anim_slide_out_ready_cb(lv_anim_t *a)
// {
//     // 更新内容
//     desktop_update_display();

//     lv_coord_t screen_w = lv_obj_get_width(desktop_screen);
//     lv_coord_t slide_dist = screen_w * 8 / 10;

//     // 计算目标中心位置（恢复对齐前的参考坐标）
//     lv_coord_t icon_target_x = (screen_w - lv_obj_get_width(icon_img)) / 2;
//     lv_coord_t label_target_x = (screen_w - lv_obj_get_width(name_label)) / 2;

//     // 滑入起始位置：从反方向进入
//     lv_coord_t start_icon_x = icon_target_x + g_slide_dir * slide_dist;
//     lv_coord_t start_label_x = label_target_x + g_slide_dir * slide_dist;

//     lv_obj_set_x(icon_img, start_icon_x);
//     lv_obj_set_x(name_label, start_label_x);
//     lv_obj_set_style_opa(icon_img, LV_OPA_50, 0);
//     lv_obj_set_style_opa(name_label, LV_OPA_50, 0);

//     // 滑入动画
//     lv_anim_t anim;
//     lv_anim_init(&anim);
//     lv_anim_set_var(&anim, icon_img);
//     lv_anim_set_exec_cb(&anim, set_x_cb);
//     lv_anim_set_values(&anim, start_icon_x, icon_target_x);
//     lv_anim_set_time(&anim, 250);
//     lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
//     lv_anim_set_ready_cb(&anim, anim_slide_in_ready_cb);
//     lv_anim_start(&anim);

//     lv_anim_t anim2;
//     lv_anim_init(&anim2);
//     lv_anim_set_var(&anim2, name_label);
//     lv_anim_set_exec_cb(&anim2, set_x_cb);
//     lv_anim_set_values(&anim2, start_label_x, label_target_x);
//     lv_anim_set_time(&anim2, 250);
//     lv_anim_set_path_cb(&anim2, lv_anim_path_ease_in_out);
//     lv_anim_start(&anim2);

//     // 淡入
//     lv_anim_t anim_opa;
//     lv_anim_init(&anim_opa);
//     lv_anim_set_var(&anim_opa, icon_img);
//     lv_anim_set_exec_cb(&anim_opa, set_opa_cb);
//     lv_anim_set_values(&anim_opa, LV_OPA_50, LV_OPA_COVER);
//     lv_anim_set_time(&anim_opa, 250);
//     lv_anim_start(&anim_opa);

//     lv_anim_t anim_opa2;
//     lv_anim_init(&anim_opa2);
//     lv_anim_set_var(&anim_opa2, name_label);
//     lv_anim_set_exec_cb(&anim_opa2, set_opa_cb);
//     lv_anim_set_values(&anim_opa2, LV_OPA_50, LV_OPA_COVER);
//     lv_anim_set_time(&anim_opa2, 250);
//     lv_anim_start(&anim_opa2);
// }

// static void anim_slide_in_ready_cb(lv_anim_t *a)
// {
//     anim_in_progress = false;
//     // 恢复居中对齐
//     lv_obj_align(icon_img, LV_ALIGN_CENTER, 0, -20);
//     lv_obj_align(name_label, LV_ALIGN_CENTER, 0, 60);
//     lv_obj_set_style_opa(icon_img, LV_OPA_COVER, 0);
//     lv_obj_set_style_opa(name_label, LV_OPA_COVER, 0);
// }

// // ---------- 3. 缩放动画（图标缩放 + 文字淡入淡出） ----------
// static void start_zoom_anim(void)
// {
//     // 缩小 + 淡出图标
//     lv_anim_t anim;
//     lv_anim_init(&anim);
//     lv_anim_set_var(&anim, icon_img);
//     lv_anim_set_exec_cb(&anim, set_zoom_cb);
//     lv_anim_set_values(&anim, LV_IMG_ZOOM_NONE, 0);
//     lv_anim_set_time(&anim, 250);
//     lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
//     lv_anim_set_ready_cb(&anim, anim_zoom_out_ready_cb);
//     lv_anim_start(&anim);

//     lv_anim_t anim_opa;
//     lv_anim_init(&anim_opa);
//     lv_anim_set_var(&anim_opa, icon_img);
//     lv_anim_set_exec_cb(&anim_opa, set_opa_cb);
//     lv_anim_set_values(&anim_opa, LV_OPA_COVER, LV_OPA_TRANSP);
//     lv_anim_set_time(&anim_opa, 250);
//     lv_anim_start(&anim_opa);

//     // 文字只淡出
//     lv_anim_t anim2;
//     lv_anim_init(&anim2);
//     lv_anim_set_var(&anim2, name_label);
//     lv_anim_set_exec_cb(&anim2, set_opa_cb);
//     lv_anim_set_values(&anim2, LV_OPA_COVER, LV_OPA_TRANSP);
//     lv_anim_set_time(&anim2, 250);
//     lv_anim_set_path_cb(&anim2, lv_anim_path_ease_in_out);
//     lv_anim_start(&anim2);
// }

// static void anim_zoom_out_ready_cb(lv_anim_t *a)
// {
//     desktop_update_display();

//     // 立即设置缩放为0，透明
//     lv_img_set_zoom(icon_img, 0);
//     lv_obj_set_style_opa(icon_img, LV_OPA_TRANSP, 0);
//     lv_obj_set_style_opa(name_label, LV_OPA_TRANSP, 0);

//     // 放大 + 淡入图标
//     lv_anim_t anim;
//     lv_anim_init(&anim);
//     lv_anim_set_var(&anim, icon_img);
//     lv_anim_set_exec_cb(&anim, set_zoom_cb);
//     lv_anim_set_values(&anim, 0, LV_IMG_ZOOM_NONE);
//     lv_anim_set_time(&anim, 250);
//     lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
//     lv_anim_set_ready_cb(&anim, anim_zoom_in_ready_cb);
//     lv_anim_start(&anim);

//     lv_anim_t anim_opa;
//     lv_anim_init(&anim_opa);
//     lv_anim_set_var(&anim_opa, icon_img);
//     lv_anim_set_exec_cb(&anim_opa, set_opa_cb);
//     lv_anim_set_values(&anim_opa, LV_OPA_TRANSP, LV_OPA_COVER);
//     lv_anim_set_time(&anim_opa, 250);
//     lv_anim_start(&anim_opa);

//     // 文字淡入
//     lv_anim_t anim2;
//     lv_anim_init(&anim2);
//     lv_anim_set_var(&anim2, name_label);
//     lv_anim_set_exec_cb(&anim2, set_opa_cb);
//     lv_anim_set_values(&anim2, LV_OPA_TRANSP, LV_OPA_COVER);
//     lv_anim_set_time(&anim2, 250);
//     lv_anim_set_path_cb(&anim2, lv_anim_path_ease_in_out);
//     lv_anim_start(&anim2);
// }

// static void anim_zoom_in_ready_cb(lv_anim_t *a)
// {
//     anim_in_progress = false;
//     // 确保最终状态
//     lv_img_set_zoom(icon_img, LV_IMG_ZOOM_NONE);
//     lv_obj_set_style_opa(icon_img, LV_OPA_COVER, 0);
//     lv_obj_set_style_opa(name_label, LV_OPA_COVER, 0);
// }

// // ========== 注册函数 ==========
// void desktop_app_register(void)
// {
//     memset(&desktop_app, 0, sizeof(desktop_app));
//     desktop_app.name = "desktop_app";
//     desktop_app.on_create = desktop_on_create;
//     desktop_app.on_key_event = desktop_on_key_event;
//     ui_service_register_app(&desktop_app);
//     ESP_LOGI(TAG, "Desktop app registered");
// }


