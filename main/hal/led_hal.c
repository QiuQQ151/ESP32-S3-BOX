#include <math.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_hal.h"

// =================== 硬件配置 ===================
#define LED_FRONT_GPIO       7
#define LED_EXTENSION_GPIO   8
#define LED_FRONT_COUNT      30
#define LED_EXTENSION_COUNT  10

// =================== 设备索引 ===================
#define LED_HAL_DEVICE_FRONT     0
#define LED_HAL_DEVICE_EXTENSION 1
#define LED_HAL_MAX_DEVICES      2

// =================== 色彩模式参数（可自由调节） ===================
#define BRIGHTNESS      40      // 全局最大亮度 (0-255)

// R 通道参数
#define TIME_SPEED_R    0.3f     // 时间流速
#define PERIOD_R        10.0f    // 空间周期（灯珠数）
#define PHASE_R         0.0f     // 初始相位（弧度）

// G 通道参数
#define TIME_SPEED_G    0.5f
#define PERIOD_G        27.0f
#define PHASE_G         2.0f

// B 通道参数
#define TIME_SPEED_B    0.7f
#define PERIOD_B        33.0f
#define PHASE_B         4.0f

static const char *TAG = "led_hal";

static led_strip_handle_t strips[LED_HAL_MAX_DEVICES] = {NULL, NULL};
static bool initialized = false;

// =================== 辅助函数：保证非负的正弦映射 ===================
static inline float sin01(float x)
{
    // sin(x) -> [0, 1]
    return sinf(x) * 0.5f + 0.5f;
}

// =================== 主渲染函数 ===================
/**
 * @brief 根据时间和灯珠索引计算 RGB 颜色
 * @param time_sec  当前时间（秒）
 * @param index     灯珠索引 (0 ~ led_count-1)
 * @param r, g, b   输出颜色值 (0-255)
 */
static void calc_color(float time_sec, int index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    float idx = (float)index;
    
    // R 通道：正弦函数，周期 PERIOD_R 颗灯珠，随时间漂移
    float val_r = sin01(2.0f * M_PI * (idx / PERIOD_R + TIME_SPEED_R * time_sec) + PHASE_R);
    // G 通道
    float val_g = sin01(2.0f * M_PI * (idx / PERIOD_G + TIME_SPEED_G * time_sec) + PHASE_G);
    // B 通道
    float val_b = sin01(2.0f * M_PI * (idx / PERIOD_B + TIME_SPEED_B * time_sec) + PHASE_B);
    
    *r = (uint8_t)(val_r * BRIGHTNESS);
    *g = (uint8_t)(val_g * BRIGHTNESS);
    *b = (uint8_t)(val_b * BRIGHTNESS);
}

/**
 * @brief 更新整个灯条
 * @param strip     灯条句柄
 * @param led_count 灯珠数量
 * @param time_sec  当前时间（秒）
 */
static void update_strip(led_strip_handle_t strip, uint32_t led_count, float time_sec)
{
    if (strip == NULL || led_count == 0) return;
    
    for (int i = 0; i < led_count; i++) {
        uint8_t r, g, b;
        calc_color(time_sec, i, &r, &g, &b);
        // WS2812B 需要 GRB 顺序
        led_strip_set_pixel(strip, i, g, r, b);
    }
    led_strip_refresh(strip);
}

// =================== 效果任务 ===================
static void effect_task(void *arg)
{
    ESP_LOGI(TAG, "Organic color mode started");
    ESP_LOGI(TAG, "  Brightness: %d", BRIGHTNESS);
    
    TickType_t start_tick = xTaskGetTickCount();
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30));  // 约 33fps，流畅且不占资源
        
        // 计算从启动到现在经过的秒数
        float time_sec = (float)((xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS) / 1000.0f;
        
        // 更新前板
        update_strip(strips[LED_HAL_DEVICE_FRONT], LED_FRONT_COUNT, time_sec);
        
        // 扩展板使用相同逻辑，但可加上一个固定偏移使其略有不同
        update_strip(strips[LED_HAL_DEVICE_EXTENSION], LED_EXTENSION_COUNT, time_sec + 1.5f);
    }
}

