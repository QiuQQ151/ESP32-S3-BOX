// led_service.c
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "led_service.h"
#include "led_hal.h"
#include "system_event.h"
#include "system_config.h"

// =================== 显示时长参数 ===================
#define BREATH_PERIOD_MS      4000  // 呼吸周期
#define RUN_SPEED_MS          100   // 跑马灯速度
#define VOLUME_DISPLAY_MS     2000  // 音量显示时长
#define ALERT_BLINK_MS        300   // 闪烁间隔
#define ALERT_TIMES           2     // 闪烁次数
// =================== 音乐模式参数 ===================
#define TIME_SPEED_R          0.3f
#define PERIOD_R              10.0f
#define PHASE_R               0.0f
#define TIME_SPEED_G          0.5f
#define PERIOD_G              27.0f
#define PHASE_G               2.0f
#define TIME_SPEED_B          0.7f
#define PERIOD_B              33.0f
#define PHASE_B               4.0f

static const char *TAG = "led_service";

static QueueHandle_t  led_service_request_queue = NULL;
static TaskHandle_t   led_render_task_handle   = NULL;  // 渲染任务句柄
static TaskHandle_t   led_service_task_handle  = NULL;  // 服务任务句柄
static bool           led_service_initialized  = false;

/* ---------- 面板状态 ---------- */
typedef struct {
    bool       is_initialized;
    led_mode_t mode;
    uint8_t    brightness;
    led_mode_t prev_mode;
    uint32_t   clock_total_sec;
    TickType_t clock_start_tick;
    TickType_t volume_end_tick;
    TickType_t alert_end_tick;
    uint8_t    volume_level;
    uint8_t    volume_brightness;
    uint8_t    alert_brightness;
    uint32_t   solid_color;          
} panel_state_t;

// 设置初始参数
static panel_state_t led_panel[LED_HAL_DEVICE_MAX] = {
    { .mode = LED_MODE_MUSIC, .brightness = 10, .solid_color = 0x0000FF00 },
    { .mode = LED_MODE_BREATH, .brightness = 10, .solid_color = 0x0000FF00 },
};

static portMUX_TYPE led_mode_mux = portMUX_INITIALIZER_UNLOCKED;

// 填充指定设备
static void fill_count_with_brightness( led_hal_device_t dev, uint32_t count, uint8_t brightness,
                                        uint8_t r, uint8_t g, uint8_t b)
{
    if (!led_hal_is_ready(dev)) return;
    uint16_t dev_count = led_hal_get_count(dev);
    for (int i = 0; i < dev_count; i++) {
        if (i < count) {
            led_hal_set_pixel(dev, i,
                              (uint8_t)((r * brightness) / 255),
                              (uint8_t)((g * brightness) / 255),
                              (uint8_t)((b * brightness) / 255));
        } else {
            led_hal_set_pixel(dev, i, 0, 0, 0);
        }
    }
    led_hal_refresh(dev);
}

// 音乐模式计算
static inline float sin01(float x) {
    return sinf(x) * 0.5f + 0.5f;
}

static void calc_color(float time_sec, int index, float brightness,
                       uint8_t *r, uint8_t *g, uint8_t *b)
{
    float idx = (float)index;
    float val_r = sin01(2.0f * M_PI * (idx / PERIOD_R + TIME_SPEED_R * time_sec) + PHASE_R);
    float val_g = sin01(2.0f * M_PI * (idx / PERIOD_G + TIME_SPEED_G * time_sec) + PHASE_G);
    float val_b = sin01(2.0f * M_PI * (idx / PERIOD_B + TIME_SPEED_B * time_sec) + PHASE_B);
    *r = (uint8_t)(val_r * brightness);
    *g = (uint8_t)(val_g * brightness);
    *b = (uint8_t)(val_b * brightness);
}

static void render_music(led_hal_device_t dev, float time_sec, uint8_t brightness)
{
    if (!led_hal_is_ready(dev)) return;
    uint16_t count = led_hal_get_count(dev);
    for (int i = 0; i < count; i++) {
        uint8_t r, g, b;
        calc_color(time_sec, i, brightness, &r, &g, &b);
        led_hal_set_pixel(dev, i, r, g, b);
    }
    led_hal_refresh(dev);
}

