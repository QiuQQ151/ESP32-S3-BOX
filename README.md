# ESP32-S3-BOX
基于ESP32的桌面盒子
# 桌面 Box 软件框架设计

## 1. 系统概述
本项目是一个基于 ESP32-S3 的多功能桌面摆件，运行 FreeRTOS 与 LVGL，通过环形灯带、音频系统、传感器、触摸屏和网络实现：  
网页配网、环形时钟/番茄钟、音乐播放器（SD卡 + 网络FM）、语音对话、小夜灯、天气提醒、红外遥控等功能。  
**设计核心**：模块高内聚低耦合，以事件驱动和消息队列完成任务间通信，硬件细节封装在 HAL 层，服务组件可插拔，预留扩展口动态识别机制。

## 2. 硬件接口汇总

| 外设                    | 接口类型           | 说明                                                                 |
|------------------------|--------------------|----------------------------------------------------------------------|
| 主屏幕（GC9A01）        | SPI                | 圆形 LCD，分辨率 240x240，SPI 模式                                   |
| 触摸屏（驱动待确认）    | I2C                | 与其它 I2C 设备共用，需确认地址                                      |
| 前面板LED（30颗WS2812B）| RMT1               | GPIO 直连，RGB 彩灯，环形排布                                        |
| 扩展口LED（10颗WS2812B）| RMT2               | GPIO 直连，用于磁吸扩展装饰                                          |
| 亮度传感器              | ADC（GPIO）        | 通过 IO 读取分压电压，获取环境亮度                                    |
| 温湿度传感器            | I2C                | SHT30 / DHT22 等，具体型号待定                                       |
| 红外发射管              | GPIO (输出)        | 控制空调等红外设备，需 38kHz 载波调制                                  |
| ES8311 音频 DAC         | I2S0 (共享)        | 播放输出，与 ES7210 共享 I2S 总线                                    |
| ES7210 麦克风采集       | I2S0 (共享)        | 录音输入，使用 I2S 时分复用或双通道（需 ESP-ADF 支持）                |
| 功放使能                | GPIO               | 高电平启用功放，节省功耗                                              |
| SD 卡                   | SDMMC              | 存储音乐文件、字体、配置等，采用 4-bit 模式以提升性能                |
| 旋转编码器（含按键）    | GPIO (A/B/Key)     | 旋转方向、步数及按键事件                                              |
| 通用 Key 按键           | GPIO               | 辅助功能键，消抖处理                                                  |
| Power 键                | I2C GPIO 扩展      | 物理按键状态通过 I2C 扩展芯片读取；短按息屏，长按唤醒对话，超长按关机 |
| 开机保持电源            | ESP32 直连 GPIO    | 上电立即拉高，锁存供电电路；关机时拉低切断电源                        |
| I2C GPIO 扩展芯片       | I2C                | （如 PCA9554）扩展 IO，用于读取 Power 键、扩展口电源使能等           |
| 磁吸扩展口              | SPI + 控制IO       | 支持双 SPI 屏幕、UART 或普通 IO，含霍尔传感器检测插拔                |
| 霍尔传感器              | GPIO（输入）       | 检测扩展设备是否接入                                                  |
| 扩展口电源使能          | I2C GPIO 扩展      | 控制扩展口供电，可独立开关                                             |
| 扩展口复用控制          | ESP32 直连 GPIO    | 可配置为 SPI CS/UART / 普通 IO，根据识别结果切换模式                  |
| USB 检测 / 电池电压     | ADC（预留）        | 硬件暂不支持，预留 ADC 通道及服务接口，后续适配电源管理 IC            |

## 3. 软件架构分层
```raw
┌──────────────────────────────────────────────┐
│        Application (应用任务 & UI)           │
│  桌面 时钟 音乐 FM 语音 天气 设置 红外       │
├──────────────────────────────────────────────┤
│        Service Layer (业务逻辑组件)          │
│  时钟服务 LED管理器 音频管理 网络管理        │
│  传感器聚合 语音引擎 红外控制 扩展口管理      │
├──────────────────────────────────────────────┤
│        Framework & Libraries                 │
│  ESP-ADF (audio)  LVGL  FreeRTOS  fatfs      │
│  lwIP  mbedTLS  esp-sr  esp-http-client      │
├──────────────────────────────────────────────┤
│        Hardware Abstraction Layer (HAL)      │
│  显示驱动 触摸驱动 音频驱动 LED驱动          │
│  传感器驱动 红外驱动 电源管理 扩展口驱动      │
└──────────────────────────────────────────────┘
```

