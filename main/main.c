#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "usb_device_uac.h"

// HAL
#include "hal/tca9535_hal.h"
#include "hal/lvgl_hal.h"
#include "hal/sd_hal.h"
#include "hal/keys_hal.h"

// Services
#include "services/system_event.h"
#include "services/wifi_service.h"
#include "services/power_service.h"
#include "services/sntp_service.h"
#include "services/audio_service.h"
#include "services/ui_service.h"
#include "services/led_service.h"

// ADF elements
#include "audio_element.h"
#include "i2s_stream.h"
#include "raw_stream.h"          // 如果确实需要 raw_stream，用于接收USB数据
#include "usb_device_uac.h"      // USB UAC 组件
#include "esp_codec_dev.h"       // 编解码器设备（ES8311）
#include "driver/i2s_std.h"
#include "board.h"
#include "audio_pipeline.h"

static const char *TAG = "app_main";
// static audio_pipeline_handle_t pipeline_player = NULL;
// static audio_element_handle_t raw_writer = NULL;
// static audio_pipeline_handle_t pipeline_recorder = NULL;
// static audio_element_handle_t i2s_reader = NULL;
// static audio_element_handle_t raw_recorder = NULL;

// // USB 回调：将数据写入 raw_stream
// static esp_err_t uac_output_cb(uint8_t *data, size_t data_len, void *arg) {
//     if (raw_writer) {
//         raw_stream_write(raw_writer, (char*)data, data_len);
//         return ESP_OK;
//     }
//     return ESP_FAIL;
// }
// static esp_err_t uac_input_cb(uint8_t *data, size_t data_len,size_t *bytes_read, void *arg) {
//     if (raw_recorder) {
//         int ret = raw_stream_read(raw_recorder, (char*)data, data_len);
//         if (ret > 0) {
//             *bytes_read = ret;
//             ESP_LOGI(TAG, "Read %d bytes from raw_recorder", ret);
//             return ESP_OK;
//         }
//     }
//     *bytes_read = 0;
//     ESP_LOGW(TAG, "No data read from raw_recorder");
//     return ESP_FAIL;
// }

// static void uac_set_volume_cb(uint32_t volume, void *arg) {  }

