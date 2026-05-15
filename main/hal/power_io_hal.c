#include "driver/gpio.h"
#include "power_io_hal.h"
#include "hal/tca9535_hal.h" // 控制使能


// 功放使能，1使能，0关闭
void pa_set(uint8_t level){
   // tca9535的io07
   tca9535_pin_mode(P07, IO_OUTPUT);
   tca9535_digital_write(P07, level);
}