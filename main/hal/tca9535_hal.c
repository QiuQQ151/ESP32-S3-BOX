#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "hal/tca9535_hal.h"

// 设备I2C地址
#define TCA9535_I2C_ADDR 0x27

// 寄存器地址
#define REG_INPUT_PORT0  0x00
#define REG_INPUT_PORT1  0x01
#define REG_OUTPUT_PORT0 0x02
#define REG_OUTPUT_PORT1 0x03
#define REG_CONFIG_PORT0 0x06
#define REG_CONFIG_PORT1 0x07

const char* TAG = "tca9535_hal";
static i2c_port_t _i2c_port = I2C_NUM_0;          // 保存当前使用的I2C端口
static uint8_t _output_cache[2] = {0xFF, 0xFF};    // 输出寄存器缓存
static uint8_t _config_cache[2] = {0xFF, 0xFF};    // 配置寄存器缓存

// static i2c_master_bus_handle_t i2c_bus = NULL;
// static i2c_master_dev_handle_t tca9535_dev = NULL;

// 内部函数：将IO号转换为端口和位
static inline void _io_to_port_bit(uint8_t ioPin, uint8_t *port, uint8_t *bit) {
    if (ioPin <= P07) {
        *port = 0;
        *bit = ioPin;
    } else {
        *port = 1;
        *bit = ioPin - 8;
    }
}

// static esp_err_t _write_reg(uint8_t reg, uint8_t value) {
//     uint8_t data[2] = {reg, value};
//     return i2c_master_transmit(tca9535_dev , data, sizeof(data), pdMS_TO_TICKS(100));
// }

// static esp_err_t _read_reg(uint8_t reg, uint8_t *value) {
//     // First write the register address, then read one byte
//     return i2c_master_transmit_receive(tca9535_dev , &reg, 1, value, 1, pdMS_TO_TICKS(100));
// }

// 内部函数：写单个寄存器
static esp_err_t _write_reg(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(_i2c_port, TCA9535_I2C_ADDR, data, 2, pdMS_TO_TICKS(100));
}

// 内部函数：读单个寄存器
static esp_err_t _read_reg(uint8_t reg, uint8_t *value) {
    return i2c_master_write_read_device(_i2c_port, TCA9535_I2C_ADDR, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}

// // 内部函数：写单个寄存器（旧版 I2C 实现）
// static esp_err_t _write_reg(uint8_t reg, uint8_t value) {
//     i2c_cmd_handle_t cmd = i2c_cmd_link_create();
//     esp_err_t err;

//     // 起始条件
//     i2c_master_start(cmd);
//     // 发送设备地址 + 写方向
//     i2c_master_write_byte(cmd, (TCA9535_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
//     // 发送寄存器地址
//     i2c_master_write_byte(cmd, reg, true);
//     // 发送数据
//     i2c_master_write_byte(cmd, value, true);
//     // 停止条件
//     i2c_master_stop(cmd);

//     err = i2c_master_cmd_begin(_i2c_port, cmd, pdMS_TO_TICKS(100));
//     i2c_cmd_link_delete(cmd);
//     return err;
// }

// // 内部函数：读单个寄存器（旧版 I2C 实现）
// static esp_err_t _read_reg(uint8_t reg, uint8_t *value) {
//     i2c_cmd_handle_t cmd = i2c_cmd_link_create();
//     esp_err_t err;

//     // 起始条件
//     i2c_master_start(cmd);
//     // 发送设备地址 + 写方向（写寄存器地址）
//     i2c_master_write_byte(cmd, (TCA9535_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
//     // 发送寄存器地址
//     i2c_master_write_byte(cmd, reg, true);
//     // 重复起始条件，准备读取
//     i2c_master_start(cmd);
//     // 发送设备地址 + 读方向
//     i2c_master_write_byte(cmd, (TCA9535_I2C_ADDR << 1) | I2C_MASTER_READ, true);
//     // 读取一个字节，从机发送 NACK（最后字节）
//     i2c_master_read_byte(cmd, value, I2C_MASTER_NACK);
//     // 停止条件
//     i2c_master_stop(cmd);

//     err = i2c_master_cmd_begin(_i2c_port, cmd, pdMS_TO_TICKS(100));
//     i2c_cmd_link_delete(cmd);
//     return err;
// }

// 初始化芯片缓存
esp_err_t tca9535_hal_init(i2c_port_t i2c_num) {
    _i2c_port = i2c_num;
    //ESP_ERROR_CHECK(i2c_master_get_bus_handle(_i2c_port, &i2c_bus));

    // 添加 TCA9535 设备
    // i2c_device_config_t dev_cfg = {
    //     .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    //     .device_address = TCA9535_I2C_ADDR,
    //     .scl_speed_hz = 100000,
    // };
    // ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &tca9535_dev));   

    esp_err_t err;
    // 读取当前硬件状态，更新缓存
    err = _read_reg(REG_CONFIG_PORT0, &_config_cache[0]);
    if (err != ESP_OK) return err;
    err = _read_reg(REG_CONFIG_PORT1, &_config_cache[1]);
    if (err != ESP_OK) return err;
    err = _read_reg(REG_OUTPUT_PORT0, &_output_cache[0]);
    if (err != ESP_OK) return err;
    err = _read_reg(REG_OUTPUT_PORT1, &_output_cache[1]);
    if (err != ESP_OK) return err;
    
    ESP_LOGI(TAG, "TCA9535 initialized, config0=0x%02X, config1=0x%02X", _config_cache[0], _config_cache[1]);
    return ESP_OK;
}

// 配置单个IO方向
esp_err_t tca9535_pin_mode(uint8_t ioPin, uint8_t mode) {
    uint8_t port, bit;
    _io_to_port_bit(ioPin, &port, &bit);
    
    if (mode == IO_OUTPUT) {
        _config_cache[port] &= ~(1 << bit);   // 清0 = 输出
    } else {
        _config_cache[port] |= (1 << bit);    // 置1 = 输入
    }
    
    uint8_t reg = (port == 0) ? REG_CONFIG_PORT0 : REG_CONFIG_PORT1;
    return _write_reg(reg, _config_cache[port]);
}

// 设置输出电平
esp_err_t tca9535_digital_write(uint8_t ioPin, uint8_t level) {
    uint8_t port, bit;
    _io_to_port_bit(ioPin, &port, &bit);
    
    if (level == IO_HIGH) {
        _output_cache[port] |= (1 << bit);
    } else {
        _output_cache[port] &= ~(1 << bit);
    }
    
    uint8_t reg = (port == 0) ? REG_OUTPUT_PORT0 : REG_OUTPUT_PORT1;
    return _write_reg(reg, _output_cache[port]);
}

// 读取电平（无论输入输出模式，都读取实际引脚电平）
esp_err_t tca9535_digital_read(uint8_t ioPin, uint8_t *level) {
    uint8_t port, bit;
    _io_to_port_bit(ioPin, &port, &bit);
    
    uint8_t reg = (port == 0) ? REG_INPUT_PORT0 : REG_INPUT_PORT1;
    uint8_t value;
    esp_err_t err = _read_reg(reg, &value);
    if (err != ESP_OK) return err;
    
    *level = (value >> bit) & 0x01;
    return ESP_OK;
}