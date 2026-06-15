// hal/lvgl_hal.h
#pragma once
#include "esp_err.h"
#include "lvgl.h"

lv_disp_t* lvgl_hal_init(void);
uint32_t lvgl_hal_get_light_adc(void);
void lvgl_hal_set_brightness(uint8_t percent);
