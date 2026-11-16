#!/bin/bash
# SBusSound bare metal - sestaveni obsahu SD karty.
#
#   ./sestav.sh
#
# Potrebuje prekladac arm-none-eabi-gcc v PATH a knihovnu Circle v podslozce
# circle/ (git submodule). Vysledek: slozka vystup/karta/ - jeji obsah se
# nakopiruje do korene SD karty (FAT32, MBR), k tomu zvuky do sounds/.
set -euo pipefail
cd "$(dirname "$0")"
KOREN=$(pwd)
CIRCLE="$KOREN/circle"
# firmware Raspberry Pi - stejna verze, jakou doporucuje Circle
FW_VERZE=0641c5bf25d185d5bf25d8dcafd1da6dc8fdbf68
FW_SOUBORY="bootcode.bin start.elf fixup.dat LICENCE.broadcom"

command -v arm-none-eabi-gcc >/dev/null || { echo "CHYBA: arm-none-eabi-gcc neni v PATH"; exit 1; }
[ -f "$CIRCLE/configure" ] || { echo "CHYBA: chybi circle/ - spust: git submodule update --init"; exit 1; }

# 1) Circle: Raspberry Pi 3, 32 bit, ovladac karty EMMC misto SDHOST
#    (SDHOST pri zapisu na kartu blokuje preruseni a zvuk pak praska).
#    Sestavuje se jen jednou; znovu po smazani circle/lib/libcircle.a.
if [ ! -f "$CIRCLE/lib/libcircle.a" ] || ! grep -q NO_SDHOST "$CIRCLE/Config.mk" 2>/dev/null; then
  echo "=== sestavuji knihovnu Circle (jednou, par minut) ==="
  ( cd "$CIRCLE" && ./configure -r 3 -d NO_SDHOST -f >/dev/null && ./makeall clean >/dev/null 2>&1 || true )
  ( cd "$CIRCLE" && ./makeall --nosample -j4 >/dev/null )
  make -C "$CIRCLE/addon/SDCard" -j4 >/dev/null
  make -C "$CIRCLE/addon/fatfs" -j4 >/dev/null
fi

# 2) kernel
echo "=== sestavuji kernel ==="
make -C "$KOREN/src" CIRCLEHOME="$CIRCLE" -j4 | grep -E "WC|error|warning" || true
[ -f "$KOREN/src/kernel8-32.img" ] || { echo "CHYBA: kernel nevznikl"; exit 1; }

# 3) obsah karty
echo "=== skladam obsah karty ==="
OUT="$KOREN/vystup/karta"
rm -rf "$OUT" && mkdir -p "$OUT"
cp -R "$KOREN/karta/." "$OUT/"
cp "$KOREN/src/kernel8-32.img" "$OUT/"
FW_CACHE="$KOREN/vystup/firmware-$FW_VERZE"
mkdir -p "$FW_CACHE"
for f in $FW_SOUBORY; do
  if [ ! -s "$FW_CACHE/$f" ]; then
    if [ -n "${FIRMWARE_DIR:-}" ]; then cp "$FIRMWARE_DIR/$f" "$FW_CACHE/$f"
    else curl -sSfL -o "$FW_CACHE/$f" "https://raw.githubusercontent.com/raspberrypi/firmware/$FW_VERZE/boot/$f"; fi
  fi
  cp "$FW_CACHE/$f" "$OUT/"
done
( cd "$OUT" && ls -la )
echo "HOTOVO: obsah karty je ve slozce vystup/karta/"
