// led_hal.c
#include "led_hal.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "led_hal";

typedef struct {
    led_strip_handle_t handle;
    uint16_t           count;
    bool               ready;
} hal_dev_t;

static hal_dev_t hal_devices[LED_HAL_DEVICE_MAX] = {0};

esp_err_t led_hal_init(led_hal_device_t dev, uint8_t gpio, uint16_t max_leds)
{
    if (dev >= LED_HAL_DEVICE_MAX) return ESP_ERR_INVALID_ARG;
    if (hal_devices[dev].ready) return ESP_OK;   // 已初始化

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = gpio,
        .max_leds       = max_leds,
        .led_model      = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma    = true,
    };

    // 扩展板使用较小的 memory block 且不用 DMA
    if (dev == LED_HAL_DEVICE_EXTENSION) {
        rmt_cfg.mem_block_symbols = 48;
        rmt_cfg.flags.with_dma    = false;
    }

    esp_err_t ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &hal_devices[dev].handle);
    if (ret == ESP_OK) {
        hal_devices[dev].count = max_leds;
        hal_devices[dev].ready = true;
        led_strip_clear(hal_devices[dev].handle);
        ESP_LOGI(TAG, "Device %d initialized, GPIO %d, count %d", dev, gpio, max_leds);
    } else {
        hal_devices[dev].handle = NULL;
        hal_devices[dev].count  = 0;
        hal_devices[dev].ready  = false;
        ESP_LOGE(TAG, "Device %d init fail: %s", dev, esp_err_to_name(ret));
    }
    return ret;
}

uint16_t led_hal_get_count(led_hal_device_t dev)
{
    if (dev >= LED_HAL_DEVICE_MAX || !hal_devices[dev].ready) return 0;
    return hal_devices[dev].count;
}

bool led_hal_is_ready(led_hal_device_t dev)
{
    if (dev >= LED_HAL_DEVICE_MAX) return false;
    return hal_devices[dev].ready;
}

void led_hal_set_pixel(led_hal_device_t dev, uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (!led_hal_is_ready(dev) || index >= hal_devices[dev].count) return;
    // 注意：led_strip_set_pixel 参数顺序为 index, red, green, blue
    led_strip_set_pixel(hal_devices[dev].handle, index, r, g, b);
}

void led_hal_refresh(led_hal_device_t dev)
{
    if (!led_hal_is_ready(dev)) return;
    led_strip_refresh(hal_devices[dev].handle);
}

void led_hal_clear(led_hal_device_t dev)
{
    if (!led_hal_is_ready(dev)) return;
    led_strip_clear(hal_devices[dev].handle);
}