## 4. FreeRTOS 任务设计

| 任务名            | 优先级 | 核心职责                                                       | 栈建议 |
|-------------------|--------|----------------------------------------------------------------|--------|
| `ui_task`         | 8 (高) | LVGL 刷新（主屏，可选扩展屏）、输入事件分发、界面状态机       | 8 KB   |
| `audio_task`      | 7      | 音乐播放/网络FM解码输出、音量控制、音频焦点管理                | 8 KB   |
| `led_task`        | 6      | 前面板 & 扩展口 LED 动画刷新（帧率 50Hz），接收模式指令         | 4 KB   |
| `voice_task`      | 6      | 语音唤醒、对话流程（deepseek API）、TTS 播放、语音识别后处理   | 10 KB  |
| `clock_task`      | 5      | 系统时间维护、NTP 同步、闹钟触发、番茄钟倒计时                  | 3 KB   |
| `network_task`    | 5      | Wi-Fi 管理、HTTP 请求（天气、FM列表）、OTA 等                  | 6 KB   |
| `sensor_task`     | 4      | 周期性采集亮度（ADC）、温湿度（I2C），发布数据                  | 2 KB   |
| `ext_mgr_task`    | 4      | 扩展口插拔检测、设备识别、动态驱动加载、模式切换                | 4 KB   |
| `sysmon_task`     | 3      | Power键动作解析（短按/长按/超长按）、通用按键扫描、心跳        | 2 KB   |
| `ir_task`         | 4      | 红外码库管理、发射调度（使用硬件定时器产生载波）                | 3 KB   |

**任务通信机制**：使用 FreeRTOS 队列、事件组、环形缓冲区；所有 LVGL 操作仅在 `ui_task` 中执行。

## 5. 核心模块详解

### 5.1 输入系统
- 所有物理输入抽象为 `input_event_t`，包含来源、事件类型和参数。
- **Power 键**（由 `sysmon_task` 管理）：
  - 短按（<500ms）：触发背光开关（息屏/亮屏）。
  - 长按（500ms~2s）：唤醒大模型对话。
  - 超长按（>5s）：执行系统关机（保存数据，拉低 PWR_HOLD）。
- 旋转编码器旋转通过中断+队列直接发送至 UI；通用按键消抖后发送事件。

### 5.2 显示系统
- 主显示器 GC9A01 驱动适配 LVGL，使用 SPI 总线，双缓冲可选。
- 界面资源策略：核心图标烧录于 NVS/Flash，保证无 SD 卡时系统可用；扩展资源（高清图标、字体）从 SD 卡加载。
- 扩展屏幕支持：预留多显示管理器，可动态注册第二个 SPI 屏幕。

### 5.3 灯带管理 (LED Manager)
- 双通道独立控制：前面板 30 颗（RMT1），扩展口 10 颗（RMT2）。
- 提供动画效果：时钟表盘、倒计时环、夜灯呼吸、天气预警等。
- 统一由 `led_task` 按 50Hz 刷新，避免资源竞争。

### 5.4 时钟服务
- 基于 ESP32 RTC + NTP 周期校准，支持多组闹钟和番茄钟倒计时。
- 所有时间相关事件通过队列广播给 UI 和 LED 任务。

### 5.5 音频系统 (基于 ESP-ADF)
- **音乐播放**：支持 SDMMC 本地文件（MP3/AAC/WAV/FLAC）和 HTTP 网络流。
- **网络 FM**：播放自定义流地址，频道列表可在线更新。
- **语音对话**：
  - 支持语音唤醒词和 Power 长按两种唤醒方式。
  - 对话流程通过 WebSocket 连接 deepseek API，TTS 语音应答。
  - 音频焦点管理：对话时暂停音乐，结束后自动恢复。

### 5.6 其他服务
- **红外控制**：码库管理，使用 RMT 产生 38kHz 载波发射。
- **扩展口管理**：霍尔传感器检测插拔，动态识别设备（屏幕/UART/IO）并加载对应驱动。
- **传感器聚合**：周期性采集亮度、温湿度，广播数据。
- **电源管理**：PWR_HOLD 维持供电，预留电池/电压检测接口。

