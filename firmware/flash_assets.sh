#!/usr/bin/env bash
# Re-flash only the assets partition (font + character faces) after ./build_assets.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PORT="${PORT:-/dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C2C160775-if02}"
uv run -q --with esptool esptool --chip esp32s3 --port "$PORT" --baud 921600 write-flash 0xa00000 "$HERE/build/assets.bin"
