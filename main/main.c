#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
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
#include "services/sntp_service.h"
#include "services/audio_service.h"
#include "services/ui_service.h"
#include "services/led_service.h"

static const char *TAG = "app_main";

// debug
void system_monitor_task(void *arg)
{
    static const char *state_name[] = {
        [eRunning]   = "运行",
        [eReady]     = "就绪",
        [eBlocked]   = "阻塞",
        [eSuspended] = "挂起",
        [eDeleted]   = "删除"
    };

    const UBaseType_t max_tasks = 32;
    TaskStatus_t *task_array = malloc(max_tasks * sizeof(TaskStatus_t));
    if (!task_array) {
        printf("MONITOR: malloc failed\n");
        vTaskDelete(NULL);
        return;
    }

    // 使用静态大缓冲区，避免栈开销，且容量足以容纳所有任务信息
    static char buf[2048];

    unsigned long prev_total_core0 = 0, prev_idle_core0 = 0;
    unsigned long prev_total_core1 = 0, prev_idle_core1 = 0;

    while (1) {
        // ===== 内存统计（无需挂起调度器） =====
        uint32_t free_heap  = esp_get_free_heap_size() / 1024;
        uint32_t free_dram  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
        uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
        size_t largest_dram  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024;
        size_t largest_psram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024;

        printf("\n============ 系统内存 ============\n");
        printf("  总可用堆: %" PRIu32 " KB\n", free_heap);
        printf("  内部 DRAM 空闲: %" PRIu32 " KB, 最大连续块: %u KB\n",
               free_dram, (unsigned)largest_dram);
        if (free_psram > 0) {
            printf("  PSRAM 空闲: %" PRIu32 " KB, 最大连续块: %u KB\n",
                   free_psram, (unsigned)largest_psram);
        }
        printf("==================================\n");

        // ===== 任务统计（挂起调度器期间只做数据采集，不使用 printf） =====
        vTaskSuspendAll();
        UBaseType_t count = uxTaskGetSystemState(task_array, max_tasks, NULL);

        int pos = 0;
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "\n| 任务名            | 状态 | 优先级 | 核心 | 剩余栈(KB) | 高水位(字) | 任务号 |\n");
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "|-------------------|------|--------|------|------------|------------|--------|\n");

        for (UBaseType_t i = 0; i < count; i++) {
            TaskStatus_t *ts = &task_array[i];

            // 跳过无效或已删除的任务
            if (ts->eCurrentState == eDeleted || ts->xHandle == NULL) {
                continue;
            }
            // 任务名可能被破坏的额外检查
            if (ts->pcTaskName == NULL || ts->pcTaskName[0] == '\0') {
                continue;
            }

            UBaseType_t high_water = uxTaskGetStackHighWaterMark(ts->xHandle);
            float stack_kb = (high_water * sizeof(StackType_t)) / 1024.0f;
            int core = ts->xCoreID;

            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "| %-17s | %-4s | %-6u | ",
                            ts->pcTaskName, state_name[ts->eCurrentState],
                            (unsigned)ts->uxCurrentPriority);
            if (core >= 0 && core <= 2)
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%-4d | ", core);
            else
                pos += snprintf(buf + pos, sizeof(buf) - pos, " ?   | ");
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "%-10.2f | %-10u | %-6u |\n",
                            stack_kb, (unsigned)high_water, (unsigned)ts->xTaskNumber);

            // 防止缓冲区溢出
            if (pos >= sizeof(buf) - 1) {
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                                "... (输出截断)\n");
                break;
            }
        }

        // CPU 利用率数据采集（仍在挂起状态下完成）
        unsigned long total_core0 = 0, total_core1 = 0;
        unsigned long idle_core0 = 0, idle_core1 = 0;

        for (UBaseType_t i = 0; i < count; i++) {
            TaskStatus_t *ts = &task_array[i];
            if (ts->eCurrentState == eDeleted || ts->xHandle == NULL) continue;
            if (ts->pcTaskName == NULL || ts->pcTaskName[0] == '\0') continue;

            if (ts->xCoreID == 0) {
                total_core0 += ts->ulRunTimeCounter;
                if (strcmp(ts->pcTaskName, "IDLE0") == 0)
                    idle_core0 = ts->ulRunTimeCounter;
            } else if (ts->xCoreID == 1) {
                total_core1 += ts->ulRunTimeCounter;
                if (strcmp(ts->pcTaskName, "IDLE1") == 0)
                    idle_core1 = ts->ulRunTimeCounter;
            }
        }

        xTaskResumeAll();   // 恢复调度器，之后可以安全使用 printf

        // 输出任务列表
        printf("%s", buf);

        // 计算并输出 CPU 利用率
        unsigned long delta_total0 = total_core0 - prev_total_core0;
        unsigned long delta_idle0 = idle_core0 - prev_idle_core0;
        unsigned long delta_total1 = total_core1 - prev_total_core1;
        unsigned long delta_idle1 = idle_core1 - prev_idle_core1;

        float usage0 = 0.0f, usage1 = 0.0f;
        if (delta_total0 > 0)
            usage0 = (1.0f - (float)delta_idle0 / delta_total0) * 100.0f;
        if (delta_total1 > 0)
            usage1 = (1.0f - (float)delta_idle1 / delta_total1) * 100.0f;

        prev_total_core0 = total_core0;
        prev_idle_core0 = idle_core0;
        prev_total_core1 = total_core1;
        prev_idle_core1 = idle_core1;

        printf("\n============ CPU 利用率 ============\n");
        printf("  Core 0: %.1f%%\n", usage0);
        printf("  Core 1: %.1f%%\n", usage1);
        printf("====================================\n\n");

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* ---------- 测试任务 ---------- */
static void test_task(void *arg)
{
    while (1) {
        // uint32_t light_dac = lvgl_hal_get_light_adc();
        // lvgl_hal_set_brightness( (uint8_t)(light_dac) );
        vTaskDelay(1000 / portTICK_PERIOD_MS);
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

    audio_service_init(); //i2c->tca9535->power_hal
    led_service_init();
    wifi_service_init();
    sntp_service_init();
    ui_service_init();

    // 发送 WiFi 连接请求（按需修改 SSID/密码）
    wifi_service_receive_data_t *wifi_payload = malloc(sizeof(wifi_service_receive_data_t));
    if (wifi_payload) {
        wifi_payload->cmd = WIFI_CMD_CONNECT;
        strcpy(wifi_payload->ssid, "WIN10-HFMP");
        strcpy(wifi_payload->password, "1234567890");
        wifi_payload->save = true;
    }
    event_data_t *evt_data = malloc(sizeof(event_data_t));
    if (evt_data) {
        evt_data->service_id = HAL;
        evt_data->event_type = REQUEST;
        evt_data->reply_queue = NULL;
        evt_data->data = wifi_payload;
        xQueueSend(get_wifi_service_queue(), &evt_data, 0);
    }

    // vTaskDelay(10000 / portTICK_PERIOD_MS);
    // // 播放音频
    // audio_service_receive_data_t* audio_payload = malloc(sizeof(audio_service_receive_data_t));
    // audio_payload->cmd = AUDIO_CMD_CONNECT;
    // strcpy(audio_payload->url, "http://lhttp.qingting.fm/live/4915/64k.mp3");
    // audio_payload->prv_type = http_str;
    // audio_payload->midle_type = mp3_dec;
    // audio_payload->back_type = i2s_hal;
    // audio_payload->volume = 50;
    // audio_payload->start_after_connect = true;

    // event_data_t *audio_evt_data = malloc(sizeof(event_data_t));
    // audio_evt_data->service_id = UI_SERVICE;
    // audio_evt_data->event_type = REQUEST;
    // audio_evt_data->reply_queue = NULL;
    // audio_evt_data->data = audio_payload;
    // audio_evt_data->data_len = sizeof(audio_service_receive_data_t);
    // xQueueSend(get_audio_service_queue(), &audio_evt_data, 0);

    //xTaskCreatePinnedToCore(test_task, "test_task", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(system_monitor_task, "monitor", 4096, NULL, 1, NULL, 1);
    vTaskDelete(NULL);
}