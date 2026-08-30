# bitBlockfinder

Zeigt auf einem **Waveshare ESP32-S3-Touch-AMOLED-1.8**, wer den aktuellen
Bitcoin-Block gefunden hat und wie lange das her ist.

<p>
  <img src="doc/block.png" width="270" alt="Seite 1: gefundener Block">
  <img src="doc/network.png" width="270" alt="Seite 2: Netz und Gerät">
</p>

Zwei Seiten, gewischt wird mit dem Finger:

1. **Block** — der Pool auf orangem Grund, das Alter des Blocks, die
   Blockhöhe, Transaktionen, Belohnung und Gebühren. Oben rechts der
   BTC-Kurs. Die Altersangabe wird orange, wenn es über zwanzig Minuten
   her ist.
2. **Netz und Gerät** — Blockzeit, Hashrate, Schwierigkeitsanpassung,
   Gebührenempfehlung, Mempool, Kurs, die drei größten Pools der letzten
   24 Stunden, dazu WLAN, Laufzeit und Alter des letzten Abrufs.

Der obere der beiden Knöpfe schaltet die Helligkeit in drei Stufen durch.
Nach 30 Sekunden ohne Berührung dimmt das Display, ganz aus geht es nie.

## Was man braucht

* Waveshare ESP32-S3-Touch-AMOLED-1.8 (16 MB Flash, 8 MB PSRAM)
* USB-C-auf-USB-C-**Datenkabel** — ein reines Ladekabel reicht nicht
* WLAN im 2,4-GHz-Band
* ESP-IDF 5.5 oder neuer

Kein Konto, kein API-Schlüssel, keine eigene Node.

## Installation

**1. ESP-IDF einrichten** — einmalig, dauert ein paar Minuten:

```bash
git clone -b v5.5.5 --depth 1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
```

```bash
cd ~/esp/esp-idf && ./install.sh esp32s3
```

**2. Repo holen und WLAN eintragen:**

```bash
git clone https://github.com/thisdev/bitBlockfinder.git
```

```bash
cd bitBlockfinder && cp local.defaults.example local.defaults
```

In `local.defaults` stehen SSID und Passwort. Die Datei ist in der
`.gitignore` und bleibt lokal.

**3. Board anschließen und flashen:**

```bash
./flash.sh
```

Das baut, flasht und öffnet den seriellen Monitor; beendet wird er mit
Strg-]. `flash.sh` lädt die ESP-IDF-Umgebung selbst und sucht den Port des
Boards. Von Hand geht es genauso mit `source activate.sh` und
`idf.py build flash monitor`.

Die Abhängigkeiten (Waveshare-BSP, LVGL) zieht der Component Manager beim
ersten Bauen von selbst.

## Einstellungen

Entweder in `local.defaults` oder über `idf.py menuconfig` → *Blockfinder*:

| Option | Vorgabe | Bedeutung |
|---|---|---|
| `CONFIG_BF_WIFI_SSID` / `_PASS` | — | Zugangsdaten |
| `CONFIG_BF_POLL_S` | `60` | Sekunden zwischen zwei Nachfragen |
| `CONFIG_BF_SHOT_ENABLE` | aus | stellt `/shot` und `/page?n=` für Bildschirmaufnahmen bereit |

## Woher die Daten kommen

Von [mempool.space](https://mempool.space) über die öffentliche
REST-Schnittstelle:

| Endpunkt | wofür | Größe | wie oft |
|---|---|---|---|
| `/api/blocks/tip/hash` | gibt es einen neuen Block? | 64 B | jede Minute |
| `/api/v1/block/<hash>` | Pool, Zeit, Gebühren | ~2 KB | nur bei neuem Block |
| `/api/v1/difficulty-adjustment` | Blockzeit, Retarget | 340 B | alle 10 min |
| `/api/v1/fees/recommended` | Gebührenempfehlung | 74 B | alle 10 min |
| `/api/mempool` | wartende Transaktionen | ~4 KB | alle 10 min |
| `/api/v1/mining/pools/24h` | Pools, Hashrate | ~3 KB | alle 10 min |
| `/api/v1/prices` | BTC-Kurs | 108 B | alle 10 min |

Der Kniff ist die erste Zeile. Statt jede Minute die Liste der letzten
fünfzehn Blöcke zu holen — 30 KB — fragt das Gerät nach dem Hash des
obersten Blocks und holt den Block selbst erst, wenn dieser Hash ein
anderer ist als der bekannte.

Das **Alter rechnet das Gerät selbst** aus der Blockzeit und der NTP-Uhr
aus. Die Minutenzahl läuft weiter, auch wenn das WLAN gerade weg ist.
Fällt eine der Anfragen aus, behält der betroffene Wert seinen letzten
Stand, statt auf null zu springen.

## Aufbau

| Datei | |
|---|---|
| `main/bitblockfinder.c` | Ablauf: was wann geholt und gezeichnet wird |
| `main/chain.c` | Antworten von mempool.space auswerten |
| `main/fetch.c` | HTTPS gegen das Wurzelzertifikat-Bundle |
| `main/ui.c` | die beiden Seiten |
| `main/net.c`, `clock.c` | WLAN und NTP |
| `main/backlight.c` | Knopf und Ruheabsenkung |
| `main/shot.c` | Bildschirmaufnahmen über HTTP, standardmäßig aus |
| `main/schrift/` | Comic Neue als LVGL-Schriften |

## Schrift und Farben

Die Schrift ist **Comic Neue** unter der SIL Open Font License; Lizenz und
Erzeugung stehen in [`main/schrift/`](main/schrift/README.md).

Die Farben stammen aus dem Stylesheet von mempool.space: Untergrund
`#11131f`, Karten `#272f4e`, dazu deren Akzentfarben auf der zweiten
Seite. Die erste Seite kommt mit Weiß und dem Bitcoin-Orange `#f7931a`
aus — sie soll gelesen und nicht entschlüsselt werden.
