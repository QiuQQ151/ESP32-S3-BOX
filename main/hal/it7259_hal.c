/*
 * SPDX-FileCopyrightText: 2015-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "it7259_hal.h"

static const char *TAG = "it7259";

/* Registers */
#define TOUCH_DEVICE_MODE      (0x00)
#define TOUCH_GESTURE_ID       (0x01)
#define TOUCH_TOUCH_POINTS     (0x02)

#define TOUCH_TOUCH1_EV_FLAG   (0x03)
#define TOUCH_TOUCH1_XH        (0x03)
#define TOUCH_TOUCH1_XL        (0x04)
#define TOUCH_TOUCH1_YH        (0x05)
#define TOUCH_TOUCH1_YL        (0x06)

#define TOUCH_TOUCH2_EV_FLAG   (0x09)
#define TOUCH_TOUCH2_XH        (0x09)
#define TOUCH_TOUCH2_XL        (0x0A)
#define TOUCH_TOUCH2_YH        (0x0B)
#define TOUCH_TOUCH2_YL        (0x0C)

#define TOUCH_TOUCH3_EV_FLAG   (0x0F)
#define TOUCH_TOUCH3_XH        (0x0F)
#define TOUCH_TOUCH3_XL        (0x10)
#define TOUCH_TOUCH3_YH        (0x11)
#define TOUCH_TOUCH3_YL        (0x12)

#define TOUCH_TOUCH4_EV_FLAG   (0x15)
#define TOUCH_TOUCH4_XH        (0x15)
#define TOUCH_TOUCH4_XL        (0x16)
#define TOUCH_TOUCH4_YH        (0x17)
#define TOUCH_TOUCH4_YL        (0x18)

#define TOUCH_TOUCH5_EV_FLAG   (0x1B)
#define TOUCH_TOUCH5_XH        (0x1B)
#define TOUCH_TOUCH5_XL        (0x1C)
#define TOUCH_TOUCH5_YH        (0x1D)
#define TOUCH_TOUCH5_YL        (0x1E)

#define TOUCH_ID_G_THGROUP             (0x80)
#define TOUCH_ID_G_THPEAK              (0x81)
#define TOUCH_ID_G_THCAL               (0x82)
#define TOUCH_ID_G_THWATER             (0x83)
#define TOUCH_ID_G_THTEMP              (0x84)
#define TOUCH_ID_G_THDIFF              (0x85)
#define TOUCH_ID_G_CTRL                (0x86)
#define TOUCH_ID_G_TIME_ENTER_MONITOR  (0x87)
#define TOUCH_ID_G_PERIODACTIVE        (0x88)
#define TOUCH_ID_G_PERIODMONITOR       (0x89)
#define TOUCH_ID_G_AUTO_CLB_MODE       (0xA0)
#define TOUCH_ID_G_LIB_VERSION_H       (0xA1)
#define TOUCH_ID_G_LIB_VERSION_L       (0xA2)
#define TOUCH_ID_G_CIPHER              (0xA3)
#define TOUCH_ID_G_MODE                (0xA4)
#define TOUCH_ID_G_PMODE               (0xA5)
#define TOUCH_ID_G_FIRMID              (0xA6)
#define TOUCH_ID_G_STATE               (0xA7)
#define TOUCH_ID_G_FT5201ID            (0xA8)
#define TOUCH_ID_G_ERR                 (0xA9)


#define POINT           0x08
#define GESTURES        0x80

#define TAP             0x20
#define FLICK           0x22
#define DOUBLE_TAP      0x23

#define UP              0x08
#define UPPER_RIGHT     0x09
#define RIGHT           0x0A
#define LOWER_RIGHT     0x0B
#define DOWN            0x0C
#define LOWER_LEFT      0x0D
#define LEFT            0x0E
#define UPPER_LEFT      0x0F

static touch_ui_state_t ui_state = {0};

/*******************************************************************************
* Function definitions
*******************************************************************************/
static esp_err_t esp_lcd_touch_touch_read_data(esp_lcd_touch_handle_t tp);
static bool esp_lcd_touch_touch_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
static esp_err_t esp_lcd_touch_touch_del(esp_lcd_touch_handle_t tp);

/* I2C read */
static esp_err_t touch_touch_i2c_write(esp_lcd_touch_handle_t tp, uint8_t reg, uint8_t data);
static esp_err_t touch_touch_i2c_read(esp_lcd_touch_handle_t tp, uint8_t reg, uint8_t *data, uint8_t len);

/* touch init */
static esp_err_t touch_touch_init(esp_lcd_touch_handle_t tp);
/* touch reset */
static esp_err_t touch_touch_reset(esp_lcd_touch_handle_t tp);

