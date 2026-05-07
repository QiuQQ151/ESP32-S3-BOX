// main/services/wifi_service.c
#include "wifi_service.h"
#include "hal/wifi_hal.h"
#include "config/system_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "string.h"

static const char *TAG = "wifi_service";

/* 任务间事件标志 */
#define WIFI_SERVICE_EVENT_CONNECT          (1 << 0)
#define WIFI_SERVICE_EVENT_DISCONNECT       (1 << 1)
#define WIFI_SERVICE_EVENT_START_AP         (1 << 2)
#define WIFI_SERVICE_EVENT_STOP_AP          (1 << 3)
#define WIFI_SERVICE_EVENT_CONNECT_SAVED    (1 << 4)

typedef struct {
    char ssid[33];
    char password[65];
    bool save;
} wifi_connect_params_t;

/* 文件作用域变量 */
static TaskHandle_t s_task_handle = NULL;
static QueueHandle_t s_event_queue = NULL;
static wifi_srv_state_t s_state = WIFI_SRV_STATE_IDLE;
static wifi_event_callback_t s_callback = NULL;
static char s_saved_ssid[33] = {0};
static char s_saved_password[65] = {0};
static bool s_has_saved_credentials = false;

/* 用于在任务间传递连接参数（由 wifi_service_connect 写入，任务读取） */
static wifi_connect_params_t s_connect_params;

/* 内部函数声明 */
static void wifi_service_task(void *pvParameters);
static void handle_connect(const wifi_connect_params_t *params);
static void handle_disconnect(void);
static void handle_start_ap(void);
static void save_credentials(const char *ssid, const char *password);
static bool load_credentials(void);
static void notify_callback(app_wifi_event_id_t event, wifi_event_data_t *data);

/* ---------- 公开函数实现 ---------- */

