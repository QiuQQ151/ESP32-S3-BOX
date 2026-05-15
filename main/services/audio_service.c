#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "board.h"
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




#include "hal/power_io_hal.h" // 控制功放使能
#include "hal/sd_hal.h"

#include <dirent.h>   // opendir, readdir, closedir, struct dirent
#include <stdio.h>    // sprintf
#include <stdlib.h>   // malloc, free
#include <strings.h>  // strcasecmp (不区分大小写的字符串比较)
#include <string.h>
#include "nvs_flash.h"
#include "fatfs_stream.h"


static const char* TAG = "audio_service";

audio_board_handle_t board_handle = NULL;
// 音频管道
static audio_pipeline_handle_t pipeline = NULL;
static audio_element_handle_t http_stream = NULL;
static audio_element_handle_t mp3_decoder = NULL;
static audio_element_handle_t i2s_writer = NULL;


static void app_radio_task(void *arg) // 音频任务
{
     vTaskDelay(20000 / portTICK_PERIOD_MS); 
    ESP_LOGI(TAG, "create pipeline...");

    // 创建音频管道
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!pipeline)
    {
        ESP_LOGE(TAG, "Failed to create pipeline");
        vTaskDelete(NULL);
        return;
    }

    // 配置 HTTP 流
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.type = AUDIO_STREAM_READER;
    http_cfg.enable_playlist_parser = false;
    http_cfg.out_rb_size = 8 * 1024;  //8
    http_cfg.task_stack = 4 * 1024;//4
    http_cfg.request_size = 4*1024;//4
    http_stream = http_stream_init(&http_cfg);

    // 设置当前电台URL
    audio_element_set_uri(http_stream, "http://lhttp.qingting.fm/live/20500149/64k.mp3"); // 默认播放第1号电台


    // 配置 mp3解码器
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    // 配置 I2S 输出
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_writer = i2s_stream_init(&i2s_cfg);

    // 将组件注册到管道
    audio_pipeline_register(pipeline, http_stream, "http");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_writer, "i2s");

    // 链接管道组件
    const char *link_tag[3] = {"http", "mp3", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    // // 启动管道
    ESP_LOGI(TAG, "Starting pipeline..."); // 先不启动
    audio_pipeline_run(pipeline);

    // 监听事件
    audio_event_iface_handle_t evt;
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);

    while (1) // 无限循环，直到外部删除任务
    {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) == ESP_OK)
        {
            ESP_LOGI(TAG, "Event received: src_type=%d, source=%p, cmd=%d, data=%p, data_len=%d",
                     msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);
        }
    }
}


#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "flac_decoder.h"
#include "esp_peripherals.h"
#include "board.h"

void list_directory(const char *path)
{
    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return;
    }

    ESP_LOGI(TAG, "Contents of '%s':", path);
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        const char *type_str = "UNKNOWN";
        if (entry->d_type == DT_REG) {
            type_str = "FILE";
        } else if (entry->d_type == DT_DIR) {
            type_str = "DIR ";
        }
        ESP_LOGI(TAG, "  [%s] %s (len=%d)", type_str, entry->d_name, (int)strlen(entry->d_name));
        count++;
    }
    closedir(dir);
    ESP_LOGI(TAG, "Total entries: %d", count);
}

