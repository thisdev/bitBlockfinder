#include "backlight.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "licht";

/* Der Taster liegt auf GPIO 0. Das ist zugleich die BOOT-Taste: Beim
 * Einschalten gedrueckt gehalten startet sie den Flash-Modus, im laufenden
 * Betrieb ist der Pin aber ein ganz normaler Eingang.
 *
 * Am Waveshare ESP32-S3-Touch-AMOLED-1.8 ist das bei aufrecht stehendem
 * Display der OBERE der beiden Knoepfe (am Geraet nachgemessen). Der untere
 * ist RESET und laesst sich per Software nicht abfragen. */
#define BUTTON_GPIO      GPIO_NUM_0
#define IDLE_MS          30000     /* nach dieser Ruhezeit absenken */
#define POLL_MS          40

static const int LEVELS[] = { 25, 50, 100 };
static int  s_index  = 2;          /* Start bei voller Helligkeit */
static bool s_dimmed = false;

/* Abgesenkt wird auf ein Viertel der gewaehlten Stufe, aber nie ganz aus:
 * Der Monitor soll im Vorbeigehen ablesbar bleiben. */
static int dim_level(void)
{
    int d = LEVELS[s_index] / 4;
    return d < 5 ? 5 : d;
}

static void apply(void)
{
    bsp_display_brightness_set(s_dimmed ? dim_level() : LEVELS[s_index]);
}

static void backlight_task(void *arg)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    bool last = true;          /* Ruhezustand ist high (Pull-up) */
    int  stable = 0;

    apply();

    while (1) {
        /* Entprellen: der Pegel muss ein paar Durchlaeufe halten. */
        bool now = gpio_get_level(BUTTON_GPIO);
        if (now != last) {
            if (++stable >= 3) {
                last   = now;
                stable = 0;
                if (!now) {          /* fallende Flanke = gedrueckt */
                    lv_display_trigger_activity(NULL);
                    if (s_dimmed) {
                        /* Erster Druck weckt nur auf. */
                        s_dimmed = false;
                        ESP_LOGI(TAG, "aufgeweckt, Stufe %d%%", LEVELS[s_index]);
                    } else {
                        s_index = (s_index + 1) % (int)(sizeof(LEVELS) / sizeof(LEVELS[0]));
                        ESP_LOGI(TAG, "Taste GPIO%d: Helligkeit %d%%",
                                 BUTTON_GPIO, LEVELS[s_index]);
                    }
                    apply();
                }
            }
        } else {
            stable = 0;
        }

        /* LVGL zaehlt die Zeit seit der letzten Eingabe selbst mit,
         * Beruehrungen des Displays setzen sie zurueck. */
        uint32_t idle = lv_display_get_inactive_time(NULL);
        if (!s_dimmed && idle > IDLE_MS) {
            s_dimmed = true;
            apply();
            ESP_LOGI(TAG, "Ruhe: abgesenkt auf %d%%", dim_level());
        } else if (s_dimmed && idle < IDLE_MS) {
            s_dimmed = false;
            apply();
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void backlight_start(void)
{
    xTaskCreate(backlight_task, "licht", 2048, NULL, 4, NULL);
}
