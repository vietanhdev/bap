#!/usr/bin/env bash
# Update the Watcher over Wi-Fi (no cable). Usage: ./ota.sh [host]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
HOST="${1:-bap.local}"
curl -sS -m 180 -X POST -H 'X-Bap: 1' -H 'Content-Type: application/octet-stream' \
  --data-binary @"$HERE/../vendor/xiaozhi-esp32/build/xiaozhi.bin" "http://$HOST/api/ota"; echo
