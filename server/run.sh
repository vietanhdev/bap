#!/usr/bin/env bash
# Render config from .env and (re)start the assistant server.
set -euo pipefail
cd "$(dirname "$0")"
set -a; source .env; set +a
: "${GEMINI_API_KEY:?Điền GEMINI_API_KEY trong .env}"
: "${GROQ_API_KEY:?Điền GROQ_API_KEY trong .env}"
export SERVER_IP="${SERVER_IP:-$(ip -4 route get 1.1.1.1 | awk '{for(i=1;i<=NF;i++) if($i=="src") print $(i+1)}')}"
mkdir -p data/music data/tmp
python3 -c 'import os,sys; sys.stdout.write(os.path.expandvars(open("config.template.yaml").read()))' > data/.config.yaml
chmod 600 data/.config.yaml
docker compose up -d --force-recreate
echo "Server: ws://${SERVER_IP}:8000/xiaozhi/v1/  OTA: http://${SERVER_IP}:8003/xiaozhi/ota/"