touch_ui_state_t* get_touch_ui_state(void);
void reset_touch_redraw_flag(void);
void init_touch_ui_state(int16_t offset_x, int16_t offset_y);

/*******************************************************************************
* Public API functions
*******************************************************************************/

esp_err_t esp_lcd_touch_new_i2c_touch(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch)
{
    esp_err_t ret = ESP_OK;

    assert(config != NULL);
    assert(out_touch != NULL);

    /* Prepare main structure */
    esp_lcd_touch_handle_t esp_lcd_touch_touch = heap_caps_calloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(esp_lcd_touch_touch, ESP_ERR_NO_MEM, err, TAG, "no mem for touch controller");

    /* Communication interface */
    esp_lcd_touch_touch->io = io;

    /* Only supported callbacks are set */
    esp_lcd_touch_touch->read_data = esp_lcd_touch_touch_read_data;
    esp_lcd_touch_touch->get_xy = esp_lcd_touch_touch_get_xy;
    esp_lcd_touch_touch->del = esp_lcd_touch_touch_del;

    /* Mutex */
    esp_lcd_touch_touch->data.lock.owner = portMUX_FREE_VAL;

    /* Save config */
    memcpy(&esp_lcd_touch_touch->config, config, sizeof(esp_lcd_touch_config_t));

    /* Prepare pin for touch interrupt */
    if (esp_lcd_touch_touch->config.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_gpio_config = {
            .mode = GPIO_MODE_INPUT,
            .intr_type = (esp_lcd_touch_touch->config.levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE),
            .pin_bit_mask = BIT64(esp_lcd_touch_touch->config.int_gpio_num)
        };
        ret = gpio_config(&int_gpio_config);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");

        /* Register interrupt callback */
        if (esp_lcd_touch_touch->config.interrupt_callback) {
            esp_lcd_touch_register_interrupt_callback(esp_lcd_touch_touch, esp_lcd_touch_touch->config.interrupt_callback);
        }
    }

    /* Prepare pin for touch controller reset */
    if (esp_lcd_touch_touch->config.rst_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t rst_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = BIT64(esp_lcd_touch_touch->config.rst_gpio_num)
        };
        ret = gpio_config(&rst_gpio_config);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");
    }

    /* Reset controller */
    ret = touch_touch_reset(esp_lcd_touch_touch);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "touch reset failed");

    /* Init controller */
    ret = touch_touch_init(esp_lcd_touch_touch);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "touch init failed");

    *out_touch = esp_lcd_touch_touch;

err:
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (0x%x)! Touch controller touch initialization failed!", ret);
        if (esp_lcd_touch_touch) {
            esp_lcd_touch_touch_del(esp_lcd_touch_touch);
        }
    }

    return ret;
}

static esp_err_t esp_lcd_touch_touch_read_data(esp_lcd_touch_handle_t tp)
{
    esp_err_t err;
    uint8_t data[30];
    uint8_t points;
    size_t i = 0;

    assert(tp != NULL);

    err = touch_touch_i2c_read(tp, TOUCH_TOUCH_POINTS, &points, 1);
    ESP_RETURN_ON_ERROR(err, TAG, "I2C read error!");

    if (points > 5 || points == 0) {
        return ESP_OK;
    }

    /* Number of touched points */
    points = (points > CONFIG_ESP_LCD_TOUCH_MAX_POINTS ? CONFIG_ESP_LCD_TOUCH_MAX_POINTS : points);

    err = touch_touch_i2c_read(tp, TOUCH_TOUCH1_XH, data, 6 * points);
    ESP_RETURN_ON_ERROR(err, TAG, "I2C read error!");

    portENTER_CRITICAL(&tp->data.lock);

    /* Number of touched points */
    tp->data.points = points;

    /* Fill all coordinates */
    for (i = 0; i < points; i++) {
        tp->data.coords[i].x = (((uint16_t)data[(i * 6) + 0] & 0x0f) << 8) + data[(i * 6) + 1];
        tp->data.coords[i].y = (((uint16_t)data[(i * 6) + 2] & 0x0f) << 8) + data[(i * 6) + 3];
    }

    portEXIT_CRITICAL(&tp->data.lock);

    return ESP_OK;
}

static bool esp_lcd_touch_touch_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    assert(tp != NULL);
    assert(x != NULL);
    assert(y != NULL);
    assert(point_num != NULL);
    assert(max_point_num > 0);

    portENTER_CRITICAL(&tp->data.lock);

    /* Count of points */
    *point_num = (tp->data.points > max_point_num ? max_point_num : tp->data.points);

    for (size_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;

        if (strength) {
            strength[i] = tp->data.coords[i].strength;
        }
    }

    /* Invalidate */
    tp->data.points = 0;

    portEXIT_CRITICAL(&tp->data.lock);

    return (*point_num > 0);
}

