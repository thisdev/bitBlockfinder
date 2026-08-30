#include "ui.h"
#include "clock.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"

/* Die eingebauten Montserrat-Schriften von LVGL decken nur ASCII ab.
 * Deshalb steht auf dem Display "Gebuehr" und nicht "Gebühr". */

#define COL_BG        lv_color_black()
#define COL_TEXT      lv_color_hex(0xFFFFFF)
#define COL_DIM       lv_color_hex(0x707070)
#define COL_TRACK     lv_color_hex(0x202020)
#define COL_TILE      lv_color_hex(0x111111)

#define COL_ORANGE    lv_color_hex(0xF7931A)   /* die Farbe von Bitcoin */
#define COL_WAIT      lv_color_hex(0xFFC400)

#define PAGES 2

/* Das Panel hat abgerundete Ecken. Diese beiden Masse halten die
 * Kopfzeile im sichtbaren Bereich. */
#define HEAD_Y      26
#define HEAD_INSET  44

/* Breite, die dem Poolnamen auf der orangen Flaeche zur Verfuegung steht. */
#define POOL_W      296

/* --- Seite 1: wer den Block gefunden hat --- */
static lv_obj_t *s_clock, *s_band, *s_band_cap, *s_pool;
static lv_obj_t *s_age, *s_height, *s_footer, *s_status;
static lv_obj_t *s_tile_a, *s_tile_a_val, *s_tile_a_cap;
static lv_obj_t *s_tile_b, *s_tile_b_val, *s_tile_b_cap;

/* --- Seite 2: Zahlen --- */
static lv_obj_t *s_diag_l, *s_diag_r;

static lv_obj_t *s_pages[PAGES], *s_dots[PAGES];
static int       s_page;

static ui_state_t s_last;
static bool       s_have_last;

static void zeichne_seite(void);

/* Deutsches Dezimalkomma. printf kennt nur den Punkt. */
static void komma(char *buf)
{
    char *p = strchr(buf, '.');
    if (p) *p = ',';
}

/* 964773 -> "964.773". Blockhoehen und Transaktionszahlen sind sechs- bis
 * siebenstellig; ohne Trennung liest die sie niemand auf einen Blick. */
