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
    // 如果已有连接，先只清理管道，不销毁 service_state
    if (prev_ele_handle || midl_ele_handle || back_ele_handle) {
        ESP_LOGI(TAG, "Disconnecting previous stream");
        cleanup_pipeline();
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
        fatfs_cfg.buf_sz = 50 * 1024;
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
        mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
        midl_ele_handle = mp3_decoder_init(&mp3_cfg);
        break;
    }
    case aac_dec: {
        aac_decoder_cfg_t aac_cfg = DEFAULT_AAC_DECODER_CONFIG();
        midl_ele_handle = aac_decoder_init(&aac_cfg);
        break;
    }
    case flac_dec: {
        flac_decoder_cfg_t flac_cfg = DEFAULT_FLAC_DECODER_CONFIG();
        midl_ele_handle = flac_decoder_init(&flac_cfg);
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
        i2s_cfg.buffer_len = 12 * 1024;     // 必须是12的倍数
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
    // service_state.service_obj 由上层设置，此处不动

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
        audio_pipeline_stop(req_pipeline);
        audio_pipeline_wait_for_stop(req_pipeline);
        audio_pipeline_terminate(req_pipeline);

        if (prev_ele_handle) {
            audio_pipeline_unregister(req_pipeline, prev_ele_handle);
            audio_element_deinit(prev_ele_handle);
            prev_ele_handle = NULL;
        }
        if (midl_ele_handle) {
            audio_pipeline_unregister(req_pipeline, midl_ele_handle);
            audio_element_deinit(midl_ele_handle);
            midl_ele_handle = NULL;
        }
        if (back_ele_handle) {
            audio_pipeline_unregister(req_pipeline, back_ele_handle);
            audio_element_deinit(back_ele_handle);
            back_ele_handle = NULL;
        }
    }
}

