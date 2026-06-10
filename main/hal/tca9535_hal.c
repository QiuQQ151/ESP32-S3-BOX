#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "hal/tca9535_hal.h"
#include "system_event.h"
#include "ui_service.h"     // 默认往ui_service发送按键事件

// 设备I2C地址
#define TCA9535_I2C_ADDR 0x27

// 寄存器地址
#define REG_INPUT_PORT0  0x00
#define REG_INPUT_PORT1  0x01
#define REG_OUTPUT_PORT0 0x02
#define REG_OUTPUT_PORT1 0x03
#define REG_CONFIG_PORT0 0x06
#define REG_CONFIG_PORT1 0x07

// 定义TCA9535引脚宏（与事件类型对应）
// 输入
#define TCA_HAL_KEY_POWER     P04
#define TCA_HAL_TEMPALERT     P06
#define TCA_HAL_EXTDIS        P05
// 输出
#define POWER_HAL_PA_PIN        P07
#define POWER_HAL_MOTOR_PIN     P00
#define POWER_HAL_LED_PIN       P02
#define POWER_HAL_EXT_PCB_PIN   P03
#define POWER_HAL_EEPROM_PIN    P01
// 系统电源使能引脚
#define POWER_HAL_SYS_POWER_PIN   P04


const char* TAG = "tca9535_hal";

static i2c_port_t _i2c_port = I2C_NUM_0;          // 保存当前使用的I2C端口
static uint8_t _output_cache[2] = {0xFF, 0xFF};    // 输出寄存器缓存
static uint8_t _config_cache[2] = {0xFF, 0xFF};    // 配置寄存器缓存

// 中断处理相关静态变量
static TaskHandle_t tca9535_task_handle = NULL;    // 任务句柄
static uint8_t last_input[2] = {0};                // 上一次输入端口状态，用于变化检测

// 内部函数声明
static esp_err_t _write_reg(uint8_t reg, uint8_t value);
static esp_err_t _read_reg(uint8_t reg, uint8_t *value);
static inline void _io_to_port_bit(uint8_t ioPin, uint8_t *port, uint8_t *bit);
static void tca9535_hal_task(void* arg);
static void IRAM_ATTR tca9535_isr_handler(void* arg);


