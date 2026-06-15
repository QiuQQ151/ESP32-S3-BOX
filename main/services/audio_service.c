#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "audio_pipeline.h"
#include "audio_element.h"
#include "audio_common.h"
#include "audio_hal.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "aac_decoder.h"
#include "flac_decoder.h"
#include "wav_decoder.h"
#include "fatfs_stream.h"
#include "raw_stream.h"
#include "usb_device_uac.h"
#include "board.h"
#include "hal/tca9535_hal.h"
#include "hal/sd_hal.h"

#include "system_event.h"
#include "audio_service.h"

static const char *TAG = "audio_service";

// ─── 硬件句柄 ─────────────────────────────────
static audio_board_handle_t board_handle = NULL;

// ─── 输出 pipeline（播放） ─────────────────
static audio_pipeline_handle_t pipeline_out = NULL;
static audio_element_handle_t   el_out_prev = NULL;
static audio_element_handle_t   el_out_mid  = NULL;
static audio_element_handle_t   el_out_back = NULL;

// ─── 输入 pipeline（录音） ─────────────────
static audio_pipeline_handle_t pipeline_in = NULL;
static audio_element_handle_t   el_in_prev = NULL;
static audio_element_handle_t   el_in_mid  = NULL;
static audio_element_handle_t   el_in_back = NULL;

// ─── 服务队列 & 状态 ─────────────────────────
static QueueHandle_t request_queue = NULL;
static QueueHandle_t s_notify_queue = NULL;

static audio_service_state_t audio_out_state = AUDIO_SERVICE_IDLE;
static audio_service_state_t audio_in_state = AUDIO_SERVICE_IDLE;
static char   el_url[200] = {0};  // 文件url
static audio_element_info_t info = {0}; // 当前播放/录音的音频格式信息
static int    audio_volume = 50;
static bool   g_uac_active = false;
static bool   audio_out_active = false;
static bool   audio_in_active = false;

// ─── 内部函数声明 ─────
static void audio_service_task(void *arg);
#define dre_input 1
#define dre_output 0
static void destroy_pipeline(int direction);
static void handle_connect(audio_service_receive_data_t *req, QueueHandle_t reply_queue);
static void handle_play(QueueHandle_t reply_queue);
static void handle_pause(QueueHandle_t reply_queue);
static void check_playback_end(void);
static void send_reply(audio_service_cmd_t cmd, QueueHandle_t target);
static void send_notification(audio_service_cmd_t cmd);

// USB UAC 回调
static esp_err_t uac_output_cb(uint8_t *data, size_t len, void *arg);
static esp_err_t uac_input_cb(uint8_t *data, size_t len, size_t *bytes_read, void *arg);
static void uac_volume_cb(uint32_t vol, void *arg);
static void uac_mute_cb(uint32_t mute, void *arg);

// 统一管线构建
static esp_err_t build_pipeline(audio_service_stream_type_t prv_type,
                                audio_service_stream_type_t mid_type,
                                audio_service_stream_type_t back_type);

// ─── 公开 API ─────────────────────────────────
esp_err_t audio_service_init(void)
{
    ESP_LOGI(TAG, "Initializing audio service");

    board_handle = audio_board_init();
    if (!board_handle) {
        ESP_LOGE(TAG, "audio_board_init failed");
        return ESP_FAIL;
    }
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    tca9535_hal_init(I2C_NUM_0);
    power_hal_pa_enable(1);
    sd_hal_init();

    request_queue = xQueueCreate(10, sizeof(event_data_t *));
    if (!request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return ESP_ERR_NO_MEM;
    }

    xTaskCreate(audio_service_task, "audio_srv", 16 * 1024, NULL, 13, NULL); // 6

    return ESP_OK;
}

QueueHandle_t get_audio_service_queue(void)
{
    return request_queue;
}

esp_err_t set_audio_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    audio_hal_set_volume(board_handle->audio_hal, volume);
    audio_volume = volume;
    return ESP_OK;
}