## 6. LVGL 界面架构
- **桌面**：横向滚动图标列表（时钟、音乐、FM、语音、天气、设置、红外等），支持触摸滑动和编码器选择。
- **多屏策略**：默认界面作用于主屏，扩展屏通过 `lv_disp_set_default()` 切换。
- **背光控制**：息屏仅关闭背光，LVGL 继续运行，触摸/按键唤醒。
- **资源降级**：无 SD 卡时自动使用内置精简图标，并隐藏依赖 SD 的功能入口。

## 7. 目录结构
```text
desk_box/
├── main/
│   ├── main.c                     // 入口，任务创建
│   ├── app/                       // 应用界面
│   │   ├── ui_desktop.c
│   ├── services/                  // 业务服务
│   │   ├── sntp_service.c/h
│   │   ├── wifi_service.c/h
│   ├── hal/                       // 硬件抽象
│   │   ├── wifi_hal.c/h
│   ├── config/
│   │   ├── pin_defs.h
│   │   └── system_config.h
│   ├── assets/                    // 内嵌图标/字体（存于Flash）
│   └── web/                       // 配网页
├── components/                    // esp-adf 等
├── partitions.csv
└── README.md
```

# 文件模板
## service模板
```c
#ifndef SERVICES_EVENT_xxx_SERVICE_H
#define SERVICES_EVENT_xxx_SERVICE_H


// =========================服务对外接口======================================================

// ===============请求服务
typedef enum {
    _CMD_ = 0,             // xxxx
} _service_cmd_t;
typedef struct {
    uint32_t cmd;  // 请求服务
    QueueHandle_t reply_queue;   // 结果通知队列 (可为 NULL)
} _service_receive_data_t;


// ==================服务回复
typedef enum {
    // 当前状态
    _SRV_OK = 0,    
    _SRV_STATE_ERROR, // 响应错误
} _service_state_t;

// 服务发给reply_queue的数据结构
typedef struct {
    // ......

    xxxx_service_state_t service_stata;    // 服务回复
} _service_send_data_t;


esp_err_t _service_init(void);  // 初始化  服务（由系统启动）   
int get_xxxx_service_ID(void);

#endif
```
```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "system_config.h"
#include "event_loop_service.h"
#include "xxx_service.h"

static const char *TAG = "_service";
static int _service_ID = 0; // 非0才是有效注册

static void _service_task(void *arg); // 服务任务
static void handle_(xxxxx_service_receive_data_t *payload); 
static void handle_reply(QueueHandle_t reply_queue);
static QueueHandle_t  _service_request_queue; // 接收来自事件循环的 app_event_t*

// ===============api==========================
esp_err_t _service_init(void)
{
    ESP_LOGI(TAG, "Initializing xxxx");
    // 初始化内容


    // 创建外部请求队列
    _service_request_queue = xQueueCreate(8, sizeof(app_event_t*));
    if (! _service_request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return ESP_ERR_NO_MEM;
    }

    // 向事件循环注册
    xxx_service_ID = event_loop_register_service("_service_task", _service_request_queue);
    // 启动服务任务
    xTaskCreate(_service_task, "_service_task", 2048, NULL, 5, NULL);  // 对外服务
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

int get_xxx_service_ID(void){
    return xxx_service_ID;
}



// 服务任务
static void xxx_service_task(void *arg){
   
    // 分发服务请求
    _service_receive_data_t *payload;
    while (1) {
        if (xQueueReceive( _service_request_queue, &payload, portMAX_DELAY) == pdTRUE) {
            switch (payload->cmd) {
                case xxxx: {
                    // 
                    handle_(payload);
                    break;
                }           
                default:
                    ESP_LOGW(TAG, "Unknown cmd: 0x%lx", payload->cmd);
                    break;
            }
            free(payload); // 释放服务请求数据的内存
        }
    }
} 
    
void handle_(xxx_service_receive_data_t *payload)
{
    // 具体操作

    // 回复请求
    if( payload->reply_queue){
        handle_reply( payload->reply_queue);
    }
}

// 处理回复
static void handle_reply(QueueHandle_t reply_queue){
    xxx_service_send_data_t *payload = malloc(sizeof(_service_send_data_t));
    // 拷贝数据
    // ...

    xQueueSend(reply_queue, &payload, 0);
}

```


## app应用模板

```c
#ifndef TEMPLATE_APP_H
#define TEMPLATE_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册应用到 UI 服务
 * 
 * 调用此函数将应用添加到系统中，之后可通过 ui_service_open_app("template_app") 打开。
 */
void template_app_register(void);

#ifdef __cplusplus
}
#endif

#endif // TEMPLATE_APP_H
```