// 初始化芯片
esp_err_t tca9535_hal_init(i2c_port_t i2c_num) {
    _i2c_port = i2c_num;
    esp_err_t err;

    // 系统上电维持
    ESP_LOGI(TAG, "Init sys power enable pin");
    gpio_config_t power_io_conf = {
        .pin_bit_mask = (1ULL << POWER_HAL_SYS_POWER_PIN),   // 选中 GPIO4
        .mode = GPIO_MODE_OUTPUT,      // 输出模式
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&power_io_conf);
    power_hal_enable_sys_power(1); 

    ESP_LOGI(TAG, "TCA9535 initializing...");
    ESP_LOGI(TAG, "TCA9535 configuring pins...");
    // 设置引脚模式
    tca9535_pin_mode(TCA_HAL_KEY_POWER, IO_INPUT);
    tca9535_pin_mode(TCA_HAL_TEMPALERT, IO_INPUT);
    tca9535_pin_mode(TCA_HAL_EXTDIS, IO_INPUT);

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

    // 读取初始输入状态，作为基准
    err = _read_reg(REG_INPUT_PORT0, &last_input[0]);
    if (err != ESP_OK) return err;
    err = _read_reg(REG_INPUT_PORT1, &last_input[1]);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "Initial input state: port0=0x%02X, port1=0x%02X", last_input[0], last_input[1]);

    ESP_LOGI(TAG, "TCA9535 configuring interrupt...");
    // ==================== 配置GPIO中断 ====================
    gpio_config_t io_conf = {
        .intr_type    = TCA9535_INT_TRIGGER,
        .pin_bit_mask = (1ULL << TCA9535_INT_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = 0,          // 禁用内部上拉（外部上拉）
        .pull_down_en = 0,          // 禁用下拉
    };
    gpio_config(&io_conf);

    // 安装中断服务（如果系统已安装，此调用可忽略 ESP_ERR_INVALID_STATE）
    gpio_install_isr_service(0);
    // 添加中断处理函数
    gpio_isr_handler_add(TCA9535_INT_GPIO, tca9535_isr_handler, NULL);
    // =======================================================

    ESP_LOGI(TAG, "TCA9535 hal task starting...");
    xTaskCreate(tca9535_hal_task, "tca9535_hal_task", 4096, NULL, 5, &tca9535_task_handle);
    if (tca9535_task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_FAIL;
    }
    return ESP_OK;
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



// ==========================内部函数=====================================
// ---------- 中断服务函数（IRAM） ----------
static void IRAM_ATTR tca9535_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // 通知任务处理中断
    vTaskNotifyGiveFromISR(tca9535_task_handle, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ---------- 任务处理函数 ----------
static void tca9535_hal_task(void* arg) {
    while (1) {
        // 等待中断通知（无限等待）
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 读取当前输入端口状态
        uint8_t current_input[2];
        esp_err_t err0 = _read_reg(REG_INPUT_PORT0, &current_input[0]);
        esp_err_t err1 = _read_reg(REG_INPUT_PORT1, &current_input[1]);
        if (err0 != ESP_OK || err1 != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read input registers after interrupt");
            continue;
        }

        // 比较变化，检测关注的引脚
        // 遍历我们关心的三个引脚，若电平改变则生成事件
        struct {
            uint8_t pin;
            tca_event_type_t event_type;
        } monitored_pins[] = {
            {TCA_HAL_KEY_POWER,   TCA_EVENT_KEYPOWER},
            {TCA_HAL_TEMPALERT,   TCA_EVENT_TEMPALERT},
            {TCA_HAL_EXTDIS,      TCA_EVENT_EXTDIS},
        };

        for (int i = 0; i < sizeof(monitored_pins)/sizeof(monitored_pins[0]); i++) {
            uint8_t pin = monitored_pins[i].pin;
            uint8_t port, bit;
            _io_to_port_bit(pin, &port, &bit);

            uint8_t old_level = (last_input[port] >> bit) & 0x01;
            uint8_t new_level = (current_input[port] >> bit) & 0x01;

            if (old_level != new_level) {
                ESP_LOGI(TAG, "Pin P%02d changed: %d -> %d", pin, old_level, new_level);

                // 构建事件数据
                tca_event_data_t *tca_event = malloc(sizeof(tca_event_data_t));
                if (tca_event == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for tca_event");
                    continue;
                }
                tca_event->event    = monitored_pins[i].event_type;
                tca_event->io_level = new_level;

                // 构建事件消息结构体
                event_data_t *event_msg = malloc(sizeof(event_data_t));
                event_msg->service_id  = TCAHAL_SERVICE;
                event_msg->event_type  = NOTIFICATION;
                event_msg->reply_queue = NULL;       // 不需要回复
                event_msg->data        = tca_event;
                event_msg->data_len    = sizeof(tca_event_data_t);

                // 发送到主事件队列
                QueueHandle_t ui_queue = get_ui_service_queue();
                if (ui_queue && xQueueSend(ui_queue, &event_msg, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "UI event queue full, event dropped");
                    if(tca_event) free(tca_event);
                    if(event_msg) free(event_msg);
                }
                ESP_LOGW(TAG, "send tca event to ui queue");
            }
        }

        // 更新上一次的输入状态
        last_input[0] = current_input[0];
        last_input[1] = current_input[1];
    }
}

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

// 内部函数：写单个寄存器
static esp_err_t _write_reg(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(_i2c_port, TCA9535_I2C_ADDR, data, 2, pdMS_TO_TICKS(100));
}

// 内部函数：读单个寄存器
static esp_err_t _read_reg(uint8_t reg, uint8_t *value) {
    return i2c_master_write_read_device(_i2c_port, TCA9535_I2C_ADDR, &reg, 1, value, 1, pdMS_TO_TICKS(100));
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