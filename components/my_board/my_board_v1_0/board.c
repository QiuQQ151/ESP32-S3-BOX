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

audio_hal_handle_t audio_board_codec_init(void)
{
    audio_hal_codec_config_t audio_codec_cfg = AUDIO_CODEC_DEFAULT_CONFIG();
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