// 完全断开连接：销毁管道并重置服务状态
static esp_err_t do_disconnect(void)
{
    cleanup_pipeline();

    // 重置状态
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


// // 1. 标准C库头文件
// #include <dirent.h>   // opendir, readdir, closedir, struct dirent
// #include <stdio.h>    // sprintf
// #include <stdlib.h>   // malloc, free
// #include <string.h>   // strcasecmp, strlen, strcpy等

// // 2. FreeRTOS头文件
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "freertos/queue.h"

// // 3. ESP-IDF系统头文件
// #include "esp_log.h"
// #include "nvs_flash.h"

// // 4. 音频框架头文件
// #include "audio_pipeline.h"
// #include "audio_element.h"
// #include "audio_event_iface.h"
// #include "audio_common.h"
// #include "audio_hal.h"
// #include "esp_codec_dev.h"
// #include "http_stream.h"
// #include "i2s_stream.h"
// #include "aac_decoder.h"
// #include "mp3_decoder.h"
// #include "flac_decoder.h"
// #include "fatfs_stream.h"
// #include "esp_peripherals.h"
// #include "board.h"

// // 5. 项目硬件抽象层头文件
// #include "hal/power_hal.h" // 控制功放使能
// #include "hal/sd_hal.h" // 控制SD卡
// #include "hal/tca9535_hal.h" // 控制tca9535 IO扩展芯片

// #include "services/system_event.h"
// #include "services/audio_service.h"

// static const char* TAG = "audio_service";

// // 音频板句柄
// audio_board_handle_t board_handle = NULL;  // 音频板句柄
// // 音频管道
// static audio_pipeline_handle_t req_pipeline = NULL;  // 主音频管道
// static audio_pipeline_handle_t ntf_pipeline = NULL;  // 通知音频管道
// static audio_element_handle_t prev_ele_handle = NULL;  // 前端元素句柄
// static audio_element_handle_t midl_ele_handle = NULL;  // 中间元素句柄
// static audio_element_handle_t back_ele_handle = NULL;  // 后端元素句柄
// // 音频服务队列
// static QueueHandle_t audio_service_request_queue = NULL;
// // 内部函数
// static void audio_service_task(void *arg); // 音频服务任务
// static void handle_connect(audio_service_receive_data_t *payload);    // 建立连接
// static void handle_disconnect(audio_service_receive_data_t *payload); // 清除连接
// static void handle_stop(audio_service_receive_data_t *payload);       // 停止播放当前连接
// static void handle_play(audio_service_receive_data_t *payload);       // 播放当前连接
// static void handle_volume(audio_service_receive_data_t *payload);     // 设置输出音量
// static void handle_reply(audio_service_receive_data_t *payload);      // 回复请求
// static void handle_request(audio_service_receive_data_t *payload);    // 处理请求
// static void handle_notification(event_data_t *evt_data); // 处理通知
// static audio_service_send_data_t service_state = {0,0};      // 默认空闲，待建立连接

// // ==================================api==================================================================

// esp_err_t audio_service_init(void){

//     //  初始化ES8311音频芯片 初始化I2C I2S
//     ESP_LOGI(TAG, "Start codec chip"); //
//     board_handle = audio_board_init(); //
//     audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
//     // 初始化电源使能
//     power_hal_init();
//     // 初始化音频管道
//     audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
//     req_pipeline = audio_pipeline_init(&pipeline_cfg);
//     if (!req_pipeline)
//     {
//         ESP_LOGE(TAG, "Failed to create req_pipeline");
//         return ESP_ERR_NO_MEM; 
//     } 
//     ntf_pipeline = audio_pipeline_init(&pipeline_cfg);
//     if (!ntf_pipeline)
//     {
//         ESP_LOGE(TAG, "Failed to create ntf_pipeline");
//         audio_pipeline_deinit(req_pipeline);
//         return ESP_ERR_NO_MEM; 
//     } 
    
//     // 初始化音频服务队列
//     audio_service_request_queue = xQueueCreate(10, sizeof(event_data_t));
//     if(audio_service_request_queue == NULL){
//         ESP_LOGE(TAG, "Failed to create audio_service_request_queue");
//         vTaskDelete(NULL);
//         return ESP_ERR_NO_MEM;
//     }
//     // 创建音频任务
//     xTaskCreate(audio_service_task, "audio_service_task", 15*1024, NULL, 5, NULL);  
//     return ESP_OK;
// }

// QueueHandle_t get_audio_service_queue(void){
//     return audio_service_request_queue;
// }


// // =======================================内部函数==================================================================

// static void audio_service_task(void *arg) // 音频服务任务
// {
//     // 分发audio服务请求
//     event_data_t *evt_data;
//     while (1) {
//         if (xQueueReceive(audio_service_request_queue, &evt_data, portMAX_DELAY) == pdTRUE) {
//             // 处理请求
//             if(evt_data->event_type == REQUEST){
//                 audio_service_receive_data_t *payload = (audio_service_receive_data_t *)evt_data->data;
//                 if(payload){
//                     ESP_LOGI(TAG, "handle request: cmd %d", payload->cmd);
//                     handle_request(payload);
//                 }
//             } else if(evt_data->event_type == NOTIFICATION){
//                 ESP_LOGI(TAG, "handle notification, from service_id %d", evt_data->service_id);
//                 handle_notification(evt_data);
//             } else{
//                 ESP_LOGE(TAG, "Unknown audio service event type %d", evt_data->event_type);
//             }
//             // 统一释放内存
//             if(evt_data->data){
//                 free(evt_data->data);
//                 evt_data->data = NULL;
//             }
//             if(evt_data){
//                 free(evt_data);
//                 evt_data = NULL;
//             }
//         }
//     }
// }

// // 处理请求
// static void handle_request(audio_service_receive_data_t *payload){
//     // 分发请求
//     switch(payload->cmd){
//         case AUDIO_CMD_CONNECT:
//             ESP_LOGI(TAG, "Received audio service request connect");
//             handle_connect(payload);
//             break;
//         case AUDIO_CMD_DISCONNECT:
//             ESP_LOGI(TAG, "Received audio service request disconnect");
//             handle_disconnect(payload);
//             break;
//         case AUDIO_CMD_STOP:
//             ESP_LOGI(TAG, "Received audio service request stop");
//             handle_stop(payload);
//             break;
//         case AUDIO_CMD_PLAY:
//             ESP_LOGI(TAG, "Received audio service request play");
//             handle_play(payload);
//             break;
//         case AUDIO_CMD_VOLUME:
//             ESP_LOGI(TAG, "Received audio service request volume");
//             handle_volume(payload);
//             break;
//         default:
//             ESP_LOGE(TAG, "Unknown audio service request");
//             break;
//     }
// } 

// // 处理通知
// static void handle_notification(event_data_t *evt_data){


// }



// static void handle_connect(audio_service_receive_data_t *payload) // 建立连接
// {
//     // 建立前端
//     if(prev_ele_handle == NULL ){
//            switch(payload->prv_type){
//                    case http_str:
//                         // 配置 HTTP 流
//                         http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
//                         http_cfg.type = AUDIO_STREAM_READER;
//                         http_cfg.enable_playlist_parser = false;
//                         http_cfg.out_rb_size = 8 * 1024;  //8
//                         http_cfg.task_stack = 4 * 1024;//4
//                         http_cfg.request_size = 4*1024;//4
//                         prev_ele_handle = http_stream_init(&http_cfg);
//                         break;
//                    default:
//                         break;
//            }
//     } 

//     // 建立中间件
//     if(midl_ele_handle == NULL ){
//            switch(payload->midle_type){
//                    case mp3_dec:
//                         // 配置 mp3解码器
//                         mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
//                         midl_ele_handle = mp3_decoder_init(&mp3_cfg);
//                         break;
//                    default:
//                         break;
//            }
//     } 

//     // 建立后端
//     if(back_ele_handle == NULL ){
//            switch(payload->back_type){
//                    case i2s_hal:
//                         // 配置 I2S 输出
//                         i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
//                         i2s_cfg.type = AUDIO_STREAM_WRITER;
//                         back_ele_handle = i2s_stream_init(&i2s_cfg);
//                         break;
//                    default:
//                         break;
//            }
//     } 

//     // 配置前端URL
//     audio_element_set_uri(prev_ele_handle, payload->url);

//     // 将组件注册到管道
//     audio_pipeline_register(req_pipeline, prev_ele_handle, "prev");
//     audio_pipeline_register(req_pipeline, midl_ele_handle, "midl");
//     audio_pipeline_register(req_pipeline, back_ele_handle, "back");

//     // 链接管道组件
//     const char *link_tag[3] = {"prev", "midl", "back"};
//     audio_pipeline_link(req_pipeline, &link_tag[0], 3);

//     // 启动管道
//     if(payload->play){
//         audio_pipeline_start(req_pipeline);
//     }

// }

// static void handle_disconnect(audio_service_receive_data_t *payload) // 清除连接
// {


// }

// static void handle_stop(audio_service_receive_data_t *payload) // 停止播放当前连接
// {


// }



// static void handle_play(audio_service_receive_data_t *payload) // 播放当前连接
// {


// }



// static void handle_volume(audio_service_receive_data_t *payload) // 设置输出音量
// {


// }

// static void handle_reply(audio_service_receive_data_t *payload) // 回复请求
// {

// }


//     // 创建音频管道
//     audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
//     req_pipeline = audio_pipeline_init(&pipeline_cfg);
//     if (!req_pipeline)
//     {
//         ESP_LOGE(TAG, "Failed to create req_pipeline");
//         vTaskDelete(NULL);
//         return;
//     }

//     // 配置 HTTP 流
//     http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
//     http_cfg.type = AUDIO_STREAM_READER;
//     http_cfg.enable_playlist_parser = false;
//     http_cfg.out_rb_size = 8 * 1024;  //8
//     http_cfg.task_stack = 4 * 1024;//4
//     http_cfg.request_size = 4*1024;//4
//     prev_ele_handle = http_stream_init(&http_cfg);

//     // 设置当前电台URL
//     audio_element_set_uri(prev_ele_handle, "http://lhttp.qingting.fm/live/20500149/64k.mp3"); // 默认播放第1号电台


//     // 配置 mp3解码器
//     mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
//     midl_ele_handle = mp3_decoder_init(&mp3_cfg);

//     // 配置 I2S 输出
//     i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
//     i2s_cfg.type = AUDIO_STREAM_WRITER;
//     back_ele_handle = i2s_stream_init(&i2s_cfg);

//     // 将组件注册到管道
//     audio_pipeline_register(req_pipeline, prev_ele_handle, "http");
//     audio_pipeline_register(req_pipeline, midl_ele_handle, "mp3");
//     audio_pipeline_register(req_pipeline, back_ele_handle, "i2s");

//     // 链接管道组件
//     const char *link_tag[3] = {"http", "mp3", "i2s"};
//     audio_pipeline_link(req_pipeline, &link_tag[0], 3);

//     // // 启动管道
//     ESP_LOGI(TAG, "Starting req_pipeline..."); // 先不启动
//     audio_pipeline_run(req_pipeline);

//     // 监听事件
//     audio_event_iface_handle_t evt;
//     audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
//     evt = audio_event_iface_init(&evt_cfg);
//     audio_pipeline_set_listener(req_pipeline, evt);

//     while (1) // 无限循环，直到外部删除任务
//     {
//         audio_event_iface_msg_t msg;
//         if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) == ESP_OK)
//         {
//             ESP_LOGI(TAG, "Event received: src_type=%d, source=%p, cmd=%d, data=%p, data_len=%d",
//                      msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);
//         }
//     }
// }


// #include <stdio.h>
// #include <string.h>
// #include <dirent.h>
// #include <stdlib.h>
// #include <strings.h>

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_log.h"

// #include "audio_element.h"
// #include "audio_pipeline.h"
// #include "audio_event_iface.h"
// #include "audio_common.h"
// #include "fatfs_stream.h"
// #include "i2s_stream.h"
// #include "flac_decoder.h"
// #include "esp_peripherals.h"
// #include "board.h"

// void list_directory(const char *path)
// {
//     DIR *dir = opendir(path);
//     if (dir == NULL) {
//         ESP_LOGE(TAG, "Failed to open directory: %s", path);
//         return;
//     }

//     ESP_LOGI(TAG, "Contents of '%s':", path);
//     struct dirent *entry;
//     int count = 0;
//     while ((entry = readdir(dir)) != NULL) {
//         const char *type_str = "UNKNOWN";
//         if (entry->d_type == DT_REG) {
//             type_str = "FILE";
//         } else if (entry->d_type == DT_DIR) {
//             type_str = "DIR ";
//         }
//         ESP_LOGI(TAG, "  [%s] %s (len=%d)", type_str, entry->d_name, (int)strlen(entry->d_name));
//         count++;
//     }
//     closedir(dir);
//     ESP_LOGI(TAG, "Total entries: %d", count);
// }

// void app_player_task(void *arg)
// {
//     // 所有指针初始化为 NULL，计数为 0
//     audio_pipeline_handle_t req_pipeline = NULL;
//     audio_element_handle_t fatfs_stream_reader = NULL;
//     audio_element_handle_t i2s_stream_writer = NULL;
//     audio_element_handle_t music_decoder = NULL;
//     audio_event_iface_handle_t evt = NULL;
//     esp_periph_set_handle_t set = NULL;
//     char *flac_files[50];
//     int file_count = 0;

//     esp_log_level_set("*", ESP_LOG_WARN);
//     esp_log_level_set(TAG, ESP_LOG_INFO);

//     // 1. 初始化外设管理器（即使没外设，创建也无妨）
//     set = esp_periph_set_init(&(esp_periph_config_t)DEFAULT_ESP_PERIPH_SET_CONFIG());

//     // // 2. 挂载 SD 卡（使用你自己的 sd_hal_init）
//     // ESP_LOGI(TAG, "[ 1 ] Mount sdcard");
//     // if (sd_hal_init() != ESP_OK) {
//     //     ESP_LOGE(TAG, "SD card mount failed");
//     //     goto cleanup;
//     // }

//     // 4. 创建音频管道元素
//     ESP_LOGI(TAG, "[3.0] Create audio req_pipeline for playback");
//     audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
//     pipeline_cfg.rb_size = 50 * 1024;   // 50KB 环形缓冲区
//     req_pipeline = audio_pipeline_init(&pipeline_cfg);
//     if (!req_pipeline) goto cleanup;

//     ESP_LOGI(TAG, "[3.1] Create fatfs stream reader");
//     fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
//     fatfs_cfg.type = AUDIO_STREAM_READER;
//     fatfs_cfg.buf_sz = 50*1024; 
//     fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);
//     if (!fatfs_stream_reader) goto cleanup;

//     ESP_LOGI(TAG, "[3.2] Create i2s stream writer");
//     i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
//     i2s_cfg.type = AUDIO_STREAM_WRITER;
//     i2s_cfg.buffer_len = 4*12*1024;                 // 默认可能是 4096
//     i2s_cfg.out_rb_size = 50 * 1024;           // 32KB

//     i2s_stream_writer = i2s_stream_init(&i2s_cfg);
//     if (!i2s_stream_writer) goto cleanup;

//     ESP_LOGI(TAG, "[3.3] Create flac decoder");
//     flac_decoder_cfg_t flac_cfg = DEFAULT_FLAC_DECODER_CONFIG();
//     flac_cfg.out_rb_size = 80 * 1024;          // 默认可能较小，建议 32KB 或更大
//     music_decoder = flac_decoder_init(&flac_cfg);
//     if (!music_decoder) goto cleanup;

//     // 注册并链接管道
//     ESP_LOGI(TAG, "[3.4] Register and link elements");
//     audio_pipeline_register(req_pipeline, fatfs_stream_reader, "file");
//     audio_pipeline_register(req_pipeline, music_decoder, "dec");
//     audio_pipeline_register(req_pipeline, i2s_stream_writer, "i2s");
//     const char *link_tag[3] = {"file", "dec", "i2s"};
//     audio_pipeline_link(req_pipeline, &link_tag[0], 3);

//     list_directory("/sdcard/music"); 
//     // 5. 扫描 .flac 文件（尝试 music 文件夹，不存在则用根目录）
//     const char *music_dir = "/sdcard/music";
//     ESP_LOGI(TAG, "[ 4 ] Scan %s for .flac files", music_dir);
//     DIR *dir = opendir(music_dir);
//     if (dir == NULL) {
//         ESP_LOGW(TAG, "Directory %s not found, falling back to /sdcard", music_dir);
//         music_dir = "/sdcard";
//         dir = opendir(music_dir);
//     }
//     if (dir == NULL) {
//         ESP_LOGE(TAG, "Cannot open any directory");
//         goto cleanup;
//     }

//     struct dirent *entry;
//     while ((entry = readdir(dir)) != NULL && file_count < 50) {
//         const char *name = entry->d_name;
//         // 只收集普通文件，且后缀为 .flac（忽略大小写）
//         if (entry->d_type == DT_REG) {
//             const char *ext = strrchr(name, '.');
//             if (ext && strcasecmp(ext, ".flac") == 0) {
//                 size_t path_len = strlen(music_dir) + strlen(name) + 2; // '/' + '\0'
//                 char *path = malloc(path_len);
//                 if (path) {
//                     sprintf(path, "%s/%s", music_dir, name);
//                     flac_files[file_count++] = path;
//                     ESP_LOGI(TAG, "Found FLAC: %s", path);
//                 }
//             }
//         }
//     }
//     closedir(dir);

//     if (file_count == 0) {
//         ESP_LOGW(TAG, "No FLAC files found");
//         goto cleanup;
//     }

//     // 按文件名排序（简单冒泡）
//     for (int i = 0; i < file_count - 1; i++) {
//         for (int j = i + 1; j < file_count; j++) {
//             if (strcasecmp(flac_files[i], flac_files[j]) > 0) {
//                 char *tmp = flac_files[i];
//                 flac_files[i] = flac_files[j];
//                 flac_files[j] = tmp;
//             }
//         }
//     }

//     // 6. 创建事件监听
//     ESP_LOGI(TAG, "[ 5 ] Set up event listener");
//     audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
//     evt = audio_event_iface_init(&evt_cfg);
//     if (!evt) goto cleanup;

//     audio_pipeline_set_listener(req_pipeline, evt);
//     audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

//     // 7. 顺序播放所有文件
//     for (int i = 0; i < file_count; i++) {
//         ESP_LOGI(TAG, "[ 6 ] Playing file %d/%d: %s", i + 1, file_count, flac_files[i]);

//         audio_element_set_uri(fatfs_stream_reader, flac_files[i]);

//         // 重置管道以便复用
//         audio_pipeline_reset_ringbuffer(req_pipeline);
//         audio_pipeline_reset_elements(req_pipeline);
//         audio_pipeline_change_state(req_pipeline, AEL_STATE_INIT);

//         audio_pipeline_run(req_pipeline);

//         // 等待当前歌曲播放结束
//         while (1) {
//             audio_event_iface_msg_t msg;
//             if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) != ESP_OK) {
//                 ESP_LOGE(TAG, "[ * ] Event interface error");
//                 continue;
//             }

//             // 处理音乐信息
//             if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
//                 msg.source == (void *) music_decoder &&
//                 msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
//                 audio_element_info_t music_info = {0};
//                 audio_element_getinfo(music_decoder, &music_info);
//                 ESP_LOGI(TAG, "[ * ] sample_rates=%d, bits=%d, ch=%d",
//                          music_info.sample_rates, music_info.bits, music_info.channels);
//                 audio_element_setinfo(i2s_stream_writer, &music_info);
//                 i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates,
//                                    music_info.bits, music_info.channels);
//                 continue;
//             }

//             // I2S 写入器停止或完成 → 当前歌曲结束
//             if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
//                 msg.source == (void *) i2s_stream_writer &&
//                 msg.cmd == AEL_MSG_CMD_REPORT_STATUS &&
//                 (((int)msg.data == AEL_STATUS_STATE_STOPPED) ||
//                  ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
//                 ESP_LOGI(TAG, "[ * ] Finished playing: %s", flac_files[i]);
//                 break;
//             }
//         }

//         audio_pipeline_stop(req_pipeline);
//         audio_pipeline_wait_for_stop(req_pipeline);
//     }

//     ESP_LOGI(TAG, "[ 7 ] All files played. Cleaning up.");

// cleanup:
//     // 释放文件路径内存
//     for (int i = 0; i < file_count; i++) {
//         free(flac_files[i]);
//     }

//     // 安全停止/销毁所有资源
//     if (req_pipeline) {
//         audio_pipeline_stop(req_pipeline);
//         audio_pipeline_wait_for_stop(req_pipeline);
//         audio_pipeline_terminate(req_pipeline);

//         audio_pipeline_remove_listener(req_pipeline);
//         if (fatfs_stream_reader) audio_pipeline_unregister(req_pipeline, fatfs_stream_reader);
//         if (i2s_stream_writer)   audio_pipeline_unregister(req_pipeline, i2s_stream_writer);
//         if (music_decoder)       audio_pipeline_unregister(req_pipeline, music_decoder);
//     }

//     if (set) {
//         esp_periph_set_stop_all(set);
//         if (evt) {
//             audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);
//         }
//     }
//     if (evt) audio_event_iface_destroy(evt);

//     if (fatfs_stream_reader) audio_element_deinit(fatfs_stream_reader);
//     if (i2s_stream_writer)   audio_element_deinit(i2s_stream_writer);
//     if (music_decoder)       audio_element_deinit(music_decoder);
//     if (req_pipeline)            audio_pipeline_deinit(req_pipeline);
//     if (set)                 esp_periph_set_destroy(set);

//     // FreeRTOS 任务必须自我删除，不能 return
//     vTaskDelete(NULL);
// }



