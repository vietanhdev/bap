#!/usr/bin/env bash
# Build a custom assets.bin: Vietnamese Nunito font + character face GIFs + wake-word model.
# Requires a firmware build first (./build.sh) for sdkconfig and esp-sr models.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../vendor/xiaozhi-esp32"
STAGE="$HERE/build/assets-stage"
rm -rf "$STAGE"; mkdir -p "$STAGE/cbin" "$STAGE/gif" "$HERE/build"
cp "$HERE/assets/fonts/font_nunito_vi_30_4.bin" "$STAGE/cbin/font_noto_sans_common_30_4.bin"
cp "$SRC/managed_components/78__xiaozhi-fonts/manifest.json" "$STAGE/manifest.json"
cp -r "$HERE/assets/faces" "$STAGE/gif/faces"
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$SRC":/project -v "$HERE":/fw -w /project \
  espressif/idf:release-v6.1 python scripts/build_default_assets.py \
    --sdkconfig /project/sdkconfig \
    --builtin_text_font font_noto_sans_basic_30_4 \
    --emoji_collection faces \
    --noto_fonts_path /fw/build/assets-stage \
    --esp_sr_model_path /project/managed_components/espressif__esp-sr/model \
    --max_size 0x1000000 \
    --output /fw/build/assets.bin
ls -la "$HERE/build/assets.bin"
