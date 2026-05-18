// 1. 标准C库头文件
#include <dirent.h>   // opendir, readdir, closedir, struct dirent
#include <stdio.h>    // sprintf
#include <stdlib.h>   // malloc, free
#include <string.h>   // strcasecmp, strlen, strcpy等

// 2. FreeRTOS头文件
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// 3. ESP-IDF系统头文件
#include "esp_log.h"
#include "nvs_flash.h"

// 4. 音频框架头文件
#include "audio_pipeline.h"
#include "audio_element.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "audio_hal.h"
#include "esp_codec_dev.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "aac_decoder.h"
#include "mp3_decoder.h"
#include "wav_decoder.h"
#include "flac_decoder.h"
#include "fatfs_stream.h"
#include "esp_peripherals.h"
#include "board.h"

// 5. 项目硬件抽象层头文件
#include "hal/power_hal.h" // 控制功放使能
#include "hal/sd_hal.h" // 控制SD卡
#include "hal/tca9535_hal.h" // 控制tca9535 IO扩展芯片

#include "services/system_event.h"
#include "services/audio_service.h"

static const char *TAG = "audio_service";

// 硬件句柄
static audio_board_handle_t board_handle = NULL;

// 主音频管道与元素
static audio_pipeline_handle_t req_pipeline = NULL;
static audio_element_handle_t prev_ele_handle = NULL;
static audio_element_handle_t midl_ele_handle = NULL;
static audio_element_handle_t back_ele_handle = NULL;

// 通知管道（暂未使用）
static audio_pipeline_handle_t ntf_pipeline = NULL;

// 服务队列
static QueueHandle_t audio_service_request_queue = NULL;

// 当前服务状态（初始：无服务对象，空闲）
static audio_service_send_data_t service_state = {
    .service_obj = AUDIO_SERVICE_NONE,
    .service_stata = AUDIO_SERVICE_IDLE,
};

// 内部函数声明
static void audio_service_task(void *arg);
static void handle_request(event_data_t *evt_data);
static void handle_notification(event_data_t *evt_data);
static esp_err_t do_connect(audio_service_receive_data_t *payload);
static esp_err_t do_disconnect(void);
static esp_err_t do_stop(void);
static esp_err_t do_play(void);
static esp_err_t do_volume(int volume);
static void send_reply(QueueHandle_t reply_queue);
static void cleanup_pipeline(void);                     // 只销毁管道，不重置状态
static esp_err_t re_connect_from_saved_state(void);    // 利用 service_state 重新连接

// ========================= API =========================

esp_err_t audio_service_init(void)
{
    ESP_LOGI(TAG, "Initializing audio service");

    // 初始化音频板（ES8311、I2C、I2S）
    board_handle = audio_board_init();
    if (!board_handle) {
        ESP_LOGE(TAG, "audio_board_init failed");
        return ESP_FAIL;
    }
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    // 使能功放
    power_hal_init();
    power_hal_pa_enable(1);

    // 初始化SD卡
    sd_hal_init();
    ESP_LOGI(TAG, "SD card initialized");

    // 创建主音频管道
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    req_pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!req_pipeline) {
        ESP_LOGE(TAG, "Failed to create req_pipeline");
        return ESP_ERR_NO_MEM;
    }

    // 创建通知音频管道
    ntf_pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!ntf_pipeline) {
        ESP_LOGE(TAG, "Failed to create ntf_pipeline");
        audio_pipeline_deinit(req_pipeline);
        req_pipeline = NULL;
        return ESP_ERR_NO_MEM;
    }

    // 创建服务队列
    audio_service_request_queue = xQueueCreate(10, sizeof(event_data_t *));
    if (!audio_service_request_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        audio_pipeline_deinit(req_pipeline);
        audio_pipeline_deinit(ntf_pipeline);
        req_pipeline = NULL;
        ntf_pipeline = NULL;
        return ESP_ERR_NO_MEM;
    }

    // 启动服务任务
    xTaskCreate(audio_service_task, "audio_srv_task", 20 * 1024, NULL, 5, NULL);
    return ESP_OK;
}

QueueHandle_t get_audio_service_queue(void)
{
    return audio_service_request_queue;
}

// ========================= 任务主循环 =========================