static esp_err_t esp_lcd_touch_touch_del(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    /* Reset GPIO pin settings */
    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.int_gpio_num);
        if (tp->config.interrupt_callback) {
            gpio_isr_handler_remove(tp->config.int_gpio_num);
        }
    }

    /* Reset GPIO pin settings */
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.rst_gpio_num);
    }

    free(tp);

    return ESP_OK;
}

/*******************************************************************************
* Private API function
*******************************************************************************/

static esp_err_t touch_touch_init(esp_lcd_touch_handle_t tp)
{
    esp_err_t ret = ESP_OK;

    // Valid touching detect threshold
    ret |= touch_touch_i2c_write(tp, TOUCH_ID_G_THGROUP, 70);

    // valid touching peak detect threshold
    ret |= touch_touch_i2c_write(tp, TOUCH_ID_G_THPEAK, 60);

    // Touch focus threshold
    ret |= touch_touch_i2c_write(tp, TOUCH_ID_G_THCAL, 16);

    // threshold when there is surface water
    ret |= touch_touch_i2c_write(tp, TOUCH_ID_G_THWATER, 60);

    // threshold of temperature compensation
    ret |= touch_touch_i2c_write(tp, TOUCH_ID_G_THTEMP, 10);

    // Touch difference threshold
    ret |= touch_touch_i2c_write(tp, TOUCH_ID_G_THDIFF, 20);

    // Delay to enter 'Monitor' status (s)
    ret |= touch_touch_i2c_write(tp, TOUCH_ID_G_TIME_ENTER_MONITOR, 2);

    // Period of 'Active' status (ms)
    ret |= touch_touch_i2c_write(tp, TOUCH_ID_G_PERIODACTIVE, 12);

    // Timer to enter 'idle' when in 'Monitor' (ms)
    ret |= touch_touch_i2c_write(tp, TOUCH_ID_G_PERIODMONITOR, 40);

    return ret;
}

/* Reset controller */
static esp_err_t touch_touch_reset(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_OK;
}

static esp_err_t touch_touch_i2c_write(esp_lcd_touch_handle_t tp, uint8_t reg, uint8_t data)
{
    assert(tp != NULL);

    // *INDENT-OFF*
    /* Write data */
    return esp_lcd_panel_io_tx_param(tp->io, reg, (uint8_t[]){data}, 1);
    // *INDENT-ON*
}

static esp_err_t touch_touch_i2c_read(esp_lcd_touch_handle_t tp, uint8_t reg, uint8_t *data, uint8_t len)
{
    assert(tp != NULL);
    assert(data != NULL);

    /* Read data */
    return esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
}

/**
 * @brief 直接从数据寄存器读取触摸和手势数据
 */
static esp_err_t read_touch_gesture_data(esp_lcd_touch_handle_t tp, uint8_t *data_buf, size_t buf_len)
{
    // 从0xE0寄存器读取数据（根据您的芯片手册）
    return touch_touch_i2c_read(tp, 0xE0, data_buf, buf_len);
}

/**
 * @brief 处理触摸坐标点
 */
static void handle_touch_points(uint8_t *buf)
{
    // 解析触摸坐标（根据您的芯片数据格式调整）
    uint16_t touchX = buf[2];  // 可能需要组合高低字节
    uint16_t touchY = buf[4];
    
    // ESP_LOGI(TAG, "Touch point: X=%d, Y=%d", touchX, touchY);
    // snprintf(ui_state.status_text, sizeof(ui_state.status_text), "Touch: X=%d Y=%d", touchX, touchY);
    
    ui_state.needs_redraw = true;
}

/**
 * @brief 处理点击手势
 */
static void handle_tap_gesture(const char *gesture_name)
{
    ui_state.tap_count++;
    ESP_LOGI(TAG, "%s detected", gesture_name);
    snprintf(ui_state.status_text, sizeof(ui_state.status_text), "%s", gesture_name);
    snprintf(ui_state.direction_text, sizeof(ui_state.direction_text), "TAP");
    
    ui_state.double_state = true;
    ui_state.needs_redraw = true;
}

/**
 * @brief 处理滑动手势
 */