// =================== 公有 API ===================
esp_err_t led_hal_init(void) {
    if (initialized) return ESP_OK;
    
    esp_err_t ret;
    
    // ============ 前板 LED 初始化 ============
    led_strip_config_t strip_config_front = {
        .strip_gpio_num = LED_FRONT_GPIO,
        .max_leds       = LED_FRONT_COUNT,
        .led_model      = LED_MODEL_WS2812,
    };
    
    led_strip_rmt_config_t rmt_config_front = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma    = true,          // DMA 确保时序稳定
    };
    
    ret = led_strip_new_rmt_device(&strip_config_front, &rmt_config_front, &strips[LED_HAL_DEVICE_FRONT]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Front LED init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Front LED: GPIO%d, %d LEDs", LED_FRONT_GPIO, LED_FRONT_COUNT);
    
    // ============ 扩展板 LED 初始化 ============
    led_strip_config_t strip_config_ext = {
        .strip_gpio_num = LED_EXTENSION_GPIO,
        .max_leds       = LED_EXTENSION_COUNT,
        .led_model      = LED_MODEL_WS2812,
    };
    
    led_strip_rmt_config_t rmt_config_ext = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,
        .mem_block_symbols = 48,
        .flags.with_dma    = true,
    };
    
    ret = led_strip_new_rmt_device(&strip_config_ext, &rmt_config_ext, &strips[LED_HAL_DEVICE_EXTENSION]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Extension LED init failed: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "Only front LED will be used");
        strips[LED_HAL_DEVICE_EXTENSION] = NULL;
    } else {
        ESP_LOGI(TAG, "Extension LED: GPIO%d, %d LEDs", LED_EXTENSION_GPIO, LED_EXTENSION_COUNT);
    }
    
    // 清空
    if (strips[LED_HAL_DEVICE_FRONT]) led_strip_clear(strips[LED_HAL_DEVICE_FRONT]);
    if (strips[LED_HAL_DEVICE_EXTENSION]) led_strip_clear(strips[LED_HAL_DEVICE_EXTENSION]);
    
    // 创建效果任务
    BaseType_t task_ret = xTaskCreate(effect_task, "led_organic", 4096, NULL, 3, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED task");
        return ESP_FAIL;
    }
    
    initialized = true;
    ESP_LOGI(TAG, "LED HAL ready");
    return ESP_OK;
}


// #include <math.h>
// #include <stdint.h>
// #include <string.h>
// #include "esp_err.h"
// #include "led_strip.h"
// #include "esp_log.h"
// #include "driver/gpio.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "led_hal.h"

// // =================== 硬件配置 ===================
// #define LED_FRONT_GPIO       7
// #define LED_EXTENSION_GPIO   8
// #define LED_FRONT_COUNT      30
// #define LED_EXTENSION_COUNT  10

// // =================== 设备索引 ===================
// #define LED_HAL_DEVICE_FRONT     0
// #define LED_HAL_DEVICE_EXTENSION 1
// #define LED_HAL_MAX_DEVICES      2

// // =================== 用户可调参数 ===================
// #define ROTATE_SPEED        0.2f    // 旋转速度 (0.1=慢, 1.0=快)
// #define MAX_BRIGHTNESS      130     // 最大亮度 (0-255)

// // =================== 固定参数 ===================
// #define BREATHE_PERIOD      10000    // 呼吸周期 (毫秒)
// #define GRADIENT_LENGTH     200     // 虚拟色条长度
// #define MIN_BRIGHTNESS_RATIO 0.2f   // 最低亮度比例，避免完全熄灭

// static const char *TAG = "led_hal";

// // =================== 数据结构 ===================
// typedef struct {
//     uint8_t r, g, b;
// } color_rgb_t;

// // =================== 全局变量 ===================
// static led_strip_handle_t strips[LED_HAL_MAX_DEVICES] = {NULL, NULL};
// static bool initialized = false;
// static color_rgb_t gradient_map[GRADIENT_LENGTH];

// // =================== 余弦插值渐变色条生成 ===================
// /**
//  * @brief 使用余弦插值生成首尾无缝的双色渐变条
//  * @param c1_r/g/b  颜色1 (出现在色条首尾)
//  * @param c2_r/g/b  颜色2 (出现在色条中点)
//  * 
//  * 数学原理：
//  *   t = (1 - cos(2π * phase)) / 2
//  *   phase=0  → t=0 → 纯颜色1
//  *   phase=0.5 → t=1 → 纯颜色2
//  *   phase=1  → t=0 → 纯颜色1（首尾衔接，导数连续）
//  */
// static void gradient_precompute_two_colors(uint8_t c1_r, uint8_t c1_g, uint8_t c1_b,
//                                            uint8_t c2_r, uint8_t c2_g, uint8_t c2_b)
// {
//     for (int i = 0; i < GRADIENT_LENGTH; i++) {
//         float phase = (float)i / GRADIENT_LENGTH;          // 0.0 ~ 1.0（不含1.0）
//         float t = (1.0f - cosf(2.0f * M_PI * phase)) / 2.0f; // 0→1→0，余弦平滑
        
