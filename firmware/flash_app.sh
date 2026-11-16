#!/usr/bin/env bash
# Flash only the application (fast). Use ./flash.sh for a full flash including assets.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PORT="${PORT:-/dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C2C160775-if02}"
uv run -q --with esptool esptool --chip esp32s3 --port "$PORT" --baud 921600 write-flash 0x200000 "$HERE/../vendor/xiaozhi-esp32/build/xiaozhi.bin"
