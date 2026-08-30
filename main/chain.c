#include "chain.h"
#include "fetch.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "kette";

#define API "https://mempool.space/api"

/* Die eingebauten Montserrat-Schriften von LVGL koennen nur ASCII. Ein
 * Poolname mit Zeichen darueber hinaus wuerde als leeres Kaestchen oder
 * gar nicht erscheinen -- also fliegen sie hier raus, bevor sie in die
 * Anzeige geraten. */
static void nur_ascii(char *dst, size_t len, const char *src)
{
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 1 < len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c >= 0x20 && c <= 0x7E) dst[j++] = (char)c;
    }
    dst[j] = '\0';
    if (j == 0) strlcpy(dst, "unbekannt", len);
}

static double zahl(const cJSON *o, const char *key, double vorgabe)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(it) ? it->valuedouble : vorgabe;
}

static bool ist_hash(const char *s)
{
    if (strlen(s) != 64) return false;
    for (int i = 0; i < 64; i++)
        if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

esp_err_t chain_tip_hash(char *buf, size_t len)
{
    esp_err_t err = fetch_text(API "/blocks/tip/hash", buf, len);
    if (err != ESP_OK) return err;
    if (!ist_hash(buf)) {
        ESP_LOGW(TAG, "Das sieht nicht nach einem Blockhash aus: \"%s\"", buf);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t chain_fetch_block(block_t *b, const char *hash)
{
    if (!ist_hash(hash)) return ESP_ERR_INVALID_ARG;

    /* Ein Block einzeln, nicht die Liste der letzten fuenfzehn: zwei
     * Kilobyte statt dreissig, und mehr braucht die Anzeige nicht. */
    char url[128];
    snprintf(url, sizeof(url), API "/v1/block/%s", hash);

    cJSON *j = NULL;
    esp_err_t err = fetch_json(url, &j);
    if (err != ESP_OK) return err;

    const cJSON *ex = cJSON_GetObjectItemCaseSensitive(j, "extras");
    const cJSON *po = ex ? cJSON_GetObjectItemCaseSensitive(ex, "pool") : NULL;
    const cJSON *nm = po ? cJSON_GetObjectItemCaseSensitive(po, "name") : NULL;

    block_t neu = { 0 };
    strlcpy(neu.hash, hash, sizeof(neu.hash));
    neu.height    = (int)zahl(j, "height", 0);
    neu.timestamp = (time_t)zahl(j, "timestamp", 0);
    neu.tx_count  = (int)zahl(j, "tx_count", 0);
    neu.size      = (int)zahl(j, "size", 0);

    /* Ohne erkannten Pool bleibt es bei "unbekannt". Das kommt vor: Wer
     * ohne Kennung im Coinbase schuerft, ist nicht zuzuordnen -- und
     * genau das soll dann auch dastehen. */
    nur_ascii(neu.pool, sizeof(neu.pool), cJSON_IsString(nm) ? nm->valuestring : NULL);

    if (ex) {
        neu.reward     = (int64_t)zahl(ex, "reward", 0);
        neu.fees       = (int64_t)zahl(ex, "totalFees", 0);
        neu.median_fee = (float)zahl(ex, "medianFee", 0);
    }

    cJSON_Delete(j);

    if (neu.height <= 0 || neu.timestamp <= 0) {
        ESP_LOGW(TAG, "Antwort ohne Hoehe oder Zeitstempel");
        return ESP_ERR_INVALID_RESPONSE;
    }

    neu.valid   = true;
    neu.fetched = time(NULL);
    *b = neu;

    ESP_LOGI(TAG, "Block %d von %s, %d Transaktionen, %lld sat Gebuehren",
             neu.height, neu.pool, neu.tx_count, (long long)neu.fees);
    return ESP_OK;
}

/* --- Netzzahlen ---------------------------------------------------------
 *
 * Vier kleine Anfragen statt einer grossen. Faellt eine aus, bleiben die
 * uebrigen Werte stehen: Die Seite soll nicht komplett leer sein, nur
 * weil gerade ein Endpunkt hakt. */

static bool schwierigkeit(stats_t *s)
{
    cJSON *j = NULL;
    if (fetch_json(API "/v1/difficulty-adjustment", &j) != ESP_OK) return false;

    s->progress_pct     = (float)zahl(j, "progressPercent", 0);
    s->change_pct       = (float)zahl(j, "difficultyChange", 0);
    s->remaining_blocks = (int)zahl(j, "remainingBlocks", 0);
    /* timeAvg kommt in Millisekunden und ist die mittlere Blockzeit der
     * laufenden Runde. Zehn Minuten sind der Sollwert; darunter heisst,
     * dass Rechenleistung dazugekommen ist. */
    s->block_min        = (float)(zahl(j, "timeAvg", 0) / 60000.0);
    s->diff_valid       = true;

    cJSON_Delete(j);
    return true;
}

static bool gebuehren(stats_t *s)
{
    cJSON *j = NULL;
    if (fetch_json(API "/v1/fees/recommended", &j) != ESP_OK) return false;

    s->fast_fee   = (int)zahl(j, "fastestFee", 0);
    s->hour_fee   = (int)zahl(j, "hourFee", 0);
    s->fees_valid = true;

    cJSON_Delete(j);
    return true;
}

static bool mempool(stats_t *s)
{
    cJSON *j = NULL;
    if (fetch_json(API "/mempool", &j) != ESP_OK) return false;

    s->mempool_count = (int)zahl(j, "count", 0);
    /* vsize ist die Groesse in virtuellen Bytes. Geteilt durch eine
     * Million ergibt das grob die Zahl der Bloecke, die noetig waeren,
     * um den Mempool zu leeren -- die anschaulichere Groesse. */
    s->mempool_mb    = (float)(zahl(j, "vsize", 0) / 1000000.0);
    s->mempool_valid = true;

    cJSON_Delete(j);
    return true;
}

/* Der Kurs ist die einzige Zahl auf der ersten Seite, die nicht aus dem
 * Block stammt. Er wird mit den uebrigen Netzzahlen geholt und ist damit
 * hoechstens zehn Minuten alt -- fuer eine Anzeige an der Wand genau
 * richtig, und es spart jede Minute einen weiteren Handschlag. */
static bool preis(stats_t *s)
{
    cJSON *j = NULL;
    if (fetch_json(API "/v1/prices", &j) != ESP_OK) return false;

    int usd = (int)zahl(j, "USD", 0);
    if (usd > 0) {
        s->price_usd   = usd;
        s->price_valid = true;
    }

    cJSON_Delete(j);
    return usd > 0;
}

static bool top_pools(stats_t *s)
{
    cJSON *j = NULL;
    if (fetch_json(API "/v1/mining/pools/24h", &j) != ESP_OK) return false;

    s->blocks_24h  = (int)zahl(j, "blockCount", 0);
    /* Hashrate in Hash je Sekunde, also eine 21-stellige Zahl. In
     * Exahash geteilt bleibt eine Zahl, die auf den Schirm passt. */
    s->hashrate_eh = (float)(zahl(j, "lastEstimatedHashrate", 0) / 1e18);

    const cJSON *pools = cJSON_GetObjectItemCaseSensitive(j, "pools");
    if (cJSON_IsArray(pools)) {
        s->pools = 0;
        /* Die Antwort kommt sortiert, aber darauf verlassen sich diese
         * drei Zeilen nicht -- sie suchen sich die groessten selbst. */
        const cJSON *p = NULL;
        cJSON_ArrayForEach(p, pools) {
            int n = (int)zahl(p, "blockCount", 0);
            const cJSON *nm = cJSON_GetObjectItemCaseSensitive(p, "name");
            if (!cJSON_IsString(nm)) continue;

            int platz = s->pools;
            while (platz > 0 && s->pool[platz - 1].blocks < n) platz--;
            if (platz >= TOP_POOLS) continue;

            for (int k = (s->pools < TOP_POOLS ? s->pools : TOP_POOLS - 1); k > platz; k--)
                s->pool[k] = s->pool[k - 1];

            nur_ascii(s->pool[platz].name, POOL_NAME_MAX, nm->valuestring);
            s->pool[platz].blocks = n;
            if (s->pools < TOP_POOLS) s->pools++;
        }
    }

    s->pools_valid = true;
    cJSON_Delete(j);
    return true;
}

esp_err_t chain_fetch_stats(stats_t *s)
{
    /* Auf dem bisherigen Stand aufsetzen statt bei null anzufangen.
     * Genau ein misslungener Handschlag unter fuenf hat sonst die
     * Hashrate auf "0 EH/s" gesetzt und die Poolliste geleert -- fuer
     * die vollen zehn Minuten bis zum naechsten Versuch. */
    stats_t neu = *s;
    bool    was = false;

    /* Die Poolliste bringt auch die Hashrate mit, deshalb muss ihr
     * Gueltigkeitszeichen vor dem Versuch zurueckgesetzt werden -- sonst
     * traegt der neue Durchlauf Namen in eine halb alte Liste. */
    neu.pools = 0;

    was |= schwierigkeit(&neu);
    was |= gebuehren(&neu);
    was |= mempool(&neu);
    was |= top_pools(&neu);
    was |= preis(&neu);

    if (!was) return ESP_FAIL;

    /* Was diesmal nicht durchkam, steht noch mit seinem alten Wert in
     * "neu" -- ausser der Poolliste, die oben geleert wurde. */
    if (neu.pools == 0 && s->pools_valid) {
        for (int k = 0; k < s->pools; k++) neu.pool[k] = s->pool[k];
        neu.pools = s->pools;
    }

    neu.fetched = time(NULL);
    *s = neu;

    ESP_LOGI(TAG, "Netz: %.1f min je Block, %.0f EH/s, Mempool %d Transaktionen, %d USD",
             neu.block_min, neu.hashrate_eh, neu.mempool_count, neu.price_usd);
    return ESP_OK;
}
