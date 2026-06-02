/*
 * ESPRESSIF MIT 许可协议
 *
 * 版权所有 (c) 2020 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * 特此免费授予在所有 ESPRESSIF SYSTEMS 产品上使用的权限，任何获得本软件及相关文档文件（“软件”）副本的人，
 * 均可不受限制地使用本软件，包括但不限于使用、复制、修改、合并、发布、分发、再许可和/或出售本软件的副本，
 * 并允许向其提供本软件的人这样做，但须符合以下条件：
 *
 * 上述版权声明和本许可声明应包含在本软件的所有副本或重要部分中。
 *
 * 本软件按“原样”提供，不提供任何明示或暗示的担保，包括但不限于适销性、特定用途适用性和非侵权的担保。
 * 在任何情况下，作者或版权持有人均不对因本软件或本软件的使用或其他交易而产生的任何索赔、损害或其他责任负责，
 * 无论是在合同诉讼、侵权行为或其他情况下。
 *
 */

#include "esp_log.h"
#include "board.h"
#include "audio_mem.h"
#include "es8311.h"
#include "es7210.h"
#include "periph_button.h"

static const char *TAG = "AUDIO_BOARD";

static audio_board_handle_t board_handle = 0;

audio_board_handle_t audio_board_init(void)
{
    if (board_handle) {
        ESP_LOGW(TAG, "The board has already been initialized!");
        return board_handle;
    }
    board_handle = (audio_board_handle_t) audio_calloc(1, sizeof(struct audio_board_handle));
    AUDIO_MEM_CHECK(TAG, board_handle, return NULL);
    board_handle->audio_hal = audio_board_codec_init();

    return board_handle;
}

esp_err_t test_es7210(void)
{
    uint8_t reg_addr = 0x00;  // ES7210 RESET 寄存器
    uint8_t data;
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (0x41 << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);  // 重复起始
    i2c_master_write_byte(cmd, (0x41 << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &data, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    
    if (ret == ESP_OK) {
        printf("ES7210 REG[0x00] = 0x%02X\n", data);
        return ESP_OK;
    } else {
        printf("Failed to read ES7210: %s\n", esp_err_to_name(ret));
        return ESP_FAIL;
    }
}

audio_hal_handle_t audio_board_codec_init(void)
{
    // ES7210
    audio_hal_codec_config_t es7210_cfg =AUDIO_ADC_DUAL_MIC_CONFIG();
    audio_hal_handle_t es7210_hal = audio_hal_init(&es7210_cfg, &AUDIO_CODEC_ES7210_DEFAULT_HANDLE); 
    audio_hal_ctrl_codec(es7210_hal, AUDIO_HAL_CODEC_MODE_ENCODE, AUDIO_HAL_CTRL_START);
    es7210_adc_set_gain(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2, GAIN_30DB); // 设置增益为 12dB

    // es8311
    audio_hal_codec_config_t audio_codec_cfg = AUDIO_DAC_PLAYBACK_CONFIG();
    audio_hal_handle_t codec_hal = audio_hal_init(&audio_codec_cfg, &AUDIO_CODEC_ES8311_DEFAULT_HANDLE);



    AUDIO_NULL_CHECK(TAG, codec_hal, return NULL);
    return codec_hal;
}

audio_board_handle_t audio_board_get_handle(void)
{
    return board_handle;
}

esp_err_t audio_board_deinit(audio_board_handle_t audio_board)
{
    esp_err_t ret = ESP_OK;
    ret |= audio_hal_deinit(audio_board->audio_hal);
    free(audio_board);
    board_handle = NULL;
    return ret;
}

esp_err_t _get_lcd_io_bus (void *bus, esp_lcd_panel_io_spi_config_t *io_config,
                           esp_lcd_panel_io_handle_t *out_panel_io)
{
    return esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)bus, io_config, out_panel_io);
}


