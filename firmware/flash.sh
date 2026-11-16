#!/usr/bin/env bash
# Flash XiaoZhi assistant firmware + custom assets to the Watcher (ESP32-S3 port).
# Does NOT touch 0x9000 (nvsfactory: device EUI/keys). Restore stock with ./restore_stock.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
B="$HERE/../vendor/xiaozhi-esp32/build"
PORT="${PORT:-/dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C2C160775-if02}"
ASSETS="${ASSETS:-$HERE/build/assets.bin}"
uv run -q --with esptool esptool --chip esp32s3 --port "$PORT" --baud 921600 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-freq 80m --flash-size 32MB \
  0x0      "$B/bootloader/bootloader.bin" \
  0x8000   "$B/partition_table/partition-table.bin" \
  0x10d000 "$B/ota_data_initial.bin" \
  0x200000 "$B/xiaozhi.bin" \
  0xa00000 "$ASSETS"
