#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Uhrzeit per NTP. Laeuft im Hintergrund an: Die Anzeige startet sofort und
 * zeigt die Uhr, sobald sie steht. */
void clock_start(void);
bool clock_valid(void);

/* "14:37" in den Puffer schreiben, oder "--:--" solange keine Zeit da ist. */
void clock_hhmm(char *buf, size_t len);

/* Minute seit Mitternacht (0..1439), oder -1 ohne gueltige Zeit. */
int  clock_minute_of_day(void);

/* JJJJMMTT, oder 0 ohne gueltige Zeit. */
int  clock_day_key(void);