// 各模式渲染
static void render_off_panel(led_hal_device_t dev)
{
    uint16_t count = led_hal_get_count(dev);
    fill_count_with_brightness(dev, count, 0, 0, 0, 0);
}

static void render_breath_panel(led_hal_device_t dev, uint8_t brightness, uint32_t solid_color)
{
    if (!led_hal_is_ready(dev)) return;
    uint16_t count = led_hal_get_count(dev);
    uint32_t elapsed = xTaskGetTickCount() * portTICK_PERIOD_MS;
    float phase = (float)(elapsed % BREATH_PERIOD_MS) / BREATH_PERIOD_MS;
    float b = (sinf(phase * 2.0f * M_PI) + 1.0f) / 2.0f;
    float r = ((solid_color >> 16) & 0xFF);
    float g = ((solid_color >> 8) & 0xFF);
    float bl = (solid_color & 0xFF);

    fill_count_with_brightness(dev, count, b * brightness, (uint8_t)r, (uint8_t)g, (uint8_t)bl);
}

static void render_bulb_panel(led_hal_device_t dev, uint8_t brightness, uint32_t color)
{
    if (!led_hal_is_ready(dev)) return;
    uint16_t count = led_hal_get_count(dev);
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    fill_count_with_brightness(dev, count, brightness, (uint8_t)r, (uint8_t)g, (uint8_t)b);
    led_hal_refresh(dev);
}

static void render_clock_panel(led_hal_device_t dev, uint8_t brightness)
{
    panel_state_t *p = &led_panel[dev];
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed_sec = (now - p->clock_start_tick) * portTICK_PERIOD_MS / 1000;

    if (elapsed_sec >= p->clock_total_sec) {
        taskENTER_CRITICAL(&led_mode_mux);
        p->mode = LED_MODE_BREATH;
        p->brightness = 40;
        p->prev_mode = LED_MODE_BREATH;
        taskEXIT_CRITICAL(&led_mode_mux);
        return;
    }

    float progress = (float)elapsed_sec / p->clock_total_sec;
    uint16_t count = led_hal_get_count(dev);
    uint32_t lit = (uint32_t)(progress * count);
    fill_count_with_brightness(dev, lit, brightness, 255, 200, 100);
}

static void render_run_panel(led_hal_device_t dev, uint8_t brightness)
{
    static uint32_t last_tick = 0;
    static int pos = 0;
    TickType_t now = xTaskGetTickCount();
    if (now - last_tick >= pdMS_TO_TICKS(RUN_SPEED_MS)) {
        last_tick = now;
        pos = (pos + 1) % led_hal_get_count(dev);
    }
    if (!led_hal_is_ready(dev)) return;
    uint16_t count = led_hal_get_count(dev);
    for (int i = 0; i < count; i++) {
        uint8_t v = (i == pos) ? brightness : 0;
        led_hal_set_pixel(dev, i, v, v, v);
    }
    led_hal_refresh(dev);
}

static void render_volume_panel(led_hal_device_t dev, uint8_t brightness)
{
    panel_state_t *p = &led_panel[dev];
    TickType_t now = xTaskGetTickCount();
    if (now >= p->volume_end_tick) {
        taskENTER_CRITICAL(&led_mode_mux);
        p->mode = p->prev_mode;
        taskEXIT_CRITICAL(&led_mode_mux);
        return;
    }
    uint16_t dev_count = led_hal_get_count(dev);
    uint32_t count = (uint32_t)(p->volume_level * dev_count / 100.0f);
    if (count > dev_count) count = dev_count;
    fill_count_with_brightness(dev, count, brightness, 0, 255, 0);
}

