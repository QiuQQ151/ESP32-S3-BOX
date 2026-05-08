// main/services/wifi_service.c
#include "string.h"
#include <stdlib.h>
#include "wifi_service.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "hal/wifi_hal.h"
#include "event_loop_service.h"


static const char *TAG = "wifi_service";

// =======================对内服务内部接口=====================================================
static wifi_service_state_t service_state = WIFI_SRV_STATE_IDLE;// WiFi当前状态

// wifi任务管理句柄
static TaskHandle_t   wifi_service_task_handle;   

static void wifi_service_task(void *arg);  // 服务任务
static void handle_connect(const wifi_service_receive_data_t *payload); // 处理连接请求
static void handle_connect_save(const wifi_service_receive_data_t *payload); // 处理连接请求
static void handle_disconnect(const wifi_service_receive_data_t *payload); // 处理断开连接请求
static void handle_cheack_stata(const wifi_service_receive_data_t *payload);
static void handle_reply(QueueHandle_t reply_queue);



// 内部子函数
static char sr_ssid[33];
static char sr_password[65];
static char sr_ip_address[20];
static int8_t sr_rsssi;
static bool s_has_credentials;// NVS有无保存的wifi
static bool load_credentials(void); // 加载wifi信息
static bool save_credentials(const char *ssid, const char *password);  // 保存WiFi信息

static QueueHandle_t   wifi_service_request_queue; // 接收来自事件循环的 app_event_t*
static int wifi_service_ID = 0;
// ==================== 对外 API ====================


