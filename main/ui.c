#include "ui.h"
#include "clock.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"

/* Comic Sans, in vier Groessen aus der TrueType-Datei uebersetzt. Die
 * eingebauten Montserrat-Schriften von LVGL sind damit raus -- und mit
 * ihnen auch die Umlaute, denn uebersetzt wurde nur ASCII. Deshalb steht
 * auf dem Display "Gebuehr" und nicht "Gebühr". */
LV_FONT_DECLARE(comic_14);
LV_FONT_DECLARE(comic_20);
LV_FONT_DECLARE(comic_28);
LV_FONT_DECLARE(comic_40);

/* --- Farben von mempool.space ------------------------------------------
 *
 * Aus deren Stylesheet uebernommen, nicht geschaetzt: der dunkelblaue
 * Grund, das etwas hellere Blau der Kacheln und die Akzentfarben. Der
 * Goldverlauf ist der ihrer Blockkaesten -- das Erkennungszeichen der
 * Seite, und hier die Flaeche mit dem Namen des Pools. */
#define COL_BG        lv_color_hex(0x11131F)   /* --bs-body-bg          */
#define COL_PANEL     lv_color_hex(0x272F4E)   /* --bs-secondary        */
#define COL_TEXT      lv_color_hex(0xFFFFFF)
#define COL_DIM       lv_color_hex(0x8A93B2)
#define COL_BLUE      lv_color_hex(0x007CFA)   /* --bs-primary          */
#define COL_GREEN     lv_color_hex(0x0AAB2F)   /* --bs-success          */
#define COL_CYAN      lv_color_hex(0x00DDFF)   /* --bs-info             */
#define COL_YELLOW    lv_color_hex(0xFFC107)   /* --bs-warning          */
#define COL_RED       lv_color_hex(0xDC3545)   /* --bs-danger           */
#define COL_GOLD_DARK lv_color_hex(0x9D7C05)   /* Blockkasten oben      */
#define COL_GOLD_LITE lv_color_hex(0xD5A90A)   /* Blockkasten unten     */

#define PAGES 2

/* Das Panel hat abgerundete Ecken. Diese beiden Masse halten die
 * Kopfzeile im sichtbaren Bereich. */
#define HEAD_Y      22
#define HEAD_INSET  44

/* Breite, die dem Poolnamen auf der goldenen Flaeche zur Verfuegung steht. */
#define POOL_W      300

/* Zeilen der zweiten Seite. */
#define ROWS        14
#define ROW_H       22

/* --- Seite 1 --- */
static lv_obj_t *s_clock, *s_band, *s_band_cap, *s_pool;
static lv_obj_t *s_age, *s_height, *s_footer, *s_status;
static lv_obj_t *s_tile_a, *s_tile_a_val, *s_tile_a_cap;
static lv_obj_t *s_tile_b, *s_tile_b_val, *s_tile_b_cap;

/* --- Seite 2 --- */
static lv_obj_t *s_row_l[ROWS], *s_row_r[ROWS];

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
 * siebenstellig; ohne Trennung liest die niemand auf einen Blick. */
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
 * Zeichenzahl deutlich breiter als "Foundry USA". Bei Comic Sans mit
 * ihren ausladenden Rundungen faellt das noch staerker aus. */
static int32_t breite(const char *s, const lv_font_t *f)
{
    int32_t w = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        w += lv_font_get_glyph_width(f, *p, p[1]);
    return w;
}

/* Die groesste Schrift, in der der Name noch in eine Zeile passt. */
static const lv_font_t *passende_schrift(const char *s)
{
    static const lv_font_t *const stufen[] = { &comic_40, &comic_28, &comic_20 };
    unsigned n = sizeof(stufen) / sizeof(stufen[0]);
    for (unsigned i = 0; i + 1 < n; i++)
        if (breite(s, stufen[i]) <= POOL_W) return stufen[i];
    return stufen[n - 1];
}

/* Eine Kachel im Stil der mempool-Karten: dunkelblaue Flaeche, runde
 * Ecken, oben die Zahl, unten wofuer sie steht. */