static void tausender(char *buf, size_t len, long v)
{
    char roh[24];
    snprintf(roh, sizeof(roh), "%ld", v < 0 ? -v : v);

    size_t n = strlen(roh), j = 0;
    if (v < 0 && j + 1 < len) buf[j++] = '-';
    for (size_t i = 0; i < n && j + 1 < len; i++) {
        if (i > 0 && (n - i) % 3 == 0 && j + 1 < len) buf[j++] = '.';
        buf[j++] = roh[i];
    }
    buf[j] = '\0';
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

/* Wie breit wird der Name in dieser Schrift?
 *
 * Zeichen zu zaehlen genuegt nicht: Grossbuchstaben brauchen fast doppelt
 * so viel Platz wie Kleinbuchstaben, "ULTIMUSPOOL" ist bei gleicher
 * Zeichenzahl deutlich breiter als "Foundry USA". Also wird gemessen. */
static int32_t breite(const char *s, const lv_font_t *f)
{
    int32_t w = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        w += lv_font_get_glyph_width(f, *p, p[1]);
    return w;
}

/* Die groesste Schrift, in der der Name noch in eine Zeile passt. Passt
 * er in keine, bleibt die kleinste -- dann bricht das Label um. */
static const lv_font_t *passende_schrift(const char *s)
{
    static const lv_font_t *const stufen[] = {
        &lv_font_montserrat_40, &lv_font_montserrat_28, &lv_font_montserrat_20,
    };
    unsigned n = sizeof(stufen) / sizeof(stufen[0]);
    for (unsigned i = 0; i + 1 < n; i++)
        if (breite(s, stufen[i]) <= POOL_W) return stufen[i];
    return stufen[n - 1];
}

static lv_obj_t *make_tile(lv_obj_t *parent, lv_obj_t **val, lv_obj_t **cap)
{
    lv_obj_t *t = lv_obj_create(parent);
    lv_obj_set_size(t, 160, 74);
    lv_obj_set_style_bg_color(t, COL_TILE, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_radius(t, 12, 0);
    lv_obj_set_style_pad_all(t, 6, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t, LV_OBJ_FLAG_GESTURE_BUBBLE);

    *val = label(t, &lv_font_montserrat_28, COL_TEXT);
    lv_obj_align(*val, LV_ALIGN_TOP_MID, 0, 0);
    *cap = label(t, NULL, COL_DIM);
    lv_obj_align(*cap, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
    return t;
}

/* --------------------------------------------------------------- Aufbau */

static void build_page_block(lv_obj_t *p)
{
    lv_obj_t *t = label(p, NULL, COL_DIM);
    lv_label_set_text(t, "bitBlockfinder");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, HEAD_Y);

    s_clock = label(p, NULL, COL_DIM);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -HEAD_INSET, HEAD_Y);

    /* Der Name steht auf einer orangen Flaeche, weil er die eine Antwort
     * ist, um die es hier geht. Alles andere auf der Seite begruendet ihn
     * nur. */
    s_band = lv_obj_create(p);
    lv_obj_set_size(s_band, 320, 116);
    lv_obj_align(s_band, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_bg_color(s_band, COL_TRACK, 0);
    lv_obj_set_style_border_width(s_band, 0, 0);
    lv_obj_set_style_radius(s_band, 18, 0);
    lv_obj_set_style_pad_all(s_band, 8, 0);
    lv_obj_clear_flag(s_band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_band, LV_OBJ_FLAG_GESTURE_BUBBLE);

    s_band_cap = label(s_band, &lv_font_montserrat_20, COL_BG);
    lv_label_set_text(s_band_cap, "gefunden von");
    lv_obj_align(s_band_cap, LV_ALIGN_TOP_MID, 0, 2);

    /* Feste Breite mit Umbruch als letzte Rueckfallebene: Ein
     * abgeschnittener Name waere schlimmer als ein umgebrochener. Die
     * Schriftgroesse waehlt update_block() so, dass es normalerweise gar
     * nicht dazu kommt. */
    s_pool = label(s_band, &lv_font_montserrat_40, COL_BG);
    lv_label_set_text(s_pool, "--");
    lv_obj_set_width(s_pool, POOL_W);
    lv_label_set_long_mode(s_pool, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_pool, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_pool, LV_ALIGN_BOTTOM_MID, 0, 0);

    s_age = label(p, &lv_font_montserrat_28, COL_TEXT);
    lv_label_set_text(s_age, "");
    lv_obj_align(s_age, LV_ALIGN_TOP_MID, 0, 192);

    s_height = label(p, NULL, COL_DIM);
    lv_label_set_text(s_height, "");
    lv_obj_align(s_height, LV_ALIGN_TOP_MID, 0, 236);

    s_tile_a = make_tile(p, &s_tile_a_val, &s_tile_a_cap);
    s_tile_b = make_tile(p, &s_tile_b_val, &s_tile_b_cap);
    lv_obj_align(s_tile_a, LV_ALIGN_TOP_MID, -84, 268);
    lv_obj_align(s_tile_b, LV_ALIGN_TOP_MID,  84, 268);

    s_footer = label(p, NULL, COL_DIM);
    lv_label_set_text(s_footer, "");
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_MID, 0, -44);

    s_status = label(p, NULL, COL_DIM);
    lv_label_set_text(s_status, "Starte ...");
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -26);
}

static void build_page_stats(lv_obj_t *p)
{
    lv_obj_t *t = label(p, NULL, COL_DIM);
    lv_label_set_text(t, "Netz und Geraet");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, HEAD_Y);

    s_diag_l = label(p, NULL, COL_DIM);
    lv_label_set_text(s_diag_l, "");
    lv_obj_set_style_text_line_space(s_diag_l, 6, 0);
    lv_obj_align(s_diag_l, LV_ALIGN_TOP_LEFT, 36, 58);

    s_diag_r = label(p, NULL, COL_TEXT);
    lv_label_set_text(s_diag_r, "");
    lv_obj_set_style_text_line_space(s_diag_r, 6, 0);
    lv_obj_set_style_text_align(s_diag_r, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_diag_r, LV_ALIGN_TOP_RIGHT, -36, 58);
}

/* Seiten werden hart umgeschaltet statt gescrollt -- aus demselben Grund
 * wie bei den anderen Projekten: Fuer eine Wischanimation reicht die
 * Bandbreite der QSPI-Anbindung nicht. */
void ui_show_page(int n)
{
    if (n < 0) n = 0;
    if (n >= PAGES) n = PAGES - 1;
    s_page = n;

    ESP_LOGI("ui", "Seite %d", n + 1);

    for (int i = 0; i < PAGES; i++) {
        if (i == n) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_dots[i], i == n ? COL_TEXT : COL_TRACK, 0);
    }

    if (s_have_last) zeichne_seite();
}

static void on_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if      (dir == LV_DIR_LEFT)  ui_show_page(s_page + 1);
    else if (dir == LV_DIR_RIGHT) ui_show_page(s_page - 1);
}

void ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, NULL);

    for (int i = 0; i < PAGES; i++) {
        lv_obj_t *p = lv_obj_create(scr);
        lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(p, COL_BG, 0);
        lv_obj_set_style_border_width(p, 0, 0);
        lv_obj_set_style_pad_all(p, 0, 0);
        lv_obj_set_style_radius(p, 0, 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(p, LV_OBJ_FLAG_GESTURE_BUBBLE);
        s_pages[i] = p;
    }

    build_page_block(s_pages[0]);
    build_page_stats(s_pages[1]);

    for (int i = 0; i < PAGES; i++) {
        s_dots[i] = lv_label_create(scr);
        lv_label_set_text(s_dots[i], "*");
        lv_obj_set_style_text_color(s_dots[i], i == 0 ? COL_TEXT : COL_TRACK, 0);
        lv_obj_align(s_dots[i], LV_ALIGN_BOTTOM_MID, i * 16 - 8, -8);
        lv_obj_move_foreground(s_dots[i]);
    }

    ui_show_page(0);
}

void ui_set_status(const char *text)
{
    lv_label_set_text(s_status, text);
}

/* ------------------------------------------------------------ Zeichnen */

/* Wie lange ist das her? Sekunden nur ganz frisch, danach Minuten, ab
 * einer Stunde Stunden und Minuten. Ein Block ist im Mittel zehn Minuten
 * alt, aber die Streuung ist gross -- eine Stunde kommt regelmaessig vor. */
static void alter_text(long s, char *buf, size_t len)
{
    if (s < 0)    s = 0;                 /* Blockzeit darf leicht vorgehen */
    if (s < 60)   { snprintf(buf, len, "gerade eben"); return; }
    if (s < 120)  { snprintf(buf, len, "vor 1 Minute"); return; }
    if (s < 3600) { snprintf(buf, len, "vor %ld Minuten", s / 60); return; }
    snprintf(buf, len, "vor %ld:%02ld Std", s / 3600, (s % 3600) / 60);
}

