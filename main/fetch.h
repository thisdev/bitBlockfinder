#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "cJSON.h"

/* Antworten von mempool.space per HTTPS holen.
 *
 * Geprueft wird gegen das Wurzelzertifikat-Bundle der ESP-IDF, damit hier
 * kein einzelnes Zertifikat gepflegt werden muss, das irgendwann ablaeuft
 * und das Geraet stumm schaltet.
 *
 * Zwei Formen, weil die Schnittstelle beides liefert: Die Endpunkte unter
 * /api/blocks/tip/ antworten mit nacktem Text (eine Zahl, ein Hash), alles
 * andere mit JSON. */

/* JSON holen. Der Aufrufer gibt das Ergebnis mit cJSON_Delete() frei. */
esp_err_t fetch_json(const char *url, cJSON **out);

/* Text in einen vorhandenen Puffer holen. Fuehrende und abschliessende
 * Leerzeichen und Zeilenumbrueche fallen weg -- eine Antwort mit
 * Zeilenumbruch am Ende laesse sonst jeden Vergleich scheitern. */
esp_err_t fetch_text(const char *url, char *buf, size_t len);
