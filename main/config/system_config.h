





// main/config/system_config.h
#ifndef CONFIG_SYSTEM_CONFIG_H
#define CONFIG_SYSTEM_CONFIG_H

#include "sdkconfig.h"

#define WIFI_AP_SSID            "DeskBox_Setup"
#define WIFI_AP_PASSWORD        "12345678"
#define WIFI_AP_MAX_CONNECTIONS 4
#define WIFI_AP_CHANNEL         6

#define WIFI_MAX_RETRY_COUNT    5
#define WIFI_RETRY_INTERVAL_MS  10000

/* SNTP服务器配置 */
#define PRIMARY_SNTP_SERVER "ntp.aliyun.com"
#define SECONDARY_SNTP_SERVER "cn.ntp.org.cn"
#define TIME_ZONE "CST-8" // 中国标准时区

#endif