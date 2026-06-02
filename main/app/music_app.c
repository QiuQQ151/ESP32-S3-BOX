// #include <string.h>
// #include <stdlib.h>
// #include <stdio.h>
// #include <dirent.h>
// #include "esp_log.h"
// #include "lvgl.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "ui_service.h"
// #include "music_app.h"
// #include "system_event.h"
// #include "keys_hal.h"
// #include "sntp_service.h"
// #include "audio_service.h"
// #include "led_service.h"

// static const char *TAG = "music_app";
// static ui_app_t s_music_app;

// // 图片资源声明
// LV_IMG_DECLARE(icon_next_40);
// LV_IMG_DECLARE(icon_play_40);
// LV_IMG_DECLARE(icon_prev_40);
// LV_IMG_DECLARE(icon_reflash_28);
// LV_IMG_DECLARE(icon_stop_40);

// // 内部 UI 句柄
// static struct {
//     lv_obj_t *time_label;
//     lv_obj_t *track_label;
//     lv_obj_t *play_btn;
//     lv_obj_t *play_icon;
//     lv_obj_t *progress_slider;
//     lv_obj_t *elapsed_label;    // 已播时间标签
//     lv_obj_t *remaining_label;  // 剩余时间标签
//     bool playing;
// } s_music_ui = {0};

// static TaskHandle_t music_update_status_task_handle = NULL;

// #define MUSIC_ROOT_PATH "/sdcard/music"
// #define MAX_TRACKS      500

// typedef struct {
//     char name[128];
//     char path[512];
// } music_track_t;

// static music_track_t *track_list = NULL;
// static int track_count = 0;
// static int current_track_index = 0;
// static int volume = 50;

// static int current_position_sec = 0;
// static int total_duration_sec = 0;

// // 内部函数声明
// static void music_on_create(ui_app_t *app);
// static void music_on_open(ui_app_t *app);
// static void music_on_close(ui_app_t *app);
// static void music_on_destroy(ui_app_t *app);
// static void music_on_event(ui_app_t *app, event_data_t *event);
// static bool music_handle_key_event(key_event_data_t *key);
// static void music_app_led_control(led_mode_t mode, uint32_t arg);
// static void music_handle_change_to_audio(audio_service_cmd_t cmd, int seek_sec);
// static void music_update_status_task(void *arg);
// static void music_update_time(void);
// static void music_update_progress(void);
// static void music_request_position_update(void);
// static int  music_scan_directory(const char *root);
// static void scan_dir_recursive(const char *dir_path, int *count);

// static void music_play_pause(void);
// static void music_prev_track(void);
// static void music_next_track(void);
// static void music_refresh(void);
// static void music_increase_volume(void);
// static void music_decrease_volume(void);

// static void play_btn_event_cb(lv_event_t *e)   { music_play_pause(); }
// static void prev_btn_event_cb(lv_event_t *e)   { music_prev_track(); }
// static void next_btn_event_cb(lv_event_t *e)   { music_next_track(); }
// static void refresh_btn_event_cb(lv_event_t *e){ music_refresh(); }
// static void progress_slider_event_cb(lv_event_t *e);

// void music_app_register(void)
// {
//     memset(&s_music_app, 0, sizeof(s_music_app));
//     s_music_app.name = "music_app";
//     s_music_app.screen = NULL;
//     s_music_app.on_create = music_on_create;
//     s_music_app.on_open = music_on_open;
//     s_music_app.on_close = music_on_close;
//     s_music_app.on_destroy = music_on_destroy;
//     s_music_app.on_event = music_on_event;
//     ui_service_register_app(&s_music_app);
//     ESP_LOGI(TAG, "Music app registered");
// }

// static void music_on_create(ui_app_t *app)
// {
//     ESP_LOGI(TAG, "music_on_create");
//     app->screen = lv_obj_create(NULL);
//     lv_obj_set_style_bg_color(app->screen, lv_color_black(), 0);

//     // 顶部时间
//     lv_obj_t *top_bar = lv_obj_create(app->screen);
//     lv_obj_set_size(top_bar, 160, 32);
//     lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 28);
//     lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
//     lv_obj_set_style_border_width(top_bar, 0, 0);
//     lv_obj_set_style_pad_all(top_bar, 0, 0);
//     s_music_ui.time_label = lv_label_create(top_bar);
//     lv_label_set_text(s_music_ui.time_label, "12:00");
//     lv_obj_set_style_text_color(s_music_ui.time_label, lv_color_white(), 0);
//     lv_obj_align(s_music_ui.time_label, LV_ALIGN_RIGHT_MID, 0, 0);

//     // 曲目显示 + 刷新按钮
//     lv_obj_t *track_area = lv_obj_create(app->screen);
//     lv_obj_set_size(track_area, 180, 36);
//     lv_obj_align(track_area, LV_ALIGN_CENTER, 0, -50);
//     lv_obj_set_style_bg_opa(track_area, LV_OPA_TRANSP, 0);
//     lv_obj_set_style_border_width(track_area, 0, 0);
//     lv_obj_set_style_pad_all(track_area, 0, 0);

//     s_music_ui.track_label = lv_label_create(track_area);
//     lv_label_set_text(s_music_ui.track_label,
//                       (track_count > 0) ? track_list[current_track_index].name : "无音乐");
//     lv_obj_set_style_text_color(s_music_ui.track_label, lv_color_white(), 0);
//     LV_FONT_DECLARE(font_alipuhui20);
//     lv_obj_set_style_text_font(s_music_ui.track_label, &font_alipuhui20, 0);
//     lv_obj_set_width(s_music_ui.track_label, 135);
//     lv_label_set_long_mode(s_music_ui.track_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
//     lv_obj_align(s_music_ui.track_label, LV_ALIGN_LEFT_MID, 0, 0);

