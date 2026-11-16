#!/usr/bin/env bash
# Restore the original SenseCraft firmware from the full 32MB backup taken on 2026-11-16.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
IMG="$HERE/../backup/watcher-w1a-esp32s3-full-32MB.bin"
PORT="${PORT:-/dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C2C160775-if02}"
( cd "$(dirname "$IMG")" && sha256sum -c --ignore-missing "$(basename "$IMG").sha256" )
uv run -q --with esptool esptool --chip esp32s3 --port "$PORT" --baud 921600 write-flash 0x0 "$IMG"
