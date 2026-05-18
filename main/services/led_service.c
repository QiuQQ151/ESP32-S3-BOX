#include <math.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_event.h"
#include "led_service.h"

// =================== 硬件配置 ===================
#define LED_FRONT_GPIO       7
#define LED_EXTENSION_GPIO   8
#define LED_FRONT_COUNT      30
#define LED_EXTENSION_COUNT  10

// =================== 音乐模式参数 ===================
#define TIME_SPEED_R        0.3f
#define PERIOD_R            10.0f
#define PHASE_R             0.0f
#define TIME_SPEED_G        0.5f
#define PERIOD_G            27.0f
#define PHASE_G             2.0f
#define TIME_SPEED_B        0.7f
#define PERIOD_B            33.0f
#define PHASE_B             4.0f

// =================== 其他参数 ===================
#define BREATH_PERIOD_MS    4000
#define RUN_SPEED_MS        100
#define VOLUME_DISPLAY_MS   3000
#define ALERT_BLINK_MS      500
#define ALERT_TIMES         3

static const char *TAG = "led_hal";

static led_strip_handle_t strips[LED_HAL_DEVICE_MAX] = {NULL, NULL};
static bool initialized = false;
static QueueHandle_t led_service_request_queue = NULL;
static TaskHandle_t led_service_task_handle = NULL;
static TaskHandle_t led_hal_task_handle = NULL;

typedef struct {
    led_mode_t mode;
    uint8_t brightness;
    led_mode_t prev_mode;
    uint32_t clock_total_sec;
    TickType_t clock_start_tick;
    TickType_t volume_end_tick;
    TickType_t alert_end_tick;
    uint8_t volume_level;
    uint8_t volume_brightness;
    uint8_t alert_brightness;
} panel_state_t;

static panel_state_t panel[LED_HAL_DEVICE_MAX] = {
    { .mode = LED_MODE_RUN, .brightness = 40 },
    { .mode = LED_MODE_BREATH, .brightness = 40 },
};

static portMUX_TYPE mode_mux = portMUX_INITIALIZER_UNLOCKED;

#define SAFE_EXT_LOOP(code) \
    if (strips[LED_HAL_DEVICE_EXTENSION] != NULL) { \
        for (int i = 0; i < LED_EXTENSION_COUNT; i++) { \
            code; \
        } \
        led_strip_refresh(strips[LED_HAL_DEVICE_EXTENSION]); \
    }

// =================== 辅助函数 ===================
static void fill_strip(led_strip_handle_t strip, uint32_t count,
                       uint8_t r, uint8_t g, uint8_t b)
{
    if (!strip || count == 0) return;
    for (int i = 0; i < count; i++) {
        led_strip_set_pixel(strip, i, g, r, b);
    }
    led_strip_refresh(strip);
}

static void fill_front_count_with_brightness(uint8_t r, uint8_t g, uint8_t b,
                                             uint32_t count, uint8_t brightness)
{
    for (int i = 0; i < LED_FRONT_COUNT; i++) {
        if (i < count) {
            led_strip_set_pixel(strips[LED_HAL_DEVICE_FRONT], i,
                                (g * brightness) / 255,
                                (r * brightness) / 255,
                                (b * brightness) / 255);
        } else {
            led_strip_set_pixel(strips[LED_HAL_DEVICE_FRONT], i, 0, 0, 0);
        }
    }
    led_strip_refresh(strips[LED_HAL_DEVICE_FRONT]);
    SAFE_EXT_LOOP(
        led_strip_set_pixel(strips[LED_HAL_DEVICE_EXTENSION], i, 0, 0, 0);
    );
}

// =================== 音乐模式 ===================
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

static void render_music(led_strip_handle_t strip, uint32_t count,
                         float time_sec, uint8_t brightness)
{
    if (!strip || count == 0) return;
    for (int i = 0; i < count; i++) {
        uint8_t r, g, b;
        calc_color(time_sec, i, brightness, &r, &g, &b);
        led_strip_set_pixel(strip, i, g, r, b);
    }
    led_strip_refresh(strip);
}

// =================== 各模式渲染 ===================
static void render_off_panel(led_hal_device_t dev)
{
    uint32_t count = (dev == LED_HAL_DEVICE_FRONT) ? LED_FRONT_COUNT : LED_EXTENSION_COUNT;
    fill_strip(strips[dev], count, 0, 0, 0);
}