//     lv_obj_t *refresh_btn = lv_btn_create(track_area);
//     lv_obj_set_size(refresh_btn, 30, 30);
//     lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, 0, 0);
//     lv_obj_set_style_radius(refresh_btn, LV_RADIUS_CIRCLE, 0);
//     lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x4FC3F7), 0);
//     lv_obj_set_style_bg_opa(refresh_btn, LV_OPA_COVER, 0);
//     lv_obj_add_event_cb(refresh_btn, refresh_btn_event_cb, LV_EVENT_CLICKED, NULL);
//     lv_obj_t *refresh_img = lv_img_create(refresh_btn);
//     lv_img_set_src(refresh_img, &icon_reflash_28);
//     lv_obj_center(refresh_img);

//     // ---- 已播时间标签 ----
//     s_music_ui.elapsed_label = lv_label_create(app->screen);
//     lv_label_set_text(s_music_ui.elapsed_label, "00:00");
//     lv_obj_set_style_text_color(s_music_ui.elapsed_label, lv_color_white(), 0);
//     lv_obj_align(s_music_ui.elapsed_label, LV_ALIGN_CENTER, -90, 0);

//     // 进度条
//     s_music_ui.progress_slider = lv_slider_create(app->screen);
//     lv_obj_set_size(s_music_ui.progress_slider, 120, 10);
//     lv_obj_align_to(s_music_ui.progress_slider, track_area, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
//     lv_slider_set_range(s_music_ui.progress_slider, 0, 1000);
//     lv_slider_set_value(s_music_ui.progress_slider, 0, LV_ANIM_OFF);
//     lv_obj_add_event_cb(s_music_ui.progress_slider, progress_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

//     // ---- 剩余时间标签 ----
//     s_music_ui.remaining_label = lv_label_create(app->screen);
//     lv_label_set_text(s_music_ui.remaining_label, "-00:00");
//     lv_obj_set_style_text_color(s_music_ui.remaining_label, lv_color_white(), 0);
//     lv_obj_align(s_music_ui.remaining_label, LV_ALIGN_CENTER, 90, 0);

//     // 底部控制栏
//     lv_obj_t *control_bar = lv_obj_create(app->screen);
//     lv_obj_set_size(control_bar, 180, 55);
//     lv_obj_align(control_bar, LV_ALIGN_BOTTOM_MID, 0, -30);
//     lv_obj_set_style_bg_opa(control_bar, LV_OPA_TRANSP, 0);
//     lv_obj_set_style_border_width(control_bar, 0, 0);
//     lv_obj_set_style_pad_all(control_bar, 0, 0);

//     lv_obj_t *prev_btn = lv_btn_create(control_bar);
//     lv_obj_set_size(prev_btn, 43, 43);
//     lv_obj_set_style_radius(prev_btn, LV_RADIUS_CIRCLE, 0);
//     lv_obj_set_style_bg_color(prev_btn, lv_color_hex(0x546E7A), 0);
//     lv_obj_set_style_bg_opa(prev_btn, LV_OPA_COVER, 0);
//     lv_obj_align(prev_btn, LV_ALIGN_LEFT_MID, 10, 0);
//     lv_obj_add_event_cb(prev_btn, prev_btn_event_cb, LV_EVENT_CLICKED, NULL);
//     lv_obj_t *prev_img = lv_img_create(prev_btn);
//     lv_img_set_src(prev_img, &icon_prev_40);
//     lv_obj_center(prev_img);

//     lv_obj_t *next_btn = lv_btn_create(control_bar);
//     lv_obj_set_size(next_btn, 43, 43);
//     lv_obj_set_style_radius(next_btn, LV_RADIUS_CIRCLE, 0);
//     lv_obj_set_style_bg_color(next_btn, lv_color_hex(0x546E7A), 0);
//     lv_obj_set_style_bg_opa(next_btn, LV_OPA_COVER, 0);
//     lv_obj_align(next_btn, LV_ALIGN_RIGHT_MID, -10, 0);
//     lv_obj_add_event_cb(next_btn, next_btn_event_cb, LV_EVENT_CLICKED, NULL);
//     lv_obj_t *next_img = lv_img_create(next_btn);
//     lv_img_set_src(next_img, &icon_next_40);
//     lv_obj_center(next_img);

//     s_music_ui.play_btn = lv_btn_create(control_bar);
//     lv_obj_set_size(s_music_ui.play_btn, 43, 43);
//     lv_obj_set_style_radius(s_music_ui.play_btn, LV_RADIUS_CIRCLE, 0);
//     lv_obj_set_style_bg_color(s_music_ui.play_btn, lv_color_hex(0x4CAF50), 0);
//     lv_obj_set_style_bg_opa(s_music_ui.play_btn, LV_OPA_COVER, 0);
//     lv_obj_align(s_music_ui.play_btn, LV_ALIGN_CENTER, 0, 0);
//     lv_obj_add_event_cb(s_music_ui.play_btn, play_btn_event_cb, LV_EVENT_CLICKED, NULL);
//     s_music_ui.play_icon = lv_img_create(s_music_ui.play_btn);
//     lv_img_set_src(s_music_ui.play_icon, &icon_stop_40);
//     lv_obj_center(s_music_ui.play_icon);
//     s_music_ui.playing = false;

//     // 扫描音乐目录
//     if (music_scan_directory(MUSIC_ROOT_PATH) > 0) {
//         lv_label_set_text(s_music_ui.track_label, track_list[0].name);
//     }

//     volume = 50;
//     if (track_count > 0) {
//         s_music_ui.playing = true;
//         lv_img_set_src(s_music_ui.play_icon, &icon_play_40);
//         music_handle_change_to_audio(AUDIO_CMD_CONNECT, 0);
//     }

