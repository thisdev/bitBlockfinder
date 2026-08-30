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

/* Comic Neue (SIL Open Font License), in vier Groessen aus der
 * TrueType-Datei uebersetzt -- siehe schrift/README.md. Uebersetzt wurde
 * nur ASCII, was hier nicht stoert: Die Oberflaeche ist englisch. */
LV_FONT_DECLARE(comic_14);
LV_FONT_DECLARE(comic_20);
LV_FONT_DECLARE(comic_28);
LV_FONT_DECLARE(comic_40);

/* --- Farben ------------------------------------------------------------
 *
 * Grund und Karten von mempool.space, aus deren Stylesheet uebernommen.
 * Die erste Seite kommt bewusst mit zwei Farben aus: weiss und das
 * Orange von Bitcoin. Wer aus zwei Metern draufschaut, soll lesen und
 * nicht Farben deuten. Die zweite Seite ist eine Zahlenliste und darf
 * ihre Werte unterscheiden. */
#define COL_BG        lv_color_hex(0x11131F)   /* --bs-body-bg          */
#define COL_PANEL     lv_color_hex(0x272F4E)   /* --bs-secondary        */
#define COL_TEXT      lv_color_hex(0xFFFFFF)
#define COL_DIM       lv_color_hex(0x8A93B2)
#define COL_ORANGE    lv_color_hex(0xF7931A)   /* Bitcoin               */

/* nur Seite 2 */
#define COL_BLUE      lv_color_hex(0x007CFA)   /* --bs-primary          */
#define COL_GREEN     lv_color_hex(0x0AAB2F)   /* --bs-success          */
#define COL_CYAN      lv_color_hex(0x00DDFF)   /* --bs-info             */
#define COL_YELLOW    lv_color_hex(0xFFC107)   /* --bs-warning          */
#define COL_RED       lv_color_hex(0xDC3545)   /* --bs-danger           */

#define PAGES 2

/* Das Panel hat abgerundete Ecken. Diese beiden Masse halten die
 * Kopfzeile im sichtbaren Bereich. */
#define HEAD_Y      22
#define HEAD_INSET  44

/* Breite, die dem Poolnamen auf der orangen Flaeche zur Verfuegung steht. */
#define POOL_W      300

/* Zeilen der zweiten Seite. */
#define ROWS        15
#define ROW_H       22

/* --- Seite 1 --- */
static lv_obj_t *s_clock, *s_price, *s_band, *s_band_cap, *s_pool;
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

/* 964773 -> "964,773". Englische Schreibweise, passend zur Oberflaeche:
 * Komma trennt die Tausender, der Punkt bleibt das Dezimalzeichen. */
static void gruppiert(char *buf, size_t len, long v)
{
    char roh[24];
    snprintf(roh, sizeof(roh), "%ld", v < 0 ? -v : v);

    size_t n = strlen(roh), j = 0;
    if (v < 0 && j + 1 < len) buf[j++] = '-';
    for (size_t i = 0; i < n && j + 1 < len; i++) {
        if (i > 0 && (n - i) % 3 == 0 && j + 1 < len) buf[j++] = ',';
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
 * Zeichenzahl deutlich breiter als "Foundry USA". */
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
    /* Kopfzeile wie bei mempool.space: links die Uhr, rechts der Kurs. */
    s_clock = label(p, &comic_14, COL_DIM);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_TOP_LEFT, HEAD_INSET, HEAD_Y);

    lv_obj_t *t = label(p, &comic_14, COL_DIM);
    lv_label_set_text(t, "bitBlockfinder");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, HEAD_Y);

    s_price = label(p, &comic_14, COL_ORANGE);
    lv_label_set_text(s_price, "");
    lv_obj_align(s_price, LV_ALIGN_TOP_RIGHT, -HEAD_INSET, HEAD_Y);

    /* Die orange Flaeche traegt die eine Antwort, um die es auf dieser
     * Seite geht. Alles andere darunter begruendet sie nur. */
    s_band = lv_obj_create(p);
    lv_obj_set_size(s_band, 320, 124);
    lv_obj_align(s_band, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_band, COL_ORANGE, 0);
    lv_obj_set_style_border_width(s_band, 0, 0);
    lv_obj_set_style_radius(s_band, 10, 0);
    lv_obj_set_style_pad_all(s_band, 8, 0);
    lv_obj_clear_flag(s_band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_band, LV_OBJ_FLAG_GESTURE_BUBBLE);

    s_band_cap = label(s_band, &comic_20, COL_BG);
    lv_label_set_text(s_band_cap, "found by");
    lv_obj_align(s_band_cap, LV_ALIGN_TOP_MID, 0, 0);

    /* Mittig im Platz unterhalb der Ueberschrift -- der Versatz ist die
     * halbe Hoehe dieser Zeile. Feste Breite mit Umbruch als letzte
     * Rueckfallebene: Ein abgeschnittener Name waere schlimmer als ein
     * umgebrochener. */
    s_pool = label(s_band, &comic_40, COL_BG);
    lv_label_set_text(s_pool, "--");
    lv_obj_set_width(s_pool, POOL_W);
    lv_label_set_long_mode(s_pool, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_pool, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_pool, LV_ALIGN_CENTER, 0, 13);

    s_age = label(p, &comic_28, COL_TEXT);
    lv_label_set_text(s_age, "");
    lv_obj_align(s_age, LV_ALIGN_TOP_MID, 0, 192);

    s_height = label(p, &comic_20, COL_TEXT);
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
    lv_label_set_text(s_status, "starting ...");
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -30);
}