static void update_block(const ui_state_t *st)
{
    char buf[48];

    clock_hhmm(buf, sizeof(buf));
    lv_label_set_text(s_clock, buf);

    const block_t *b = st->block;
    if (!b->valid) {
        lv_obj_set_style_bg_color(s_band, COL_TRACK, 0);
        lv_obj_set_style_text_color(s_band_cap, COL_DIM, 0);
        lv_obj_set_style_text_color(s_pool, COL_DIM, 0);
        lv_label_set_text(s_band_cap, "warte auf Daten");
        lv_label_set_text(s_pool, "--");
        lv_label_set_text(s_age, "");
        lv_label_set_text(s_height, "");
        lv_label_set_text(s_footer, "");
        lv_obj_add_flag(s_tile_a, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_tile_b, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_set_style_bg_color(s_band, COL_ORANGE, 0);
    lv_obj_set_style_text_color(s_band_cap, COL_BG, 0);
    lv_obj_set_style_text_color(s_pool, COL_BG, 0);
    lv_label_set_text(s_band_cap, "gefunden von");

    /* Poolnamen sind zwischen fuenf ("OCEAN") und fuenfzehn Zeichen lang
     * ("Mining Squared"). Eine feste Schriftgroesse waere entweder
     * verschenkt oder zu gross. */
    lv_obj_set_style_text_font(s_pool, passende_schrift(b->pool), 0);
    lv_label_set_text(s_pool, b->pool);

    long alter = (long)(time(NULL) - b->timestamp);
    alter_text(alter, buf, sizeof(buf));
    lv_label_set_text(s_age, buf);
    /* Ueber zwanzig Minuten ohne Block ist nichts Schlimmes, aber
     * bemerkenswert -- gelb sagt das, ohne zu alarmieren. */
    lv_obj_set_style_text_color(s_age, alter > 20 * 60 ? COL_WAIT : COL_TEXT, 0);

    char zahl[24];
    tausender(zahl, sizeof(zahl), b->height);
    lv_label_set_text_fmt(s_height, "Block %s", zahl);

    tausender(zahl, sizeof(zahl), b->tx_count);
    lv_label_set_text(s_tile_a_val, zahl);
    lv_label_set_text(s_tile_a_cap, "Transaktionen");
    lv_obj_clear_flag(s_tile_a, LV_OBJ_FLAG_HIDDEN);

    /* Die Belohnung sind neue Muenzen plus Gebuehren. Drei Nachkommastellen
     * reichen: Die Halbierung auf 3,125 BTC ist damit zu sehen, das
     * Rauschen der Gebuehren steht darunter in Sats. */
    snprintf(zahl, sizeof(zahl), "%.3f", (double)b->reward / 1e8);
    komma(zahl);
    lv_label_set_text(s_tile_b_val, zahl);
    lv_obj_set_style_text_color(s_tile_b_val, COL_ORANGE, 0);
    lv_label_set_text(s_tile_b_cap, "BTC Belohnung");
    lv_obj_clear_flag(s_tile_b, LV_OBJ_FLAG_HIDDEN);

    tausender(zahl, sizeof(zahl), (long)b->fees);
    lv_label_set_text_fmt(s_footer, "davon %s sat Gebuehren", zahl);
}

/* Zwei Spalten: Bezeichnungen links, Werte rechtsbuendig. Damit die
 * Zeilen nebeneinander stehen bleiben, bekommt jede Zeile genau einen
 * Eintrag in beiden Puffern -- auch die leeren Trennzeilen. */
static void update_stats(const ui_state_t *st)
{
    const stats_t *s = st->stats;
    const block_t *b = st->block;

    char links[400], rechts[400], wert[64], hilf[24], alt[32];
    int  l = 0, r = 0;
    time_t now = time(NULL);

    #define ZEILE(name, ...)                                             \
        do {                                                             \
            snprintf(wert, sizeof(wert), __VA_ARGS__);                   \
            l += snprintf(links  + l, sizeof(links)  - l, "%s\n", name); \
            r += snprintf(rechts + r, sizeof(rechts) - r, "%s\n", wert); \
        } while (0)

    #define LEERZEILE()                                                  \
        do {                                                             \
            l += snprintf(links  + l, sizeof(links)  - l, "\n");         \
            r += snprintf(rechts + r, sizeof(rechts) - r, "\n");         \
        } while (0)

    if (s->valid) {
        snprintf(hilf, sizeof(hilf), "%.1f", (double)s->block_min);
        komma(hilf);
        ZEILE("Blockzeit", "%s min", hilf);
        ZEILE("Hashrate", "%.0f EH/s", (double)s->hashrate_eh);
        snprintf(hilf, sizeof(hilf), "%+.1f", (double)s->change_pct);
        komma(hilf);
        ZEILE("Schwierigkeit", "%s %% in %d", hilf, s->remaining_blocks);
    } else {
        ZEILE("Blockzeit", "--");
        ZEILE("Hashrate", "--");
        ZEILE("Schwierigkeit", "--");
    }

    if (s->fees_valid) ZEILE("Gebuehr jetzt / 1 Std", "%d / %d sat/vB", s->fast_fee, s->hour_fee);
    else               ZEILE("Gebuehr jetzt / 1 Std", "--");

    if (s->mempool_valid) {
        tausender(hilf, sizeof(hilf), s->mempool_count);
        ZEILE("Mempool", "%s Tx, %.0f MB", hilf, (double)s->mempool_mb);
    } else {
        ZEILE("Mempool", "--");
    }

    LEERZEILE();

    /* Wer die letzten 24 Stunden gefunden hat. Genau das ordnet den
     * Namen auf der ersten Seite ein: Foundry mit einem Viertel aller
     * Bloecke ist kein Zufallstreffer. */
    if (s->pools > 0) {
        ZEILE("Bloecke in 24 Std", "%d", s->blocks_24h);
        for (int i = 0; i < s->pools; i++)
            ZEILE(s->pool[i].name, "%d", s->pool[i].blocks);
    } else {
        ZEILE("Bloecke in 24 Std", "--");
    }

    LEERZEILE();

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ZEILE("WLAN", "%d dBm", ap.rssi);
    else                                         ZEILE("WLAN", "getrennt");

    int64_t up = esp_timer_get_time() / 1000000;
    ZEILE("Laufzeit", "%lldd %02lld:%02lld",
          up / 86400, (up % 86400) / 3600, (up % 3600) / 60);

    if (b->valid) {
        alter_text((long)(now - b->fetched), alt, sizeof(alt));
        ZEILE("Block geholt", "%s", alt);
    } else {
        ZEILE("Block geholt", "nie");
    }

    if (s->fetched > 0) {
        alter_text((long)(now - s->fetched), alt, sizeof(alt));
        ZEILE("Zahlen geholt", "%s", alt);
    } else {
        ZEILE("Zahlen geholt", "nie");
    }

    #undef ZEILE
    #undef LEERZEILE

    lv_label_set_text(s_diag_l, links);
    lv_label_set_text(s_diag_r, rechts);
}

/* Was nicht zu sehen ist, muss auch nicht gezeichnet werden. */
static void zeichne_seite(void)
{
    if (!s_last.block || !s_last.stats) return;
    switch (s_page) {
        case 0: update_block(&s_last); break;
        case 1: update_stats(&s_last); break;
    }
}

void ui_update(const ui_state_t *st)
{
    s_last      = *st;
    s_have_last = true;
    zeichne_seite();
}
