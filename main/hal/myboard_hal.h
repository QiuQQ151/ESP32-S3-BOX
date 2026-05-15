#pragma once

audio_hal_handle_t get_manual_audio_hal(void);

// 一次性初始化所有音频控制硬件（I2C + ES8311 + 功放）
void myboard_hal_init(void);