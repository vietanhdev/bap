#!/usr/bin/env bash
# Build XiaoZhi firmware for SenseCAP Watcher.
#   MODE=gemini (default): direct device -> Gemini Live, no server. Key from ../secrets/GEMINI_API_KEY
#   MODE=server: self-hosted xiaozhi-esp32-server at SERVER_IP
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../vendor/xiaozhi-esp32"
MODE="${MODE:-gemini}"
WAKE_WORD="${WAKE_WORD:-hiesp}"
LANG_CODE="${LANG_CODE:-vi-VN}"
BOARD_DIR="$SRC/main/boards/sensecap-watcher"

# Our sources live in firmware/src and are copied into the upstream tree for the build.
rm -rf "$SRC/main/assistant"; mkdir -p "$SRC/main/assistant"
cp "$HERE"/src/assistant/* "$SRC/main/assistant/"
cp "$HERE"/src/protocols/* "$SRC/main/protocols/"
embed() {  # embed <file> <symbol> <output header>
  { echo '#pragma once'; echo "// Generated from firmware/$1 by build.sh"
    printf 'static const char %s[] = R"BAPDATA(' "$2"; cat "$HERE/$1"; echo ')BAPDATA";'; } > "$3"
}
embed web/index.html kWebPage "$SRC/main/assistant/web_page.h"
embed data/assistant.json kAssistantJson "$SRC/main/assistant/builtin_data.h"

python3 - "$HERE/config.home.json" "$BOARD_DIR/config.home.json" "$MODE" <<'PY'
import json, os, sys
src, dst, mode = sys.argv[1:4]
cfg = json.load(open(src))
app = cfg["builds"][0]["sdkconfig_append"]
if mode == "gemini":
    key = open(os.path.join(os.path.dirname(src), "..", "secrets", "GEMINI_API_KEY")).read().strip()
    app = [l for l in app if not l.startswith("CONFIG_OTA_URL")]
    app += ["CONFIG_USE_GEMINI_LIVE=y", f'CONFIG_GEMINI_API_KEY="{key}"',
            f'CONFIG_GEMINI_MODEL="{os.environ.get("GEMINI_MODEL", "gemini-3.1-flash-live-preview")}"',
            f'CONFIG_GEMINI_VOICE="{os.environ.get("GEMINI_VOICE", "Leda")}"',
            "CONFIG_SEND_WAKE_WORD_DATA=n"]
else:
    ip = os.environ.get("SERVER_IP", "192.168.0.58")
    app = [l.replace("__OTA_URL__", f"http://{ip}:8003/xiaozhi/ota/") for l in app]
cfg["builds"][0]["sdkconfig_append"] = app
json.dump(cfg, open(dst, "w"), indent=2)
os.chmod(dst, 0o600)
PY

docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$SRC":/project -w /project \
  espressif/idf:release-v6.1 \
  bash -c "git config --global --add safe.directory '*'; python scripts/build.py sensecap-watcher -c config.home.json --language ${LANG_CODE} --wake-word ${WAKE_WORD} ${EXTRA_ARGS:-}"
echo "Built MODE=${MODE}"
