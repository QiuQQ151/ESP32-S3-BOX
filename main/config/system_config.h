// main/config/system_config.h
#ifndef CONFIG_SYSTEM_CONFIG_H
#define CONFIG_SYSTEM_CONFIG_H
/*
* 系统配置文件
*/

// 任务优先级配置
#define TASK_PRIO_DEBUG 1
#define TASK_PRIO_HAL 2
#define TASK_PRIO_KEY_HAL 2
#define TASK_PRIO_EXTIO_HAL 2

#define TASK_PRIO_LED_SERVICE 3
#define TASK_PRIO_AUDIO_SERVICE 3
#define TASK_PRIO_SNTP_SERVICE 2
#define TASK_PRIO_UI_SERVICE 3
#define TASK_PRIO_WIFI_SERVICE 3

#define TASK_PRIO_MUSIC_APP 3
#define TASK_PRIO_RADIO_APP 3

// 队列任务
#define QUEUE_HAL_SIZE 50
#define QUEUE_SERVICE_SIZE 50
#define QUEUE_APP_SIZE 50

// 任务使用的核心
#define TASK_CORE_HAL 1
#define TASK_CORE_SERVICE 1
#define TASK_CORE_APP 1

// WIFI配置
#define WIFI_AP_SSID            "QQQ-BOX-V1.1"
#define WIFI_AP_PASSWORD        "1234567890"
#define WIFI_AP_MAX_CONNECTIONS 4
#define WIFI_AP_CHANNEL         6
#define WIFI_MAX_RETRY_COUNT    5
#define WIFI_RETRY_INTERVAL_MS  10000

/* SNTP服务器配置 */
#define PRIMARY_SNTP_SERVER "ntp.aliyun.com"
#define SECONDARY_SNTP_SERVER "cn.ntp.org.cn"
#define TIME_ZONE "CST-8" // 中国标准时区

// ---------- 音乐文件定义 ----------
#define MUSIC_ROOT_PATH            "/sdcard/music"        // 音乐根目录
#define MUSIC_CACHE_FILE_PATH       "/sdcard/music/.track_cache"  // 缓存文件
#define MUSIC_MAX_TRACKS            500


#endif