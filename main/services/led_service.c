#include <math.h> 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "services/led_service.h"

static led_strip_handle_t led_strip_front;
static led_strip_handle_t led_strip_top;


// HSV 转 RGB，hue: 0-360, saturation: 0-100, value: 0-100
uint32_t hsv2rgb(uint16_t hue, uint8_t saturation, uint8_t value) {
    uint8_t r, g, b;
    uint8_t region, remainder, p, q, t;
    
    if (saturation == 0) {
        r = value;
        g = value;
        b = value;
        return (r << 16) | (g << 8) | b;
    }
    
    region = hue / 60;
    remainder = (hue % 60) * 255 / 60;
    
    p = (value * (255 - saturation)) / 255;
    q = (value * (255 - (saturation * remainder) / 255)) / 255;
    t = (value * (255 - (saturation * (255 - remainder)) / 255)) / 255;
    
    switch (region) {
        case 0:
            r = value; g = t; b = p;
            break;
        case 1:
            r = q; g = value; b = p;
            break;
        case 2:
            r = p; g = value; b = t;
            break;
        case 3:
            r = p; g = q; b = value;
            break;
        case 4:
            r = t; g = p; b = value;
            break;
        default:
            r = value; g = p; b = q;
            break;
    }
    
    return (r << 16) | (g << 8) | b;
}


void led_task(void *pvParameters){
    float pos_front = 0.0f;         // 前灯带亮点浮点位置 (0 ~ max_leds_front)
    float pos_top = 0.0f;           // 顶灯带亮点浮点位置
    uint8_t hue_offset = 0;
    float breath_phase = 0;
    
    const int max_leds_front = 30;
    const int max_leds_top = 10;
    const int decay_distance = 20;
    float speed = 0.1f;            // 旋转速度 (每帧移动距离)，值越小越慢
    
    while(1){
        // 呼吸效果 (每帧更新)
        breath_phase += 0.03f;
        if (breath_phase > 2 * 3.14159f) breath_phase -= 2 * 3.14159f;
        int global_max_brightness = 10 + 10 * (sinf(breath_phase) + 1) / 2;
        
        // 更新浮点位置 (旋转)
        pos_front += speed;
        if (pos_front >= max_leds_front) pos_front -= max_leds_front;
        pos_top += speed;
        if (pos_top >= max_leds_top) pos_top -= max_leds_top;
        
        // 色相流动 (可独立于位置)
        hue_offset = (hue_offset + 2) % 360;
        
        // ---------- 前灯带 ----------
        for(int led = 0; led < max_leds_front; led++) {
            // 计算环状浮点距离
            float dist = fabs(led - pos_front);
            if (dist > max_leds_front / 2) dist = max_leds_front - dist;
            
            int brightness = global_max_brightness - dist * (global_max_brightness / decay_distance);
            if (brightness < 0) brightness = 0;
            
            uint8_t led_hue = (hue_offset + led * 5) % 360;
            uint32_t rgb = hsv2rgb(led_hue, 100, brightness);
            uint8_t red = (rgb >> 16) & 0xFF;
            uint8_t green = (rgb >> 8) & 0xFF;
            uint8_t blue = rgb & 0xFF;
            led_strip_set_pixel(led_strip_front, led, red, green, blue);
        }
        led_strip_refresh(led_strip_front);
        
        // ---------- 顶灯带 ----------
        for(int led = 0; led < max_leds_top; led++) {
            float dist = fabs(led - pos_top);
            if (dist > max_leds_top / 2) dist = max_leds_top - dist;
            int brightness = global_max_brightness - dist * (global_max_brightness / 3);
            if (brightness < 0) brightness = 0;
            
            uint8_t led_hue = (hue_offset + 180 + led * 15) % 360;
            uint32_t rgb = hsv2rgb(led_hue, 100, brightness);
            uint8_t red = (rgb >> 16) & 0xFF;
            uint8_t green = (rgb >> 8) & 0xFF;
            uint8_t blue = rgb & 0xFF;
            led_strip_set_pixel(led_strip_top, led, red, green, blue);
        }
        led_strip_refresh(led_strip_top);
        
        vTaskDelay(15 / portTICK_PERIOD_MS);
    }
}


void led_hal_init(void){

    // 


    // front 
    // 配置灯带参数
    led_strip_config_t strip_config = {
        .strip_gpio_num = 7,             // 你的GPIO引脚号
        .max_leds = 30,                  // 灯带上的LED数量
       // .led_pixel_format = LED_PIXEL_FORMAT_GRB, // 颜色格式，WS2812B通常为GRB
        .led_model = LED_MODEL_WS2812,   // LED型号
    };
    // 配置RMT后端（最常用）
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz分辨率
        .flags.with_dma = false,           // 如果灯珠很多，可启用DMA以提高性能
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_front));

    // top
    // 配置灯带参数
     strip_config.strip_gpio_num = 8;             // 你的GPIO引脚号
     strip_config.max_leds = 10;                 // 灯带上的LED数量
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_top));

    xTaskCreate(led_task, "led_task", 4096, NULL, 2, NULL);
}


