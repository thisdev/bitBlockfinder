#pragma once
#include <stdint.h>

/* Helligkeitsverwaltung: drei feste Stufen per Taste, dazu automatisches
 * Absenken bei Ruhe. Beides greift ineinander -- der erste Druck nach dem
 * Absenken weckt nur auf und schaltet noch nicht weiter. */
void backlight_start(void);
