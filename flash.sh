#!/usr/bin/env bash
# Bauen und aufs Board bringen, ohne vorher die ESP-IDF-Umgebung von Hand
# zu aktivieren.
#
#   ./flash.sh               bauen, flashen, Monitor
#   ./flash.sh monitor       nur den Monitor oeffnen
#   ./flash.sh menuconfig    beliebiger idf.py-Befehl
#
# Ohne Befehl laeuft "build flash monitor". Der Monitor endet mit Strg-].

# Kein "set -u": ESP-IDFs export.sh stolpert ueber ungesetzte Variablen.
set -eo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

die() { echo "$*" >&2; exit 1; }

case "${1-}" in
    -h|--help) sed -n '2,9p' "$0" | sed -E "s/^# ?//"; exit 0 ;;
esac

cmds=("$@")
[ ${#cmds[@]} -gt 0 ] || cmds=(build flash monitor)

# --- Umgebung ----------------------------------------------------------------
# export.sh braucht ein paar Sekunden. In einer Shell, in der die Umgebung
# schon aktiv ist, wird sie deshalb nicht noch einmal geladen.
if ! command -v idf.py >/dev/null 2>&1; then
    [ -f "$ROOT/activate.sh" ] || die "activate.sh fehlt neben flash.sh."
    source "$ROOT/activate.sh" >/dev/null || die "ESP-IDF liess sich nicht aktivieren. Einmal \"source activate.sh\" von Hand ausfuehren zeigt warum."
fi

# Der Port kann aus einer aelteren Shell stammen und ins Leere zeigen.
if [ -z "$ESPPORT" ] || [ ! -e "$ESPPORT" ]; then
    unset ESPPORT
    for p in /dev/cu.usbmodem* /dev/ttyACM*; do
        [ -e "$p" ] && export ESPPORT="$p" && break
    done
fi

for c in "${cmds[@]}"; do
    case "$c" in
        flash|monitor|app-flash|erase-flash|erase_flash)
            [ -n "$ESPPORT" ] || die "Kein Board gefunden. USB-C-Datenkabel angeschlossen?" ;;
    esac
done

cd "$ROOT"
echo "==> idf.py ${cmds[*]} | ${ESPPORT:-ohne Board}"
exec idf.py "${cmds[@]}"