int get_audio_volume(void)
{
    return audio_volume;
}

// ─── 服务主循环 ────────────────────────────────
static void audio_service_task(void *arg)
{
    event_data_t *evt = NULL;

    while (1) {
        if (xQueueReceive(request_queue, &evt, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (evt) {
                if (evt->event_type == REQUEST && evt->data) {
                    audio_service_receive_data_t *req = (audio_service_receive_data_t *)evt->data;
                    QueueHandle_t reply = evt->reply_queue;

                    if (reply) {
                        s_notify_queue = reply;
                    }

                    switch (req->cmd) {
                    case AUDIO_CMD_CONNECT:
                        ESP_LOGI(TAG, "CMD_CONNECT: %s", req->url);
                        handle_connect(req, reply);
                        break;
                    case AUDIO_CMD_DISCONNECT:
                        ESP_LOGI(TAG, "CMD_DISCONNECT");
                        destroy_pipeline(dre_output);
                        destroy_pipeline(dre_input);
                        audio_out_state = AUDIO_SERVICE_IDLE;
                        send_reply(AUDIO_CMD_DISCONNECT, reply);
                        break;
                    case AUDIO_CMD_PLAY:
                        ESP_LOGI(TAG, "CMD_PLAY");
                        handle_play(reply);
                        break;
                    case AUDIO_CMD_PAUSE:
                        ESP_LOGI(TAG, "CMD_PAUSE");
                        handle_pause(reply);
                        break;
                    case AUDIO_CMD_VOLUME:
                        ESP_LOGI(TAG, "CMD_VOLUME: %d", req->volume);
                        set_audio_volume(req->volume);
                        send_reply(AUDIO_CMD_VOLUME, reply);
                        break;
                    default:
                        ESP_LOGW(TAG, "Unknown cmd: %d", req->cmd);
                        break;
                    }
                }
                if (evt->data) free(evt->data);
                free(evt);
            }
        }

        check_playback_end();
    }
}

// ─── 销毁所有管线 ─────────────────────────────
static void destroy_pipeline(int direction)
{
    g_uac_active = false; // 必须在销毁管道前关闭 UAC 活动标志，以避免回调访问已销毁的元素引发错误
    if(direction == dre_output){
        if (pipeline_out) {
            audio_pipeline_stop(pipeline_out);
            audio_pipeline_wait_for_stop(pipeline_out);
            audio_pipeline_terminate(pipeline_out);
            audio_pipeline_deinit(pipeline_out);
            pipeline_out = NULL;
            ESP_LOGI(TAG, "Output pipeline destroyed");            
        }
        el_out_prev = NULL;
        el_out_mid  = NULL;
        el_out_back = NULL;
        audio_out_active = false;
    }

    if(direction == dre_input){
        if ( pipeline_in ) {
            audio_pipeline_stop(pipeline_in);
            audio_pipeline_wait_for_stop(pipeline_in);
            audio_pipeline_terminate(pipeline_in);
            audio_pipeline_deinit(pipeline_in);
            pipeline_in = NULL;
            ESP_LOGI(TAG, "Input pipeline destroyed");            
        }
        el_in_prev = NULL;
        el_in_mid  = NULL;
        el_in_back = NULL;
        audio_in_active = false;
   }
   
}

// ─── 连接处理 ──────────────────────
static void handle_connect(audio_service_receive_data_t *req, QueueHandle_t reply_queue)
{
    strncpy(el_url, req->url, sizeof(el_url) - 1);
    set_audio_volume(req->volume);

    esp_err_t ret = build_pipeline(req->prv_type, req->midle_type, req->back_type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build pipeline");
        goto connect_fail;
    }

    // 根据实际创建的管线决定后续操作
    if (pipeline_out && audio_out_active == false ) {
        // 先确定 UAC 标志（无论是否自动启动）
        g_uac_active = (req->prv_type == usb_uac);
        if (req->start_after_connect) {
            audio_pipeline_run(pipeline_out);
            audio_out_state = AUDIO_SERVICE_PLAYING;
            audio_out_active = true;
        } else {
            audio_out_state = AUDIO_SERVICE_CONNECTED;
            audio_out_active = false;
        }
    }
    if (pipeline_in && audio_in_active == false) { 
        // 录音管线
        if (req->back_type == usb_uac) {
            g_uac_active = true;            // 输入管线也可能涉及 UAC
        }
        if (req->start_after_connect) {
            audio_pipeline_run(pipeline_in);
            audio_in_state = AUDIO_SERVICE_PLAYING;   // 录音状态也用 PLAYING 表示
            audio_in_active = true;
        } else {
            audio_in_state = AUDIO_SERVICE_CONNECTED;
            audio_in_active = false;
        }
    }

    send_reply(AUDIO_CMD_CONNECT, reply_queue);
    return;

connect_fail:
    destroy_pipeline(dre_output);
    destroy_pipeline(dre_input);
    audio_out_state = AUDIO_SERVICE_ERROR;
    audio_in_state = AUDIO_SERVICE_ERROR;
    send_notification(AUDIO_CMD_ERROR);
    if (reply_queue) send_reply(AUDIO_CMD_ERROR, reply_queue);
}


/**
 * @brief 根据类型和角色创建管线元素
 * @param type      元素类型
 * @param is_output 是否为输出管线
 * @return 成功返回句柄，失败返回 NULL
 */
static audio_element_handle_t create_element_by_role(audio_service_stream_type_t type, bool is_output)
{
   if( type !=  audio_type_none){
       switch (type) {
           // 编解码器
            case mp3_dec: {
                mp3_decoder_cfg_t cfg = DEFAULT_MP3_DECODER_CONFIG();
                return mp3_decoder_init(&cfg);
            }
            case aac_dec: {
                aac_decoder_cfg_t cfg = { .out_rb_size = 32*1024, .task_stack = 8*1024, .task_prio = 13, .stack_in_ext = true };
                return aac_decoder_init(&cfg);
            }
            case flac_dec: {
                flac_decoder_cfg_t cfg = { .out_rb_size = 4*12*1024, .task_stack = 24*1024, .task_prio = 13, .stack_in_ext = true };
                return flac_decoder_init(&cfg);
            }
            case wav_dec: {
                wav_decoder_cfg_t cfg = { .out_rb_size = 32*1024, .task_stack = 6*1024, .task_prio = 13, .stack_in_ext = true };
                return wav_decoder_init(&cfg);
            }
            // 流元素
            case http_str:
            case https_str: {
                http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
                http_cfg.type = (is_output == true) ? AUDIO_STREAM_READER : AUDIO_STREAM_WRITER;
                http_cfg.enable_playlist_parser = false;
                http_cfg.out_rb_size = 8 * 1024;
                http_cfg.task_stack = 4 * 1024;
                http_cfg.request_size = 4096;
                audio_element_handle_t el = http_stream_init(&http_cfg);
                if (el) audio_element_set_uri(el, el_url);
                return el;
            }
            case file_str: {
                fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
                fatfs_cfg.type = (is_output == true) ? AUDIO_STREAM_READER : AUDIO_STREAM_WRITER;
                fatfs_cfg.buf_sz = 50 * 1024;
                fatfs_cfg.ext_stack = true;
                audio_element_handle_t el = fatfs_stream_init(&fatfs_cfg);
                if (el) audio_element_set_uri(el, el_url);
                return el;
            }
            // 缓冲区
            case raw_hal: {
                raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
                raw_cfg.type =  AUDIO_STREAM_WRITER; 
                raw_cfg.out_rb_size = 8 * 1024;
                audio_element_handle_t el = raw_stream_init(&raw_cfg);
                if (el) audio_element_setinfo(el, &info);
                return el;
            }
            // 底层i2s，不要使用外部ram
            case i2s_hal: {
                static bool is_i2s_init = false;
                audio_element_handle_t el = NULL;
                i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(I2S_NUM_0, 48000, I2S_DATA_BIT_WIDTH_16BIT, AUDIO_STREAM_READER);
                i2s_cfg.type = (is_output == true) ? AUDIO_STREAM_WRITER : AUDIO_STREAM_READER; // 播放到喇叭往i2s写，录音从i2s读
                // 如果是第一次初始化 i2s，正常安装驱动；如果之前已经初始化过（可能是输入管线先占用了 i2s），则复用已有驱动
                if (is_i2s_init) {  
                    i2s_cfg.uninstall_drv = true;
                }
                el = i2s_stream_init(&i2s_cfg);
                is_i2s_init = true;
                if (el) audio_element_setinfo(el, &info);
                return el;
            }
            default:
                return NULL;
       }
   }
   return NULL;
}

static esp_err_t build_pipeline(audio_service_stream_type_t prv_type,
                                audio_service_stream_type_t mid_type,
                                audio_service_stream_type_t back_type)
{
    // 1. 判断方向并清理旧管线

    bool is_output = false;
    audio_pipeline_handle_t pipeline;
    if (back_type == i2s_hal) {
        is_output = true;
        destroy_pipeline(dre_output);
    } else{
        destroy_pipeline(dre_input);
    }

    // 2. 初始化 UAC（如需要）
    if (prv_type == usb_uac || back_type == usb_uac) {
        static bool uac_dev_inited = false;
        if (!uac_dev_inited) {
            uac_device_config_t uac_cfg = {
                .output_cb     = uac_output_cb,
                .input_cb      = uac_input_cb,
                .set_volume_cb = uac_volume_cb,
                .set_mute_cb   = uac_mute_cb,
            };
            if (uac_device_init(&uac_cfg) != ESP_OK) {
                ESP_LOGE(TAG, "UAC device init failed");
                return ESP_FAIL;
            }
            uac_dev_inited = true;
        }
    }

    // 3. 音频信息
    info.bits = 16;
    info.channels = 2;  //2
    info.sample_rates = 48000;

    // 4. 创建三个元素(usb_uac会返回NULL)
    audio_element_handle_t el_prev = create_element_by_role(prv_type, is_output);
    audio_element_handle_t el_mid = create_element_by_role(mid_type, is_output);;
    audio_element_handle_t el_back = create_element_by_role(back_type, is_output);

    // 6. 创建 pipeline 容器
    audio_pipeline_cfg_t pipe_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipe_cfg);
    if( pipeline == NULL){
        ESP_LOGE(TAG, "Failed to create pipeline");
        goto fail;
    } else {
    ESP_LOGI(TAG, "Pipeline created: %p", pipeline);
    }

    // 7. 注册元素并连接（根据实际创建的元素数量灵活连接）
    if( el_prev ) audio_pipeline_register(pipeline, el_prev, "tag_prev");
    if(el_mid) audio_pipeline_register(pipeline, el_mid, "tag_mid");
    if(el_back) audio_pipeline_register(pipeline, el_back, "tag_back");

    if( el_prev == NULL){
        const char *link[2] = {"tag_mid", "tag_back"};
        audio_pipeline_link(pipeline, link, 2); 
        ESP_LOGI(TAG, "Linked mid -> back");       
    } else if( el_back == NULL){
        const char *link[2] = {"tag_prev", "tag_mid"};
        audio_pipeline_link(pipeline, link, 2);   
        ESP_LOGI(TAG, "Linked prev -> mid");     
    } else {
        const char *link[3] = {"tag_prev", "tag_mid", "tag_back"};
        audio_pipeline_link(pipeline, link, 3);
        ESP_LOGI(TAG, "Linked prev -> mid -> back");
    }

    // 8. 保存 pipeline 句柄
    if (is_output) {
        pipeline_out = pipeline;
        el_out_prev = el_prev;
        el_out_mid  = el_mid;
        el_out_back = el_back;
    } else {
        pipeline_in = pipeline;
        el_in_prev = el_prev;
        el_in_mid  = el_mid;
        el_in_back = el_back;
    }

    ESP_LOGI(TAG, "%s pipeline assembled", is_output ? "Output" : "Input");
    return ESP_OK;

fail:
    if (el_prev) audio_element_deinit(el_prev);
    if (el_mid)  audio_element_deinit(el_mid);
    if (el_back) audio_element_deinit(el_back);
    if (is_output) {
        el_out_prev = NULL; el_out_mid = NULL; el_out_back = NULL;
    } else {
        el_in_prev = NULL; el_in_mid = NULL; el_in_back = NULL;
    }
    return ESP_FAIL;
}