//     xTaskCreate(music_update_status_task, "music_update_status", 4096, NULL, 5,
//                 &music_update_status_task_handle);
//     music_app_led_control(LED_MODE_MUSIC, 100);
//     ESP_LOGI(TAG, "Music UI created");
// }

// static void music_on_open(ui_app_t *app)
// {
//     if (s_music_ui.track_label && track_count > 0) {
//         lv_label_set_text(s_music_ui.track_label, track_list[current_track_index].name);
//     }
// }

// static void music_on_close(ui_app_t *app) { }

// static void music_on_destroy(ui_app_t *app)
// {
//     if (music_update_status_task_handle) {
//         vTaskDelete(music_update_status_task_handle);
//         music_update_status_task_handle = NULL;
//     }
//     music_handle_change_to_audio(AUDIO_CMD_DISCONNECT, 0);
//     if (track_list) {
//         free(track_list);
//         track_list = NULL;
//     }
//     track_count = 0;
//     memset(&s_music_ui, 0, sizeof(s_music_ui));
//     app->screen = NULL;
// }

// static void music_on_event(ui_app_t *app, event_data_t *event)
// {
//     if (!event) return;
//     if (event->event_type == NOTIFICATION) {
//         if (event->service_id == KEYHAL_SERVICE) {
//             key_event_data_t *key_data = (key_event_data_t *)event->data;
//             if (key_data) music_handle_key_event(key_data);
//         } else if (event->service_id == SNTP_SERVICE) {
//             if (event->data) {
//                 sntp_service_send_data_t *sntp = (sntp_service_send_data_t *)event->data;
//                 if (s_music_ui.time_label) {
//                     lv_label_set_text(s_music_ui.time_label, sntp->current_time);
//                 }
//             }
//         } else if (event->service_id == AUDIO_SERVICE) {
//             audio_service_receive_data_t *audio_evt = (audio_service_receive_data_t *)event->data;
//             if (audio_evt) {
//                 switch (audio_evt->cmd) {
//                     case AUDIO_CMD_POSITION_UPDATE:
//                         current_position_sec = audio_evt->current_position;
//                         total_duration_sec = audio_evt->total_duration;
//                         music_update_progress();
//                         break;
//                     case AUDIO_CMD_END:
//                         ESP_LOGI(TAG, "Track finished, playing next");
//                         music_next_track();
//                         break;
//                     default:
//                         break;
//                 }
//             }
//         }
//     }
//     if (event->data) free(event->data);
//     free(event);
// }

// static bool music_handle_key_event(key_event_data_t *key)
// {
//     if (key->event == KEY_EVENT_PRESS) {
//         switch (key->key_id) {
//             case KEY_ID_ENTER:
//                 music_play_pause();
//                 return true;
//             case KEY_ID_BACK: {
//                 ui_service_receive_data_t *cmd = malloc(sizeof(ui_service_receive_data_t));
//                 if (!cmd) return true;
//                 cmd->cmd = UI_CMD_GO_HOME;
//                 event_data_t *evt = malloc(sizeof(event_data_t));
//                 if (!evt) { free(cmd); return true; }
//                 evt->service_id = UI_SERVICE;
//                 evt->event_type = REQUEST;
//                 evt->data = cmd;
//                 evt->data_len = sizeof(ui_service_receive_data_t);
//                 xQueueSend(get_ui_service_queue(), &evt, 0);
//                 return true;
//             }
//             default: return true;
//         }
//     } else if (key->event == KEY_EVENT_ROTATE_CW) {
//         music_increase_volume();
//         return true;
//     } else if (key->event == KEY_EVENT_ROTATE_CCW) {
//         music_decrease_volume();
//         return true;
//     }
//     return false;
// }

// static void music_update_status_task(void *arg)
// {
//     while (1) {
//         vTaskDelay(pdMS_TO_TICKS(1000));
//         music_update_time();
//         static int counter = 0;
//         if (++counter % 2 == 0) {
//             music_request_position_update();
//         }
//     }
// }

// static void music_request_position_update(void)
// {
//     audio_service_receive_data_t *pos_payload = calloc(1, sizeof(audio_service_receive_data_t));
//     if (!pos_payload) return;
//     pos_payload->cmd = AUDIO_CMD_GET_INFO;
//     event_data_t *evt = malloc(sizeof(event_data_t));
//     if (!evt) { free(pos_payload); return; }
//     evt->service_id = AUDIO_SERVICE;
//     evt->event_type = REQUEST;
//     evt->reply_queue = get_ui_service_queue();
//     evt->data = pos_payload;
//     evt->data_len = sizeof(audio_service_receive_data_t);
//     xQueueSend(get_audio_service_queue(), &evt, 0);
// }

// static void music_update_progress(void)
// {
//     if (!s_music_ui.progress_slider) return;
//     if (total_duration_sec > 0) {
//         int progress = (current_position_sec * 1000) / total_duration_sec;
//         if (progress > 1000) progress = 1000;
//         lv_slider_set_value(s_music_ui.progress_slider, progress, LV_ANIM_ON);
//     }
//     // 更新时间显示
//     char buf[16];
//     int elapsed = current_position_sec;
//     int remaining = total_duration_sec - current_position_sec;
//     if (remaining < 0) remaining = 0;
//     snprintf(buf, sizeof(buf), "%02d:%02d", elapsed/60, elapsed%60);
//     lv_label_set_text(s_music_ui.elapsed_label, buf);
//     snprintf(buf, sizeof(buf), "-%02d:%02d", remaining/60, remaining%60);
//     lv_label_set_text(s_music_ui.remaining_label, buf);
// }

