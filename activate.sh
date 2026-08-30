#!/usr/bin/env bash
# ESP-IDF-Umgebung aktivieren:  source activate.sh
#
# Fuer den Alltag reicht ./flash.sh -- das laedt diese Datei selbst. Von Hand
# gesourced wird sie, wenn man idf.py direkt aufrufen will.
#
# Sucht ESP-IDF an den ueblichen Stellen, stellt bei Bedarf eine passende
# Python-Version voran und setzt den Port des angeschlossenen Boards.

# --- ESP-IDF finden ---
if [ -z "$IDF_PATH" ]; then
    for d in "$HOME/esp/esp-idf" "$HOME/esp-idf" "/opt/esp-idf"; do
        [ -f "$d/export.sh" ] && IDF_PATH="$d" && break
    done
fi
if [ -z "$IDF_PATH" ] || [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "ESP-IDF nicht gefunden. Einmalig einrichten:"
    echo "  git clone -b v5.5.5 --depth 1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf"
    echo "  cd ~/esp/esp-idf && ./install.sh esp32s3"
    return 1 2>/dev/null || exit 1
fi

# --- Python ---
# ESP-IDF ist nicht fuer jede Python-Version freigegeben. Auf dem Mac liegt
# 3.14 im PATH; export.sh sucht das venv anhand der Version im PATH und findet
# dann ein idf5.5_py3.14_env, das install.sh nie angelegt hat. Deshalb eine
# bekannt gute Version voranstellen, falls vorhanden.
for v in 3.12 3.11 3.10; do
    p="/opt/homebrew/opt/python@$v/libexec/bin"
    [ -d "$p" ] && export PATH="$p:$PATH" && break
done

source "$IDF_PATH/export.sh"

# --- Port des Boards ---
# Der ESP32-S3 meldet sich mit eigenem USB, unter macOS als cu.usbmodem*,
# unter Linux als ttyACM*.
unset ESPPORT
for p in /dev/cu.usbmodem* /dev/ttyACM*; do
    [ -e "$p" ] && export ESPPORT="$p" && break
done
[ -n "$ESPPORT" ] && echo "Board: $ESPPORT" || echo "Kein Board gefunden. USB-C-Datenkabel angeschlossen?"
