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

// ==================== 中断配置（请根据硬件设计修改） =====================
#define TCA9535_INT_GPIO        GPIO_NUM_16        // 连接TCA9535 INT引脚的ESP32 GPIO
#define TCA9535_INT_TRIGGER     GPIO_INTR_ANYEDGE  // 中断触发方式：NEGEDGE/POSEDGE/ANYEDGE/LOW_LEVEL...
// =======================================================================

// 对外接口
// ==================== 数据定义 =====================
/**
 * @brief 按键事件类型
 */
typedef enum {
    TCA_EVENT_KEYPOWER,        // power电源键
    TCA_EVENT_TEMPALERT,       // 板载温度报警
    TCA_EVENT_EXTDIS,          // 扩展版霍尔传感器
} tca_event_type_t;

typedef struct {
    tca_event_type_t event;    // 事件类型
    int io_level;              // io口电平
} tca_event_data_t;

// ==================== 接口定义 =====================
esp_err_t tca9535_hal_init(i2c_port_t i2c_num);

void power_hal_init(void);
void power_hal_enable_sys_power(bool enable);// 系统电源使能
void power_hal_pa_enable(uint8_t enable); // 设置功使能
void power_hal_motor_enable(uint16_t enable);// 设置马达使能
void power_hal_led_enable(uint8_t enable);// 设置前置led使能
void power_hal_ext_pcb_enable(uint8_t led_index, uint8_t enable);// 设置扩展PCB的使能
void power_hal_eeprom_enable(bool enable);// 设置EEPROM使能


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