// static void music_update_time(void)
// {
//     sntp_service_receive_data_t *sntp = malloc(sizeof(sntp_service_receive_data_t));
//     if (!sntp) return;
//     sntp->cmd = SNTP_CMD_GET_TIME;
//     event_data_t *evt = malloc(sizeof(event_data_t));
//     if (!evt) { free(sntp); return; }
//     evt->service_id = UI_SERVICE;
//     evt->event_type = REQUEST;
//     evt->reply_queue = get_ui_service_queue();
//     evt->data = sntp;
//     evt->data_len = sizeof(sntp_service_receive_data_t);
//     xQueueSend(get_sntp_service_queue(), &evt, 0);
// }

// // 核心命令发送函数：对于 CONNECT/PAUSE/RESUME/VOLUME 等分别处理
// static void music_handle_change_to_audio(audio_service_cmd_t cmd, int seek_sec)
// {
//     if (track_count == 0 && cmd != AUDIO_CMD_DISCONNECT) return;

//     // 若是连接命令，先断开旧连接
//     if (cmd == AUDIO_CMD_CONNECT) {
//         audio_service_receive_data_t *disc = calloc(1, sizeof(audio_service_receive_data_t));
//         if (disc) {
//             disc->cmd = AUDIO_CMD_DISCONNECT;
//             event_data_t *evt = malloc(sizeof(event_data_t));
//             if (evt) {
//                 evt->service_id = UI_SERVICE;
//                 evt->event_type = REQUEST;
//                 evt->data = disc;
//                 evt->data_len = sizeof(audio_service_receive_data_t);
//                 xQueueSend(get_audio_service_queue(), &evt, 0);
//             } else free(disc);
//         }
//         vTaskDelay(pdMS_TO_TICKS(200));
//     }

//     // 选择解码器 (仅 CONNECT 需要)
//     audio_service_stream_type_t decoder = mp3_dec;
//     if (cmd == AUDIO_CMD_CONNECT) {
//         const char *name = track_list[current_track_index].name;
//         const char *ext = strrchr(name, '.');
//         if (ext) {
//             if (strcasecmp(ext, ".mp3") == 0) decoder = mp3_dec;
//             else if (strcasecmp(ext, ".wav") == 0) decoder = wav_dec;
//             else if (strcasecmp(ext, ".flac") == 0) decoder = flac_dec;
//             else if (strcasecmp(ext, ".aac") == 0) decoder = aac_dec;
//         }
//     }

//     audio_service_receive_data_t *payload = malloc(sizeof(audio_service_receive_data_t));
//     if (!payload) return;
//     memset(payload, 0, sizeof(*payload));
//     payload->cmd = cmd;
//     if (cmd == AUDIO_CMD_CONNECT) {
//         strcpy(payload->url, track_list[current_track_index].path);
//         payload->prv_type = file_str;
//         payload->midle_type = decoder;
//         payload->back_type = i2s_hal;
//         payload->start_after_connect = s_music_ui.playing;
//         if (seek_sec > 0) {
//             payload->current_position = seek_sec;
//         }
//     }
//     payload->volume = volume;

//     event_data_t *evt = malloc(sizeof(event_data_t));
//     if (!evt) { free(payload); return; }
//     evt->service_id = UI_SERVICE;
//     evt->event_type = REQUEST;
//     evt->reply_queue = get_ui_service_queue();
//     evt->data = payload;
//     evt->data_len = sizeof(audio_service_receive_data_t);
//     xQueueSend(get_audio_service_queue(), &evt, 0);

//     // 重置进度显示（连接时）
//     if (cmd == AUDIO_CMD_CONNECT) {
//         current_position_sec = seek_sec;
//         total_duration_sec = 0;
//         music_update_progress();
//     }
// }

// static void music_play_pause(void)
// {
//     if (!s_music_ui.play_icon || track_count == 0) return;
//     if (s_music_ui.playing) {
//         ESP_LOGI(TAG, "pause");
//         lv_img_set_src(s_music_ui.play_icon, &icon_stop_40);
//         s_music_ui.playing = false;
//         music_handle_change_to_audio(AUDIO_CMD_PAUSE, 0);
//     } else {
//         ESP_LOGI(TAG, "resume");
//         lv_img_set_src(s_music_ui.play_icon, &icon_play_40);
//         s_music_ui.playing = true;
//         music_handle_change_to_audio(AUDIO_CMD_RESUME, 0);
//     }
// }

// static void music_prev_track(void)
// {
//     if (track_count == 0) return;
//     current_track_index = (current_track_index - 1 + track_count) % track_count;
//     if (s_music_ui.track_label) {
//         lv_label_set_text(s_music_ui.track_label, track_list[current_track_index].name);
//     }
//     music_handle_change_to_audio(AUDIO_CMD_CONNECT, 0);
// }

// static void music_next_track(void)
// {
//     if (track_count == 0) return;
//     current_track_index = (current_track_index + 1) % track_count;
//     if (s_music_ui.track_label) {
//         lv_label_set_text(s_music_ui.track_label, track_list[current_track_index].name);
//     }
//     music_handle_change_to_audio(AUDIO_CMD_CONNECT, 0);
// }

// static void music_refresh(void)
// {
//     if (track_list) {
//         free(track_list);
//         track_list = NULL;
//     }
//     track_count = 0;
//     current_track_index = 0;
//     if (music_scan_directory(MUSIC_ROOT_PATH) > 0) {
//         lv_label_set_text(s_music_ui.track_label, track_list[0].name);
//         s_music_ui.playing = true;
//         lv_img_set_src(s_music_ui.play_icon, &icon_play_40);
//         music_handle_change_to_audio(AUDIO_CMD_CONNECT, 0);
//     } else {
//         lv_label_set_text(s_music_ui.track_label, "无音乐");
//     }
// }

