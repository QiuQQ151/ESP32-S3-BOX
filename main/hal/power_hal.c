#include "driver/gpio.h"
#include "esp_log.h"
#include "power_hal.h"
#include "hal/tca9535_hal.h" // 控制使能

// 定义TCA9535引脚宏，方便后续修改
#define POWER_HAL_PA_PIN        P07
#define POWER_HAL_MOTOR_PIN     P00
#define POWER_HAL_LED_PIN       P02
#define POWER_HAL_EXT_PCB_PIN   P03
#define POWER_HAL_EEPROM_PIN    P01

// 系统电源使能引脚
#define POWER_HAL_SYS_POWER_PIN   P04

static const char *TAG = "power_hal";



// 初始化电源使能
void power_hal_init(void){
   // 初始化系统电源使能引脚
   ESP_LOGI(TAG, "Start power hal");

   ESP_LOGI(TAG, "Init sys power enable pin");
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << POWER_HAL_SYS_POWER_PIN),   // 选中 GPIO4
        .mode = GPIO_MODE_OUTPUT,      // 输出模式
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    power_hal_enable_sys_power(1); // 系统上电维持

    // 初始化tca9535 IO扩展芯片
    ESP_LOGI(TAG, "Start tca9535 chip"); //
    tca9535_hal_init(I2C_NUM_0);
}

// 系统电源使能
void power_hal_enable_sys_power(bool enable){
   gpio_set_level(POWER_HAL_SYS_POWER_PIN, enable);
}

// 功放使能，1使能，0关闭
void power_hal_pa_enable(uint8_t enable){
   tca9535_pin_mode(POWER_HAL_PA_PIN, IO_OUTPUT);
   tca9535_digital_write(POWER_HAL_PA_PIN, enable);
}

// 设置马达使能
void power_hal_motor_enable(uint16_t enable){
   tca9535_pin_mode(POWER_HAL_MOTOR_PIN, IO_OUTPUT);
   tca9535_digital_write(POWER_HAL_MOTOR_PIN, enable);
}

// 设置前置led使能
void power_hal_led_enable(uint8_t enable){
   tca9535_pin_mode(POWER_HAL_LED_PIN, IO_OUTPUT);
   tca9535_digital_write(POWER_HAL_LED_PIN, enable);
}

// 设置扩展PCB的使能
void power_hal_ext_pcb_enable(uint8_t led_index, uint8_t enable){
   tca9535_pin_mode(POWER_HAL_EXT_PCB_PIN, IO_OUTPUT);
   tca9535_digital_write(POWER_HAL_EXT_PCB_PIN, enable);
}

// 设置EEPROM使能
void power_hal_eeprom_enable(bool enable){
   tca9535_pin_mode(POWER_HAL_EEPROM_PIN, IO_OUTPUT);
   tca9535_digital_write(POWER_HAL_EEPROM_PIN, enable);
}
