# Schriften

**Comic Neue** von Craig Rozynski und Mitwirkenden, unter der
[SIL Open Font License 1.1](OFL.txt) — der Lizenztext liegt daneben.

Aus der TrueType-Datei mit `lv_font_conv` in vier Größen übersetzt, 4 bpp,
nur ASCII (0x20–0x7F):

| Datei | Größe | Schnitt | wofür |
|---|---|---|---|
| `comic_14.c` | 14 px | Regular | Fließtext, zweite Seite |
| `comic_20.c` | 20 px | Regular | Bildunterschriften, Blockhöhe |
| `comic_28.c` | 28 px | Bold | Alter des Blocks, Kachelwerte |
| `comic_40.c` | 40 px | Bold | Name des Pools |

Die großen Größen sind fett gesetzt: Comic Neue Regular ist deutlich
leichter als Comic Sans und wirkt auf dem AMOLED aus zwei Metern dünn.

Neu erzeugen:

```
npm i lv_font_conv
npx lv_font_conv --font ComicNeue-Bold.ttf --size 40 --bpp 4 \
    --no-compress --format lvgl --range 0x20-0x7F \
    --lv-include lvgl.h -o comic_40.c
```

Der Name der Variablen ergibt sich aus dem Dateinamen, deshalb heißen sie
`comic_14` bis `comic_40` und nicht nach der Schrift.