static void audio_service_task(void *arg)
{
    event_data_t *evt_data;
    while (1) {
        if (xQueueReceive(audio_service_request_queue, &evt_data, portMAX_DELAY) == pdTRUE) {
            if (evt_data->event_type == REQUEST) {
                handle_request(evt_data);
            } else if (evt_data->event_type == NOTIFICATION) {
                handle_notification(evt_data);
            } else {
                ESP_LOGE(TAG, "Unknown event type: %d", evt_data->event_type);
            }
            // 释放事件数据
            if (evt_data->data) {
                free(evt_data->data);
            }
            free(evt_data);
        }
    }
}

// ========================= 请求分发 =========================

static void handle_request(event_data_t *evt_data)
{
    audio_service_receive_data_t *payload = (audio_service_receive_data_t *)evt_data->data;
    if (!payload) return;

    QueueHandle_t reply_queue = evt_data->reply_queue;
    esp_err_t ret = ESP_OK;

    switch (payload->cmd) {
    case AUDIO_CMD_CONNECT:
        ESP_LOGI(TAG, "CMD_CONNECT: %s", payload->url);
        ret = do_connect(payload);
        break;
    case AUDIO_CMD_DISCONNECT:
        ESP_LOGI(TAG, "CMD_DISCONNECT");
        ret = do_disconnect();
        break;
    case AUDIO_CMD_STOP:
        ESP_LOGI(TAG, "CMD_STOP");
        ret = do_stop();
        break;
    case AUDIO_CMD_PLAY:
        ESP_LOGI(TAG, "CMD_PLAY");
        ret = do_play();
        break;
    case AUDIO_CMD_VOLUME:
        ESP_LOGI(TAG, "CMD_VOLUME: %d", payload->volume);
        ret = do_volume(payload->volume);
        break;
    default:
        ESP_LOGE(TAG, "Unknown command: %d", payload->cmd);
        ret = ESP_FAIL;
        break;
    }

    if (ret != ESP_OK) {
        service_state.service_stata = AUDIO_SERVICE_ERROR;
    }
    // 每次请求处理后均回复当前状态
    if (reply_queue) {
        send_reply(reply_queue);
    }
}

