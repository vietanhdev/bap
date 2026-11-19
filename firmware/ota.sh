#!/usr/bin/env bash
# Update the Watcher over Wi-Fi (no cable). Usage: ./ota.sh [host]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
HOST="${1:?usage: ota.sh <device IP or bap-xxxx.local>}"
curl -sS -m 180 -X POST -H 'X-Bap: 1' -H 'Content-Type: application/octet-stream' \
  --data-binary @"$HERE/../vendor/xiaozhi-esp32/build/bap.bin" "http://$HOST/api/ota"; echo
