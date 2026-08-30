#include "fetch.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"

static const char *TAG = "abruf";

/* Grosszuegig, aber begrenzt: Eine unerwartet lange Antwort darf nicht
 * den Speicher leerraeumen. Die echten Antworten liegen bei 64 Bytes
 * (Blockhash) bis 4 KB (Mempool mit Gebuehrenverteilung). */
#define FETCH_MAX   49152
#define FETCH_START  4096

/* Antwort in einen frisch angelegten Puffer holen. Bei ESP_OK gehoert er
 * dem Aufrufer, der ihn mit free() wieder loswird. */
static esp_err_t holen(const char *url, char **out, size_t *out_len)
{
    *out = NULL;
    if (out_len) *out_len = 0;

    esp_http_client_config_t cfg = {
        .url               = url,
        .timeout_ms        = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* mempool.space ist kostenlos und ohne Anmeldung nutzbar. Ein
         * ehrlicher Bezeichner ist das Mindeste, damit der Betreiber
         * sieht, wer da klopft, und im Zweifel jemanden erreichen kann. */
        .user_agent        = "bitBlockfinder (+https://blog.bitlager.de/esp32/)",
        .buffer_size       = 2048,
    };

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_ERR_NO_MEM;

    char     *buf = NULL;
    size_t    cap = 0, used = 0;
    esp_err_t err = esp_http_client_open(c, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Verbindung fehlgeschlagen: %s", esp_err_to_name(err));
        goto done;
    }

    int64_t len = esp_http_client_fetch_headers(c);
    int status  = esp_http_client_get_status_code(c);
    if (status != 200) {
        /* 429 heisst: zu oft gefragt. Kommt bei einem Abruf je Minute
         * nicht vor, soll aber im Log als solches erkennbar sein. */
        ESP_LOGW(TAG, "HTTP %d von %s", status, url);
        err = ESP_FAIL;
        goto done;
    }

    cap = (len > 0 && len < FETCH_MAX) ? (size_t)len + 1 : FETCH_START;
    /* Ein paar Kilobyte gehoeren ins PSRAM. Der interne Speicher wird von
     * LVGL und dem WLAN-Stack gebraucht. */
    buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) { err = ESP_ERR_NO_MEM; goto done; }

    while (1) {
        if (used + 1 >= cap) {
            if (cap >= FETCH_MAX) {
                ESP_LOGW(TAG, "Antwort laenger als %d Bytes, abgebrochen", FETCH_MAX);
                err = ESP_ERR_INVALID_SIZE;
                goto done;
            }
            size_t neu = cap * 2 > FETCH_MAX ? FETCH_MAX : cap * 2;
            char  *p   = heap_caps_realloc(buf, neu, MALLOC_CAP_SPIRAM);
            if (!p) { err = ESP_ERR_NO_MEM; goto done; }
            buf = p;
            cap = neu;
        }

        int n = esp_http_client_read(c, buf + used, cap - used - 1);
        if (n < 0) { err = ESP_FAIL; goto done; }
        if (n == 0) break;
        used += (size_t)n;
    }

    buf[used] = '\0';
    ESP_LOGD(TAG, "%u Bytes gelesen", (unsigned)used);

    *out = buf;
    if (out_len) *out_len = used;
    buf = NULL;                 /* gehoert jetzt dem Aufrufer */
    err = ESP_OK;

done:
    free(buf);
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return err;
}

esp_err_t fetch_json(const char *url, cJSON **out)
{
    *out = NULL;

    char     *buf = NULL;
    size_t    len = 0;
    esp_err_t err = holen(url, &buf, &len);
    if (err != ESP_OK) return err;

    *out = cJSON_ParseWithLength(buf, len);
    free(buf);

    if (!*out) {
        ESP_LOGW(TAG, "Antwort ist kein gueltiges JSON");
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t fetch_text(const char *url, char *buf, size_t len)
{
    char     *roh = NULL;
    esp_err_t err = holen(url, &roh, NULL);
    if (err != ESP_OK) return err;

    char *a = roh;
    while (*a && isspace((unsigned char)*a)) a++;
    char *e = a + strlen(a);
    while (e > a && isspace((unsigned char)e[-1])) e--;
    *e = '\0';

    strlcpy(buf, a, len);
    free(roh);
    return ESP_OK;
}
