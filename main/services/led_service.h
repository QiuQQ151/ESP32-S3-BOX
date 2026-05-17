#pragma once
#include "esp_err.h"
#include "freertos/queue.h"

/* ========== 模式枚举 ========== */
typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_BREATH,         // 呼吸 （白色呼吸）
    LED_MODE_RAINBOW,        // 彩虹旋转（指生成一圈渐变色，然后不断旋转）
    LED_MODE_CLOCK,          // 时钟（是番茄时钟，由外部传入种时长后，由内部去计时）
    LED_MODE_BULB,             // 灯泡 （白色常量）
    LED_MODE_MUSIC,          // 音乐
    LED_MODE_VOLUME,         // 音量指示（通知）
    LED_MODE_ALERT,          // 警告闪烁（通知）
} led_mode_t;

typedef enum {
    LED_FRONT = 0,
    LED_EXTENSION,
    LED_DEVICE_MAX
} led_device_t;

/* ========== 指令 ========== */
typedef enum {
    LED_CMD_SET_MODE = 0,       // 设置模式
    LED_CMD_SET_BRIGHTNESS,     // 设置亮度
    LED_CMD_GET_STATUS,         // 获取状态（需提供 reply_queue）
} led_service_cmd_t;

/* ========== 扁平服务请求结构 ========== */
typedef struct {
    led_service_cmd_t   cmd;            // 指令
    led_device_t        device;         // 设备
    led_mode_t          mode;           // 模式（关灯/氛围灯/通知）
    uint8_t             brightness;     // 亮度 0-255 （每次设置都有效）
    uint8_t             volume;         // 音量 0-100（模式为音量时有效）
    uint8_t             alert_count;    // 警告闪烁次数（模式为警告时有效）
    // 时钟专用
    uint8_t             clock_hour;     // 时 0-23
    uint8_t             clock_minute;   // 分 0-59
    uint8_t             clock_second;   // 秒 0-59
} led_service_receive_data_t;

/* ========== 状态回复 ========== */
typedef enum {
    LED_SERVICE_OK = 0,
    LED_SERVICE_ERROR,
} led_service_state_t;

typedef struct {
    led_service_state_t service_state;
    led_device_t        device;
    led_mode_t          current_mode;
    uint8_t             current_brightness;
} led_service_send_data_t;

/* ========== 接口 ========== */
esp_err_t led_service_init(void);
QueueHandle_t get_led_service_queue(void);

// #pragma once
// #include "led_strip.h"
// #include "esp_err.h"
// #include "freertos/queue.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// typedef enum {
//     LED_MODE_OFF = 0,        // 关闭LED
//     LED_MODE_CLOCK,          // 时钟模式
//     LED_MODE_MUSIC,          // 音乐模式
//     LED_MODE_BREATH,         // 呼吸模式
//     LED_MODE_RAINBOW,        // 彩虹模式
//     LED_MODE_BULB,           // 灯泡模式
//     LED_MODE_MAX,            // 模式最大值
// } led_mode_t;

// typedef enum {
//     LED_FRONT = 0,           // 前置LED
//     LED_EXTENSION,           // 扩展板LED
//     LED_DEVICE_MAX,          // LED设备最大值
// } led_device_t;

// // 时钟模式需要的数据
// typedef struct {
//     uint8_t hour;           // 时钟模式：小时 (0-23)
//     uint8_t minute;         // 时钟模式：分钟 (0-59)
//     uint8_t second;         // 时钟模式：秒 (0-59)
// } led_clock_data_t;

// // ===============LED服务请求
// typedef enum {
//     LED_CMD_SET_MODE = 0,       // 设置LED模式
//     LED_CMD_SET_BRIGHTNESS,     // 设置LED亮度
//     LED_CMD_GET_STATUS,         // 获取LED状态
// } led_service_cmd_t;

// typedef struct {
//     led_service_cmd_t cmd;       // 请求命令
//     led_device_t device;        // LED设备 (用于LED_DEVICE_CONTROL_CMD)
//     led_mode_t mode;            // LED模式 (用于LED_MODE_SET_CMD)
//     uint8_t brightness;         // 亮度值 0-255 (用于LED_BRIGHTNESS_SET_CMD)
//     void* data;                 // 通用数据指针 (用于LED_GET_STATUS_CMD)
// } led_service_receive_data_t;


// // ==================LED服务回复
// typedef enum {
//     LED_SERVICE_OK = 0,             // 服务响应成功
//     LED_SERVICE_ERROR,              // 服务响应错误
// } led_service_state_t;

// // 服务发给reply_queue的数据结构
// typedef struct {
//     led_service_state_t service_state;  // 服务回复状态
//     led_device_t device;                // LED设备
//     led_mode_t current_mode;            // 当前LED模式
//     uint8_t current_brightness;         // 当前亮度
// } led_service_send_data_t;

// // led服务的对外接
// esp_err_t led_service_init(void);
// QueueHandle_t get_led_service_queue(void);