static void handle_flick_gesture(esp_lcd_touch_handle_t tp, uint8_t *buf)
{
    // 假设方向信息在buf[10]（根据您的芯片手册调整）
    uint8_t direction = buf[10];
    const char *dir_name = "";
    
    switch (direction) {
        case UP:
            if (ui_state.y_offset > 40) {
                ui_state.y_offset -= 5;
                ui_state.c_x_offset = ui_state.x_offset - 1;
                ui_state.c_y_offset = ui_state.y_offset + 48;
                ui_state.width = 50;
                ui_state.height = 6;
            }
            dir_name = "UP";
            break;
            
        case RIGHT:
            if (ui_state.x_offset < 152) {
                ui_state.x_offset += 5;
                ui_state.c_x_offset = ui_state.x_offset - 6;
                ui_state.c_y_offset = ui_state.y_offset - 1;
                ui_state.width = 6;
                ui_state.height = 50;
            }
            dir_name = "RIGHT";
            break;
            
        case DOWN:
            if (ui_state.y_offset < 152) {
                ui_state.y_offset += 5;
                ui_state.c_x_offset = ui_state.x_offset - 1;
                ui_state.c_y_offset = ui_state.y_offset - 6;
                ui_state.width = 50;
                ui_state.height = 6;
            }
            dir_name = "DOWN";
            break;
            
        case LEFT:
            if (ui_state.x_offset > 40) {
                ui_state.x_offset -= 5;
                ui_state.c_x_offset = ui_state.x_offset + 48;
                ui_state.c_y_offset = ui_state.y_offset - 1;
                ui_state.width = 6;
                ui_state.height = 50;
            }
            dir_name = "LEFT";
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown direction: 0x%02X", direction);
            return;
    }
    
    ESP_LOGI(TAG, "Flick gesture: %s", dir_name);
    snprintf(ui_state.status_text, sizeof(ui_state.status_text), "Flick: %s", dir_name);
    snprintf(ui_state.direction_text, sizeof(ui_state.direction_text), "%s", dir_name);
    
    ui_state.slide_state = true;
    ui_state.needs_redraw = true;
}

/**
 * @brief 处理手势
 */
static void handle_gestures(esp_lcd_touch_handle_t tp, uint8_t *buf)
{
    uint8_t gesture_type = buf[1];
    
    switch (gesture_type) {
        case TAP:
            handle_tap_gesture("Single Tap");
            break;
            
        case DOUBLE_TAP:
            handle_tap_gesture("Double Tap");
            break;
            
        case FLICK:
            handle_flick_gesture(tp, buf);
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown gesture: 0x%02X", gesture_type);
            snprintf(ui_state.status_text, sizeof(ui_state.status_text), "Unknown Gesture: 0x%02X", gesture_type);
            ui_state.needs_redraw = true;
            break;
    }
}

/**
 * @brief 处理触摸点和手势
 */
bool process_touch_and_gestures(esp_lcd_touch_handle_t tp)
{
    uint8_t buf[32] = {0};
    esp_err_t ret;
    
    // 读取触摸数据
    ret = read_touch_gesture_data(tp, buf, sizeof(buf));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read touch data: %s", esp_err_to_name(ret));
        return false;
    }
    
    bool data_processed = false;
    
    // 检查是否有报点（触摸坐标）
    if (buf[0] & POINT) {
        handle_touch_points(buf);
        data_processed = true;
    }
    
    // 检查是否有手势
    if (buf[0] & GESTURES) {
        handle_gestures(tp, buf);
        data_processed = true;
    }
    
    return data_processed;
}


/**
 * @brief 获取UI状态
 */
touch_ui_state_t* get_touch_ui_state(void)
{
    return &ui_state;
}

/**
 * @brief 重置重绘标志
 */
void reset_touch_redraw_flag(void)
{
    ui_state.needs_redraw = false;
}

/**
 * @brief 初始化触摸UI状态
 */
void init_touch_ui_state(int16_t offset_x, int16_t offset_y)
{
    memset(&ui_state, 0, sizeof(ui_state));
    snprintf(ui_state.status_text, sizeof(ui_state.status_text), "Ready");
    snprintf(ui_state.direction_text, sizeof(ui_state.direction_text), "NONE");

    ui_state.x_offset = offset_x;
    ui_state.y_offset = offset_y;
}


bool get_touch_xy(esp_lcd_touch_handle_t tp, uint16_t* x, uint16_t* y){
    uint8_t buf[32] = {0};
    esp_err_t ret;
   
    //读取触摸数据
   ret = read_touch_gesture_data(tp, buf, sizeof(buf));    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read touch data: %s", esp_err_to_name(ret));
        return false;
    }   

   if(buf[0] & POINT){
        // 4.解析触摸坐标（根据您的芯片数据格式调整）
        *x = buf[2];  // 可能需要组合高低字节
        *y = buf[4];

        return true;
   }
return false;
}