esp_err_t wifi_service_init(void)
{
    // 1. 初始化 HAL
    ESP_ERROR_CHECK(wifi_hal_init());

    // 2. 加载 NVS 凭证
    load_credentials();

    // 3. 创建外部wifi请求队列
     wifi_service_request_queue = xQueueCreate(8, sizeof(app_event_t*));
    if (! wifi_service_request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return ESP_ERR_NO_MEM;
    }

    // 4. 向事件循环注册
    wifi_service_ID = event_loop_register_service("wifi_service_task", wifi_service_request_queue);

    // 5. 启动服务任务
    xTaskCreate(wifi_service_task, "wifi_service_task", 3584, NULL, 5, &wifi_service_task_handle);
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

// 获取wifi服务的注册id
int get_wifi_service_ID(void){
    return wifi_service_ID;
}

// ==================wifi服务内部函数==================================================

   
// wifi服务任务
static void wifi_service_task(void *arg)
{
    // 分发WiFi服务请求
    wifi_service_receive_data_t *payload;
    while (1) {
        if (xQueueReceive( wifi_service_request_queue, &payload, portMAX_DELAY) == pdTRUE) {
            switch (payload->cmd) {
                case WIFI_CMD_CONNECT: {
                    // 请求连接
                    handle_connect(payload);
                    break;
                }
                case WIFI_CMD_DISCONNECT: {
                    // 断开连接
                    handle_disconnect(payload);
                    break;
                }
                case WIFI_CMD_CONNECT_SAVED:{
                    // 连接NVS中保存的wifi信息
                    handle_connect_save(payload);
                    break;
                } 
                case WIFI_CMD_CHEACK_STATA:{
                    // 查询WiFi状态
                    handle_cheack_stata(payload);
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


// 处理连接请求
static void handle_connect(const wifi_service_receive_data_t *payload){

    ESP_LOGI(TAG, "Connecting to %s...", payload->ssid);
    service_state = WIFI_SRV_STATE_CONNECTING;
    
    // 连接wifi
    esp_err_t ret = wifi_hal_connect(payload->ssid, payload->password);
    if (ret == ESP_OK) {

        service_state = WIFI_SRV_STATE_CONNECTED;
        strcpy(sr_ssid, payload->ssid);
        strcpy(sr_password, payload->password);

        // 保存到NVS
        if (payload->save) {
            save_credentials(payload->ssid, payload->password);
        }

    } else {
        service_state = WIFI_SRV_STATE_ERROR;
    }
    // 构造回复
    if(payload->reply_queue){
         handle_reply(payload->reply_queue);
    }

}

// 处理连接请求
static void handle_connect_save(const wifi_service_receive_data_t *payload){

    ESP_LOGI(TAG, "loading credentials");
    
    if( load_credentials() ){
        ESP_LOGI(TAG, "Connecting to %s...", sr_ssid);
        service_state = WIFI_SRV_STATE_CONNECTING;
        
        // 连接wifi
        esp_err_t ret = wifi_hal_connect(sr_ssid, sr_password);
        if (ret == ESP_OK) {
            service_state = WIFI_SRV_STATE_CONNECTED;
        } else {
            service_state = WIFI_SRV_STATE_ERROR;
        }
        // 构造回复
        if(payload->reply_queue){
            handle_reply(payload->reply_queue);
        }
    }
    else {
      ESP_LOGI(TAG, "loading credentials err");   
    }
}

// 处理断开连接请求
static void handle_disconnect(const wifi_service_receive_data_t *payload){
    esp_err_t ret = wifi_hal_disconnect();
    if( ret == ESP_OK ){
        service_state = WIFI_SRV_STATE_DISCONNECTED;
        strcpy(sr_ssid,  "\0");
        strcpy(sr_password, "\0");
        strcpy(sr_ip_address, "\0");
    } else{
      service_state = WIFI_SRV_STATE_ERROR;
    }
     
    // 构造回复
    if(payload->reply_queue){
        handle_reply(payload->reply_queue);
    }
}

// 检查wifi状态
static void handle_cheack_stata(const wifi_service_receive_data_t *payload){
    // 查询ip
    if (service_state != WIFI_SRV_STATE_CONNECTED) {
        snprintf(sr_ip_address, sizeof(sr_ip_address), "0.0.0.0");
    } 
    else{
        esp_netif_ip_info_t ip;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!netif) {
            snprintf(sr_ip_address, sizeof(sr_ip_address), "0.0.0.0");
        } 
        else{
            esp_netif_get_ip_info(netif, &ip);
            snprintf(sr_ip_address, sizeof(sr_ip_address), IPSTR, IP2STR(&ip.ip));            
        }
    }
    // 
    sr_rsssi = wifi_hal_get_rssi();
    // 构造回复
    if(payload->reply_queue){
        handle_reply(payload->reply_queue);
    }
}

// 回复请求
static void handle_reply(QueueHandle_t reply_queue){

    wifi_service_send_data_t *payload =(wifi_service_send_data_t *)malloc(sizeof(wifi_service_send_data_t));
    if (payload) {
        strcpy(payload->ssid,  sr_ssid);
        strcpy(payload->password, sr_password);
        strcpy(payload->ip_address, sr_ip_address);
        payload->rssi = sr_rsssi;
        payload->service_stata = service_state;
        xQueueSend(reply_queue, &payload, 0);
    } 
    else{
        ESP_LOGE(TAG,"malloc wifi_service_send_data_t err");
    }
}

// 加载wifi信息
static bool load_credentials(void){
    nvs_handle_t nvs;
    if (nvs_open("wifi_cfg", NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGE(TAG,"open NVS err");  
        s_has_credentials = false;
        return false;
    }

    size_t len = sizeof( sr_ssid);
    if (nvs_get_str(nvs, "ssid",  sr_ssid, &len) != ESP_OK) 
        goto fail;

    len = sizeof(sr_password);
    if (nvs_get_str(nvs, "pass", sr_password, &len) != ESP_OK) 
        goto fail;

    nvs_close(nvs);
    s_has_credentials = true;
    ESP_LOGI(TAG, "Loaded saved SSID: %s",  sr_ssid);
    return true;
fail:
    nvs_close(nvs);
    s_has_credentials = false;
    ESP_LOGE(TAG,"load NVS err"); 
    return false;
}

// 保存WiFi信息
static bool save_credentials(const char *ssid, const char *password){

    nvs_handle_t nvs;
    // 打开NVS
    if (nvs_open("wifi_cfg", NVS_READWRITE, &nvs) != ESP_OK) {
      ESP_LOGE(TAG,"open NVS err");   
      s_has_credentials = false;
      return false;
    }
       
    // 存放
    nvs_set_str(nvs, "ssid", ssid);
    nvs_set_str(nvs, "pass", password);
    nvs_commit(nvs);
    nvs_close(nvs);
    s_has_credentials = true;
    return true;
}


