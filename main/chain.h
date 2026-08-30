#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

/* Alles, was von mempool.space kommt.
 *
 * Zwei Dinge werden getrennt geholt, weil sie sich unterschiedlich schnell
 * aendern: der aktuelle Block (im Mittel alle zehn Minuten) und die
 * Netzzahlen fuer die zweite Seite (in Minuten kaum). */

#define POOL_NAME_MAX   28
#define TOP_POOLS        3

typedef struct {
    bool     valid;
    char     hash[65];
    int      height;
    time_t   timestamp;          /* Zeitstempel im Blockkopf, UTC        */
    char     pool[POOL_NAME_MAX];
    int      tx_count;
    int64_t  reward;             /* Sats, neue Muenzen samt Gebuehren    */
    int64_t  fees;               /* Sats, nur die Gebuehren              */
    int      size;               /* Bytes                                */
    float    median_fee;         /* sat/vB                               */
    time_t   fetched;            /* wann zuletzt erfolgreich geholt      */
} block_t;

/* Jede Gruppe hat ihr eigenes Gueltigkeitszeichen, weil jede aus einer
 * eigenen Anfrage stammt. Faellt eine aus, bleiben die anderen stehen --
 * und die ausgefallene behaelt ihre vorigen Werte, statt auf null zu
 * springen. Eine zehn Minuten alte Hashrate ist mehr wert als eine 0. */
typedef struct {
    bool     diff_valid;
    float    progress_pct;       /* Fortschritt in der Schwierigkeitsrunde */
    float    change_pct;         /* erwartete Aenderung beim Retarget      */
    int      remaining_blocks;
    float    block_min;          /* mittlere Blockzeit in Minuten          */

    bool     fees_valid;
    int      fast_fee, hour_fee; /* sat/vB                                 */

    bool     mempool_valid;
    int      mempool_count;
    float    mempool_mb;         /* wartende Blockgroesse in MB (vsize)    */

    bool     price_valid;
    int      price_usd;          /* Bitcoinkurs in ganzen US-Dollar         */

    bool     pools_valid;
    int      pools;              /* gefuellte Eintraege in pool[]          */
    int      blocks_24h;         /* Bloecke insgesamt in 24 Stunden        */
    float    hashrate_eh;        /* geschaetzte Rechenleistung in EH/s      */
    struct {
        char name[POOL_NAME_MAX];
        int  blocks;
    } pool[TOP_POOLS];

    time_t   fetched;
} stats_t;

/* Hash des obersten Blocks holen -- vierundsechzig Bytes Antwort, die
 * kleinste sinnvolle Anfrage. Damit laesst sich jede Minute nachsehen,
 * ohne einem kostenlosen Dienst zur Last zu fallen: Erst wenn dieser
 * Hash ein anderer ist als der bekannte, wird der Block selbst geholt. */
esp_err_t chain_tip_hash(char *buf, size_t len);

/* Den genannten Block samt Pool holen. Ueberschreibt *b nur bei Erfolg. */
esp_err_t chain_fetch_block(block_t *b, const char *hash);

/* Netzzahlen fuer die zweite Seite. Teilerfolge sind ausdruecklich
 * erlaubt: Was diesmal nicht durchkam, behaelt seinen letzten Wert.
 * ESP_FAIL kommt nur zurueck, wenn keine einzige Anfrage geklappt hat --
 * dann bleibt *s vollstaendig unberuehrt. */
esp_err_t chain_fetch_stats(stats_t *s);
