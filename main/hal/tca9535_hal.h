#ifndef TCA9535_H
#define TCA9535_H

#include <stdint.h>
#include "driver/i2c.h"

// IO口编号宏定义
#define P00  0
#define P01  1
#define P02  2
#define P03  3
#define P04  4
#define P05  5
#define P06  6
#define P07  7
#define P10  8
#define P11  9
#define P12  10
#define P13  11
#define P14  12
#define P15  13
#define P16  14
#define P17  15

// 方向定义
#define IO_INPUT  1
#define IO_OUTPUT 0

// 电平定义
#define IO_LOW   0
#define IO_HIGH  1


esp_err_t tca9535_hal_init(i2c_port_t i2c_num);

/**
 * @brief 配置单个IO口方向
 * @param ioPin IO口编号（P00~P17）
 * @param mode IO_INPUT 或 IO_OUTPUT
 * @return ESP_OK / ESP_FAIL
 */
esp_err_t tca9535_pin_mode(uint8_t ioPin, uint8_t mode);

/**
 * @brief 设置单个IO口输出电平（仅当配置为输出时有效）
 * @param ioPin IO口编号（P00~P17）
 * @param level IO_LOW 或 IO_HIGH
 * @return ESP_OK / ESP_FAIL
 */
esp_err_t tca9535_digital_write(uint8_t ioPin, uint8_t level);

/**
 * @brief 读取单个IO口的电平（实际物理电平）
 * @param ioPin IO口编号（P00~P17）
 * @param level 输出电平指针（IO_LOW或IO_HIGH）
 * @return ESP_OK / ESP_FAIL
 */
esp_err_t tca9535_digital_read(uint8_t ioPin, uint8_t *level);

#endif