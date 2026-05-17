#include "keys_hal.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "system_event.h"   // 
#include "ui_service.h"  // 默认往ui_service发送按键事件

static const char *TAG = "KEY_HAL";

// 按键配置
typedef struct {
    int pin;
    key_id_t id;
    int last_stable_state;   // 上次稳定状态（0:按下, 1:释放）
    uint32_t last_tick;      // 上次变更时间戳（us）
} key_config_t;

static key_config_t s_key_configs[] = {
    { .pin = -1, .id = KEY_ID_ENCODER_SW },
    { .pin = -1, .id = KEY_ID_BACK  },
    { .pin = -1, .id = KEY_ID_ENTER  }
};
static int s_num_keys = sizeof(s_key_configs) / sizeof(s_key_configs[0]);

// 编码器旋转变量
static int s_encoder_clk_pin = -1;
static int s_encoder_dt_pin  = -1;
static volatile int s_encoder_last_clk = 0;

// 目标服务 ID（由上层传入）
static int s_target_service_id = -1;

// 内部事件队列（中断 -> 处理任务）
static QueueHandle_t s_event_queue = NULL;

// 内部事件结构
typedef struct {
    key_id_t key_id;
    int new_state;      // 0:按下, 1:释放（对于旋转事件无意义）
    int is_rotate;      // 是否为旋转事件
    int rotate_dir;     // 1:正转, -1:反转
} hal_key_event_t;

/**
 * @brief 发送按键事件到事件循环中心
 */
static void send_to_service(key_id_t id, key_event_type_t type, int rotate_diff)
{
    // 分配 payload（最里层）
    key_event_data_t *payload = malloc(sizeof(key_event_data_t));
    if (!payload) {
        ESP_LOGE(TAG, "Failed to allocate payload data");
        return;
    }
    payload->key_id = id;
    payload->event = type;
    payload->rotate_diff = rotate_diff;

    // 分配事件外壳（最外层）
    event_data_t *evt = malloc(sizeof(event_data_t));
    if (!evt) {
        ESP_LOGE(TAG, "Failed to allocate system_event");
        free(payload);
        return;
    }
    evt->service_id = KEYHAL_SERVICE;
    evt->event_type = NOTIFICATION;
    evt->reply_queue = NULL;
    evt->data = payload;
    evt->data_len = sizeof(key_event_data_t);

    // 发送到主事件队列
    QueueHandle_t ui_queue = get_ui_service_queue();
    if (ui_queue && xQueueSend(ui_queue, &evt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "UI event queue full, event dropped");
        free(payload);
        free(evt);
    }
    ESP_LOGW(TAG, "send keys event to ui queue");
    // 注意：事件循环任务会在收到后释放 evt，并转发 payload 到目标服务队列；
    // 目标服务负责最终释放 payload。
}

/**
 * @brief 按键处理任务（去抖动、发送事件）
 */