esp_err_t wifi_service_init(void)
{
    esp_err_t ret = wifi_hal_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi HAL init failed: %d", ret);
        return ret;
    }

    s_event_queue = xQueueCreate(10, sizeof(uint32_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    load_credentials();
    s_state = WIFI_SRV_STATE_IDLE;
    ESP_LOGI(TAG, "WiFi service initialized");
    return ESP_OK;
}

esp_err_t wifi_service_start(void)
{
    if (s_task_handle != NULL) {
        ESP_LOGW(TAG, "WiFi service already started");
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreate(
        wifi_service_task,
        "wifi_srv",
        4096,
        NULL,
        5,
        &s_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi service task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "WiFi service started");
    return ESP_OK;
}

esp_err_t wifi_service_connect_saved(void)
{
    if (!s_has_saved_credentials) {
        ESP_LOGW(TAG, "No saved WiFi credentials");
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t event = WIFI_SERVICE_EVENT_CONNECT_SAVED;
    xQueueSend(s_event_queue, &event, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t wifi_service_connect(const char *ssid, const char *password, bool save)
{
    if (s_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 将参数保存到文件作用域静态变量 */
    strncpy(s_connect_params.ssid, ssid, sizeof(s_connect_params.ssid) - 1);
    s_connect_params.ssid[sizeof(s_connect_params.ssid) - 1] = '\0';
    strncpy(s_connect_params.password, password, sizeof(s_connect_params.password) - 1);
    s_connect_params.password[sizeof(s_connect_params.password) - 1] = '\0';
    s_connect_params.save = save;

    uint32_t event = WIFI_SERVICE_EVENT_CONNECT;
    xQueueSend(s_event_queue, &event, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t wifi_service_disconnect(void)
{
    uint32_t event = WIFI_SERVICE_EVENT_DISCONNECT;
    xQueueSend(s_event_queue, &event, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t wifi_service_start_provisioning(void)
{
    uint32_t event = WIFI_SERVICE_EVENT_START_AP;
    xQueueSend(s_event_queue, &event, portMAX_DELAY);
    return ESP_OK;
}

wifi_srv_state_t wifi_service_get_state(void)
{
    return s_state;
}

esp_err_t wifi_service_get_ip_str(char *ip_buf, size_t buf_len)
{
    if (s_state != WIFI_SRV_STATE_CONNECTED) {
        snprintf(ip_buf, buf_len, "0.0.0.0");
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        snprintf(ip_buf, buf_len, "0.0.0.0");
        return ESP_FAIL;
    }
    esp_netif_get_ip_info(netif, &ip_info);
    snprintf(ip_buf, buf_len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

int8_t wifi_service_get_rssi(void)
{
    return wifi_hal_get_rssi();
}

void wifi_service_register_callback(wifi_event_callback_t callback)
{
    s_callback = callback;
}

bool wifi_service_is_connected(void)
{
    return (s_state == WIFI_SRV_STATE_CONNECTED);
}

/* ---------- 静态函数实现 ---------- */

static void wifi_service_task(void *pvParameters)
{
    uint32_t event;
    ESP_LOGI(TAG, "WiFi service task started");

    /* 启动后自动尝试连接已保存的网络 */
    if (s_has_saved_credentials) {
        wifi_connect_params_t params;
        strncpy(params.ssid, s_saved_ssid, sizeof(params.ssid) - 1);
        strncpy(params.password, s_saved_password, sizeof(params.password) - 1);
        params.save = false;
        handle_connect(&params);
    }

    while (1) {
        if (xQueueReceive(s_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            switch (event) {
                case WIFI_SERVICE_EVENT_CONNECT:
                    /* 直接使用文件作用域的 s_connect_params */
                    handle_connect(&s_connect_params);
                    break;
                case WIFI_SERVICE_EVENT_CONNECT_SAVED:
                    if (s_has_saved_credentials) {
                        wifi_connect_params_t params;
                        strncpy(params.ssid, s_saved_ssid, sizeof(params.ssid) - 1);
                        strncpy(params.password, s_saved_password, sizeof(params.password) - 1);
                        params.save = false;
                        handle_connect(&params);
                    }
                    break;
                case WIFI_SERVICE_EVENT_DISCONNECT:
                    handle_disconnect();
                    break;
                case WIFI_SERVICE_EVENT_START_AP:
                    handle_start_ap();
                    break;
                case WIFI_SERVICE_EVENT_STOP_AP:
                    wifi_hal_stop_ap();
                    s_state = WIFI_SRV_STATE_IDLE;
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown event: %lu", event);
                    break;
            }
        }
    }
}

static void handle_connect(const wifi_connect_params_t *params)
{
    ESP_LOGI(TAG, "Connecting to WiFi: %s", params->ssid);
    s_state = WIFI_SRV_STATE_CONNECTING;

    /* 通知外部：正在尝试连接（可视为重试） */
    wifi_event_data_t evt_data = {0};
    notify_callback(APP_WIFI_EVENT_CONNECTION_FAILED, &evt_data);  // 这里可以新增一个事件，但暂时复用

    esp_err_t ret = wifi_hal_connect(params->ssid, params->password);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected successfully");
        s_state = WIFI_SRV_STATE_CONNECTED;

        if (params->save) {
            save_credentials(params->ssid, params->password);
        }

        char ip_str[16];
        wifi_service_get_ip_str(ip_str, sizeof(ip_str));

        wifi_event_data_t cd = {
            .ssid = params->ssid,
            .ip_address = ip_str,
        };
        notify_callback(APP_WIFI_EVENT_CONNECTED, &cd);
    } else {
        ESP_LOGE(TAG, "WiFi connection failed: %d", ret);
        s_state = WIFI_SRV_STATE_ERROR;

        wifi_event_data_t ed = {
            .error_code = ret,
        };
        notify_callback(APP_WIFI_EVENT_CONNECTION_FAILED, &ed);
    }
}

static void handle_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting WiFi");
    wifi_hal_disconnect();
    s_state = WIFI_SRV_STATE_IDLE;

    wifi_event_data_t evt_data = {0};
    notify_callback(APP_WIFI_EVENT_DISCONNECTED, &evt_data);
}

static void handle_start_ap(void)
{
    ESP_LOGI(TAG, "Starting AP mode for provisioning");
    wifi_hal_start_ap(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    s_state = WIFI_SRV_STATE_AP_MODE;

    wifi_event_data_t evt_data = {0};
    notify_callback(APP_WIFI_EVENT_AP_STARTED, &evt_data);
}

static void save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return;
    }

    nvs_set_str(nvs_handle, WIFI_CONFIG_KEY_SSID, ssid);
    nvs_set_str(nvs_handle, WIFI_CONFIG_KEY_PASS, password);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    strncpy(s_saved_ssid, ssid, sizeof(s_saved_ssid) - 1);
    strncpy(s_saved_password, password, sizeof(s_saved_password) - 1);
    s_has_saved_credentials = true;

    ESP_LOGI(TAG, "WiFi credentials saved");
}

static bool load_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved WiFi credentials found");
        s_has_saved_credentials = false;
        return false;
    }

    size_t size = sizeof(s_saved_ssid);
    ret = nvs_get_str(nvs_handle, WIFI_CONFIG_KEY_SSID, s_saved_ssid, &size);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        s_has_saved_credentials = false;
        return false;
    }

    size = sizeof(s_saved_password);
    ret = nvs_get_str(nvs_handle, WIFI_CONFIG_KEY_PASS, s_saved_password, &size);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        s_has_saved_credentials = false;
        return false;
    }

    nvs_close(nvs_handle);
    s_has_saved_credentials = true;
    ESP_LOGI(TAG, "Loaded saved WiFi credentials: SSID=%s", s_saved_ssid);
    return true;
}

static void notify_callback(app_wifi_event_id_t event, wifi_event_data_t *data)
{
    if (s_callback) {
        s_callback(event, data);
    }
}