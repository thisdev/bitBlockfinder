#include "shot.h"
#include "sdkconfig.h"

#if CONFIG_BF_SHOT_ENABLE

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "shot";

#define SHOT_W     BSP_LCD_H_RES
#define SHOT_H     BSP_LCD_V_RES
#define SHOT_BYTES (SHOT_W * SHOT_H * 2)

static uint8_t *s_buf;

static esp_err_t shot_handler(httpd_req_t *req)
{
    if (!s_buf) {
        /* 330 KB gehoeren ins PSRAM, im internen Speicher waeren sie
         * nicht unterzubringen. */
        s_buf = heap_caps_malloc(SHOT_BYTES, MALLOC_CAP_SPIRAM);
        if (!s_buf) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "kein Speicher");
            return ESP_FAIL;
        }
    }

    lv_image_dsc_t dsc;
    bsp_display_lock(0);
    lv_refr_now(NULL);
    lv_result_t r = lv_snapshot_take_to_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565,
                                            &dsc, s_buf, SHOT_BYTES);
    bsp_display_unlock();

    if (r != LV_RESULT_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Aufnahme fehlgeschlagen");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Aufnahme %ux%u, %u Bytes",
             (unsigned)dsc.header.w, (unsigned)dsc.header.h, (unsigned)dsc.data_size);

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "X-Width",  "368");
    httpd_resp_set_hdr(req, "X-Height", "448");
    return httpd_resp_send(req, (const char *)dsc.data, dsc.data_size);
}

static esp_err_t page_handler(httpd_req_t *req)
{
    char query[32] = {0};
    char wert[8]   = {0};
    int  n = 0;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "n", wert, sizeof(wert)) == ESP_OK) {
        n = atoi(wert);
    }

    bsp_display_lock(0);
    ui_show_page(n);
    lv_refr_now(NULL);          /* sofort zeichnen statt auf den Takt warten */
    bsp_display_unlock();

    /* Danach bewusst grosszuegig warten. Eine Aufnahme, die eine halb
     * aufgebaute Seite erwischt, kostet mehr Zeit als diese Pause. */
    vTaskDelay(pdMS_TO_TICKS(700));

    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "ok");
}

void shot_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size     = 5120;
    cfg.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP-Dienst konnte nicht starten");
        return;
    }

    static const httpd_uri_t shot = { .uri = "/shot", .method = HTTP_GET, .handler = shot_handler };
    static const httpd_uri_t page = { .uri = "/page", .method = HTTP_GET, .handler = page_handler };
    httpd_register_uri_handler(server, &shot);
    httpd_register_uri_handler(server, &page);

    ESP_LOGI(TAG, "Aufnahmedienst bereit: /shot, /page?n=0..1");
}

#else
void shot_start(void) { }
#endif