// ========================= 核心操作实现 =========================
static esp_err_t do_connect(audio_service_receive_data_t *payload)
{
    // 1. 彻底清理旧资源（管道 + 所有元素）
    cleanup_pipeline();

    // 2. 确保 req_pipeline 可用（cleanup_pipeline 已将其置 NULL，需重建）
    if (req_pipeline == NULL) {
        audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
        req_pipeline = audio_pipeline_init(&pipeline_cfg);
        if (req_pipeline == NULL) {
            ESP_LOGE(TAG, "Failed to create new req_pipeline");
            return ESP_ERR_NO_MEM;
        }
    }

    // ---------- 创建前端元素 ----------
    switch (payload->prv_type) {
    case http_str:
    case https_str: {
        http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
        http_cfg.type = AUDIO_STREAM_READER;
        http_cfg.enable_playlist_parser = false;
        http_cfg.out_rb_size = 10 * 1024;
        http_cfg.task_stack = 10 * 1024;
        http_cfg.request_size = 15 * 1024;
        prev_ele_handle = http_stream_init(&http_cfg);
        if (!prev_ele_handle) {
            ESP_LOGE(TAG, "Failed to init http stream");
            return ESP_FAIL;
        }
        audio_element_set_uri(prev_ele_handle, payload->url);
        break;
    }
    case file_str: {
        fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
        fatfs_cfg.type = AUDIO_STREAM_READER;
        fatfs_cfg.buf_sz = 100 * 1024;
        prev_ele_handle = fatfs_stream_init(&fatfs_cfg);
        if (!prev_ele_handle) {
            ESP_LOGE(TAG, "Failed to init fatfs stream");
            return ESP_FAIL;
        }
        audio_element_set_uri(prev_ele_handle, payload->url);
        break;
    }
    default:
        ESP_LOGE(TAG, "Unsupported frontend type");
        return ESP_FAIL;
    }

    // ---------- 创建解码器元素 ----------
    switch (payload->midle_type) {
        case mp3_dec: {
            mp3_decoder_cfg_t mp3_cfg = {
                .out_rb_size       = 8 * 1024,
                .task_stack        = 6 * 1024,
                .task_core         = 0,
                .task_prio         = 5,
                .stack_in_ext      = false,
                .id3_parse_enable  = false
            };
            midl_ele_handle = mp3_decoder_init(&mp3_cfg);
            break;
        }
        case aac_dec: {
            aac_decoder_cfg_t aac_cfg = {
                .out_rb_size       = 8 * 1024,
                .task_stack        = 8 * 1024,
                .task_core         = 0,
                .task_prio         = 5,
                .stack_in_ext      = false,
                .plus_enable       = true
            };
            midl_ele_handle = aac_decoder_init(&aac_cfg);
            break;
        }
        case flac_dec: {
            flac_decoder_cfg_t flac_cfg = {
                .out_rb_size       = 36 * 1024,
                .task_stack        = 24 * 1024,
                .task_core         = 0,
                .task_prio         = 6,
                .stack_in_ext      = false
            };
            midl_ele_handle = flac_decoder_init(&flac_cfg);
            break;
        }
        case wav_dec: {
            wav_decoder_cfg_t wav_cfg = {
                .out_rb_size       = 8 * 1024,
                .task_stack        = 6 * 1024,
                .task_core         = 0,
                .task_prio         = 5,
                .stack_in_ext      = false
            };
            midl_ele_handle = wav_decoder_init(&wav_cfg);
            break;
        }
        default:
            ESP_LOGE(TAG, "Unsupported decoder type");
            audio_element_deinit(prev_ele_handle);
            prev_ele_handle = NULL;
            return ESP_FAIL;
    }

    if (!midl_ele_handle) {
        ESP_LOGE(TAG, "Failed to init decoder");
        audio_element_deinit(prev_ele_handle);
        prev_ele_handle = NULL;
        return ESP_FAIL;
    }

    // ---------- 创建后端元素 ----------
    switch (payload->back_type) {
    case i2s_hal: {
        i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
        i2s_cfg.type = AUDIO_STREAM_WRITER;
        i2s_cfg.buffer_len = 12 * 1024;
        i2s_cfg.out_rb_size = 50 * 1024;
        back_ele_handle = i2s_stream_init(&i2s_cfg);
        break;
    }
    default:
        ESP_LOGE(TAG, "Unsupported backend type");
        audio_element_deinit(prev_ele_handle);
        audio_element_deinit(midl_ele_handle);
        prev_ele_handle = NULL;
        midl_ele_handle = NULL;
        return ESP_FAIL;
    }
    if (!back_ele_handle) {
        ESP_LOGE(TAG, "Failed to init I2S stream");
        audio_element_deinit(prev_ele_handle);
        audio_element_deinit(midl_ele_handle);
        prev_ele_handle = NULL;
        midl_ele_handle = NULL;
        return ESP_FAIL;
    }

    // ---------- 组装管道 ----------
    audio_pipeline_register(req_pipeline, prev_ele_handle, "prev");
    audio_pipeline_register(req_pipeline, midl_ele_handle, "midl");
    audio_pipeline_register(req_pipeline, back_ele_handle, "back");
    const char *link_tag[3] = {"prev", "midl", "back"};
    audio_pipeline_link(req_pipeline, &link_tag[0], 3);

    // ---------- 更新状态 ----------
    strncpy(service_state.url, payload->url, sizeof(service_state.url) - 1);
    service_state.url[sizeof(service_state.url) - 1] = '\0';
    service_state.prv_type = payload->prv_type;
    service_state.midle_type = payload->midle_type;
    service_state.back_type = payload->back_type;
    service_state.volume = payload->volume;

    if (payload->start_after_connect) {
        audio_pipeline_run(req_pipeline);
        service_state.service_stata = AUDIO_SERVICE_PLAYING;
        ESP_LOGI(TAG, "Pipeline started");
    } else {
        service_state.service_stata = AUDIO_SERVICE_STOPPED;
        ESP_LOGI(TAG, "Pipeline ready, not started");
    }

    // 应用音量
    do_volume(payload->volume);

    return ESP_OK;
}

// 只销毁管道元素，不改变 service_state（用于连接切换或重连时清理旧资源）
static void cleanup_pipeline(void)
{
    if (req_pipeline) {
        // 1. 停止管道并等待任务结束
        audio_pipeline_stop(req_pipeline);
        audio_pipeline_wait_for_stop(req_pipeline);
        
        // 2. terminate 管道（内部会 terminate 所有注册元素）
        audio_pipeline_terminate(req_pipeline);
        
        // 3. 直接销毁管道（内部会 deinit 所有元素）
        audio_pipeline_deinit(req_pipeline);
        req_pipeline = NULL;
    }

    // 元素句柄已被管道销毁，直接置空即可
    prev_ele_handle = NULL;
    midl_ele_handle = NULL;
    back_ele_handle = NULL;
}