static void render_breath_panel(led_hal_device_t dev, uint8_t brightness)
{
    uint32_t count = (dev == LED_HAL_DEVICE_FRONT) ? LED_FRONT_COUNT : LED_EXTENSION_COUNT;
    uint32_t elapsed = xTaskGetTickCount() * portTICK_PERIOD_MS;
    float phase = (float)(elapsed % BREATH_PERIOD_MS) / BREATH_PERIOD_MS;
    float b = (sinf(phase * 2.0f * M_PI) + 1.0f) / 2.0f;
    uint8_t w = (uint8_t)(b * brightness);
    fill_strip(strips[dev], count, w, w, w);
}

static void render_bulb_panel(led_hal_device_t dev, uint8_t brightness)
{
    uint32_t count = (dev == LED_HAL_DEVICE_FRONT) ? LED_FRONT_COUNT : LED_EXTENSION_COUNT;
    fill_strip(strips[dev], count, brightness, brightness, brightness);
}

static void render_clock_panel(void)
{
    panel_state_t *p = &panel[LED_HAL_DEVICE_FRONT];
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed_sec = (now - p->clock_start_tick) * portTICK_PERIOD_MS / 1000;

    if (elapsed_sec >= p->clock_total_sec) {
        taskENTER_CRITICAL(&mode_mux);
        p->mode = LED_MODE_BREATH;
        p->brightness = 40;
        p->prev_mode = LED_MODE_BREATH;
        taskEXIT_CRITICAL(&mode_mux);
        return;
    }

    float progress = (float)elapsed_sec / p->clock_total_sec;
    uint32_t lit = (uint32_t)(progress * LED_FRONT_COUNT);
    fill_front_count_with_brightness(255, 200, 100, lit, p->brightness);
}

static void render_run_panel(led_hal_device_t dev, uint8_t brightness)
{
    static uint32_t last_tick = 0;
    static int pos = 0;
    TickType_t now = xTaskGetTickCount();
    if (now - last_tick >= pdMS_TO_TICKS(RUN_SPEED_MS)) {
        last_tick = now;
        pos = (pos + 1) % LED_FRONT_COUNT;
    }
    uint32_t count = (dev == LED_HAL_DEVICE_FRONT) ? LED_FRONT_COUNT : LED_EXTENSION_COUNT;
    for (int i = 0; i < count; i++) {
        uint8_t v = (i == pos) ? brightness : 0;
        led_strip_set_pixel(strips[dev], i, v, v, v);
    }
    led_strip_refresh(strips[dev]);
}

static void render_volume_panel(void)
{
    panel_state_t *p = &panel[LED_HAL_DEVICE_FRONT];
    TickType_t now = xTaskGetTickCount();
    if (now >= p->volume_end_tick) {
        taskENTER_CRITICAL(&mode_mux);
        p->mode = p->prev_mode;
        taskEXIT_CRITICAL(&mode_mux);
        return;
    }
    uint32_t count = (uint32_t)(p->volume_level  * LED_FRONT_COUNT / 100.0f);
    if (count > LED_FRONT_COUNT) count = LED_FRONT_COUNT;
    for (int i = 0; i < LED_FRONT_COUNT; i++) {
        uint8_t r = 0, g = 0, b = 0;
        if (i < count) {
            float ratio = (float)i / LED_FRONT_COUNT;
            r = (uint8_t)(ratio * p->volume_brightness);
            g = (uint8_t)((1.0f - ratio) * p->volume_brightness);
        }
        led_strip_set_pixel(strips[LED_HAL_DEVICE_FRONT], i, g, r, b);
    }
    led_strip_refresh(strips[LED_HAL_DEVICE_FRONT]);
    SAFE_EXT_LOOP(
        led_strip_set_pixel(strips[LED_HAL_DEVICE_EXTENSION], i, 0, 0, 0);
    );
}

