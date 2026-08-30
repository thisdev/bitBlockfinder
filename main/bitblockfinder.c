/*
 * bitBlockfinder -- Waveshare ESP32-S3-Touch-AMOLED-1.8
 *
 * Zeigt, wer den aktuellen Bitcoin-Block gefunden hat und wie lange das
 * her ist. Mehr nicht -- deshalb steht der Name des Pools gross auf
 * oranger Flaeche und alles andere klein darunter.
 *
 * Die Daten kommen von mempool.space. Gefragt wird jede Minute nach dem
 * Hash des obersten Blocks; nur wenn der ein anderer ist als der
 * bekannte, wird der Block selbst geholt. Das sind im Alltag 60 winzige
 * Anfragen je Stunde und sechs etwas groessere -- wenig genug fuer einen
 * Dienst, der nichts kostet.
 *
 * Das Alter des Blocks rechnet das Geraet selbst aus der Blockzeit und
 * der NTP-Uhr aus. Die Minutenzahl laeuft deshalb weiter, auch wenn das
 * WLAN gerade weg ist -- dann eben ohne neuen Block.
 */
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "sdkconfig.h"

#include "chain.h"
#include "net.h"
#include "ui.h"
#include "backlight.h"
#include "clock.h"
#include "shot.h"

static const char *TAG = "blockfinder";

static block_t s_block;
static stats_t s_stats;

/* Takt der Schleife. Der Abruf haengt nicht daran, sondern an den
 * Faelligkeiten unten -- so zaehlt die Minutenangabe fluessig hoch, ohne
 * dass dafuer irgendjemand gefragt wird. */
#define TICK_MS      5000
#define RETRY_S        30      /* nach einem Fehlschlag frueher erneut */
#define ERSTVERSUCH_S   5      /* solange ueberhaupt nichts dasteht     */
#define STATS_MIN      10      /* Netzzahlen aendern sich langsam      */

static void status(const char *text)
{
    bsp_display_lock(0);
    ui_set_status(text);
    bsp_display_unlock();
}

/* Den aktuellen Stand aufs Display bringen.
 *
 * Eigene Funktion, weil sie zweimal gebraucht wird: einmal in jedem
 * Durchlauf und einmal sofort, wenn ein neuer Block da ist. Ohne das
 * zweite Mal haette der erste Block bis nach den vier Abrufen der
 * zweiten Seite gewartet -- beim Kaltstart eine halbe Minute, in der das
 * Geraet kaputt aussieht. */
static void anzeigen(time_t now)
{
    ui_state_t st = { .block = &s_block, .stats = &s_stats };

    /* Die Statuszeile nennt im Normalfall die Quelle. Erst wenn der
     * Abruf mehrfach hintereinander nicht durchkommt, wird daraus eine
     * Meldung -- eine Zahl ohne Hinweis auf ihr Alter waere schlimmer
     * als gar keine. */
    char hinweis[64];
    long still = s_block.valid ? (long)(now - s_block.fetched) : -1;
    if (still < 0)
        snprintf(hinweis, sizeof(hinweis), "asking mempool.space ...");
    else if (still > 3 * CONFIG_BF_POLL_S)
        snprintf(hinweis, sizeof(hinweis), "no update for %ld min", still / 60);
    else
        snprintf(hinweis, sizeof(hinweis), "data from mempool.space");

    bsp_display_lock(0);
    ui_update(&st);
    ui_set_status(hinweis);
    bsp_display_unlock();
}

/* Wie lange nach einem Fehlschlag gewartet wird.
 *
 * Ein Handshake mit mempool.space geht auf diesem Board gelegentlich
 * daneben und kostet beim naechsten Versuch ohnehin fuenf Sekunden. Steht
 * schon ein Block auf dem Schirm, darf das in Ruhe warten -- er wird ja
 * nur um eine Minute aelter. Beim Kaltstart dagegen ist der Bildschirm
 * leer, und eine halbe Minute Wartezeit sieht aus wie ein defektes
 * Geraet. */
static int wartezeit(void)
{
    return s_block.valid ? RETRY_S : ERSTVERSUCH_S;
}

static void loop_task(void *arg)
{
    time_t naechster_tip = 0;
    time_t naechste_zahlen = 0;
    char   bekannt[80] = { 0 };     /* Hash des angezeigten Blocks */

    while (1) {
        time_t now = time(NULL);

        /* Ohne Uhrzeit waere jede Altersangabe geraten. */
        if (!clock_valid()) {
            status("waiting for the clock ...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (now >= naechster_tip) {
            /* Der erste Durchlauf holt Block und Netzzahlen am Stueck und
             * braucht dafuer ein paar Sekunden. Ohne diese Zeile stuende
             * so lange noch "verbinde mit WLAN" da. */
            if (!s_block.valid) status("asking mempool.space ...");

            char tip[80];
            if (chain_tip_hash(tip, sizeof(tip)) == ESP_OK) {
                naechster_tip = now + CONFIG_BF_POLL_S;

                if (strcmp(tip, bekannt) != 0) {
                    if (chain_fetch_block(&s_block, tip) == ESP_OK) {
                        strlcpy(bekannt, tip, sizeof(bekannt));
                        ESP_LOGI(TAG, "Neuer Block %d von %s",
                                 s_block.height, s_block.pool);
                        anzeigen(time(NULL));   /* sofort, nicht erst nachher */
                    } else {
                        naechster_tip = now + wartezeit();
                    }
                }
            } else {
                naechster_tip = now + wartezeit();
            }
        }

        /* Die Zahlen der zweiten Seite haben Zeit. Sie kosten vier
         * Abrufe, und solange auf der ersten Seite noch nichts steht,
         * waere jede Sekunde dafuer an der falschen Stelle ausgegeben. */
        if (s_block.valid && now >= naechste_zahlen) {
            naechste_zahlen = now + (chain_fetch_stats(&s_stats) == ESP_OK
                                     ? STATS_MIN * 60 : RETRY_S * 4);
        }

        /* Die Abrufe oben koennen Sekunden gedauert haben. */
        anzeigen(time(NULL));

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    bsp_display_start();
    bsp_display_backlight_on();
    bsp_display_lock(0);
    ui_create();
    bsp_display_unlock();

    /* Frueh starten: Taste und Ruheabsenkung sollen auch dann arbeiten,
     * wenn es mit dem WLAN nicht klappt und man am Geraet steht. */
    backlight_start();

    status("connecting to wifi ...");
    if (net_connect() != ESP_OK) {
        status("wifi failed - check local.defaults");
        /* Kein return: Der Knopf soll weiter reagieren, und sobald das
         * WLAN doch noch kommt, laeuft die Schleife von selbst an. */
    }

    clock_start();
    shot_start();

    xTaskCreate(loop_task, "blockfinder", 8192, NULL, 5, NULL);
}