// ─── 播放 / 暂停 ──
static void handle_play(QueueHandle_t reply_queue)
{
    if (audio_out_state == AUDIO_SERVICE_PLAYING) {
        ESP_LOGI(TAG, "Already playing");
        send_reply(AUDIO_CMD_PLAY, reply_queue);
        return;
    }
    if (audio_out_state == AUDIO_SERVICE_PAUSED) {
        if (pipeline_out) {
            if (g_uac_active) {
                audio_pipeline_resume(pipeline_out);
            } else {
                if (el_out_back) audio_element_resume(el_out_back, 0, portMAX_DELAY);  
                if (el_out_mid)  audio_element_resume(el_out_mid, 0, portMAX_DELAY);
                if (el_out_prev) audio_element_resume(el_out_prev, 0, portMAX_DELAY);        
                audio_pipeline_change_state(pipeline_out, AEL_STATE_RUNNING);
            }
            audio_out_state = AUDIO_SERVICE_PLAYING;
            // audio_out_active 在暂停时本就为 true，此处不变
            ESP_LOGI(TAG, "Resumed");
            send_reply(AUDIO_CMD_PLAY, reply_queue);
            return;
        }
    }
    if (audio_out_state == AUDIO_SERVICE_CONNECTED && pipeline_out) {
        audio_pipeline_run(pipeline_out);
        audio_out_state = AUDIO_SERVICE_PLAYING;
        audio_out_active = true;         
        ESP_LOGI(TAG, "Started playing");
        send_reply(AUDIO_CMD_PLAY, reply_queue);
        return;
    }
    ESP_LOGW(TAG, "Cannot play, state=%d", audio_out_state);
    audio_out_state = AUDIO_SERVICE_ERROR;
    send_notification(AUDIO_CMD_ERROR);
}

