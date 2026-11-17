#!/usr/bin/env bash
# Flash only the application (fast). Use ./flash.sh for a full flash including assets.
# Also resets otadata so the device boots ota_0 even after earlier Wi-Fi (OTA) updates to ota_1.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/scripts/find_port.sh"
uv run -q --with esptool esptool --chip esp32s3 --port "$PORT" --baud 921600 write-flash \
  0x10d000 "$HERE/../vendor/xiaozhi-esp32/build/ota_data_initial.bin" \
  0x200000 "$HERE/../vendor/xiaozhi-esp32/build/xiaozhi.bin"
