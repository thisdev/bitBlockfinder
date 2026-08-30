# bitBlockfinder

Eine Bitcoin-Blockanzeige für den Waveshare ESP32-S3-Touch-AMOLED-1.8.

Sie beantwortet genau eine Frage: **Wer hat den aktuellen Block gefunden,
und wie lange ist das her?** Der Name des Pools steht groß auf oranger
Fläche, darunter die Minuten. Alles andere ist Begründung und steht klein.

## Die zwei Seiten

Gewischt wird mit dem Finger, zwei Punkte am unteren Rand zeigen, wo man ist.

1. **Block.** Wer ihn gefunden hat, wie lange das her ist, die Blockhöhe,
   die Zahl der Transaktionen und die Belohnung samt Gebühren. Dauert es
   länger als zwanzig Minuten, wird die Altersangabe gelb — kein Fehler,
   nur bemerkenswert.
2. **Netz und Gerät.** Mittlere Blockzeit, geschätzte Hashrate, die
   nächste Schwierigkeitsanpassung, Gebührenempfehlung, Mempool. Darunter
   die drei Pools mit den meisten Blöcken der letzten 24 Stunden — das
   ordnet den Namen von Seite 1 ein. Zuletzt WLAN, Laufzeit und das Alter
   der beiden Abrufe.

Der obere der beiden Knöpfe schaltet die Helligkeit in drei Stufen durch.
Nach 30 Sekunden ohne Berührung dimmt das Display, ganz aus geht es nie.

## Woher die Daten kommen

Von [mempool.space](https://mempool.space) über die öffentliche
REST-Schnittstelle, ohne Anmeldung und ohne Schlüssel:

| Endpunkt | wofür | wie groß | wie oft |
|---|---|---|---|
| `/api/blocks/tip/hash` | gibt es einen neuen Block? | 64 Bytes | jede Minute |
| `/api/v1/block/<hash>` | Pool, Zeit, Gebühren | ~2 KB | nur bei neuem Block |
| `/api/v1/difficulty-adjustment` | Blockzeit, Retarget | 340 Bytes | alle 10 min |
| `/api/v1/fees/recommended` | Gebührenempfehlung | 74 Bytes | alle 10 min |
| `/api/mempool` | wartende Transaktionen | ~4 KB | alle 10 min |
| `/api/v1/mining/pools/24h` | Pools, Hashrate | ~3 KB | alle 10 min |

Der Trick ist die erste Zeile. Es wäre bequem, jede Minute die Liste der
letzten fünfzehn Blöcke zu holen — das sind 30 KB. Stattdessen fragt das
Gerät nach dem Hash des obersten Blocks, vierundsechzig Bytes, und holt
den Block selbst erst, wenn dieser Hash ein anderer ist als der bekannte.
Im Alltag sind das 60 winzige Anfragen je Stunde und sechs etwas größere.
Wenig genug für einen Dienst, der nichts kostet.

Das **Alter rechnet das Gerät selbst** aus der Blockzeit und der NTP-Uhr
aus. Die Minutenzahl läuft deshalb weiter, auch wenn das WLAN gerade weg
ist — dann eben ohne neuen Block. Kommt der Abruf länger als drei
Intervalle nicht durch, sagt die Statuszeile es.

## Voraussetzungen

* Waveshare ESP32-S3-Touch-AMOLED-1.8 (16 MB Flash, 8 MB PSRAM)
* **USB-C auf USB-C Datenkabel.** Ein reines Ladekabel reicht nicht.
* WLAN im 2,4-GHz-Band
* ESP-IDF v5.5 oder neuer

Kein Konto, kein Schlüssel, keine eigene Node.

## Installation

```
cp local.defaults.example local.defaults   # WLAN eintragen
./flash.sh                                 # bauen, flashen, Monitor
```

`flash.sh` lädt die ESP-IDF-Umgebung selbst und sucht den Port des Boards;
von Hand geht es genauso mit `source activate.sh && idf.py build flash
monitor`. Der Monitor endet mit Strg-].

`local.defaults` und `sdkconfig` stehen in der `.gitignore` — in beiden
steht das WLAN-Passwort, in `sdkconfig` einkompiliert.

## Was beim Bauen zu beachten war

**Zeichenpuffer und TLS teilen sich denselben internen Speicher.** Das BSP
legt den LVGL-Puffer ohne DMA-Kennzeichnung an; der SPI-Treiber muss
deshalb vor jeder Übertragung eine Kopie in DMA-fähigem internem RAM
anlegen. Bei der Vorgabe von 100 Zeilen sind das 72 KB am Stück, die in
einem durch TLS zerstückelten Heap nicht mehr zu bekommen sind — beim
bitSolarPlaner hat genau das den Bildaufbau zum Stehen gebracht. Hier
holt das Gerät beim Start sogar sechs Antworten hintereinander, also
stehen beide Gegenmaßnahmen in `sdkconfig.defaults`: kleinerer
Zeichenpuffer (40 Zeilen) und dynamische mbedTLS-Puffer.

**Poolnamen werden gemessen, nicht gezählt.** Sie sind zwischen fünf
(`OCEAN`) und fünfzehn Zeichen lang (`Mining Squared`), und
Großbuchstaben brauchen fast doppelt so viel Platz wie Kleinbuchstaben:
`ULTIMUSPOOL` ist bei elf Zeichen breiter als `Foundry USA`. Die
Schriftgröße richtet sich deshalb nach der tatsächlichen Pixelbreite —
40 Punkt, wenn es passt, sonst 28, sonst 20.

**Nur ASCII.** Die eingebauten Montserrat-Schriften von LVGL können
nichts anderes; alles darüber fliegt vor der Anzeige heraus. Deshalb
steht auf dem Display „Gebuehr" und nicht „Gebühr". In einem Jahr
Blockhistorie kam kein Poolname mit Sonderzeichen vor — die Prüfung
bleibt trotzdem drin.

**Ohne erkannten Pool steht „unbekannt".** Wer ohne Kennung im Coinbase
schürft, ist nicht zuzuordnen. Genau das soll dann auch dastehen.

## Trockenlauf ohne Board

`chain.c` hängt an keiner ESP-Bibliothek außer cJSON. Mit einem kleinen
Rahmenprogramm, das die Antworten aus Dateien statt aus dem Netz liest,
lässt sich die gesamte Auswertung auf dem Rechner durchspielen — inklusive
Tausenderpunkten, Altersangaben und der Auswahl der drei größten Pools.
