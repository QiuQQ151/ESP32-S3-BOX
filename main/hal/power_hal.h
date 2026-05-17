#ifndef POWER_IO_HAL_H
#define POWER_IO_HAL_H

void power_hal_init(void);
void power_hal_enable_sys_power(bool enable);// 系统电源使能
void power_hal_pa_enable(uint8_t enable); // 设置功使能
void power_hal_motor_enable(uint16_t enable);// 设置马达使能
void power_hal_led_enable(uint8_t enable);// 设置前置led使能
void power_hal_ext_pcb_enable(uint8_t led_index, uint8_t enable);// 设置扩展PCB的使能
void power_hal_eeprom_enable(bool enable);// 设置EEPROM使能


#endif