// // 进度条拖动回调
// static void progress_slider_event_cb(lv_event_t *e)
// {
//     lv_obj_t *slider = lv_event_get_target(e);
//     if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED &&
//         lv_obj_has_state(slider, LV_STATE_EDITED) && total_duration_sec > 0) {
//         int progress = lv_slider_get_value(slider);
//         int seek_sec = (progress * total_duration_sec) / 1000;
//         // 重新连接并跳转
//         music_handle_change_to_audio(AUDIO_CMD_CONNECT, seek_sec);
//     }
// }

// static void music_increase_volume(void) {
//     volume = (volume < 100) ? volume + 1 : 100;
//     music_handle_change_to_audio(AUDIO_CMD_VOLUME, 0);
// }

// static void music_decrease_volume(void) {
//     volume = (volume > 0) ? volume - 1 : 0;
//     music_handle_change_to_audio(AUDIO_CMD_VOLUME, 0);
// }

// static void music_app_led_control(led_mode_t mode, uint32_t arg) {
//     led_service_receive_data_t* led_payload = malloc(sizeof(led_service_receive_data_t));
//     if(led_payload){
//         led_payload->device = LED_HAL_DEVICE_FRONT;
//         led_payload->mode = mode;
//         led_payload->brightness = 30;
//         led_payload->arg = arg;
//         event_data_t* led_event = malloc(sizeof(event_data_t));
//         if(led_event){
//             led_event->service_id = HAL;
//             led_event->event_type = REQUEST;
//             led_event->data = led_payload;
//             led_event->data_len = sizeof(led_service_receive_data_t);
//             xQueueSend(get_led_service_queue(), &led_event, 0);
//         } else {
//             free(led_payload);
//         }
//     }
// }

// // ========== 文件扫描函数（原样保留）==========
// static void scan_dir_recursive(const char *dir_path, int *count) 
// {
//     DIR *dir = opendir(dir_path);
//     if (!dir) { return; }
//     struct dirent *entry;
//     while ((entry = readdir(dir)) != NULL && *count < MAX_TRACKS) {
//         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
//         char full_path[512];
//         int written = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
//         if (written >= sizeof(full_path)) continue;
//         if (entry->d_type == DT_DIR) {
//             scan_dir_recursive(full_path, count);
//         } else {
//             const char *ext = strrchr(entry->d_name, '.');
//             if (!ext) continue;
//             if (strcasecmp(ext, ".mp3") == 0 ||
//                 strcasecmp(ext, ".wav") == 0 ||
//                 strcasecmp(ext, ".flac") == 0 ||
//                 strcasecmp(ext, ".aac") == 0) {
//                 strncpy(track_list[*count].name, entry->d_name, sizeof(track_list[*count].name)-1);
//                 strncpy(track_list[*count].path, full_path, sizeof(track_list[*count].path)-1);
//                 (*count)++;
//             }
//         }
//     }
//     closedir(dir);
// }

// static int music_scan_directory(const char *root)
// {
//     if (track_list) { free(track_list); track_list = NULL; }
//     track_count = 0;
//     track_list = calloc(MAX_TRACKS, sizeof(music_track_t));
//     if (!track_list) return 0;
//     int count = 0;
//     scan_dir_recursive(root, &count);
//     if (count == 0) {
//         free(track_list);
//         track_list = NULL;
//     } else {
//         track_list = realloc(track_list, count * sizeof(music_track_t));
//     }
//     track_count = count;
//     return count;
// }

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_service.h"
#include "music_app.h"
#include "system_event.h"
#include "keys_hal.h"
#include "sntp_service.h"
#include "audio_service.h"
#include "led_service.h"

static const char *TAG = "music_app";
static ui_app_t s_music_app;

// ---------- 图片资源声明 ----------
LV_IMG_DECLARE(icon_next_40);
LV_IMG_DECLARE(icon_play_40);
LV_IMG_DECLARE(icon_prev_40);
LV_IMG_DECLARE(icon_reflash_28);
LV_IMG_DECLARE(icon_stop_40);

// ---------- 内部 UI 句柄 ----------
static struct {
    lv_obj_t *time_label;
    lv_obj_t *track_label;
    lv_obj_t *play_btn;
    lv_obj_t *play_icon;
    lv_obj_t *remaining_label;
    bool playing;
} s_music_ui = {0};

// ---------- 任务句柄 ----------
static TaskHandle_t music_update_status_task_handle = NULL;

// ---------- 音乐文件定义 ----------
#define MUSIC_ROOT_PATH "/sdcard/music"   // 音乐根目录
#define MAX_TRACKS      500

typedef struct {
    char name[128];     // 文件名（不含路径）
    char path[512];     // 完整路径，支持深层子目录
} music_track_t;

static music_track_t *track_list = NULL;
static int track_count = 0;
static int current_track_index = 0;
static int volume = 50;
static int current_position_sec = 0;
static int total_duration_sec = 0;

// ---------- 内部函数声明 ----------
static void music_on_create(ui_app_t *app);
static void music_on_open(ui_app_t *app);
static void music_on_close(ui_app_t *app);
static void music_on_destroy(ui_app_t *app);
static void music_on_event(ui_app_t *app, event_data_t *event);
static bool music_handle_key_event(key_event_data_t *key);
static void music_app_led_control(led_mode_t mode, uint32_t arg);
static void music_handle_change_to_audio(audio_service_cmd_t cmd);
static void music_update_status_task(void *arg);
static void music_update_time(void);
static int  music_scan_directory(const char *root);
static void scan_dir_recursive(const char *dir_path, int *count);

// 按钮操作
static void music_play_pause(void);
static void music_prev_track(void);
static void music_next_track(void);
static void music_refresh(void);
static void music_increase_volume(void);
static void music_decrease_volume(void);
static void music_request_position_update(void);
static void music_update_remaining_time(void);

