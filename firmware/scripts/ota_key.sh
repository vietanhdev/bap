#!/usr/bin/env bash
# Create the RSA-3072 key that signs firmware for verified OTA updates.
#
#   firmware/scripts/ota_key.sh
#
# The key goes to secrets/ota_signing_key.pem (never committed). Back it up somewhere safe: a device
# running signed firmware only accepts Wi-Fi updates signed with this key. If the key is lost, the
# device can still be re-flashed over USB (firmware/flash_app.sh), which also installs a new key.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
KEY="$HERE/../../secrets/ota_signing_key.pem"
if [[ -e "$KEY" ]]; then
  echo "$KEY already exists; refusing to overwrite it (devices trust that key)." >&2
  exit 1
fi
mkdir -p "$(dirname "$KEY")" && chmod 700 "$(dirname "$KEY")"
uv run -q --with esptool espsecure generate-signing-key --version 2 --scheme rsa3072 "$KEY"
chmod 600 "$KEY"
echo "Created $KEY. Back it up: without it, signed devices need a USB flash to update."
