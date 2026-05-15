#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "es8311.h"
#include "audio_hal.h"
#include "myboard_hal.h"

static const char *TAG = "myboard_hal";

static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static audio_hal_handle_t myboard_audio_hal = NULL;

// 功放使能 GPIO（根据您的硬件修改）
//#define PA_ENABLE_GPIO   GPIO_NUM_???

void manual_pa_init(void)
{
    // gpio_config_t io_conf = {
    //     .pin_bit_mask = (1ULL << PA_ENABLE_GPIO),
    //     .mode = GPIO_MODE_OUTPUT,
    //     .intr_type = GPIO_INTR_DISABLE,
    //     .pull_down_en = 0,
    //     .pull_up_en = 0,
    // };
    // gpio_config(&io_conf);
    // gpio_set_level(PA_ENABLE_GPIO, 1);  // 默认使能
}

void pa_set(int enable)
{
    // gpio_set_level(PA_ENABLE_GPIO, enable ? 1 : 0);
}

// 手动初始化 I2C 总线 (用于 ES8311)
static void manual_i2c_bus_init(void)
{
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_1,     
        .scl_io_num = GPIO_NUM_2,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus_handle));
    ESP_LOGI(TAG, "I2C master bus created on port 0");
}

// 手动初始化 ES8311 编解码器，生成 audio_hal 句柄
static void manual_es8311_init(void)
{
    // 创建 ES8311 设备
    es8311_config_t es8311_cfg = {
        .i2c_bus_handle = i2c_bus_handle,
        .i2c_addr = ES8311_ADDR_0,      // 0x18
        .rate = 44100,
        .channel = AUDIO_HAL_2CH,
        .bit = AUDIO_HAL_BIT_16,
        .mclk_mode = ES_MCLK_MODE_0,    // 根据硬件可能需要调整
    };
    audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    assert(codec_if);

    // 包装成 audio_hal 接口
    audio_hal_config_t hal_cfg = {
        .codec_if = codec_if,
    };
    myboard_audio_hal = audio_hal_new(&hal_cfg);
    assert(myboard_audio_hal);

    ESP_LOGI(TAG, "ES8311 initialized");
}

// 对外接口：获取 audio_hal 句柄（替代 board_handle->audio_hal）
audio_hal_handle_t get_manual_audio_hal(void)
{
    return myboard_audio_hal;
}

// 一次性初始化所有音频控制硬件（I2C + ES8311 + 功放）
void myboard_hal_init(void)
{
    manual_i2c_bus_init();
    manual_es8311_init();
    //manual_pa_init();
    ESP_LOGI(TAG, "Myboard hardware init done");
}