// 按钮事件回调
static void play_btn_event_cb(lv_event_t *e)   { music_play_pause(); }
static void prev_btn_event_cb(lv_event_t *e)   { music_prev_track(); }
static void next_btn_event_cb(lv_event_t *e)   { music_next_track(); }
static void refresh_btn_event_cb(lv_event_t *e){ music_refresh(); }

// ========== 注册函数 ==========
void music_app_register(void)
{
    memset(&s_music_app, 0, sizeof(s_music_app));
    s_music_app.name = "music_app";
    s_music_app.screen = NULL;
    s_music_app.on_create = music_on_create;
    s_music_app.on_open = music_on_open;
    s_music_app.on_close = music_on_close;
    s_music_app.on_destroy = music_on_destroy;
    s_music_app.on_event = music_on_event;
    ui_service_register_app(&s_music_app);
    ESP_LOGI(TAG, "Music app registered");
}

// ========== 生命周期回调 ==========
static void music_on_create(ui_app_t *app)
{
    ESP_LOGI(TAG, "music_on_create");
    app->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(app->screen, lv_color_black(), 0);

    // ---------- 顶部状态栏（仅时间）----------
    lv_obj_t *top_bar = lv_obj_create(app->screen);
    lv_obj_set_size(top_bar, 160, 32);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);

    s_music_ui.time_label = lv_label_create(top_bar);
    lv_label_set_text(s_music_ui.time_label, "12:00");
    lv_obj_set_style_text_color(s_music_ui.time_label, lv_color_white(), 0);
    lv_obj_align(s_music_ui.time_label, LV_ALIGN_RIGHT_MID, 0, 0);

    // ---------- 曲目显示 + 刷新按钮 ----------
    lv_obj_t *track_area = lv_obj_create(app->screen);
    lv_obj_set_size(track_area, 180, 36);
    lv_obj_align(track_area, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_bg_opa(track_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(track_area, 0, 0);
    lv_obj_set_style_pad_all(track_area, 0, 0);

    s_music_ui.track_label = lv_label_create(track_area);
    lv_label_set_text(s_music_ui.track_label,
                      (track_count > 0) ? track_list[current_track_index].name : "无音乐");
    lv_obj_set_style_text_color(s_music_ui.track_label, lv_color_white(), 0);
    LV_FONT_DECLARE(font_alipuhui20);
    lv_obj_set_style_text_font(s_music_ui.track_label, &font_alipuhui20, 0);
    lv_obj_set_width(s_music_ui.track_label, 135);
    lv_label_set_long_mode(s_music_ui.track_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_music_ui.track_label, LV_ALIGN_LEFT_MID, 0, 0);

    // 刷新按钮（重新扫描目录）
    lv_obj_t *refresh_btn = lv_btn_create(track_area);
    lv_obj_set_size(refresh_btn, 30, 30);
    lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(refresh_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_style_bg_opa(refresh_btn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(refresh_btn, refresh_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *refresh_img = lv_img_create(refresh_btn);
    lv_img_set_src(refresh_img, &icon_reflash_28);
    lv_obj_center(refresh_img);

    // ---------- 剩余时间标签 ----------
    s_music_ui.remaining_label = lv_label_create(app->screen);
    lv_label_set_text(s_music_ui.remaining_label, "-00:00");
    lv_obj_set_style_text_color(s_music_ui.remaining_label, lv_color_white(), 0);
    lv_obj_align(s_music_ui.remaining_label, LV_ALIGN_CENTER, 0, 20);

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
    s_music_ui.play_btn = lv_btn_create(control_bar);
    lv_obj_set_size(s_music_ui.play_btn, 43, 43);
    lv_obj_set_style_radius(s_music_ui.play_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_music_ui.play_btn, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_bg_opa(s_music_ui.play_btn, LV_OPA_COVER, 0);
    lv_obj_align(s_music_ui.play_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(s_music_ui.play_btn, play_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_music_ui.play_icon = lv_img_create(s_music_ui.play_btn);
    lv_img_set_src(s_music_ui.play_icon, &icon_stop_40);
    lv_obj_center(s_music_ui.play_icon);
    s_music_ui.playing = false;

    // 扫描音乐目录，构建播放列表（递归）
    if (music_scan_directory(MUSIC_ROOT_PATH) > 0) {
        lv_label_set_text(s_music_ui.track_label, track_list[0].name);
    }

    volume = 90;
    if (track_count > 0) {
        s_music_ui.playing = true;
        lv_img_set_src(s_music_ui.play_icon, &icon_play_40);
        music_handle_change_to_audio(AUDIO_CMD_CONNECT);
    }

    xTaskCreate(music_update_status_task, "music_update_status", 4096, NULL, 5,
                &music_update_status_task_handle);
    music_app_led_control(LED_MODE_MUSIC, 100);
    ESP_LOGI(TAG, "Music UI created");
}

static void music_on_open(ui_app_t *app)
{
    if (s_music_ui.track_label && track_count > 0) {
        lv_label_set_text(s_music_ui.track_label, track_list[current_track_index].name);
    }
    ESP_LOGI(TAG, "music_on_open, current track: %s",
             (track_count > 0) ? track_list[current_track_index].name : "none");
}

static void music_on_close(ui_app_t *app)
{
    ESP_LOGI(TAG, "music_on_close");
}

static void music_on_destroy(ui_app_t *app)
{
    ESP_LOGI(TAG, "music_on_destroy");
    if (music_update_status_task_handle) {
        vTaskDelete(music_update_status_task_handle);
        music_update_status_task_handle = NULL;
    }
    music_handle_change_to_audio(AUDIO_CMD_DISCONNECT);

    if (track_list) {
        free(track_list);
        track_list = NULL;
    }
    track_count = 0;
    memset(&s_music_ui, 0, sizeof(s_music_ui));
    app->screen = NULL;
}

static void music_on_event(ui_app_t *app, event_data_t *event)
{
    if (!event) return;

    if (event->event_type == NOTIFICATION) {
        if (event->service_id == KEYHAL_SERVICE) {
            key_event_data_t *key_data = (key_event_data_t *)event->data;
            if (key_data) music_handle_key_event(key_data);
        } else if (event->service_id == SNTP_SERVICE) {
            if (event->data) {
                sntp_service_send_data_t *sntp = (sntp_service_send_data_t *)event->data;
                if (s_music_ui.time_label) {
                    lv_label_set_text(s_music_ui.time_label, sntp->current_time);
                }
            }
        } else if (event->service_id == AUDIO_SERVICE) {
            audio_service_send_data_t *audio_evt = (audio_service_send_data_t *)event->data;
            if (audio_evt) {
                switch (audio_evt->cmd) {
                    case AUDIO_CMD_POSITION_UPDATE:
                        current_position_sec = audio_evt->current_position;
                        total_duration_sec = audio_evt->total_duration;
                        music_update_remaining_time();
                        break;
                    case AUDIO_CMD_END:
                        ESP_LOGI(TAG, "Track finished, playing next");
                        music_next_track();
                        break;
                    default:
                        break;
                }
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
static bool music_handle_key_event(key_event_data_t *key)
{
    if (key->event == KEY_EVENT_PRESS) {
        switch (key->key_id) {
            case KEY_ID_ENTER:
                music_play_pause();
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
                evt->reply_queue = get_ui_service_queue();
                evt->data = cmd;
                evt->data_len = sizeof(ui_service_receive_data_t);
                xQueueSend(get_ui_service_queue(), &evt, 0);
                ESP_LOGI(TAG, "send ui_event to ui_service_queue");
                music_app_led_control(LED_MODE_BREATH, 100);
                return true;
            }
            default: return true;
        }
    } else if (key->event == KEY_EVENT_ROTATE_CW) {
        music_increase_volume();
        return true;
    } else if (key->event == KEY_EVENT_ROTATE_CCW) {
        music_decrease_volume();
        return true;
    }
    return false;
}

// ========== 状态更新任务 ==========
static void music_update_status_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        music_update_time();
        static int counter = 0;
        if (++counter % 2 == 0) {
            music_request_position_update();
        }
    }
}

static void music_update_time(void)
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
static void music_play_pause(void)
{
    if (!s_music_ui.play_icon || track_count == 0) return;
    if (s_music_ui.playing) {
        ESP_LOGI(TAG, "pause");
        lv_img_set_src(s_music_ui.play_icon, &icon_stop_40);
        s_music_ui.playing = false;
        music_handle_change_to_audio(AUDIO_CMD_PAUSE);
    } else {
        ESP_LOGI(TAG, "play");
        lv_img_set_src(s_music_ui.play_icon, &icon_play_40);
        s_music_ui.playing = true;
        music_handle_change_to_audio(AUDIO_CMD_PLAY);
    }
    music_app_led_control(LED_MODE_ALERT, 0);
}

static void music_prev_track(void)
{
    if (track_count == 0) return;
    current_track_index = (current_track_index - 1 + track_count) % track_count;
    if (s_music_ui.track_label) {
        lv_label_set_text(s_music_ui.track_label, track_list[current_track_index].name);
    }
    ESP_LOGI(TAG, "prev track: %s", track_list[current_track_index].name);
    music_handle_change_to_audio(AUDIO_CMD_CONNECT);
}

static void music_next_track(void)
{
    if (track_count == 0) return;
    current_track_index = (current_track_index + 1) % track_count;
    if (s_music_ui.track_label) {
        lv_label_set_text(s_music_ui.track_label, track_list[current_track_index].name);
    }
    ESP_LOGI(TAG, "next track: %s", track_list[current_track_index].name);
    music_handle_change_to_audio(AUDIO_CMD_CONNECT);
}

static void music_refresh(void)
{
    ESP_LOGI(TAG, "refresh, re-scanning music directory");
    if (track_list) {
        free(track_list);
        track_list = NULL;
    }
    track_count = 0;
    current_track_index = 0;

    if (music_scan_directory(MUSIC_ROOT_PATH) > 0) {
        lv_label_set_text(s_music_ui.track_label, track_list[0].name);
        s_music_ui.playing = true;
        lv_img_set_src(s_music_ui.play_icon, &icon_play_40);
        music_handle_change_to_audio(AUDIO_CMD_CONNECT);
    } else {
        lv_label_set_text(s_music_ui.track_label, "无音乐");
    }
}

static void music_handle_change_to_audio(audio_service_cmd_t cmd)
{
    if (track_count == 0) return;

    const char *name = track_list[current_track_index].name;
    const char *ext = strrchr(name, '.');
    audio_service_stream_type_t decoder = mp3_dec;

    if (ext) {
        if (strcasecmp(ext, ".mp3") == 0) {
            decoder = mp3_dec;
        } else if (strcasecmp(ext, ".wav") == 0) {
            decoder = wav_dec;
        } else if (strcasecmp(ext, ".flac") == 0) {
            decoder = flac_dec;
        } else if (strcasecmp(ext, ".aac") == 0) {
            decoder = aac_dec;
        } else {
            decoder = mp3_dec;
        }
    }

    audio_service_receive_data_t* audio_payload = malloc(sizeof(audio_service_receive_data_t));
    if (!audio_payload) {
        ESP_LOGE(TAG, "malloc audio_service_receive_data_t err");
        return;
    }

    audio_payload->cmd = cmd;
    audio_payload->content_id = current_track_index;
    strcpy(audio_payload->url, track_list[current_track_index].path);
    audio_payload->prv_type = file_str;
    audio_payload->midle_type = decoder;
    audio_payload->back_type = i2s_hal;
    audio_payload->volume = volume;
    audio_payload->start_after_connect = s_music_ui.playing;
    audio_payload->seek_position = 0;

    event_data_t *evt_data = malloc(sizeof(event_data_t));
    if (!evt_data) {
        ESP_LOGE(TAG, "malloc event_data_t err");
        free(audio_payload);
        return;
    }

    evt_data->service_id = UI_SERVICE;
    evt_data->event_type = REQUEST;
    evt_data->reply_queue = get_ui_service_queue();
    evt_data->data = audio_payload;
    evt_data->data_len = sizeof(audio_service_receive_data_t);
    xQueueSend(get_audio_service_queue(), &evt_data, 0);
}
static void music_increase_volume(void) { 
    ESP_LOGI(TAG, "increase volume"); 
    volume += 1;
    if(volume > 100) volume = 100;
    music_handle_change_to_audio(AUDIO_CMD_VOLUME);
    music_app_led_control(LED_MODE_VOLUME, volume);
}

static void music_decrease_volume(void) { 
    ESP_LOGI(TAG, "decrease volume"); 
    volume -= 1;
    if(volume < 0) volume = 0;
    music_handle_change_to_audio(AUDIO_CMD_VOLUME);
    music_app_led_control(LED_MODE_VOLUME, volume);
}

static void music_request_position_update(void)
{
    audio_service_receive_data_t *pos_payload = calloc(1, sizeof(audio_service_receive_data_t));
    if (!pos_payload) return;
    pos_payload->cmd = AUDIO_CMD_GET_INFO;
    event_data_t *evt = malloc(sizeof(event_data_t));
    if (!evt) { free(pos_payload); return; }
    evt->service_id = UI_SERVICE;
    evt->event_type = REQUEST;
    evt->reply_queue = get_ui_service_queue();
    evt->data = pos_payload;
    evt->data_len = sizeof(audio_service_receive_data_t);
    if (xQueueSend(get_audio_service_queue(), &evt, 0) != pdPASS) {
        free(pos_payload);
        free(evt);
    }
}

static void music_update_remaining_time(void)
{
    if (!s_music_ui.remaining_label) return;
    int remaining = total_duration_sec - current_position_sec;
    if (remaining < 0) remaining = 0;
    char buf[16];
    snprintf(buf, sizeof(buf), "-%02d:%02d", remaining / 60, remaining % 60);
    lv_label_set_text(s_music_ui.remaining_label, buf);
}

// ========== LED 控制 ==========
static void music_app_led_control(led_mode_t mode, uint32_t arg) {
    led_service_receive_data_t* led_payload = (led_service_receive_data_t*)malloc(sizeof(led_service_receive_data_t));
    if(led_payload){
        led_payload->device = LED_HAL_DEVICE_FRONT;
        led_payload->mode = mode;
        led_payload->brightness = 30;
        led_payload->arg = arg;
        
        event_data_t* led_event = (event_data_t*)malloc(sizeof(event_data_t));
        if(led_event){
            led_event->service_id = HAL;
            led_event->event_type = REQUEST;
            led_event->data = led_payload;
            led_event->data_len = sizeof(led_service_receive_data_t);
            xQueueSend(get_led_service_queue(), &led_event, 0);
        } else{
            free(led_payload);
        }
    }
}

// ========== 递归扫描辅助函数 ==========
static void scan_dir_recursive(const char *dir_path, int *count) 
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open directory: %s", dir_path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && *count < MAX_TRACKS) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // 构造完整路径
        char full_path[512];
        int written = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (written >= sizeof(full_path)) {
            ESP_LOGW(TAG, "Path too long, skipping: %s/%s", dir_path, entry->d_name);
            continue;
        }

        if (entry->d_type == DT_DIR) {
            // 递归进入子目录
            scan_dir_recursive(full_path, count);
        } else {
            // 检查音乐文件扩展名
            const char *ext = strrchr(entry->d_name, '.');
            if (!ext) continue;
            if (strcasecmp(ext, ".mp3") == 0 ||
                strcasecmp(ext, ".wav") == 0 ||
                strcasecmp(ext, ".flac") == 0 ||
                strcasecmp(ext, ".aac") == 0) {

                // 存储文件名
                strncpy(track_list[*count].name, entry->d_name, sizeof(track_list[*count].name) - 1);
                track_list[*count].name[sizeof(track_list[*count].name) - 1] = '\0';

                // 存储完整路径
                strncpy(track_list[*count].path, full_path, sizeof(track_list[*count].path) - 1);
                track_list[*count].path[sizeof(track_list[*count].path) - 1] = '\0';

                (*count)++;
            }
        }
    }
    closedir(dir);
}

// ========== 目录扫描入口 ==========
static int music_scan_directory(const char *root)
{
    // 释放旧列表
    if (track_list) {
        free(track_list);
        track_list = NULL;
    }
    track_count = 0;

    // 分配初始数组
    track_list = (music_track_t *)calloc(MAX_TRACKS, sizeof(music_track_t));
    if (!track_list) {
        ESP_LOGE(TAG, "Failed to allocate track list");
        return 0;
    }

    int count = 0;
    scan_dir_recursive(root, &count);

    if (count == 0) {
        free(track_list);
        track_list = NULL;
    } else {
        // 缩小到实际使用量
        track_list = (music_track_t *)realloc(track_list, count * sizeof(music_track_t));
    }
    track_count = count;
    ESP_LOGI(TAG, "Scanned %d tracks from %s", count, root);
    return count;
}