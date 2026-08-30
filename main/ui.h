#pragma once
#include <stdbool.h>
#include "chain.h"

/* Alle ui_*-Funktionen erwarten, dass LVGL gesperrt ist (bsp_display_lock). */

typedef struct {
    const block_t *block;
    const stats_t *stats;
} ui_state_t;

void ui_create(void);
void ui_set_status(const char *text);

/* Seite 0 oder 1 anzeigen, ohne Wischgeste. */
void ui_show_page(int n);

void ui_update(const ui_state_t *st);