static void handle_pause(QueueHandle_t reply_queue)
{
    if (audio_out_state != AUDIO_SERVICE_PLAYING) {
        ESP_LOGW(TAG, "Cannot pause, state=%d", audio_out_state);
        return;
    }
    if (pipeline_out) {
        if (g_uac_active) {
            audio_pipeline_pause(pipeline_out);
        } else {
            if (el_out_prev) audio_element_pause(el_out_prev);
            if (el_out_mid)  audio_element_pause(el_out_mid);
            if (el_out_back) audio_element_pause(el_out_back);
            audio_pipeline_change_state(pipeline_out, AEL_STATE_PAUSED);
        }
        audio_out_state = AUDIO_SERVICE_PAUSED;
        // audio_out_active 暂停时仍为 true
        ESP_LOGI(TAG, "Paused");
        send_reply(AUDIO_CMD_PAUSE, reply_queue);
    }
}

// ─── 播放结束检测 ────────────────────────────
static void check_playback_end(void)
{
    if (g_uac_active) return;
    if (audio_out_state != AUDIO_SERVICE_PLAYING && audio_out_state != AUDIO_SERVICE_PAUSED) return;
    if (!el_out_mid) return;   // 无解码器不检测

    audio_element_state_t st = audio_element_get_state(el_out_mid);
    if (st == AEL_STATE_FINISHED || st == AEL_STATE_STOPPED) {
        ESP_LOGI(TAG, "Playback finished");
        destroy_pipeline(dre_output);
        audio_out_state = AUDIO_SERVICE_IDLE;
        // audio_out_active 由 destroy_pipeline 设为 false
        send_notification(AUDIO_CMD_END);
    }
}

