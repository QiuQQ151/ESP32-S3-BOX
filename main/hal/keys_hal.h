#ifndef KEY_HAL_H
#define KEY_HAL_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 按键事件类型
 */
typedef enum {
    KEY_EVENT_PRESS,       // 按键按下
    KEY_EVENT_RELEASE,     // 按键释放
    KEY_EVENT_ROTATE_CW,   // 顺时针旋转（正转）
    KEY_EVENT_ROTATE_CCW   // 逆时针旋转（反转）
} key_event_type_t;

/**
 * @brief 按键 ID 定义
 */
typedef enum {
    KEY_ID_ENCODER_SW = 0,   // 编码器上的按键
    KEY_ID_BACK,        // 独立按键1
    KEY_ID_ENTER        // 独立按键2
} key_id_t;

/**
 * @brief 按键事件数据结构（将作为 app_event_t 的 payload 发送给服务层）
 */
typedef struct {
    key_id_t         key_id;      // 哪个按键
    key_event_type_t event;       // 事件类型
    int              rotate_diff; // 旋转差值（仅对旋转事件有效，正转+1，反转-1）
} key_event_data_t;

/**
 * @brief 初始化按键 HAL 层
 *      - ESP_OK: 成功
 *      - ESP_ERR_NO_MEM: 内存不足
 *      - ESP_ERR_INVALID_ARG: 参数错误
 */
esp_err_t key_hal_init(void);

#ifdef __cplusplus
}
#endif

#endif // KEY_HAL_H