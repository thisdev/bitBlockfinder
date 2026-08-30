#include "net.h"
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

static const char *TAG = "net";

#define BIT_GOT_IP  BIT0
#define BIT_FAILED  BIT1
#define MAX_RETRY   8

static EventGroupHandle_t s_events;
static int s_retries;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries < MAX_RETRY) {
            s_retries++;
            ESP_LOGW(TAG, "Verbindung verloren, Versuch %d/%d", s_retries, MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, BIT_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP-Adresse: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retries = 0;
        xEventGroupSetBits(s_events, BIT_GOT_IP);
    }
}

esp_err_t net_connect(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &on_event, NULL, NULL));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid,     CONFIG_BF_WIFI_SSID, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, CONFIG_BF_WIFI_PASS, sizeof(wc.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Verbinde mit \"%s\" ...", CONFIG_BF_WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_GOT_IP | BIT_FAILED,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    if (bits & BIT_GOT_IP) return ESP_OK;
    ESP_LOGE(TAG, "WLAN-Verbindung fehlgeschlagen");
    return ESP_FAIL;
}