static void render_alert_panel(void)
{
    panel_state_t *p = &panel[LED_HAL_DEVICE_FRONT];
    TickType_t now = xTaskGetTickCount();
    if (now >= p->alert_end_tick) {
        taskENTER_CRITICAL(&mode_mux);
        led_mode_t restore = p->prev_mode;
        if (restore == LED_MODE_CLOCK) {
            uint32_t elapsed = (now - p->clock_start_tick) * portTICK_PERIOD_MS / 1000;
            if (elapsed >= p->clock_total_sec) {
                restore = LED_MODE_BREATH;
                p->brightness = 10;
            }
        }
        p->mode = restore;
        taskEXIT_CRITICAL(&mode_mux);
        return;
    }
    uint32_t phase_ms = (now * portTICK_PERIOD_MS) % (ALERT_BLINK_MS * 2);
    bool on = (phase_ms < ALERT_BLINK_MS);
    uint8_t r = on ? p->alert_brightness : 0;
    fill_strip(strips[LED_HAL_DEVICE_FRONT], LED_FRONT_COUNT, 0, r, 0);
    SAFE_EXT_LOOP(
        led_strip_set_pixel(strips[LED_HAL_DEVICE_EXTENSION], i, 0, 0, 0);
    );
}

// =================== 效果任务 ===================
static void led_hal_task(void *arg)
{
    TickType_t start_tick = xTaskGetTickCount();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30));
        float time_sec = (float)((xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS) / 1000.0f;

        for (int d = 0; d < LED_HAL_DEVICE_MAX; d++) {
            if (strips[d] == NULL) continue;

            led_mode_t mode;
            uint8_t brightness;
            taskENTER_CRITICAL(&mode_mux);
            mode = panel[d].mode;
            brightness = panel[d].brightness;
            taskEXIT_CRITICAL(&mode_mux);

            switch (mode) {
                case LED_MODE_OFF:
                    render_off_panel(d);
                    break;
                case LED_MODE_BREATH:
                    render_breath_panel(d, brightness);
                    break;
                case LED_MODE_BULB:
                    render_bulb_panel(d, brightness);
                    break;
                case LED_MODE_MUSIC:
                    render_music(strips[d],
                                 (d == LED_HAL_DEVICE_FRONT) ? LED_FRONT_COUNT : LED_EXTENSION_COUNT,
                                 time_sec + ((d == LED_HAL_DEVICE_EXTENSION) ? 1.5f : 0.0f),
                                 brightness);
                    break;
                case LED_MODE_RUN:
                    render_run_panel(d, brightness);
                    break;
                case LED_MODE_CLOCK:
                    if (d == LED_HAL_DEVICE_FRONT) render_clock_panel();
                    else render_off_panel(d);
                    break;
                case LED_MODE_VOLUME:
                    if (d == LED_HAL_DEVICE_FRONT) render_volume_panel();
                    else render_off_panel(d);
                    break;
                case LED_MODE_ALERT:
                    if (d == LED_HAL_DEVICE_FRONT) render_alert_panel();
                    else render_off_panel(d);
                    break;
            }
        }
    }
}

/**
 * @brief 统一设置面板模式（所有模式均通过此函数）
 * @param dev        面板选择
 * @param mode       模式
 * @param brightness 最大亮度 (1~255)
 * @param arg        附加参数：
 *                   - 时钟模式：倒计时秒数 (1~3600)
 *                   - 音量模式：音量值 (0~255)
 *                   - 其他模式：忽略（传 0）
 */
static esp_err_t led_hal_set_panel_mode(led_hal_device_t dev, led_mode_t mode,
                                 uint8_t brightness, uint32_t arg)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (dev >= LED_HAL_DEVICE_MAX) return ESP_ERR_INVALID_ARG;
    if (brightness == 0) brightness = 1;

    taskENTER_CRITICAL(&mode_mux);
    panel_state_t *p = &panel[dev];

    switch (mode) {
        case LED_MODE_CLOCK: {
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
            if (dev != LED_HAL_DEVICE_FRONT) {
                taskEXIT_CRITICAL(&mode_mux);
                return ESP_ERR_INVALID_ARG;
            }
            if (p->mode != LED_MODE_VOLUME && p->mode != LED_MODE_ALERT)
                p->prev_mode = p->mode;
            p->volume_level = (uint8_t)arg;
            p->volume_brightness = brightness;
            p->volume_end_tick = xTaskGetTickCount() + pdMS_TO_TICKS(VOLUME_DISPLAY_MS);
            p->mode = LED_MODE_VOLUME;
            break;
        }
        case LED_MODE_ALERT: {
            if (dev != LED_HAL_DEVICE_FRONT) {
                taskEXIT_CRITICAL(&mode_mux);
                return ESP_ERR_INVALID_ARG;
            }
            if (p->mode != LED_MODE_VOLUME && p->mode != LED_MODE_ALERT)
                p->prev_mode = p->mode;
            p->alert_brightness = brightness;
            p->alert_end_tick = xTaskGetTickCount() + pdMS_TO_TICKS(ALERT_BLINK_MS * 2 * ALERT_TIMES);
            p->mode = LED_MODE_ALERT;
            break;
        }
        default: {
            p->mode = mode;
            p->brightness = brightness;
            p->prev_mode = mode;
            break;
        }
    }

    taskEXIT_CRITICAL(&mode_mux);
    ESP_LOGI(TAG, "Panel %d -> mode %d, brightness %d", dev, mode, brightness);
    return ESP_OK;
}