static void render_alert_panel(led_hal_device_t dev, uint8_t brightness)
{
    panel_state_t *p = &led_panel[dev];
    TickType_t now = xTaskGetTickCount();
    // 恢复先前模式
    if (now >= p->alert_end_tick) {
        taskENTER_CRITICAL(&led_mode_mux);
        led_mode_t restore = p->prev_mode;
        if (restore == LED_MODE_CLOCK) {
            uint32_t elapsed = (now - p->clock_start_tick) * portTICK_PERIOD_MS / 1000;
            if (elapsed >= p->clock_total_sec) {
                restore = LED_MODE_BREATH;
                p->brightness = 10;
            }
        }
        p->mode = restore;
        taskEXIT_CRITICAL(&led_mode_mux);
        return;
    }
    uint32_t phase_ms = (now * portTICK_PERIOD_MS) % (ALERT_BLINK_MS * 2);
    bool on = (phase_ms < ALERT_BLINK_MS);
    uint8_t led_bright = on ? brightness : 0;
    fill_count_with_brightness(dev, led_hal_get_count(dev), led_bright, 255, 0, 0);
}


/* ---------- 统一渲染任务---------- */
static void led_service_render_task(void *arg)
{
    TickType_t start_tick = xTaskGetTickCount();
    while (1) { 
        vTaskDelay(pdMS_TO_TICKS(40)); // 10Hz
        float time_sec = (float)((xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS) / 1000.0f;

        // 对每一个led面板进行渲染
        for (int d = 0; d < LED_HAL_DEVICE_MAX; d++) {
            if (!led_hal_is_ready(d)) continue;

            led_mode_t mode;
            uint8_t brightness;
            uint32_t solid_color;
            taskENTER_CRITICAL(&led_mode_mux);
            mode        = led_panel[d].mode;
            brightness  = led_panel[d].brightness;
            solid_color = led_panel[d].solid_color;
            taskEXIT_CRITICAL(&led_mode_mux);

            switch (mode) {
                case LED_MODE_OFF:
                    render_off_panel(d);
                    break;
                case LED_MODE_BREATH: // 呼吸
                    render_breath_panel(d, brightness, solid_color);
                    break;
                case LED_MODE_BULB: // 灯泡
                    render_bulb_panel(d, brightness, solid_color);
                    break;
                case LED_MODE_MUSIC: // 音乐
                    render_music(d, time_sec + ((d == LED_HAL_DEVICE_EXTENSION) ? 1.5f : 0.0f), brightness);
                    break;
                case LED_MODE_RUN: // 跑马灯
                    render_run_panel(d, brightness);
                    break;
                case LED_MODE_CLOCK: // 时钟
                    render_clock_panel(d, brightness);
                    break;
                case LED_MODE_VOLUME: // 音量
                    render_volume_panel(d, brightness);
                    break;
                case LED_MODE_ALERT:  // 警告
                    render_alert_panel(d, brightness);
                    break;
            }
        }
    }
}

// 面板操作
static esp_err_t led_hal_set_panel_mode(led_hal_device_t dev, led_mode_t mode,
                                        uint8_t brightness, uint32_t arg)
{
    if (!led_service_initialized){
        ESP_LOGE(TAG, "LED service not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (dev >= LED_HAL_DEVICE_MAX){
        ESP_LOGE(TAG, "Invalid device, check your device ID");
        return ESP_ERR_INVALID_ARG;
    }
    if (brightness == 0) brightness = 1;

    taskENTER_CRITICAL(&led_mode_mux);
    panel_state_t *p = &led_panel[dev];
    
    // 对指定面板进行参数设定
    switch (mode) {
        case LED_MODE_CLOCK: {
            // 时钟模式
            uint32_t total = arg;
            if (total == 0) total = 1;
            if (total > 3600) total = 3600;
            p->clock_total_sec = total;
            p->clock_start_tick = xTaskGetTickCount();
            p->mode = LED_MODE_CLOCK;
            p->brightness = brightness;
            p->prev_mode = LED_MODE_CLOCK;
            break;
        }
        case LED_MODE_VOLUME: {
            // 音量模式
            if (p->mode != LED_MODE_VOLUME && p->mode != LED_MODE_ALERT)
                p->prev_mode = p->mode;
            p->volume_level = (uint8_t)arg;
            p->volume_brightness = brightness;
            p->volume_end_tick = xTaskGetTickCount() + pdMS_TO_TICKS(VOLUME_DISPLAY_MS);
            p->mode = LED_MODE_VOLUME;
            break;
        }
        case LED_MODE_ALERT: {
            // 警告模式
            if (p->mode != LED_MODE_VOLUME && p->mode != LED_MODE_ALERT)
                p->prev_mode = p->mode;
            p->alert_brightness = brightness;
            p->alert_end_tick = xTaskGetTickCount() + pdMS_TO_TICKS(ALERT_BLINK_MS * 2 * ALERT_TIMES);
            p->mode = LED_MODE_ALERT;
            break;
        }
        default: {
            // 其他模式
            p->mode = mode;
            p->brightness = brightness;
            p->prev_mode = mode;
            break;
        }
    }

    taskEXIT_CRITICAL(&led_mode_mux);
    ESP_LOGI(TAG, "led_panel %d -> mode %d, brightness %d", dev, mode, brightness);
    return ESP_OK;
}

// 服务任务：分发请求
static void led_service_task(void *arg)
{
    event_data_t *evt_data;
    while (1) {
        if (xQueueReceive(led_service_request_queue, &evt_data, portMAX_DELAY) == pdTRUE) {
            // 处理请求
            if (evt_data->event_type == REQUEST) {
                led_service_receive_data_t *payload = (led_service_receive_data_t *)evt_data->data;
                if (payload) {
                    ESP_LOGI(TAG, "led service req: dev:%d mode:%d", payload->device, payload->mode);
                    (void)led_hal_set_panel_mode(payload->device, payload->mode,
                                                 payload->brightness, payload->arg);
                }
            } 
            // 处理通知
            else if (evt_data->event_type == NOTIFICATION){
                ESP_LOGI(TAG, "led service notify");   
            }
            // 错误
            else{
                ESP_LOGE(TAG, "led service unsupported event type %d", evt_data->event_type);
            }
            // 清理
            if (evt_data->data) {
                free(evt_data->data);
                evt_data->data = NULL;
            }
            if (evt_data)
            {
                free(evt_data);
                evt_data = NULL;
            }

        }
    }
}

// ===============================对外接口================================================
esp_err_t led_service_init(void)
{
    if (led_service_initialized) return ESP_OK;

    // 初始化 HAL 层
    esp_err_t ret = led_hal_init(LED_HAL_DEVICE_FRONT, LED_FRONT_GPIO, DEFAULT_FRONT_COUNT);
    if (ret == ESP_OK) {
        led_panel[LED_HAL_DEVICE_FRONT].is_initialized = true;
    } 
    ret = led_hal_init(LED_HAL_DEVICE_EXTENSION, LED_EXTENSION_GPIO, DEFAULT_EXTENSION_COUNT);
    if (ret == ESP_OK) {
        led_panel[LED_HAL_DEVICE_EXTENSION].is_initialized = true;
    } 

    // 创建渲染任务
    if (xTaskCreate(led_service_render_task, "led_service_render_task", 4096, NULL, TASK_PRIO_LED_SERVICE, &led_render_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "led_service_render_task create fail");
        return ESP_FAIL;
    }

    // 创建服务请求队列
    led_service_request_queue = xQueueCreate(QUEUE_SERVICE_SIZE, sizeof(event_data_t *));
    if (!led_service_request_queue) {
        ESP_LOGE(TAG, "led_service_request_queue create fail");
        vTaskDelete(led_render_task_handle);
        return ESP_FAIL;
    }

    // 创建服务分发任务
    if (xTaskCreatePinnedToCore(led_service_task, "led_service_task", 4096, NULL, TASK_PRIO_LED_SERVICE, &led_service_task_handle, TASK_CORE_SERVICE) != pdPASS) {
        ESP_LOGE(TAG, "led_service_task create fail");
        vTaskDelete(led_render_task_handle);
        vQueueDelete(led_service_request_queue);
        return ESP_FAIL;
    }

    led_service_initialized = true;
    ESP_LOGI(TAG, "LED service ready");
    return ESP_OK;
}

QueueHandle_t get_led_service_queue(void)
{
    return led_service_request_queue;
}