//         gradient_map[i].r = (uint8_t)(c1_r + (c2_r - c1_r) * t);
//         gradient_map[i].g = (uint8_t)(c1_g + (c2_g - c1_g) * t);
//         gradient_map[i].b = (uint8_t)(c1_b + (c2_b - c1_b) * t);
//     }
// }

// // =================== 呼吸亮度计算 ===================
// /**
//  * @brief 计算当前呼吸亮度值
//  * @param elapsed_ms   自启动以来的毫秒数
//  * @param max_brightness 最大亮度上限
//  * @return 当前亮度 (0 ~ max_brightness)
//  */
// static uint8_t breathe_brightness(uint32_t elapsed_ms, uint8_t max_brightness)
// {
//     float phase = (float)(elapsed_ms % BREATHE_PERIOD) / BREATHE_PERIOD; // 0.0 ~ 1.0
//     float angle = phase * 2.0f * M_PI;                                    // 0 ~ 2π
//     float brightness = (sinf(angle) + 1.0f) / 2.0f;                       // 0.0 ~ 1.0
    
//     // 保持最低亮度，避免完全熄灭
//     brightness = MIN_BRIGHTNESS_RATIO + brightness * (1.0f - MIN_BRIGHTNESS_RATIO);
    
//     return (uint8_t)(brightness * max_brightness);
// }

// // =================== 窗口取样并显示 ===================
// /**
//  * @brief 从渐变色条中滑动窗口取样，应用呼吸亮度后显示
//  * @param strip      LED 灯条句柄
//  * @param led_count  物理灯珠数量
//  * @param offset     窗口起始位置 (0.0 ~ GRADIENT_LENGTH)
//  * @param brightness 当前呼吸亮度
//  */
// static void gradient_window_show(led_strip_handle_t strip,
//                                  uint32_t led_count,
//                                  float offset,
//                                  uint8_t brightness)
// {
//     if (strip == NULL || led_count == 0) return;
    
//     // 规范化 offset 到 [0, GRADIENT_LENGTH)
//     while (offset < 0.0f) offset += (float)GRADIENT_LENGTH;
//     while (offset >= (float)GRADIENT_LENGTH) offset -= (float)GRADIENT_LENGTH;
    
//     float scale = (float)GRADIENT_LENGTH / led_count;
    
//     for (int i = 0; i < led_count; i++) {
//         // 计算当前灯珠在虚拟色条上的位置
//         float pos = offset + i * scale;
        
//         // 安全循环边界处理
//         while (pos >= (float)GRADIENT_LENGTH) pos -= (float)GRADIENT_LENGTH;
//         while (pos < 0.0f) pos += (float)GRADIENT_LENGTH;
        
//         // 线性插值
//         int idx0 = (int)pos;
//         int idx1 = (idx0 + 1) % GRADIENT_LENGTH;
//         float frac = pos - (float)idx0;
        
//         uint8_t r = (uint8_t)(gradient_map[idx0].r * (1.0f - frac) + gradient_map[idx1].r * frac);
//         uint8_t g = (uint8_t)(gradient_map[idx0].g * (1.0f - frac) + gradient_map[idx1].g * frac);
//         uint8_t b = (uint8_t)(gradient_map[idx0].b * (1.0f - frac) + gradient_map[idx1].b * frac);
        
//         // 应用呼吸亮度
//         r = (uint8_t)((uint16_t)r * brightness / 255);
//         g = (uint8_t)((uint16_t)g * brightness / 255);
//         b = (uint8_t)((uint16_t)b * brightness / 255);
        
//         // WS2812B 需要 GRB 顺序
//         led_strip_set_pixel(strip, i, g, r, b);
//     }
    
//     led_strip_refresh(strip);
// }

// // =================== 测试任务 ===================
// static void test_task(void *arg)
// {
//     // 预计算双色渐变：深蓝 → 暖橙
//     gradient_precompute_two_colors(
//         0, 100, 255,    // 颜色1: 蓝色
//         255, 120, 0     // 颜色2: 橙色
//     );
    
//     float offset_front = 0.0f;
//     float offset_ext = 0.0f;
    
//     ESP_LOGI(TAG, "Dual-color gradient + breathe started");
//     ESP_LOGI(TAG, "  Rotate speed: %.1f", (double)ROTATE_SPEED);
//     ESP_LOGI(TAG, "  Max brightness: %d", MAX_BRIGHTNESS);
//     ESP_LOGI(TAG, "  Breathe period: %d ms", BREATHE_PERIOD);
    
