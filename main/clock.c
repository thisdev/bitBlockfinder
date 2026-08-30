#include "clock.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "uhr";

static bool have_time(struct tm *out)
{
    time_t now = time(NULL);
    localtime_r(&now, out);
    /* Vor 2020 kann die Uhr nur ungestellt sein. */
    return out->tm_year >= 120;
}

void clock_start(void)
{
    /* Mitteleuropaeische Zeit samt Sommerzeitregel. Ohne die laege der
     * Tageswechsel je nach Jahreszeit eine Stunde daneben. */
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&cfg);
    ESP_LOGI(TAG, "NTP gestartet");
}

bool clock_valid(void)
{
    struct tm tm;
    return have_time(&tm);
}

void clock_hhmm(char *buf, size_t len)
{
    struct tm tm;
    if (have_time(&tm)) snprintf(buf, len, "%02d:%02d", tm.tm_hour, tm.tm_min);
    else                snprintf(buf, len, "--:--");
}

int clock_minute_of_day(void)
{
    struct tm tm;
    if (!have_time(&tm)) return -1;
    return tm.tm_hour * 60 + tm.tm_min;
}

int clock_day_key(void)
{
    struct tm tm;
    if (!have_time(&tm)) return 0;
    return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}