void app_player_task(void *arg)
{
    // 所有指针初始化为 NULL，计数为 0
    audio_pipeline_handle_t pipeline = NULL;
    audio_element_handle_t fatfs_stream_reader = NULL;
    audio_element_handle_t i2s_stream_writer = NULL;
    audio_element_handle_t music_decoder = NULL;
    audio_event_iface_handle_t evt = NULL;
    esp_periph_set_handle_t set = NULL;
    char *flac_files[50];
    int file_count = 0;

    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    // 1. 初始化外设管理器（即使没外设，创建也无妨）
    set = esp_periph_set_init(&(esp_periph_config_t)DEFAULT_ESP_PERIPH_SET_CONFIG());

    // // 2. 挂载 SD 卡（使用你自己的 sd_hal_init）
    // ESP_LOGI(TAG, "[ 1 ] Mount sdcard");
    // if (sd_hal_init() != ESP_OK) {
    //     ESP_LOGE(TAG, "SD card mount failed");
    //     goto cleanup;
    // }

    // 4. 创建音频管道元素
    ESP_LOGI(TAG, "[3.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_cfg.rb_size = 50 * 1024;   // 50KB 环形缓冲区
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!pipeline) goto cleanup;

    ESP_LOGI(TAG, "[3.1] Create fatfs stream reader");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_cfg.buf_sz = 50*1024; 
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);
    if (!fatfs_stream_reader) goto cleanup;

    ESP_LOGI(TAG, "[3.2] Create i2s stream writer");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.buffer_len = 4*12*1024;                 // 默认可能是 4096
    i2s_cfg.out_rb_size = 50 * 1024;           // 32KB

    i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    if (!i2s_stream_writer) goto cleanup;

    ESP_LOGI(TAG, "[3.3] Create flac decoder");
    flac_decoder_cfg_t flac_cfg = DEFAULT_FLAC_DECODER_CONFIG();
    flac_cfg.out_rb_size = 80 * 1024;          // 默认可能较小，建议 32KB 或更大
    music_decoder = flac_decoder_init(&flac_cfg);
    if (!music_decoder) goto cleanup;

    // 注册并链接管道
    ESP_LOGI(TAG, "[3.4] Register and link elements");
    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_register(pipeline, music_decoder, "dec");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");
    const char *link_tag[3] = {"file", "dec", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    list_directory("/sdcard/music"); 
    // 5. 扫描 .flac 文件（尝试 music 文件夹，不存在则用根目录）
    const char *music_dir = "/sdcard/music";
    ESP_LOGI(TAG, "[ 4 ] Scan %s for .flac files", music_dir);
    DIR *dir = opendir(music_dir);
    if (dir == NULL) {
        ESP_LOGW(TAG, "Directory %s not found, falling back to /sdcard", music_dir);
        music_dir = "/sdcard";
        dir = opendir(music_dir);
    }
    if (dir == NULL) {
        ESP_LOGE(TAG, "Cannot open any directory");
        goto cleanup;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && file_count < 50) {
        const char *name = entry->d_name;
        // 只收集普通文件，且后缀为 .flac（忽略大小写）
        if (entry->d_type == DT_REG) {
            const char *ext = strrchr(name, '.');
            if (ext && strcasecmp(ext, ".flac") == 0) {
                size_t path_len = strlen(music_dir) + strlen(name) + 2; // '/' + '\0'
                char *path = malloc(path_len);
                if (path) {
                    sprintf(path, "%s/%s", music_dir, name);
                    flac_files[file_count++] = path;
                    ESP_LOGI(TAG, "Found FLAC: %s", path);
                }
            }
        }
    }
    closedir(dir);

    if (file_count == 0) {
        ESP_LOGW(TAG, "No FLAC files found");
        goto cleanup;
    }

    // 按文件名排序（简单冒泡）
    for (int i = 0; i < file_count - 1; i++) {
        for (int j = i + 1; j < file_count; j++) {
            if (strcasecmp(flac_files[i], flac_files[j]) > 0) {
                char *tmp = flac_files[i];
                flac_files[i] = flac_files[j];
                flac_files[j] = tmp;
            }
        }
    }

    // 6. 创建事件监听
    ESP_LOGI(TAG, "[ 5 ] Set up event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);
    if (!evt) goto cleanup;

    audio_pipeline_set_listener(pipeline, evt);
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    // 7. 顺序播放所有文件
    for (int i = 0; i < file_count; i++) {
        ESP_LOGI(TAG, "[ 6 ] Playing file %d/%d: %s", i + 1, file_count, flac_files[i]);

        audio_element_set_uri(fatfs_stream_reader, flac_files[i]);

        // 重置管道以便复用
        audio_pipeline_reset_ringbuffer(pipeline);
        audio_pipeline_reset_elements(pipeline);
        audio_pipeline_change_state(pipeline, AEL_STATE_INIT);

        audio_pipeline_run(pipeline);

        // 等待当前歌曲播放结束
        while (1) {
            audio_event_iface_msg_t msg;
            if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) != ESP_OK) {
                ESP_LOGE(TAG, "[ * ] Event interface error");
                continue;
            }

            // 处理音乐信息
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                msg.source == (void *) music_decoder &&
                msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info = {0};
                audio_element_getinfo(music_decoder, &music_info);
                ESP_LOGI(TAG, "[ * ] sample_rates=%d, bits=%d, ch=%d",
                         music_info.sample_rates, music_info.bits, music_info.channels);
                audio_element_setinfo(i2s_stream_writer, &music_info);
                i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates,
                                   music_info.bits, music_info.channels);
                continue;
            }

            // I2S 写入器停止或完成 → 当前歌曲结束
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                msg.source == (void *) i2s_stream_writer &&
                msg.cmd == AEL_MSG_CMD_REPORT_STATUS &&
                (((int)msg.data == AEL_STATUS_STATE_STOPPED) ||
                 ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
                ESP_LOGI(TAG, "[ * ] Finished playing: %s", flac_files[i]);
                break;
            }
        }

        audio_pipeline_stop(pipeline);
        audio_pipeline_wait_for_stop(pipeline);
    }

    ESP_LOGI(TAG, "[ 7 ] All files played. Cleaning up.");

cleanup:
    // 释放文件路径内存
    for (int i = 0; i < file_count; i++) {
        free(flac_files[i]);
    }

    // 安全停止/销毁所有资源
    if (pipeline) {
        audio_pipeline_stop(pipeline);
        audio_pipeline_wait_for_stop(pipeline);
        audio_pipeline_terminate(pipeline);

        audio_pipeline_remove_listener(pipeline);
        if (fatfs_stream_reader) audio_pipeline_unregister(pipeline, fatfs_stream_reader);
        if (i2s_stream_writer)   audio_pipeline_unregister(pipeline, i2s_stream_writer);
        if (music_decoder)       audio_pipeline_unregister(pipeline, music_decoder);
    }

    if (set) {
        esp_periph_set_stop_all(set);
        if (evt) {
            audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);
        }
    }
    if (evt) audio_event_iface_destroy(evt);

    if (fatfs_stream_reader) audio_element_deinit(fatfs_stream_reader);
    if (i2s_stream_writer)   audio_element_deinit(i2s_stream_writer);
    if (music_decoder)       audio_element_deinit(music_decoder);
    if (pipeline)            audio_pipeline_deinit(pipeline);
    if (set)                 esp_periph_set_destroy(set);

    // FreeRTOS 任务必须自我删除，不能 return
    vTaskDelete(NULL);
}



void audio_service_init(void){

    //  初始化ES8311音频芯片 初始化I2C I2S
    ESP_LOGI(TAG, "Start codec chip");
    board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    audio_hal_set_volume(board_handle->audio_hal, 0);
    pa_set(1);
    audio_hal_set_volume(board_handle->audio_hal, 60);
   
    //xTaskCreate(app_player_task, "app_player_task", 10*1024, NULL, 5, NULL);  
}