//     TickType_t start_tick = xTaskGetTickCount();
    
//     while (1) {
//         vTaskDelay(pdMS_TO_TICKS(20));  // 50fps
        
//         // 计算呼吸亮度
//         uint32_t elapsed = (uint32_t)((xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS);
//         uint8_t brightness = breathe_brightness(elapsed, MAX_BRIGHTNESS);
        
//         // 前板正向旋转
//         offset_front += ROTATE_SPEED;
//         while (offset_front >= (float)GRADIENT_LENGTH) offset_front -= (float)GRADIENT_LENGTH;
        
//         // 扩展板反向旋转
//         offset_ext -= ROTATE_SPEED;
//         while (offset_ext < 0.0f) offset_ext += (float)GRADIENT_LENGTH;
        
//         // 刷新显示
//         gradient_window_show(strips[LED_HAL_DEVICE_FRONT], LED_FRONT_COUNT, offset_front, brightness);
//         gradient_window_show(strips[LED_HAL_DEVICE_EXTENSION], LED_EXTENSION_COUNT, offset_ext, brightness);
//     }
// }

// // =================== 公有 API ===================
// esp_err_t led_hal_init(void) {
//     if (initialized) return ESP_OK;
    
//     esp_err_t ret;
    
//     // ============ 前板 LED 初始化 ============
//     led_strip_config_t strip_config_front = {
//         .strip_gpio_num = LED_FRONT_GPIO,
//         .max_leds       = LED_FRONT_COUNT,
//         .led_model      = LED_MODEL_WS2812,
//     };
    
//     led_strip_rmt_config_t rmt_config_front = {
//         .clk_src           = RMT_CLK_SRC_DEFAULT,
//         .resolution_hz     = 10 * 1000 * 1000,  // 10MHz
//         .mem_block_symbols = 64,
//         .flags.with_dma    = true,               // 开启 DMA，避免时序中断
//     };
    
//     ret = led_strip_new_rmt_device(&strip_config_front, &rmt_config_front, &strips[LED_HAL_DEVICE_FRONT]);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to init front LED on GPIO %d: %s", LED_FRONT_GPIO, esp_err_to_name(ret));
//         return ret;
//     }
//     ESP_LOGI(TAG, "Front LED: GPIO%d, %d LEDs", LED_FRONT_GPIO, LED_FRONT_COUNT);
    
//     // ============ 扩展板 LED 初始化 ============
//     led_strip_config_t strip_config_ext = {
//         .strip_gpio_num = LED_EXTENSION_GPIO,
//         .max_leds       = LED_EXTENSION_COUNT,
//         .led_model      = LED_MODEL_WS2812,
//     };
    
//     led_strip_rmt_config_t rmt_config_ext = {
//         .clk_src           = RMT_CLK_SRC_DEFAULT,
//         .resolution_hz     = 10 * 1000 * 1000,
//         .mem_block_symbols = 48,
//         .flags.with_dma    = true,
//     };
    
//     ret = led_strip_new_rmt_device(&strip_config_ext, &rmt_config_ext, &strips[LED_HAL_DEVICE_EXTENSION]);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to init ext LED on GPIO %d: %s", LED_EXTENSION_GPIO, esp_err_to_name(ret));
//         ESP_LOGW(TAG, "Extension LED disabled, only front LED will work");
//         strips[LED_HAL_DEVICE_EXTENSION] = NULL;
//     } else {
//         ESP_LOGI(TAG, "Extension LED: GPIO%d, %d LEDs", LED_EXTENSION_GPIO, LED_EXTENSION_COUNT);
//     }
    
//     // 清空所有灯珠
//     if (strips[LED_HAL_DEVICE_FRONT] != NULL) {
//         led_strip_clear(strips[LED_HAL_DEVICE_FRONT]);
//     }
//     if (strips[LED_HAL_DEVICE_EXTENSION] != NULL) {
//         led_strip_clear(strips[LED_HAL_DEVICE_EXTENSION]);
//     }
    
//     // 创建测试任务
//     BaseType_t task_ret = xTaskCreate(test_task, "led_effect", 4096, NULL, 2, NULL);
//     if (task_ret != pdPASS) {
//         ESP_LOGE(TAG, "Failed to create LED effect task");
//         return ESP_FAIL;
//     }
    
//     initialized = true;
//     ESP_LOGI(TAG, "LED HAL initialization complete");
//     return ESP_OK;
// }