```c
#include "template_app.h"
#include "ui_service.h"
#include "esp_log.h"
#include "lvgl.h"
#include "keys_hal.h"
#include <inttypes.h>   // for PRIu32

// 应用标签（用于日志）
static const char *TAG = "template_app";

// 全局应用实例
static ui_app_t s_template_app;

// ---------- 回调函数声明 ----------
static void template_on_create(ui_app_t *app);
static void template_on_resume(ui_app_t *app);
static void template_on_pause(ui_app_t *app);
static void template_on_destroy(ui_app_t *app);
static bool template_on_key_event(ui_app_t *app, void *key_event);
static void template_on_receive_data(ui_app_t *app, uint32_t cmd, void *data, size_t len);

// ---------- 注册函数实现 ----------
void template_app_register(void)
{
    s_template_app.name = "template_app";               // 应用唯一标识名
    s_template_app.screen = NULL;
    s_template_app.on_create = template_on_create;
    s_template_app.on_resume = template_on_resume;
    s_template_app.on_pause = template_on_pause;
    s_template_app.on_destroy = template_on_destroy;
    s_template_app.on_key_event = template_on_key_event;
    s_template_app.on_receive_data = template_on_receive_data;

    ui_service_register_app(&s_template_app);
    ESP_LOGI(TAG, "Template app registered");
}

// ---------- 回调函数实现 ----------

/**
 * @brief 创建应用界面（首次打开时调用）
 */
static void template_on_create(ui_app_t *app)
{
    ESP_LOGI(TAG, "on_create");

    // 创建主屏幕
    app->screen = lv_obj_create(NULL);
    if (!app->screen) {
        ESP_LOGE(TAG, "Failed to create screen");
        return;
    }

    // ========== 在此添加你的 UI 控件 ==========
    // 示例：添加一个标签
    lv_obj_t *label = lv_label_create(app->screen);
    lv_label_set_text(label, "Template App");
    lv_obj_center(label);
    // ========================================
}

/**
 * @brief 应用从后台恢复到前台时调用
 */
static void template_on_resume(ui_app_t *app)
{
    ESP_LOGI(TAG, "on_resume");
    // 刷新数据、恢复播放等
}

/**
 * @brief 应用被切换到后台时调用
 */
static void template_on_pause(ui_app_t *app)
{
    ESP_LOGI(TAG, "on_pause");
    // 暂停播放、保存状态等
}

/**
 * @brief 应用被销毁时调用（返回桌面清空栈时触发）
 */
static void template_on_destroy(ui_app_t *app)
{
    ESP_LOGI(TAG, "on_destroy");
    if (app->screen) {
        lv_obj_del(app->screen);
        app->screen = NULL;
    }
    // 释放其他动态分配的资源
}

/**
 * @brief 按键事件处理
 * @param app      当前应用实例
 * @param key_event 按键数据结构（key_event_data_t*）
 * @return true 表示事件已处理，系统不再执行默认行为；
 *         false 表示未处理，系统会执行默认行为（如 BACK 键触发返回）
 */
static bool template_on_key_event(ui_app_t *app, void *key_event)
{
    key_event_data_t *key = (key_event_data_t *)key_event;

    if (key->event == KEY_EVENT_PRESS) {
        switch (key->key_id) {
            case KEY_ID_ENTER:
                ESP_LOGI(TAG, "ENTER pressed");
                // TODO: 处理确认操作
                return true;   // 已处理

            // 如果硬件支持方向键，可取消注释以下代码
            // case KEY_ID_UP:
            //     ESP_LOGI(TAG, "UP pressed");
            //     return true;
            // case KEY_ID_DOWN:
            //     ESP_LOGI(TAG, "DOWN pressed");
            //     return true;
            // case KEY_ID_LEFT:
            //     ESP_LOGI(TAG, "LEFT pressed");
            //     return true;
            // case KEY_ID_RIGHT:
            //     ESP_LOGI(TAG, "RIGHT pressed");
            //     return true;

            default:
                break;
        }
    }
    // 未处理的按键（如 BACK 键）将交给系统默认处理
    return false;
}

/**
 * @brief 接收其他服务发来的自定义数据
 * @param cmd   命令码（可自定义）
 * @param data  数据指针
 * @param len   数据长度
 */
static void template_on_receive_data(ui_app_t *app, uint32_t cmd, void *data, size_t len)
{
    ESP_LOGI(TAG, "Received custom command: %" PRIu32 ", len=%d", cmd, len);
    // TODO: 根据 cmd 处理不同消息
}
```