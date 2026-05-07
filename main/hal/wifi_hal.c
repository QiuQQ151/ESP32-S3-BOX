#include "wifi_hal.h"
#include "config/system_config.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "string.h"

static const char *TAG = "wifi_hal";

#define WIFI_CONNECTED_BIT   BIT0
#define WIFI_FAIL_BIT        BIT1
#define WIFI_GOT_IP_BIT      BIT2

static EventGroupHandle_t s_wifi_event_group = NULL;
static wifi_state_t s_wifi_state = WIFI_STATE_UNINIT;
static int s_retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data);

esp_err_t wifi_hal_init(void)
{
    if (s_wifi_event_group != NULL) {
        ESP_LOGW(TAG, "WiFi HAL already initialized");
        return ESP_OK;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    s_wifi_state = WIFI_STATE_DISCONNECTED;
    ESP_LOGI(TAG, "WiFi HAL initialized");
    return ESP_OK;
}

esp_err_t wifi_hal_connect(const char *ssid, const char *password)
{
    if (s_wifi_state == WIFI_STATE_CONNECTING || s_wifi_state == WIFI_STATE_CONNECTED) {
        ESP_LOGW(TAG, "Already connecting or connected");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_state = WIFI_STATE_CONNECTING;
    s_retry_count = 0;

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
        s_wifi_state = WIFI_STATE_CONNECTED;
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi connection failed");
        wifi_hal_disconnect();
        s_wifi_state = WIFI_STATE_ERROR;
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout");
        wifi_hal_disconnect();
        s_wifi_state = WIFI_STATE_ERROR;
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t wifi_hal_disconnect(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_wifi_state = WIFI_STATE_DISCONNECTED;
    ESP_LOGI(TAG, "WiFi disconnected");
    return ESP_OK;
}

esp_err_t wifi_hal_start_ap(const char *ssid, const char *password)
{
    if (s_wifi_state == WIFI_STATE_AP_MODE) {
        ESP_LOGW(TAG, "Already in AP mode");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting AP: %s", ssid);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);
    wifi_config.ap.channel = WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = WIFI_AP_MAX_CONNECTIONS;
    wifi_config.ap.authmode = (strlen(password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_state = WIFI_STATE_AP_MODE;
    ESP_LOGI(TAG, "AP mode started");
    return ESP_OK;
}

esp_err_t wifi_hal_stop_ap(void)
{
    if (s_wifi_state != WIFI_STATE_AP_MODE) {
        return ESP_OK;
    }
    esp_wifi_stop();
    s_wifi_state = WIFI_STATE_DISCONNECTED;
    return ESP_OK;
}

esp_err_t wifi_hal_scan(void)
{
    if (s_wifi_state == WIFI_STATE_AP_MODE) {
        ESP_LOGW(TAG, "Cannot scan in AP mode");
        return ESP_ERR_INVALID_STATE;
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    return ESP_OK;
}

wifi_state_t wifi_hal_get_state(void)
{
    return s_wifi_state;
}

int8_t wifi_hal_get_rssi(void)
{
    if (s_wifi_state != WIFI_STATE_CONNECTED) return 0;
    wifi_ap_record_t ap_info;
    esp_wifi_sta_get_ap_info(&ap_info);
    return ap_info.rssi;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t *event = event_data;
                ESP_LOGI(TAG, "STA connected, channel: %d", event->channel);
                s_retry_count = 0;
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *disconn = event_data;
                ESP_LOGW(TAG, "STA disconnected, reason: %d", disconn->reason);
                if (s_retry_count < WIFI_MAX_RETRY_COUNT) {
                    ESP_LOGI(TAG, "Retrying (%d/%d)...", s_retry_count+1, WIFI_MAX_RETRY_COUNT);
                    esp_wifi_connect();
                    s_retry_count++;
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    s_wifi_state = WIFI_STATE_ERROR;
                }
                break;
            }
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP started");
                break;
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = event_data;
                ESP_LOGI(TAG, "Station " MACSTR " connected", MAC2STR(event->mac));
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event = event_data;
                ESP_LOGI(TAG, "Station " MACSTR " disconnected", MAC2STR(event->mac));
                break;
            }
            case WIFI_EVENT_SCAN_DONE: {
                uint16_t ap_count = 0;
                esp_wifi_scan_get_ap_num(&ap_count);
                ESP_LOGI(TAG, "Scan done, %d APs", ap_count);
                break;
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_state = WIFI_STATE_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_GOT_IP_BIT);
    }
}