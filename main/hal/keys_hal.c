// hal/keys.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#include "event_loop_service.h"
#include "key_input_service.h"
#include "keys_hal.h"
#define KEY_COUNT 3

static const int key_pins[KEY_COUNT] = {GPIO_NUM_0, GPIO_NUM_1, GPIO_NUM_2};

extern QueueHandle_t main_event_queue; // 事件处理队列

// typedef struct {
//     uint32_t cmd;  // 请求服务
//     union {
//         uint8_t  key_id;        // 按键编号
//         int8_t   enc_dir;       // 编码器方向 +1/-1
//     } data;    
//     QueueHandle_t reply_queue;   // 结果通知队列 (可为 NULL)
// } key_input_service_receive_data_t;

struct key_input_service_receive_data_t payload; // 使用预分配，加快速度，注意服务函数不要free

static void IRAM_ATTR key_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t)arg;
    // 找出按键编号
    uint8_t key_id = 0xFF;
    for (int i = 0; i < KEY_COUNT; i++) {
        if (key_pins[i] == gpio_num) { key_id = i; break; }
    }


    // 生成事件外壳
    app_event_t evt = {
        .cmd    = get_key_input_service_ID(),
        .payload = &payload
    };
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(main_event_queue, &evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void keys_init(void) {
    // 批量初始化
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_NEGEDGE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = 0,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    for (int i = 0; i < KEY_COUNT; i++) {
        io_conf.pin_bit_mask |= (1ULL << key_pins[i]);
    }
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    for (int i = 0; i < KEY_COUNT; i++) {
        gpio_isr_handler_add(key_pins[i], key_isr_handler, (void *)key_pins[i]);
    }
}