/* ========== 主任务：分发请求 ========== */
static void led_service_task(void *arg) {
    event_data_t *evt_data;
    while (1) {
        if (xQueueReceive(led_service_request_queue, &evt_data, portMAX_DELAY) == pdTRUE) {
            if (evt_data->event_type == REQUEST) {
                led_service_receive_data_t *payload = (led_service_receive_data_t *)evt_data->data;
                if ( payload){
                    ESP_LOGI(TAG, "led service req: dev:%d mode:%d", payload->device, payload->mode);
                    led_hal_set_panel_mode(payload->device, payload->mode, payload->brightness, payload->arg);                  
                }
            } else {
                ESP_LOGE(TAG, "led service unsupported event type %d", evt_data->event_type);
            }
            if(evt_data->data){
                free(evt_data->data);
                evt_data->data = NULL;
            }
            if(evt_data){
                free(evt_data);
                evt_data = NULL;
            }
        }
    }
}

// =================== 服务 API ===================
esp_err_t led_service_init(void) {
    if (initialized) return ESP_OK;

    led_strip_config_t cfg_front = {
        .strip_gpio_num = LED_FRONT_GPIO,
        .max_leds       = LED_FRONT_COUNT,
        .led_model      = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_front = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma    = true,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&cfg_front, &rmt_front, &strips[LED_HAL_DEVICE_FRONT]));

    led_strip_config_t cfg_ext = {
        .strip_gpio_num = LED_EXTENSION_GPIO,
        .max_leds       = LED_EXTENSION_COUNT,
        .led_model      = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_ext = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,
        .mem_block_symbols = 48,
        .flags.with_dma    = false,
    };
    esp_err_t ret = led_strip_new_rmt_device(&cfg_ext, &rmt_ext, &strips[LED_HAL_DEVICE_EXTENSION]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ext LED init fail: %s", esp_err_to_name(ret));
        strips[LED_HAL_DEVICE_EXTENSION] = NULL;
    }

    if (strips[LED_HAL_DEVICE_FRONT]) led_strip_clear(strips[LED_HAL_DEVICE_FRONT]);
    if (strips[LED_HAL_DEVICE_EXTENSION]) led_strip_clear(strips[LED_HAL_DEVICE_EXTENSION]);

    if (xTaskCreate(led_hal_task, "led_hal_task", 4096, NULL, 2, &led_hal_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "led_hal_task create fail");
        return ESP_FAIL;
    } else{
        // 创建对外服务队列，用于接收其他服务的请求
        led_service_request_queue = xQueueCreate(20, sizeof(event_data_t));
        if (!led_service_request_queue) {
            ESP_LOGE(TAG, "led_service_request_queue create fail");
            vTaskDelete(led_hal_task_handle);
            return ESP_FAIL;
        } else{
            // 创建LED服务任务
            if (xTaskCreate(led_service_task, "led_service_task", 4096, NULL, 3, &led_service_task_handle) != pdPASS) {
                ESP_LOGE(TAG, "led_service_task create fail");
                vTaskDelete(led_hal_task_handle);
                vTaskDelete(led_service_task_handle);
                return ESP_FAIL;
            }
        }
    }
    initialized = true;
    ESP_LOGI(TAG, "LED HAL ready");
    return ESP_OK;
}

QueueHandle_t get_led_service_queue(void) {
    return led_service_request_queue;
}