static lv_obj_t *make_tile(lv_obj_t *parent, lv_obj_t **val, lv_obj_t **cap)
{
    lv_obj_t *t = lv_obj_create(parent);
    lv_obj_set_size(t, 158, 78);
    lv_obj_set_style_bg_color(t, COL_PANEL, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_radius(t, 10, 0);
    lv_obj_set_style_pad_all(t, 6, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t, LV_OBJ_FLAG_GESTURE_BUBBLE);

    *val = label(t, &comic_28, COL_TEXT);
    lv_obj_align(*val, LV_ALIGN_TOP_MID, 0, 2);
    *cap = label(t, &comic_14, COL_DIM);
    lv_obj_align(*cap, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
    return t;
}

/* --------------------------------------------------------------- Aufbau */

static void build_page_block(lv_obj_t *p)
{
    lv_obj_t *t = label(p, &comic_14, COL_DIM);
    lv_label_set_text(t, "bitBlockfinder");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, HEAD_Y);

    s_clock = label(p, &comic_14, COL_DIM);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -HEAD_INSET, HEAD_Y);

    /* Der Kasten in Gold ist das Erkennungszeichen von mempool.space --
     * dort steht ein gefundener Block genau so da. Hier traegt er den
     * Namen dessen, der ihn gefunden hat: die eine Antwort, um die es
     * auf dieser Seite geht. */
    s_band = lv_obj_create(p);
    lv_obj_set_size(s_band, 320, 124);
    lv_obj_align(s_band, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_band, COL_GOLD_DARK, 0);
    lv_obj_set_style_bg_grad_color(s_band, COL_GOLD_LITE, 0);
    lv_obj_set_style_bg_grad_dir(s_band, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(s_band, 0, 0);
    lv_obj_set_style_radius(s_band, 10, 0);
    lv_obj_set_style_pad_all(s_band, 8, 0);
    lv_obj_clear_flag(s_band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_band, LV_OBJ_FLAG_GESTURE_BUBBLE);

    s_band_cap = label(s_band, &comic_20, COL_BG);
    lv_label_set_text(s_band_cap, "gefunden von");
    lv_obj_align(s_band_cap, LV_ALIGN_TOP_MID, 0, 0);

    /* Feste Breite mit Umbruch als letzte Rueckfallebene: Ein
     * abgeschnittener Name waere schlimmer als ein umgebrochener. */
    s_pool = label(s_band, &comic_40, COL_BG);
    lv_label_set_text(s_pool, "--");
    lv_obj_set_width(s_pool, POOL_W);
    lv_label_set_long_mode(s_pool, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_pool, LV_TEXT_ALIGN_CENTER, 0);
    /* Optisch mittig, nicht rechnerisch: Unter der Grundlinie von Comic
     * Sans steht mehr Luft als darueber, ohne den Versatz haengt der Name
     * im Kasten nach unten. */
    lv_obj_align(s_pool, LV_ALIGN_BOTTOM_MID, 0, -6);

    s_age = label(p, &comic_28, COL_TEXT);
    lv_label_set_text(s_age, "");
    lv_obj_align(s_age, LV_ALIGN_TOP_MID, 0, 192);

    /* Blockhoehen stehen bei mempool.space in Blau -- dort sind es
     * Verweise. Hier ist es schlicht die Farbe, an der man sie erkennt. */
    s_height = label(p, &comic_20, COL_BLUE);
    lv_label_set_text(s_height, "");
    lv_obj_align(s_height, LV_ALIGN_TOP_MID, 0, 234);

    s_tile_a = make_tile(p, &s_tile_a_val, &s_tile_a_cap);
    s_tile_b = make_tile(p, &s_tile_b_val, &s_tile_b_cap);
    lv_obj_align(s_tile_a, LV_ALIGN_TOP_MID, -82, 270);
    lv_obj_align(s_tile_b, LV_ALIGN_TOP_MID,  82, 270);

    s_footer = label(p, &comic_14, COL_DIM);
    lv_label_set_text(s_footer, "");
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_MID, 0, -48);

    s_status = label(p, &comic_14, COL_DIM);
    lv_label_set_text(s_status, "Starte ...");
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -30);
}

