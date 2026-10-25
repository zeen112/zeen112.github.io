#!/usr/bin/env bash
# Build firmware lalu salin 4 file biner ke editor/public/firmware (dipakai tombol "Flash via USB").
# Butuh PlatformIO CLI (pio). Jalankan dari folder mana pun.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$ROOT/firmware"
OUT="$ROOT/editor/public/firmware"
ENV=esp32dev
BUILD="$FW/.pio/build/$ENV"

pio run -d "$FW" -e "$ENV"

BOOT_APP0="$(find "$HOME/.platformio/packages" -name boot_app0.bin 2>/dev/null | head -n1)"
if [ -z "$BOOT_APP0" ]; then
  echo "boot_app0.bin tidak ditemukan. Pastikan framework Arduino ESP32 sudah ter-install di PlatformIO." >&2
  exit 1
fi

mkdir -p "$OUT"
cp "$BUILD/bootloader.bin"  "$OUT/bootloader.bin"
cp "$BUILD/partitions.bin"  "$OUT/partitions.bin"
cp "$BOOT_APP0"             "$OUT/boot_app0.bin"
cp "$BUILD/firmware.bin"    "$OUT/firmware.bin"
echo "Firmware siap di $OUT"
ls -l "$OUT"