// 完全断开连接：销毁管道并重置服务状态
static esp_err_t do_disconnect(void)
{
    cleanup_pipeline();
    memset(&service_state, 0, sizeof(service_state));
    service_state.service_obj = AUDIO_SERVICE_NONE;
    service_state.service_stata = AUDIO_SERVICE_IDLE;
    ESP_LOGI(TAG, "Disconnected");
    return ESP_OK;
}

static esp_err_t do_stop(void)
{
    if (service_state.service_stata == AUDIO_SERVICE_PLAYING) {
        if (req_pipeline && prev_ele_handle && midl_ele_handle && back_ele_handle) {
            audio_pipeline_stop(req_pipeline);
            audio_pipeline_wait_for_stop(req_pipeline);
            service_state.service_stata = AUDIO_SERVICE_STOPPED;
            ESP_LOGI(TAG, "Playback stopped");
        } else {
            ESP_LOGE(TAG, "Pipeline invalid while trying to stop");
            return ESP_FAIL;
        }
    } else {
        ESP_LOGI(TAG, "Already stopped or idle, do nothing");
    }
    return ESP_OK;
}

static esp_err_t do_play(void)
{
    // 已经在播放
    if (service_state.service_stata == AUDIO_SERVICE_PLAYING) {
        ESP_LOGI(TAG, "Already playing");
        return ESP_OK;
    }

    // 没有连接信息，无法播放
    if (service_state.url[0] == '\0') {
        ESP_LOGE(TAG, "No saved URL to play, connect first");
        return ESP_FAIL;
    }

    // 如果管道元素已经丢失，重新连接
    if (!prev_ele_handle || !midl_ele_handle || !back_ele_handle) {
        ESP_LOGI(TAG, "Re-connecting from saved state");
        return re_connect_from_saved_state();
    }

    // 元素还在，但如果是网络流，直接 run 可能因连接超时而失败，建议重新连接
    if (service_state.prv_type == http_str || service_state.prv_type == https_str) {
        ESP_LOGI(TAG, "HTTP stream: re-connect to resume");
        // cleanup_pipeline 仅清理资源，service_state 不变
        cleanup_pipeline();
        return re_connect_from_saved_state();
    }

    // 本地文件流：重置管道状态后即可运行
    audio_pipeline_reset_ringbuffer(req_pipeline);
    audio_pipeline_reset_elements(req_pipeline);
    audio_pipeline_change_state(req_pipeline, AEL_STATE_INIT);
    audio_pipeline_run(req_pipeline);
    service_state.service_stata = AUDIO_SERVICE_PLAYING;
    ESP_LOGI(TAG, "Playback resumed");
    return ESP_OK;
}

// 利用 service_state 中保存的参数重新连接并播放
static esp_err_t re_connect_from_saved_state(void)
{
    audio_service_receive_data_t reconnect_payload;
    memset(&reconnect_payload, 0, sizeof(reconnect_payload));
    reconnect_payload.cmd = AUDIO_CMD_CONNECT;
    strncpy(reconnect_payload.url, service_state.url, sizeof(reconnect_payload.url) - 1);
    reconnect_payload.prv_type = service_state.prv_type;
    reconnect_payload.midle_type = service_state.midle_type;
    reconnect_payload.back_type = service_state.back_type;
    reconnect_payload.volume = service_state.volume;
    reconnect_payload.start_after_connect = true;

    return do_connect(&reconnect_payload);
}

static esp_err_t do_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    audio_hal_set_volume(board_handle->audio_hal, volume);
    service_state.volume = volume;
    ESP_LOGI(TAG, "Volume set to %d", volume);
    return ESP_OK;
}

// ========================= 回复与通知 =========================

static void send_reply(QueueHandle_t reply_queue)
{
    if (!reply_queue) return;

    audio_service_send_data_t *reply_data = malloc(sizeof(audio_service_send_data_t));
    if (!reply_data) return;
    memcpy(reply_data, &service_state, sizeof(audio_service_send_data_t));

    event_data_t *evt = malloc(sizeof(event_data_t));
    if (!evt) {
        free(reply_data);
        return;
    }
    evt->service_id = AUDIO_SERVICE;
    evt->event_type = NOTIFICATION;
    evt->reply_queue = NULL;
    evt->data = reply_data;
    evt->data_len = sizeof(audio_service_send_data_t);

    if (xQueueSend(reply_queue, &evt, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send reply");
        free(evt->data);
        free(evt);
    }
}

static void handle_notification(event_data_t *evt_data)
{
    // 预留通知处理（如音频事件回调）
    ESP_LOGI(TAG, "Notification from service %d", evt_data->service_id);
}