static void build_page_stats(lv_obj_t *p)
{
    lv_obj_t *t = label(p, &comic_14, COL_DIM);
    lv_label_set_text(t, "Netz und Geraet");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, HEAD_Y);

    /* Eine Karte wie auf mempool.space, statt Text auf nacktem Grund. */
    lv_obj_t *karte = lv_obj_create(p);
    lv_obj_set_size(karte, 330, 338);
    lv_obj_align(karte, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(karte, COL_PANEL, 0);
    lv_obj_set_style_border_width(karte, 0, 0);
    lv_obj_set_style_radius(karte, 12, 0);
    lv_obj_set_style_pad_all(karte, 14, 0);
    lv_obj_clear_flag(karte, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(karte, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* Jede Zeile aus zwei Labeln statt einem Textblock je Spalte: nur so
     * laesst sich jeder Wert einzeln einfaerben. */
    for (int i = 0; i < ROWS; i++) {
        s_row_l[i] = label(karte, &comic_14, COL_DIM);
        lv_label_set_text(s_row_l[i], "");
        lv_obj_align(s_row_l[i], LV_ALIGN_TOP_LEFT, 0, i * ROW_H);

        s_row_r[i] = label(karte, &comic_14, COL_TEXT);
        lv_label_set_text(s_row_r[i], "");
        lv_obj_set_style_text_align(s_row_r[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(s_row_r[i], LV_ALIGN_TOP_RIGHT, 0, i * ROW_H);
    }
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
        lv_obj_set_style_bg_color(s_dots[i], i == n ? COL_BLUE : COL_PANEL, 0);
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
    /* Einmal hier gesetzt, erben es alle Label ohne eigene Angabe. */
    lv_obj_set_style_text_font(scr, &comic_14, LV_PART_MAIN);
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

    /* Zwei kleine Punkte statt Sternchen -- der aktive in Blau. */
    for (int i = 0; i < PAGES; i++) {
        s_dots[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(s_dots[i]);
        lv_obj_set_size(s_dots[i], 8, 8);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_dots[i], i == 0 ? COL_BLUE : COL_PANEL, 0);
        lv_obj_align(s_dots[i], LV_ALIGN_BOTTOM_MID, i * 18 - 9, -12);
        lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
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
        lv_obj_set_style_bg_color(s_band, COL_PANEL, 0);
        lv_obj_set_style_bg_grad_color(s_band, COL_PANEL, 0);
        lv_obj_set_style_text_color(s_band_cap, COL_DIM, 0);
        lv_obj_set_style_text_color(s_pool, COL_DIM, 0);
        lv_label_set_text(s_band_cap, "warte auf Daten");
        lv_obj_set_style_text_font(s_pool, &comic_40, 0);
        lv_label_set_text(s_pool, "--");
        lv_label_set_text(s_age, "");
        lv_label_set_text(s_height, "");
        lv_label_set_text(s_footer, "");
        lv_obj_add_flag(s_tile_a, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_tile_b, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_set_style_bg_color(s_band, COL_GOLD_DARK, 0);
    lv_obj_set_style_bg_grad_color(s_band, COL_GOLD_LITE, 0);
    lv_obj_set_style_text_color(s_band_cap, COL_BG, 0);
    lv_obj_set_style_text_color(s_pool, COL_BG, 0);
    lv_label_set_text(s_band_cap, "gefunden von");

    lv_obj_set_style_text_font(s_pool, passende_schrift(b->pool), 0);
    lv_label_set_text(s_pool, b->pool);

    long alter = (long)(time(NULL) - b->timestamp);
    alter_text(alter, buf, sizeof(buf));
    lv_label_set_text(s_age, buf);
    /* Gruen heisst frisch gefunden, gelb heisst es dauert -- ueber
     * zwanzig Minuten ist nichts Schlimmes, aber bemerkenswert. */
    lv_obj_set_style_text_color(s_age,
                                alter < 120      ? COL_GREEN :
                                alter > 20 * 60  ? COL_YELLOW : COL_TEXT, 0);

    char zahl[24];
    tausender(zahl, sizeof(zahl), b->height);
    lv_label_set_text_fmt(s_height, "Block %s", zahl);

    tausender(zahl, sizeof(zahl), b->tx_count);
    lv_label_set_text(s_tile_a_val, zahl);
    lv_obj_set_style_text_color(s_tile_a_val, COL_CYAN, 0);
    lv_label_set_text(s_tile_a_cap, "Transaktionen");
    lv_obj_clear_flag(s_tile_a, LV_OBJ_FLAG_HIDDEN);

    /* Die Belohnung sind neue Muenzen plus Gebuehren. Drei Nachkommastellen
     * reichen: Die Halbierung auf 3,125 BTC ist damit zu sehen, das
     * Rauschen der Gebuehren steht darunter in Sats. */
    snprintf(zahl, sizeof(zahl), "%.3f", (double)b->reward / 1e8);
    komma(zahl);
    lv_label_set_text(s_tile_b_val, zahl);
    lv_obj_set_style_text_color(s_tile_b_val, COL_YELLOW, 0);
    lv_label_set_text(s_tile_b_cap, "BTC Belohnung");
    lv_obj_clear_flag(s_tile_b, LV_OBJ_FLAG_HIDDEN);

    tausender(zahl, sizeof(zahl), (long)b->fees);
    lv_label_set_text_fmt(s_footer, "davon %s sat Gebuehren", zahl);
}

/* Eine Zeile der zweiten Seite setzen. */
static void zeile(int i, const char *name, lv_color_t col, const char *fmt, ...)
{
    if (i < 0 || i >= ROWS) return;

    lv_label_set_text(s_row_l[i], name);

    char wert[48];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(wert, sizeof(wert), fmt, ap);
    va_end(ap);

    lv_label_set_text(s_row_r[i], wert);
    lv_obj_set_style_text_color(s_row_r[i], col, 0);
}

static void leerzeile(int i)
{
    if (i < 0 || i >= ROWS) return;
    lv_label_set_text(s_row_l[i], "");
    lv_label_set_text(s_row_r[i], "");
}

static void update_stats(const ui_state_t *st)
{
    const stats_t *s = st->stats;
    const block_t *b = st->block;
    char hilf[32], alt[32];
    time_t now = time(NULL);
    int i = 0;

    if (s->valid) {
        snprintf(hilf, sizeof(hilf), "%.1f", (double)s->block_min);
        komma(hilf);
        zeile(i++, "Blockzeit", COL_TEXT, "%s min", hilf);
        zeile(i++, "Hashrate", COL_CYAN, "%.0f EH/s", (double)s->hashrate_eh);
        snprintf(hilf, sizeof(hilf), "%+.1f", (double)s->change_pct);
        komma(hilf);
        /* Steigende Schwierigkeit heisst, dass Rechenleistung dazugekommen
         * ist -- gruen. Fallende rot, nach derselben Lesart. */
        zeile(i++, "Schwierigkeit", s->change_pct >= 0 ? COL_GREEN : COL_RED,
              "%s %% in %d", hilf, s->remaining_blocks);
    } else {
        zeile(i++, "Blockzeit", COL_DIM, "--");
        zeile(i++, "Hashrate", COL_DIM, "--");
        zeile(i++, "Schwierigkeit", COL_DIM, "--");
    }

    if (s->fees_valid) zeile(i++, "Gebuehr jetzt / 1 Std", COL_YELLOW,
                             "%d / %d sat/vB", s->fast_fee, s->hour_fee);
    else               zeile(i++, "Gebuehr jetzt / 1 Std", COL_DIM, "--");

    if (s->mempool_valid) {
        tausender(hilf, sizeof(hilf), s->mempool_count);
        zeile(i++, "Mempool", COL_TEXT, "%s Tx, %.0f MB", hilf, (double)s->mempool_mb);
    } else {
        zeile(i++, "Mempool", COL_DIM, "--");
    }

    leerzeile(i++);

    /* Wer die letzten 24 Stunden gefunden hat. Genau das ordnet den Namen
     * auf der ersten Seite ein: Foundry mit einem Viertel aller Bloecke
     * ist kein Zufallstreffer. */
    if (s->pools > 0) {
        zeile(i++, "Pools der letzten 24 Std", COL_DIM, "%d Bloecke", s->blocks_24h);
        for (int k = 0; k < s->pools && i < ROWS; k++)
            zeile(i++, s->pool[k].name, COL_GOLD_LITE, "%d", s->pool[k].blocks);
    } else {
        zeile(i++, "Pools der letzten 24 Std", COL_DIM, "--");
    }

    while (i < ROWS - 3) leerzeile(i++);

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) zeile(i++, "WLAN", COL_TEXT, "%d dBm", ap.rssi);
    else                                         zeile(i++, "WLAN", COL_RED, "getrennt");

    int64_t up = esp_timer_get_time() / 1000000;
    zeile(i++, "Laufzeit", COL_TEXT, "%lldd %02lld:%02lld",
          up / 86400, (up % 86400) / 3600, (up % 3600) / 60);

    if (b->valid) {
        alter_text((long)(now - b->fetched), alt, sizeof(alt));
        zeile(i++, "Abruf", COL_TEXT, "%s", alt);
    } else {
        zeile(i++, "Abruf", COL_DIM, "nie");
    }
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
