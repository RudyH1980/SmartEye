#!/usr/bin/env bash
# Builds the single image the browser installer flashes.
# Run from the firmware directory after a successful build.
set -e

ESPTOOL=$(find "$HOME/.platformio/packages/tool-esptoolpy" -name esptool.py | head -1)
OUT=../docs/smarteye-firmware.bin

python "$ESPTOOL" --chip esp32s3 merge_bin -o "$OUT" \
    --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x0     .pio/build/smarteye/bootloader.bin \
    0x8000  .pio/build/smarteye/partitions.bin \
    0x10000 .pio/build/smarteye/firmware.bin

echo "Klaar: $OUT"
