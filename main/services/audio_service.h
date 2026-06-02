#pragma once

#include "esp_err.h"
#include "freertos/queue.h"

#define CONTENT_ID_ALWAYS_NEW  (-1)

// ========== 请求命令 ==========
typedef enum {
    // 请求端命令
    AUDIO_CMD_CONNECT = 0,        // 建立/切换连接
    AUDIO_CMD_DISCONNECT = 1,     // 断开连接（销毁），回到 IDLE
    AUDIO_CMD_PLAY = 2,           // 播放
    AUDIO_CMD_PAUSE = 4,           // 暂停播放
    AUDIO_CMD_VOLUME = 5,         // 设置音量

    // 服务端通知
    AUDIO_CMD_END = 101,                // 播放结束通知
    AUDIO_CMD_ERROR = 102,              // 播放错误通知
} audio_service_cmd_t;

// ========== 管道类型 ==========
typedef enum {
    audio_type_none = 0,
    //
    http_str,
    https_str,
    file_str,
    usb_uac,   // USB声卡
    //
    aac_dec,
    mp3_dec,
    flac_dec,
    wav_dec,
    //
    i2s_hal,
    raw_hal,
} audio_service_stream_type_t;

// ========== 请求数据结构（app -> audio_service） ==========
typedef struct {
    audio_service_cmd_t cmd;
    char url[200];
    audio_service_stream_type_t prv_type;
    audio_service_stream_type_t midle_type;
    audio_service_stream_type_t back_type;
    int volume;
    bool start_after_connect; // 建立连接后是否播放
} audio_service_receive_data_t;

// ========== 服务状态枚举 ==========
typedef enum {
    AUDIO_SERVICE_ERROR = -1,  // 异常
    AUDIO_SERVICE_IDLE = 0,   // 空闲
    AUDIO_SERVICE_CONNECTED,  // 已连接但未运行
    AUDIO_SERVICE_PLAYING,  // 正在播放
    AUDIO_SERVICE_PAUSED,   // 已暂停
} audio_service_state_t;

// ========== 回复/通知数据结构（audio_service -> app） ==========
typedef struct {
    audio_service_cmd_t cmd;             // END / ERROR / 或回复对应请求
    audio_service_state_t service_state;
    char url[200];
    int volume;
} audio_service_send_data_t;

// ========== 对外 API ==========
esp_err_t audio_service_init(void);
QueueHandle_t get_audio_service_queue(void);
esp_err_t set_audio_volume(int volume);
int get_audio_volume(void);