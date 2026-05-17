#ifndef SERVICES_UI_SERVICE_H
#define SERVICES_UI_SERVICE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "system_event.h"

#ifdef __cplusplus
extern "C" {
#endif

// ========================= 服务对外接口 ========================

// ---------- UI 服务可处理的命令 ----------
typedef enum {
    UI_CMD_OPEN_APP = 0,      // 打开指定应用（payload 为应用名字符串）
    UI_CMD_GO_HOME,           // 回到桌面（关闭当前应用，打开桌面）
    UI_CMD_UPDATE_WIDGET,     // 更新桌面小组件（payload 自定义）
    UI_CMD_CUSTOM             // 扩展自定义命令，将原样转发给当前应用
} ui_service_cmd_t;

// 接收数据结构（服务队列元素）
typedef struct {
    ui_service_cmd_t cmd;     // 命令
    void *data;               // 附带数据（由发送方分配，处理方负责释放）
    size_t data_len;          // 数据长度
} ui_service_receive_data_t;

// ---------- 服务回复（可选） ----------
typedef enum {
    UI_SERVICE_OK = 0,
    UI_SERVICE_ERROR,
} ui_service_state_t;

typedef struct {
    ui_service_state_t state;
    void *data;
    size_t data_len;
} ui_service_reply_data_t;

// ---------- 应用接口 ----------
typedef struct ui_app {
    const char *name;                       // 应用名称（唯一标识）
    lv_obj_t *screen;                       // 主屏幕对象（首次 on_create 后赋值）

    // 生命周期回调
    void (*on_create)(struct ui_app *app);  // 屏幕首次创建时调用
    void (*on_open)(struct ui_app *app);    // 应用被打开/切换到前台时调用
    void (*on_close)(struct ui_app *app);   // 应用被关闭/离开前台时调用
    void (*on_destroy)(struct ui_app *app); // 应用资源释放时调用（可选）

    // 按键处理（可选，若实现 on_event 可不用此回调）
    bool (*on_key_event)(struct ui_app *app, void *key_event); // key_event 为 key_event_data_t*

    // 通用事件处理：接收所有通知（包括按键）及未处理的请求。
    // event 的所有权转移给应用，应用负责释放 event 及其内部 data。
    void (*on_event)(struct ui_app *app, event_data_t *event);
} ui_app_t;

// ---------- 公开 API ----------
esp_err_t ui_service_init(void);                        // 初始化 UI 服务
void ui_service_register_app(ui_app_t *app);            // 注册应用
void ui_service_open_app(const char *name);             // 打开指定应用
void ui_service_go_home(void);                          // 回到桌面（关闭当前应用）
QueueHandle_t get_ui_service_queue(void);               // 获取 UI 服务请求队列

// 获取上一次从桌面启动的应用名（桌面应用可据此高亮对应图标）
const char* ui_service_get_last_launched_app(void);

#ifdef __cplusplus
}
#endif

#endif // SERVICES_UI_SERVICE_H


// #ifndef SERVICES_UI_SERVICE_H
// #define SERVICES_UI_SERVICE_H

// #include "freertos/FreeRTOS.h"
// #include "freertos/queue.h"
// #include "lvgl.h"
// #include "keys_hal.h"
// #include "lvgl_hal.h"
// #include "sd_hal.h"

// #ifdef __cplusplus
// extern "C" {
// #endif

// // ========================= 服务对外接口 ========================

// // ---------- 请求服务命令 ----------
// typedef enum {
//     UI_CMD_KEY_EVENT = 0,       // 按键事件（payload 为 key_event_data_t*）
//     UI_CMD_UPDATE_WIDGET,       // 更新桌面小组件（payload 自定义）
//     UI_CMD_OPEN_APP,            // 打开指定应用（payload 为 app_name 字符串）
//     UI_CMD_GO_BACK,             // 返回上一个应用
//     UI_CMD_TO_HOME,             // 回到桌面
//     UI_CMD_CLOSE_APP,           // 关闭当前应用
//     UI_CMD_CUSTOM               // 扩展自定义命令
// } ui_service_cmd_t;

// // 接收数据结构（服务队列元素）
// typedef struct {
//     ui_service_cmd_t cmd;       // ui_service_cmd_t
//     void *data;                 // 附带数据（由发送方分配，服务方负责释放）
//     size_t data_len;            // 数据长度
// } ui_service_receive_data_t;

// // ---------- 服务回复 ----------
// typedef enum {
//     UI_SERVICE_OK = 0,
//     UI_SERVICE_ERROR,
// } ui_service_state_t;

// typedef struct {
//     ui_service_state_t state;   // 服务状态
//     void *data;                 // 回复数据（由服务分配，调用方释放）
//     size_t data_len;
// } ui_service_reply_data_t;

// // ---------- 应用接口（每个应用需实现） ----------
// typedef struct ui_app {
//     const char *name;                       // 应用名（用于查找）
//     lv_obj_t *screen;                       // 主屏幕对象（首次创建后赋值）
//     void (*on_create)(struct ui_app *app);  // 屏幕创建时调用
//     void (*on_resume)(struct ui_app *app);  // 从后台恢复时调用
//     void (*on_pause)(struct ui_app *app);   // 切到后台时调用
//     void (*on_destroy)(struct ui_app *app); // 退出应用时调用（可销毁屏幕）
//     // 按键处理：返回 true 表示已处理该事件，否则由系统默认处理
//     bool (*on_key_event)(struct ui_app *app, void *key_event); // key_event 为 key_event_data_t*
//     // 接收其他服务发来的自定义数据
//     void (*on_receive_data)(struct ui_app *app, int service_id, void *data, size_t len);
// } ui_app_t;

// // ---------- 公开 API ----------
// esp_err_t ui_service_init(void);            // 初始化 UI 服务
// int get_ui_service_ID(void);                // 获取服务 ID
// void ui_service_register_app(ui_app_t *app); // 注册应用
// void ui_service_open_app(const char *name); // 打开应用（内部调用，可在事件回调中使用）
// void ui_service_go_back(void);              // 返回
// void ui_service_go_home(void);              // 回桌面
// QueueHandle_t get_ui_service_queue(void);  // 获取 UI 服务的请求队列（供其他服务发送命令用）

// #ifdef __cplusplus
// }
// #endif

// #endif // SERVICES_UI_SERVICE_H