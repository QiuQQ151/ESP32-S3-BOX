#ifndef LED_HAL_H
#define LED_HAL_H

#include "esp_err.h"
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "led_hal.h"  

typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_BREATH,
    LED_MODE_CLOCK,   // 仅front led使用
    LED_MODE_BULB,
    LED_MODE_MUSIC,
    LED_MODE_RUN,
    LED_MODE_VOLUME,  // 临时模式（3秒后恢复）
    LED_MODE_ALERT,   // 临时模式（闪3次后恢复）
} led_mode_t;

/* ========== 服务请求结构 ========== */
typedef struct {
    led_hal_device_t    device;         // 设备
    led_mode_t          mode;           // 模式（关灯/氛围灯/通知）
    uint8_t             brightness;     // 亮度 0-255 （每次设置都有效）
    uint32_t            arg;            // 附加参数：
                                        //   - 时钟模式：倒计时秒数 (1~3600)
                                        //   - 音量模式：音量值 (0~255)
                                        //   - 灯泡模式：颜色值 (0x00RRGGBB)
                                        //   - 其他模式：忽略（传 0）
} led_service_receive_data_t;

/* ========== 状态回复 ========== */
typedef enum {
    LED_SERVICE_OK = 0,
    LED_SERVICE_ERROR,
} led_service_state_t;

typedef struct {
    led_service_state_t service_state;
    led_hal_device_t    device;
    led_mode_t          current_mode;
    uint8_t             current_brightness;
} led_service_send_data_t;

esp_err_t led_service_init(void);
QueueHandle_t get_led_service_queue(void);


#endif /* LED_HAL_H */