static void build_page_stats(lv_obj_t *p)
{
    lv_obj_t *t = label(p, &comic_14, COL_DIM);
    lv_label_set_text(t, "network and device");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, HEAD_Y);

    /* Eine Karte wie auf mempool.space, statt Text auf nacktem Grund. */
    lv_obj_t *karte = lv_obj_create(p);
    lv_obj_set_size(karte, 330, 356);
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

/* Seiten werden hart umgeschaltet statt gescrollt: Fuer eine
 * Wischanimation reicht die Bandbreite der QSPI-Anbindung nicht. */
void ui_show_page(int n)
{
    if (n < 0) n = 0;
    if (n >= PAGES) n = PAGES - 1;
    s_page = n;

    ESP_LOGI("ui", "Seite %d", n + 1);

    for (int i = 0; i < PAGES; i++) {
        if (i == n) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_dots[i], i == n ? COL_ORANGE : COL_PANEL, 0);
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

    for (int i = 0; i < PAGES; i++) {
        s_dots[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(s_dots[i]);
        lv_obj_set_size(s_dots[i], 8, 8);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_dots[i], i == 0 ? COL_ORANGE : COL_PANEL, 0);
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
    if (s < 60)   { snprintf(buf, len, "just now"); return; }
    if (s < 120)  { snprintf(buf, len, "1 minute ago"); return; }
    if (s < 3600) { snprintf(buf, len, "%ld minutes ago", s / 60); return; }
    snprintf(buf, len, "%ld:%02ld hours ago", s / 3600, (s % 3600) / 60);
}

static void update_block(const ui_state_t *st)
{
    char buf[48], zahl[24];

    clock_hhmm(buf, sizeof(buf));
    lv_label_set_text(s_clock, buf);

    if (st->stats->price_valid) {
        gruppiert(zahl, sizeof(zahl), st->stats->price_usd);
        lv_label_set_text_fmt(s_price, "$%s", zahl);
    } else {
        lv_label_set_text(s_price, "");
    }

    const block_t *b = st->block;
    if (!b->valid) {
        lv_obj_set_style_bg_color(s_band, COL_PANEL, 0);
        lv_obj_set_style_text_color(s_band_cap, COL_DIM, 0);
        lv_obj_set_style_text_color(s_pool, COL_DIM, 0);
        lv_label_set_text(s_band_cap, "waiting for data");
        lv_obj_set_style_text_font(s_pool, &comic_40, 0);
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
    lv_label_set_text(s_band_cap, "found by");

    lv_obj_set_style_text_font(s_pool, passende_schrift(b->pool), 0);
    lv_label_set_text(s_pool, b->pool);

    long alter = (long)(time(NULL) - b->timestamp);
    alter_text(alter, buf, sizeof(buf));
    lv_label_set_text(s_age, buf);
    /* Ueber zwanzig Minuten ohne Block ist nichts Schlimmes, aber
     * bemerkenswert. Mehr als diesen einen Wechsel braucht die Seite
     * nicht -- sie soll gelesen und nicht entschluesselt werden. */
    lv_obj_set_style_text_color(s_age, alter > 20 * 60 ? COL_ORANGE : COL_TEXT, 0);

    /* Die Blockhoehe steht ohne Trennzeichen da: Sie ist keine Menge,
     * sondern eine laufende Nummer. */
    lv_label_set_text_fmt(s_height, "block %d", b->height);

    gruppiert(zahl, sizeof(zahl), b->tx_count);
    lv_label_set_text(s_tile_a_val, zahl);
    lv_obj_set_style_text_color(s_tile_a_val, COL_TEXT, 0);
    lv_label_set_text(s_tile_a_cap, "transactions");
    lv_obj_clear_flag(s_tile_a, LV_OBJ_FLAG_HIDDEN);

    /* Die Belohnung sind neue Muenzen plus Gebuehren. Drei Nachkommastellen
     * reichen: Die Halbierung auf 3,125 BTC ist damit zu sehen, das
     * Rauschen der Gebuehren steht darunter in Sats. */
    snprintf(zahl, sizeof(zahl), "%.3f", (double)b->reward / 1e8);
    lv_label_set_text(s_tile_b_val, zahl);
    lv_obj_set_style_text_color(s_tile_b_val, COL_ORANGE, 0);
    lv_label_set_text(s_tile_b_cap, "BTC reward");
    lv_obj_clear_flag(s_tile_b, LV_OBJ_FLAG_HIDDEN);

    gruppiert(zahl, sizeof(zahl), (long)b->fees);
    lv_label_set_text_fmt(s_footer, "including %s sat in fees", zahl);
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

    /* Jede Zeile haengt an ihrer eigenen Anfrage. Was nicht da ist, steht
     * als Strich da -- eine 0 waere eine Behauptung. */
    if (s->diff_valid) zeile(i++, "block time", COL_TEXT, "%.1f min", (double)s->block_min);
    else               zeile(i++, "block time", COL_DIM, "--");

    /* Die Hashrate kommt mit der Poolliste, nicht mit der Schwierigkeit. */
    if (s->pools_valid) zeile(i++, "hashrate", COL_CYAN, "%.0f EH/s", (double)s->hashrate_eh);
    else                zeile(i++, "hashrate", COL_DIM, "--");

    /* Steigende Schwierigkeit heisst, dass Rechenleistung dazugekommen
     * ist -- gruen. Fallende rot, nach derselben Lesart. */
    if (s->diff_valid) zeile(i++, "difficulty", s->change_pct >= 0 ? COL_GREEN : COL_RED,
                             "%+.1f %% in %d", (double)s->change_pct, s->remaining_blocks);
    else               zeile(i++, "difficulty", COL_DIM, "--");

    if (s->fees_valid) zeile(i++, "fee now / 1 hour", COL_YELLOW,
                             "%d / %d sat/vB", s->fast_fee, s->hour_fee);
    else               zeile(i++, "fee now / 1 hour", COL_DIM, "--");

    if (s->mempool_valid) {
        gruppiert(hilf, sizeof(hilf), s->mempool_count);
        zeile(i++, "mempool", COL_TEXT, "%s tx, %.0f MB", hilf, (double)s->mempool_mb);
    } else {
        zeile(i++, "mempool", COL_DIM, "--");
    }

    if (s->price_valid) {
        gruppiert(hilf, sizeof(hilf), s->price_usd);
        zeile(i++, "price", COL_ORANGE, "$%s", hilf);
    } else {
        zeile(i++, "price", COL_DIM, "--");
    }

    leerzeile(i++);

    /* Wer die letzten 24 Stunden gefunden hat. Genau das ordnet den Namen
     * auf der ersten Seite ein: Foundry mit einem Viertel aller Bloecke
     * ist kein Zufallstreffer. */
    if (s->pools_valid && s->pools > 0) {
        zeile(i++, "pools, last 24 h", COL_DIM, "%d blocks", s->blocks_24h);
        for (int k = 0; k < s->pools && i < ROWS; k++)
            zeile(i++, s->pool[k].name, COL_BLUE, "%d", s->pool[k].blocks);
    } else {
        zeile(i++, "pools, last 24 h", COL_DIM, "--");
    }

    while (i < ROWS - 3) leerzeile(i++);

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) zeile(i++, "wifi", COL_TEXT, "%d dBm", ap.rssi);
    else                                         zeile(i++, "wifi", COL_RED, "disconnected");

    int64_t up = esp_timer_get_time() / 1000000;
    zeile(i++, "uptime", COL_TEXT, "%lldd %02lld:%02lld",
          up / 86400, (up % 86400) / 3600, (up % 3600) / 60);

    if (b->valid) {
        alter_text((long)(now - b->fetched), alt, sizeof(alt));
        zeile(i++, "last update", COL_TEXT, "%s", alt);
    } else {
        zeile(i++, "last update", COL_DIM, "never");
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