/* ---------- 测试任务 ---------- */
static void test_task(void *arg)
{
    while (1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    // 基础初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    led_service_init();
    wifi_service_init();
    audio_service_init();
    sntp_service_init();
    ui_service_init();
 
    
    // // 设置与USB匹配的音频格式
    // audio_element_info_t info = { .bits = 16, .channels = 2, .sample_rates = 48000 };    
    // // ================= USB 音频输出管线配置（播放） =================
    // // 2. 创建 raw_stream 元素（用于接收 USB 数据）
    // raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    // raw_cfg.type = AUDIO_STREAM_WRITER;
    // raw_cfg.out_rb_size = 8*1024; //
    // raw_writer = raw_stream_init(&raw_cfg);
    // // 3. 创建 i2s_stream 元素（用于播放）
    // i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(I2S_NUM_0, 48000, I2S_DATA_BIT_WIDTH_16BIT, AUDIO_STREAM_WRITER);
    // i2s_cfg.chan_cfg.dma_desc_num = 8; 
    // i2s_cfg.chan_cfg.dma_frame_num = 512; 
    // i2s_cfg.task_stack = 4*1024; //
    // i2s_cfg.buffer_len = 12*100; //
    // i2s_cfg.stack_in_ext = true; // 允许任务栈在 PSRAM（如果需要更大的栈）
    // audio_element_handle_t i2s_writer = i2s_stream_init(&i2s_cfg);
    // audio_element_setinfo(i2s_writer, &info);
    // audio_element_setinfo(raw_writer, &info);
    // // 4. 创建管道并连接 raw -> i2s
    // audio_pipeline_cfg_t pipe_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    // pipeline_player = audio_pipeline_init(&pipe_cfg);
    // audio_pipeline_register(pipeline_player, raw_writer, "raw");
    // audio_pipeline_register(pipeline_player, i2s_writer, "i2s");
    // const char *link_player[] = {"raw", "i2s"};
    // audio_pipeline_link(pipeline_player, link_player, 2);
    // audio_pipeline_run(pipeline_player);

    // // ================= USB 音频输入管线配置（录音） =================
    // // 1. 创建 I2S 输入流，与播放共享 I2S_NUM_0
    // i2s_stream_cfg_t i2s_read_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(
    //     I2S_NUM_0, 48000, I2S_DATA_BIT_WIDTH_16BIT, AUDIO_STREAM_READER);
    // i2s_read_cfg.uninstall_drv = true;
    // i2s_reader = i2s_stream_init(&i2s_read_cfg);
    // // 2. 创建 raw_stream 作为录音缓冲
    // raw_stream_cfg_t raw_cfg_rec = RAW_STREAM_CFG_DEFAULT();
    // raw_cfg_rec.type = AUDIO_STREAM_WRITER;   // 往缓冲里写录音数据
    // raw_cfg_rec.out_rb_size = 8 * 1024;
    // raw_recorder = raw_stream_init(&raw_cfg_rec);
    // audio_element_setinfo(i2s_reader, &info);
    // audio_element_setinfo(raw_recorder, &info);

    // // 4. 组建管线：i2s_reader → raw_recorder
    // audio_pipeline_cfg_t pipe_cfg_rec = DEFAULT_AUDIO_PIPELINE_CONFIG();
    // pipeline_recorder = audio_pipeline_init(&pipe_cfg);
    // audio_pipeline_register(pipeline_recorder, i2s_reader, "i2s_in");
    // audio_pipeline_register(pipeline_recorder, raw_recorder, "raw_mic");
    // const char *link_rec[] = { "i2s_in", "raw_mic" };
    // audio_pipeline_link(pipeline_recorder, link_rec, 2);
    // audio_pipeline_run(pipeline_recorder);

    // // 配置 UAC 设备（仅播放模式）
    // uac_device_config_t uac_cfg = {
    //     .output_cb = uac_output_cb,
    //     .input_cb = uac_input_cb,
    //     .cb_ctx = NULL,
    //     .set_volume_cb = uac_set_volume_cb,
    //     .set_mute_cb = NULL,
    // };
    // ret = uac_device_init(&uac_cfg);
    
    // // 播放uac
    // audio_service_receive_data_t *audio_payload = malloc(sizeof(audio_service_receive_data_t));
    // audio_payload->cmd                  = AUDIO_CMD_CONNECT;
    // audio_payload->prv_type             = usb_uac;                // 指定 USB 类型
    // audio_payload->midle_type           = raw_hal;
    // audio_payload->back_type            = i2s_hal;
    // audio_payload->volume               = 70;
    // audio_payload->start_after_connect  = true;                       // 立即开始
    // event_data_t *audio_evt = malloc(sizeof(event_data_t));
    // audio_evt->service_id  = HAL;
    // audio_evt->event_type  = REQUEST;
    // audio_evt->reply_queue = NULL;   // 接收状态通知
    // audio_evt->data        = audio_payload;
    // xQueueSend(get_audio_service_queue(), &audio_evt, 0);

    // // 录音uac
    // audio_payload = malloc(sizeof(audio_service_receive_data_t));
    // audio_payload->cmd                  = AUDIO_CMD_CONNECT;
    // audio_payload->prv_type             = i2s_hal;                // 指定 USB 类型
    // audio_payload->midle_type           = raw_hal;
    // audio_payload->back_type            = usb_uac;
    // audio_payload->volume               = 70;
    // audio_payload->start_after_connect  = true;                       // 立即开始
    // audio_evt = malloc(sizeof(event_data_t));
    // audio_evt->service_id  = HAL;
    // audio_evt->event_type  = REQUEST;
    // audio_evt->reply_queue = NULL;   // 接收状态通知
    // audio_evt->data        = audio_payload;
    // xQueueSend(get_audio_service_queue(), &audio_evt, 0);


    // // 发送 WiFi 连接请求（按需修改 SSID/密码）
    // wifi_service_receive_data_t *wifi_payload = malloc(sizeof(wifi_service_receive_data_t));
    // if (wifi_payload) {
    //     wifi_payload->cmd = WIFI_CMD_CONNECT;
    //     strcpy(wifi_payload->ssid, "ZTE_49A720");
    //     strcpy(wifi_payload->password, "1234567890");
    //     wifi_payload->save = true;
    // }
    // event_data_t *evt_data = malloc(sizeof(event_data_t));
    // if (evt_data) {
    //     evt_data->service_id = HAL;
    //     evt_data->event_type = REQUEST;
    //     evt_data->reply_queue = NULL;
    //     evt_data->data = wifi_payload;
    //     xQueueSend(get_wifi_service_queue(), &evt_data, 0);
    // }

    xTaskCreate(test_task, "test_task", 4096, NULL, 2, NULL);
    vTaskDelete(NULL);
}
// // main.c
// #include <stdio.h>
// #include<string.h>
// #include "sdkconfig.h"
// #include "esp_log.h"
// #include "nvs_flash.h"
// #include "driver/gpio.h"
// #include "usb_device_uac.h"
// // 
// #include "hal/tca9535_hal.h"
// #include "hal/lvgl_hal.h"
// #include "hal/sd_hal.h"
// // #include "hal/led_hal.h"
// #include "hal/keys_hal.h"
// // 
// #include "services/system_event.h"
// #include "services/wifi_service.h"
// #include "services/power_service.h"
// #include "services/sntp_service.h"
// #include "services/audio_service.h"
// #include "services/ui_service.h"
// #include "services/led_service.h"

// static const char *TAG = "app_main";

// // 测试任务
// static void test_task(void *arg)
// {
//     while(1){
//         vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
//         // led_service_receive_data_t* led_payload = (led_service_receive_data_t*)malloc(sizeof(led_service_receive_data_t));
//         // if(led_payload){
//         //     led_payload->device = LED_HAL_DEVICE_FRONT;
//         //     led_payload->mode = LED_MODE_ALERT;
//         //     led_payload->brightness = 80;
//         //     led_payload->arg = 0;
            
//         //     // 
//         //     event_data_t* led_event = (event_data_t*)malloc(sizeof(event_data_t));
//         //     if(led_event){
//         //         led_event->service_id = HAL;
//         //         led_event->event_type = REQUEST;
//         //         led_event->data = led_payload;
//         //         led_event->data_len = sizeof(led_service_receive_data_t);
//         //         xQueueSend(get_led_service_queue(), &led_event, 0);
//         //     } else{
//         //         ESP_LOGE(TAG, "malloc led_event failed");
//         //         free(led_payload);
//         //     }
//         // }
//         // ESP_LOGI(TAG, "test_task");
//         // // 切换到呼吸模式
//         // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_BREATH, 80, 0);
//         // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
//         // // 启动番茄时钟
//         // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_CLOCK, 80, 10);
//         // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
//         // // 模拟音量变化
//         // int i = 50;
//         // while( i < 100){
//         //     led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_VOLUME, 80, i);
//         //     i++;
//         //     vTaskDelay(100 / portTICK_PERIOD_MS); // 0.1s
//         // }
//         // // 切回音乐模式
//         // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_MUSIC, 250, 0);
//         // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s        
//         // // 触发警报
//         // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_ALERT, 80, 50);
//         // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
//         // // 切回音乐模式
//         // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_MUSIC, 250, 0);
//         // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
//         // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_BULB, 250, 0);
//         // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
//         // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_RUN, 80, 0);
//         // vTaskDelay(10000 / portTICK_PERIOD_MS); // 10s
//         // led_hal_set_panel_mode(LED_HAL_DEVICE_FRONT, LED_MODE_OFF, 80, 0);
//     }
// }

// #include "audio_element.h"
// #include "i2s_stream.h"
// static audio_element_handle_t s_i2s_writer = NULL;
// // USB 音频输出回调
// static esp_err_t uac_device_output_cb(uint8_t *buf, size_t len, void *arg)
// {
//     // 使用 audio_element_output 向 I2S 流元素写入数据
//     int written = audio_element_output(s_i2s_writer, (char *)buf, len);
//     if (written != len) {
//        // ESP_LOGW(TAG, "audio_element_output short: %d/%d", written, len);
//     }
//     return ESP_OK;
// }

// // 可选的回调（静音、音量）
// static void uac_device_set_mute_cb(uint32_t mute, void *arg) {
//    // ESP_LOGI(TAG, "Mute: %d", mute);
// }
// static void uac_device_set_volume_cb(uint32_t volume, void *arg) {
//    // ESP_LOGI(TAG, "Volume: %d", volume);
// }




// void app_main(void)
// {
//     // ================系统初始化
//     //  NVS
//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ret = nvs_flash_init();
//     }
//     ESP_ERROR_CHECK(ret); 

//     led_service_init(); // 初始化LED服务
//     //vTaskDelay(1000 / portTICK_PERIOD_MS); // 1s
//     wifi_service_init();
//     //vTaskDelay(1000 / portTICK_PERIOD_MS); // 1s
//     audio_service_init(); // 初始化了IIC（I2C_NUM_0）和IIS，并初始化了tca9535 IO扩展芯片（IIC通信）
//     //vTaskDelay(1000 / portTICK_PERIOD_MS); // 1s
//     sntp_service_init(); // 初始化了NTP客户端(需要WiFi连接) 
//     //vTaskDelay(1000 / portTICK_PERIOD_MS); // 1s
//     ui_service_init(); // 注释里面含iic初始化，里面初始化通用按键key_hal_init();
//     //  power_service_init();
   
//     // 2. 创建 I2S 写入流（播放）
//     i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_TYLE_AND_CH(
//         I2S_NUM_0, 44100, I2S_DATA_BIT_WIDTH_16BIT,
//         AUDIO_STREAM_WRITER, I2S_SLOT_MODE_MONO
//     );
//     // 如果 audio_service 已经初始化了 I2S 硬件，这里不要改变 i2s_port 和 i2s_config
//     // 通常使用默认值即可（会沿用已有配置）
//     s_i2s_writer = i2s_stream_init(&i2s_cfg);
//     if (!s_i2s_writer) {
//         ESP_LOGE(TAG, "Failed to create I2S writer");
//         return;
//     }
//     // 启动 I2S 流元素（必须！）
//     audio_element_run(s_i2s_writer);
//     ESP_LOGI(TAG, "I2S writer created and running");

//     // 3. 初始化 USB 音频设备（确保内部已初始化 TinyUSB）
//     uac_device_config_t config = {
//         .output_cb = uac_device_output_cb,
//         .input_cb = NULL,
//         .set_mute_cb = uac_device_set_mute_cb,
//         .set_volume_cb = uac_device_set_volume_cb,
//         .cb_ctx = NULL,
//     };
//     ret = uac_device_init(&config);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "uac_device_init failed: %d", ret);
//         return;
//     }

//     // 请求一次WiFi服务
//     wifi_service_receive_data_t *wifi_payload = (wifi_service_receive_data_t*)malloc(sizeof(wifi_service_receive_data_t));
//     if(wifi_payload){
//         // 分配WiFi信息
//         wifi_payload->cmd = WIFI_CMD_CONNECT;
//         strcpy(wifi_payload->ssid,"MEIZU 21 Pro");
//         strcpy(wifi_payload->password,"1234567890");
//         wifi_payload->save = true;
//     }
//     event_data_t *evt_data = malloc(sizeof(event_data_t));
//     if(evt_data){
//         evt_data->service_id = HAL; //
//         evt_data->event_type = REQUEST;
//         evt_data->reply_queue = NULL; // 不需要回复
//         evt_data->data = wifi_payload;
//         ESP_LOGI(TAG,"req wifi conect");
//         xQueueSend(get_wifi_service_queue(), &evt_data, 0);
//     }

//     // // 请求一次audio服务
//     // audio_service_receive_data_t *audio_payload = (audio_service_receive_data_t*)malloc(sizeof(audio_service_receive_data_t));
//     // if(audio_payload){
//     //     // 分配音频信息
//     //     audio_payload->cmd = AUDIO_CMD_CONNECT;
//     // }
//     // event_data_t* evt_audio_data = malloc(sizeof(event_data_t));
//     // if(evt_audio_data){
//     //     evt_audio_data->service_id = HAL; //
//     //     evt_audio_data->event_type = REQUEST;
//     //     evt_audio_data->reply_queue = NULL; // 不需要回复
//     //     evt_audio_data->data = audio_payload;
//     //     ESP_LOGI(TAG,"req audio conect");
//     //     xQueueSend(get_audio_service_queue(), &evt_audio_data, 0);
//     // }   

   
//     // 启动test
//     xTaskCreate(test_task, "test_task", 4096, NULL,2, NULL);


//     // 删除main任务
//     vTaskDelete(NULL);
// }