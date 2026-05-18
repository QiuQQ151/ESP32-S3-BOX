#pragma once

// Audio service
// ===============请求服务
typedef enum {
    AUDIO_CMD_CONNECT = 0,             // 建立连接 // 默认不播放当前连接
    AUDIO_CMD_DISCONNECT = 1,          // 清除连接
    AUDIO_CMD_STOP = 3,                // 停止播放当前连接
    AUDIO_CMD_PLAY = 4,                // 播放当前连接
    AUDIO_CMD_VOLUME = 5,              // 设置输出音量
} audio_service_cmd_t;

// 管道类型
typedef enum {
    // uri类型
    http_str = 0,
    https_str,
    file_str,
    // 编解码器类型
    aac_dec,
    mp3_dec,
    flac_dec,
    wav_dec,
    // 后端类型
    i2s_hal,
} audio_service_stream_type_t;

typedef struct {
    audio_service_cmd_t cmd;  // 请求服务
    char url[200];             // 连接URL
    audio_service_stream_type_t prv_type;        // 前端类型 http/https/file 等
    audio_service_stream_type_t midle_type;      // 中间件类型 解码器
    audio_service_stream_type_t back_type;       // 后端类型类型 es8311
    int volume;               // 输出音量 0-100
    bool start_after_connect;    // 是否开始播放当前连接
} audio_service_receive_data_t;


// ==================服务回复
typedef enum{
   // 服务对象
   AUDIO_SERVICE_NONE = 0, // 无服务对象
   AUDIO_SERVICE_RADIO = 1, // radio
   AUDIO_SERVICE_MUSIC,    // music
   AUDIO_SERVICE_AICHAT,   // aichat
   AUDIO_SERVICE_CLOCK,    // 时钟服务
} audio_service_obj_t;

typedef enum {  
    AUDIO_SERVICE_ERROR,   // 响应错误
    AUDIO_SERVICE_IDLE,    // 空闲待建立连接
    AUDIO_SERVICE_PLAYING, // 连接建立且正在播放
    AUDIO_SERVICE_STOPPED, // 连接建立且停止播放
} audio_service_state_t;

// 服务发给reply_queue的数据结构
typedef struct {
    audio_service_obj_t service_obj;        // 服务对象
    audio_service_state_t service_stata;    // 服务状态
    char url[200];                          // 连接URL
    audio_service_stream_type_t prv_type;        // 前端类型 http/https/file 等
    audio_service_stream_type_t midle_type;      // 中间件类型 解码器
    audio_service_stream_type_t back_type;       // 后端类型类型 es8311
    int volume;               // 输出音量 0-100
} audio_service_send_data_t;

esp_err_t audio_service_init(void);  // 初始化  服务（由系统启动）   
QueueHandle_t get_audio_service_queue(void);