// ─── 回复与通知辅助函数 ──────────────────────
static void send_reply(audio_service_cmd_t cmd, QueueHandle_t target)
{
    if (!target) return;

    audio_service_send_data_t *data = calloc(1, sizeof(audio_service_send_data_t));
    if (!data) return;
    data->cmd = cmd;
    data->service_state = audio_out_state;
    strncpy(data->url, el_url, sizeof(data->url) - 1);
    data->volume = audio_volume;

    event_data_t *evt = malloc(sizeof(event_data_t));
    if (!evt) { free(data); return; }
    evt->service_id = AUDIO_SERVICE;
    evt->event_type = NOTIFICATION;
    evt->reply_queue = NULL;
    evt->data = data;
    evt->data_len = sizeof(audio_service_send_data_t);

    if (xQueueSend(target, &evt, 0) != pdTRUE) {
        free(evt->data);
        free(evt);
    }
}

static void send_notification(audio_service_cmd_t cmd)
{
    if (!s_notify_queue) return;
    send_reply(cmd, s_notify_queue);
}

// ─── USB UAC 回调────────────────────
static esp_err_t uac_output_cb(uint8_t *data, size_t len, void *arg)
{
    if (el_out_mid && g_uac_active ) {
        raw_stream_write(el_out_mid, (char *)data, len);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t uac_input_cb(uint8_t *data, size_t len, size_t *bytes_read, void *arg)
{
    if (el_in_mid && g_uac_active) {
        int ret = raw_stream_read(el_in_mid, (char *)data, len);
        if (ret > 0) {
            *bytes_read = ret;
            ESP_LOGI(TAG, "Read %d bytes from raw_recorder", ret);
            return ESP_OK;
        }
    }
    *bytes_read = 0;
    ESP_LOGW(TAG, "No data read from raw_recorder");
    return ESP_FAIL;
}

static int vol_to_x(uint32_t vol) {
    if (vol == 0 || vol >= 100) {
        // ln(0) 负无穷，这里按 0 处理（可根据实际需求修改）
        return vol;
    } 
    double ratio = (double)vol / 124.0;
    double x = (log(ratio) + 1.0) * 100.0;
    return (int)x;   // 直接截断取整，如需四舍五入可改为 (int)(x + 0.5) 等
}

static void uac_volume_cb(uint32_t vol, void *arg)
{
    int vol_x = vol_to_x(vol);
    set_audio_volume(vol);
    ESP_LOGI(TAG, "Volume set to %ld via UAC callback", vol);
}

static void uac_mute_cb(uint32_t mute, void *arg)
{
    static int last_volume = 0; // 用于恢复音量的临时变量
    static bool is_muted = false; // 当前静音状态 // uac设定立体声，实际硬件为单声道，会收到两次回调，只需处理一次
    if( mute > 0 && is_muted == false ){
        // 激活静音
        is_muted = true;
        last_volume = audio_volume; // 保存当前音量以便恢复
        ESP_LOGI(TAG, "Muting audio via UAC callback, saving volume %d", last_volume);
        set_audio_volume(0);
    } else if( mute == 0 && is_muted == true ){
        is_muted = false;
        ESP_LOGI(TAG, "Unmuting audio via UAC callback, restoring volume %d", last_volume);
        set_audio_volume(last_volume);
    }
    ESP_LOGI(TAG, "Mute set to %ld via UAC callback", mute);
}