#define ENCODER_MIN_STEPS  2   // 旋转步数阈值，可根据需要调整
static void key_hal_task(void *arg)
{
    hal_key_event_t evt;
    uint32_t now_us;
    const uint32_t DEBOUNCE_US = 50000;
    int32_t rot_accum = 0;   // 旋转步数累积

    while (1) {
        if (xQueueReceive(s_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            if (evt.is_rotate) {
                // 仅累加，不立即发送
                rot_accum += evt.rotate_dir;

                // 检查是否达到阈值
                if (rot_accum >= ENCODER_MIN_STEPS) {
                    send_to_service(KEY_ID_ENCODER_SW,
                                    KEY_EVENT_ROTATE_CW,
                                    rot_accum);   // 正值，携带累积步数
                    rot_accum = 0;
                } else if (rot_accum <= -ENCODER_MIN_STEPS) {
                    send_to_service(KEY_ID_ENCODER_SW,
                                    KEY_EVENT_ROTATE_CCW,
                                    -rot_accum);  // 绝对值
                    rot_accum = 0;
                }
                continue;
            }

            // --- 按键事件去抖动（原逻辑）---
            key_config_t *cfg = NULL;
            for (int i = 0; i < s_num_keys; i++) {
                if (s_key_configs[i].id == evt.key_id) {
                    cfg = &s_key_configs[i];
                    break;
                }
            }
            if (!cfg || cfg->pin == -1) continue;

            now_us = esp_timer_get_time();
            if ((now_us - cfg->last_tick) > DEBOUNCE_US) {
                int level = gpio_get_level(cfg->pin);
                if (level != cfg->last_stable_state) {
                    cfg->last_stable_state = level;
                    cfg->last_tick = now_us;
                    key_event_type_t type = (level == 0) ? KEY_EVENT_PRESS : KEY_EVENT_RELEASE;
                    send_to_service(evt.key_id, type, 0);
                }
            }
        }
    }
}

/**
 * @brief GPIO 中断处理函数
 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    hal_key_event_t evt;
    evt.is_rotate = 0;
    evt.rotate_dir = 0;

    // 处理编码器旋转（仅 CLK 引脚触发）
    if (gpio_num == s_encoder_clk_pin) {
        int clk = gpio_get_level(s_encoder_clk_pin);
        if (clk != s_encoder_last_clk) {
            int dt = gpio_get_level(s_encoder_dt_pin);
            evt.is_rotate = 1;
            evt.rotate_dir = (dt != clk) ? 1 : -1;
            evt.key_id = KEY_ID_ENCODER_SW;   // 旋转事件属于编码器按键ID
            s_encoder_last_clk = clk;
            xQueueSendFromISR(s_event_queue, &evt, NULL);
        }
        return;
    }

    // 处理按键（包括编码器按键和独立IO按键）
    for (int i = 0; i < s_num_keys; i++) {
        if (s_key_configs[i].pin == gpio_num) {
            evt.is_rotate = 0;
            evt.key_id = s_key_configs[i].id;
            xQueueSendFromISR(s_event_queue, &evt, NULL);
            break;
        }
    }
}

static esp_err_t init_key_gpio(int pin, key_id_t id)
{
    if (pin < 0) return ESP_ERR_INVALID_ARG;
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) return ret;
    ret = gpio_isr_handler_add(pin, gpio_isr_handler, (void *)pin);
    return ret;
}

static esp_err_t init_encoder_rotary(int clk_pin, int dt_pin)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << clk_pin) | (1ULL << dt_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) return ret;
    // 只为 CLK 引脚添加中断
    ret = gpio_isr_handler_add(clk_pin, gpio_isr_handler, (void *)clk_pin);
    return ret;
}

esp_err_t key_hal_init(void)
{
    s_encoder_clk_pin = 10;
    s_encoder_dt_pin  = 9;

    s_key_configs[0].pin = 11;
    s_key_configs[1].pin = 0;
    s_key_configs[2].pin = 3;

    for (int i = 0; i < s_num_keys; i++) {
        s_key_configs[i].last_stable_state = 1;   // 初始释放
        s_key_configs[i].last_tick = 0;
    }

    s_event_queue = xQueueCreate(20, sizeof(hal_key_event_t));
    if (!s_event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    // 安装 GPIO ISR 服务（全局一次）
    static bool isr_installed = false;
    if (!isr_installed) {
        gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        isr_installed = true;
    }

    // 初始化编码器旋转引脚
    esp_err_t ret = init_encoder_rotary(s_encoder_clk_pin, s_encoder_dt_pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init encoder rotary pins");
        return ret;
    }
    s_encoder_last_clk = gpio_get_level(s_encoder_clk_pin);

    // 初始化所有按键 GPIO
    for (int i = 0; i < s_num_keys; i++) {
        if (s_key_configs[i].pin >= 0) {
            ret = init_key_gpio(s_key_configs[i].pin, s_key_configs[i].id);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to init key pin %d", s_key_configs[i].pin);
                return ret;
            }
        }
    }

    // 创建按键处理任务
    xTaskCreate(key_hal_task, "key_hal_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Key HAL initialized, target service ID = %d", s_target_service_id);
    return ESP_OK;
}