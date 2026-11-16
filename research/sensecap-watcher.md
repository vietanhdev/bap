# Seeed Studio SenseCAP Watcher: research notes

*Compiled 2026-11-16 from public web sources. Nothing was bought, flashed, or signed up for. Items marked **[unverified]** could not be confirmed from a primary source. Items marked **[inference]** are my own reasoning, not a sourced claim.*

---

## TL;DR

- The Watcher is a ~US$50–100 desk/wall gadget. It pairs an **ESP32-S3** (Wi-Fi/BLE, UI, audio, networking) with a **Himax HX6538 (WiseEye2)** microNPU that runs small vision models (person, face, custom TinyML) on-device. It also has a 1.45" round touchscreen, a camera, one mic, a 1 W speaker, and a scroll wheel.
- It ships in two "personalities", which are also sold as different SKUs:
  1. **SenseCraft task firmware (W1-A / W1-B):** you tell it "notify me when X happens" in the phone app. A tiny on-device model triggers, a cloud (or self-hosted) VLM confirms, and it sends alerts by app push, HTTP, UART, Home Assistant or Node-RED.
  2. **XiaoZhi firmware ("Watcher for XiaoZhi"):** an open-source voice agent (wake word → ASR → LLM → TTS) with **MCP tool calling**. The backend can be self-hosted (`xiaozhi-esp32-server`, which supports Ollama, local ASR and local TTS).
- Everything is open: Apache-2.0 hardware/firmware repos, an ESP-IDF SDK, and the MIT-licensed XiaoZhi firmware. That makes **fully local designs feasible**, with your RTX 3090 box as the "big brain".
- **Best first project for you:** use the Watcher's on-device person or custom trigger, push events over LAN HTTP to your own receiver on the 3090 box, verify them with a local VLM, and send Telegram alerts plus Home Assistant entities. That is a privacy-first, self-hosted "smart camera that understands scenes".

---

## 1. Hardware and platform

### 1.1 Specs

| Item | Value | Source |
|---|---|---|
| Main MCU | ESP32-S3 @ 240 MHz, 8 MB PSRAM, 32 MB flash | [Seeed product page][prod], [HW overview][hw] |
| AI chip | Himax HX6538 (WiseEye2): Cortex-M55 + Ethos-U55 microNPU, 16 MB flash | [prod], [hw] |
| HX6538 detail | Cortex-M55 "big" up to 400 MHz + "little" up to 150 MHz, Ethos-U55 up to 400 MHz, ~2.4 MB SRAM (same chip as Grove Vision AI V2) | [CNX Software][cnx] (third-party). Not stated on the Watcher pages |
| Camera | OV5647, 120° FOV, fixed focus at 3 m. Resolution **not stated** by Seeed (the OV5647 sensor itself is 5 MP; the effective capture resolution is **[unverified]**) | [prod], [hw] |
| Display | 1.45" round touchscreen, 412×412 | [prod] |
| Audio | 1× microphone, 1 W speaker (I2S) | [prod], [hw] |
| Input / indicator | Scroll wheel with push button (power, navigation, push-to-talk), reset pinhole, 1× RGB LED | [prod], [quick start][qs] |
| Storage | microSD up to 32 GB (FAT32) | [prod] |
| Radio | Wi-Fi 802.11 b/g/n **2.4 GHz only**, BLE 5 | [prod] |
| Expansion | 1× Grove I2C; 2×4 female header (I2C, 2× GPIO, 3.3 V out, 5 V in, GND) | [prod], [hw] |
| Power | 5 V DC over USB-C. Back port is power only; bottom port is power + programming. Seeed recommends a 5 V/1 A adapter and warns that >5 V can damage it | [hw] |
| Battery | 3.7 V **400 mAh** Li-ion, described as *backup* power | [prod] |
| Size / temperature | 69 × 65 × 20 mm, 0–45 °C | [prod] |
| Mounting | Wall/desk/bracket. Box includes swivel base, 1/4" tripod adapter, USB-C cable | [prod] |
| Serial console | Two USB serial ports: ESP32 console at 115200 (commands include `wifi_sta`, `ota`, `taskflow`, `battery`, `record`, `rgb`, `factory_reset`…), and Himax flashing over Xmodem | [hw], [Hackster hands-on][hackster] |

**Battery life:** I found no official or review runtime figure **[unverified]**. With 400 mAh, a Wi-Fi radio, a screen and an NPU, expect roughly an hour or less of active use **[inference]**. Treat it as a mains-powered device with UPS-like backup.

### 1.2 Firmware options

| Firmware | What it is | Licence / source | Notes |
|---|---|---|---|
| **Stock SenseCraft task firmware** | Factory firmware. Tasks are created by chat or voice in the SenseCraft app, then run as an on-device "task flow" (trigger → analyse → notify). | Factory .bin files in [OSHW-SenseCAP-Watcher][oshw] (Apache-2.0) | Shipped on W1-A (clear) / W1-B (white). |
| **SenseCAP-Watcher-Firmware (ESP-IDF SDK)** | SDK components plus examples (incl. `factory_firmware`), ESP-IDF v5.2.1, target esp32s3 | [GitHub][sdk], Apache-2.0, ~76★, no tagged releases | Full control. You can write your own C firmware (e.g. post JPEGs straight to your server). Highest effort. |
| **XiaoZhi (xiaozhi-esp32)** | Open-source voice-agent firmware: offline wake word (ESP-SR), streaming ASR→LLM→TTS or realtime voice models, device- and cloud-side **MCP**, 39 UI languages. SenseCAP Watcher is a listed board. | [78/xiaozhi-esp32][xz] MIT, ~33k★. Current README asks for ESP-IDF ≥ 6.0.1 | Defaults to the xiaozhi.me cloud server. Can point at self-hosted servers (see 1.4). Seeed sells a dedicated "Watcher for XiaoZhi" SKU. |
| **Seeed "Watcher Agent" / Visual-trigger XiaoZhi build** | Seeed's XiaoZhi build: a Himax person or face detection **wakes the conversation**. Managed from the SenseCraft AI "Watcher Agent" workspace (web control panel, roles, voices, MCP endpoint). | Merged binary v2.0.4 on Seeed's file server. Source build uses ESP-IDF 5.5.1 + xiaozhi repo | Visual wake needs ≥1.8.8. Above v2.0.3 you must say "Turn on the inference switch". **Non-EN units need a separate authentication file** to flash it ([visual trigger guide][vt]). Default wake word "Jarvis"; changing it requires recompiling ([web panel][panel]). |
| **Agora voice-agent firmware** | Community PlatformIO firmware that joins an Agora RTC session for a conversational agent | [AgoraIO-Community repo][agora], no licence file, 3★ | Depends on Agora cloud. Niche. |

⚠️ **SKU / licence gating:** Seeed's W1-A product page says the Clear Enclosure "AI Vision Edition" is **not compatible with the XiaoZhi firmware** ([prod]). The visual-trigger guide also mentions auth files for some units ([vt]). The upstream MIT xiaozhi firmware builds for the Watcher board, so this restriction probably applies to Seeed's packaged XiaoZhi/Agent service rather than to self-built firmware **[unverified; confirm before buying]**. **If voice-agent use matters, buy the "for XiaoZhi" SKU, or one of each.**

Flashing paths ([vt]): the SenseCraft AI web flasher (browser serial), Espressif Flash Download Tool ≥3.9.6, `esptool.py`, or building from source. Factory images are in [oshw] for recovery. Seeed advises not touching the Himax firmware except to restore it.

### 1.3 What runs where

| Layer | Where | Detail |
|---|---|---|
| Small vision models (person, face, gesture, pet, custom TinyML) | **On-device (Himax HX6538)** | Pick from the [SenseCraft AI model library][models] or train your own in SenseCraft AI (Models → Training, supports Watcher + Grove Vision AI V2) ([training guide][train]). In tasks: Detail Config → "Use TinyML Model". Seeed's XiaoZhi visual-trigger build currently supports **only Face and Person** models ([vt]). Edge Impulse custom training is also mentioned ([makiuto blog][makiuto], secondary). |
| Wake word | On-device (ESP32-S3, ESP-SR) | XiaoZhi firmware. |
| Task parsing ("tell me when the cat is on the table" → task flow JSON) | Cloud LLM (SenseCraft) **or** local "Watcher APP" service | See plans below. |
| Image analysis (VLM confirms the scene) | Cloud (SenseCraft / OpenAI) **or** local (Llama 3.1 + LLaVA via Watcher APP) | ([local deploy][local], [service framework][svc]) |
| Voice chat (ASR / LLM / TTS) | xiaozhi.me cloud, SenseCraft Agent cloud, **or self-hosted** xiaozhi-esp32-server | ([xz], [xzs]) |

Seeed's three deployment modes ([service framework][svc]):
1. **Cloud efficient:** orchestration and AI in the SenseCraft cloud.
2. **Hybrid:** orchestration in SenseCraft, vision analysis on your local server ("private Watcher service" URL).
3. **Local secure:** orchestration and vision analysis entirely on your server. Tasks are issued by voice; notifications are **UART and HTTP only** (no app push).

Seeed advertises "Local AI + Cloud LLM": the on-device model gates the expensive cloud call, so requests are only counted when the tiny model fires ([pricing][price]).

### 1.4 Local / self-hosted LLM options

1. **Seeed "Watcher APP" (Device AI Service)** ([local deploy][local])
   - Runs Llama 3.1 + LLaVA locally (~10 GB download), or OpenAI with your own key.
   - Installers: Windows .exe, macOS .dmg, **Linux .deb arm64 only** (Jetson AGX Orin listed). **No x86-64 Linux build and no Docker** are documented, so it won't install natively on a typical x86 RTX 3090 Linux box. A Windows VM or other workaround would be needed **[inference]**.
   - Pairing: copy a URL and token from the Watcher APP into the SenseCraft app's "Device AI Service" setting. The port and protocol are not documented, and whether it is OpenAI-compatible (i.e. could point at Ollama) is **[unverified]**.
   - Seeed benchmarks (task / image analysis): RTX 4060 5 s / 4 s; Mac Mini M1 36 s / 8 s; Jetson AGX Orin 32 GB 18 s / 7 s; CPU-only 17 min / 4 min.
2. **Self-hosted XiaoZhi backend: [xinnan-tech/xiaozhi-esp32-server][xzs]** (MIT, ~11k★, Docker)
   - Local ASR: FunASR, Sherpa, Vosk. LLM: Ollama or any OpenAI-compatible endpoint (so vLLM, llama.cpp and LM Studio all work). Local TTS: FishSpeech, GPT-SoVITS, IndexTTS, PaddleSpeech. VLLM: any OpenAI-compatible endpoint.
   - Supports MCP (server-side MCP, MCP access points, custom tools).
   - The README says it is **not production-ready / not security-audited**: keep it on LAN or Tailscale.
   - Other server ports exist in Java and Go ([xz] README).
   - Seeed has a reference build: [XiaoZhi + Dify + Home Assistant MCP][hadify], all in Docker locally, though the example uses a cloud LLM key.
3. **Your own receiver:** just consume the Watcher's **HTTP push** on your LAN and do all heavy AI yourself (see Idea #1). This avoids relying on Seeed's local app.
4. **Custom ESP-IDF firmware** ([sdk]): no SenseCraft at all. Highest effort.

### 1.5 Outputs and integrations

| Output | How | Cloud? | Source |
|---|---|---|---|
| SenseCraft app push | Default alarm action | Cloud | [qs] |
| **HTTP push** | Task → Detail Config → HTTP Push Notification: URL plus optional token. The device posts alarm events (text, image, sensor values) to your URL. The Node-RED example listens on `POST /v1/notification/event`, and alarm text appears as `events.text`. **The full JSON schema is not documented**; capture it yourself. | Device → your LAN endpoint | [Node-RED guide][nr], [HA guide][ha] |
| UART alarm | Task → "Serial Port / UART Output". Binary struct `tf_module_uart_alarm_t`, format chosen via `output_format`. A Japanese page notes bounding-box output was still in development in v1.1. | Local | [UART][uart] |
| MQTT / HTTP API (cloud) | SenseCraft data platform (Standard/Pro plans); the Node-RED "sensecap" palette OpenStream node uses org ID + API key | Cloud | [price], [nr] |
| **Home Assistant** | HACS integration [SenseCraft-HomeAssistant][hacs] (MIT, small: ~10★). Use the **host/id (LAN)** option with the device EUI; point the task's HTTP push at `http://<HA-IP>:8887`. Entities include `sensor.alarm`, `image.alarm_image`, plus temperature/humidity/CO₂ sensors. A reviewer called it "very preliminary" ([hackster]). | Local LAN | [ha] |
| HA voice / control | XiaoZhi → Dify agent → HA MCP Server integration | Local, except the LLM in the example | [hadify] |
| Node-RED | Official flows to Telegram, Discord, WhatsApp, Twilio, IFTTT, Kafka, MongoDB, P5.js, Open Interpreter | Depends on target | [nr] |
| MCP tools (XiaoZhi) | Your Python FastMCP server connects out over WebSocket to the agent's MCP endpoint (`mcp_pipe.py`, example [mcp-calculator][mcpcalc]). With Seeed's Agent the endpoint lives in the SenseCraft cloud; with a self-hosted server it's yours. | Cloud relay or self-hosted | [MCP endpoint][mcp] |
| Grove sensors via MCP | Seeed guide "Extending Grove with MCP" | Local | [xiaozhi section][xzsec] |

### 1.6 SenseCraft plans (cost of the cloud path)

From [Seeed's pricing page][price]:
- **On-Premise:** free. You store data and use the local HTTP API; task analysis, image analysis and LLM chat are listed as unlimited, because you run them.
- **Standard:** free. Data retained 3 months, MQTT/HTTP API. LLM chat **200 requests/month**. Image analysis listed as "15 minutes of service per request" (wording unclear).
- **Pro:** US$6.90, **per device (EUI)**, non-expiring. 20,000 image analyses + 1,000 LLM chats. One free trial activates on binding.
- Model repository, training and upload are free on all plans.

### 1.7 Price and where to buy (checked 2026-11-16; prices move)

| Variant | Price seen | Where |
|---|---|---|
| W1-A (clear enclosure, "AI Vision Edition") | **US$49.41** sale (list US$54.90) | [Seeed store][prod] |
| Launch price, for reference | US$99 | [Make:][make] |
| W1-B (white) | ¥18,513 (JP, incl. tax) | [Switch Science][ss] |
| Watcher for XiaoZhi | ~US$54.90 retail (excl. tax); US$63.10 at RobotShop; €73.95 at OpenELAB | [Switch Science XiaoZhi][ssxz], [RobotShop][rs] |
| Accessories | Mini tripod (separate); 5 V/3 A adapter suggested | [prod], [qs] |

Several EU and UK retailers showed sold-out listings at search time. Check Seeed direct first.

---

## 2. Prebuilt applications (out of the box or published projects)

| # | Application | What it does | Setup | Cloud vs local | Cost |
|---|---|---|---|---|---|
| 1 | **Task flows ("monitor X and notify me")** | Describe a task by chat or push-to-talk ("tell me if someone is at the door between 22:00 and 06:00"). Watcher shows a task card (Object / Behavior / Notification / Time range / Frequency) and runs it: on-device model + VLM + alarm (sound, LED, app, HTTP, UART). Built-in templates: person, cat, "paper" hand gesture. ([qs], [task guide][taskg]) | **Easy** (phone app, 10 min) | Cloud by default; hybrid or local with Watcher APP | Free tier 200 LLM chats/month; Pro $6.90/device |
| 2 | **SenseCraft model zoo + custom TinyML** | Swap the on-device model (e.g. a keyboard detector in Seeed's example) or train your own (classification/detection) in SenseCraft AI, then tick "Use TinyML Model" in a task. ([models], [train], [Hackster custom TinyML][hacktiny]) | **Easy–Medium** | Training in SenseCraft cloud (free); inference on-device | Free |
| 3 | **Local AI deployment (Watcher APP)** | Seeed desktop app runs Llama 3.1 + LLaVA so task parsing and image analysis stay on your machine ([local]) | **Easy** on Win/macOS/Jetson; awkward on x86 Linux | Local (task creation still via the SenseCraft app) | Free; needs a GPU PC |
| 4 | **Home Assistant (SenseCraft HACS integration)** | Alarm state, alarm image and sensor entities in HA; build automations or dashboards ([ha], [hacs]) | **Medium** (HACS, EUI, HTTP push) | Local LAN | Free |
| 5 | **Node-RED → Telegram / Discord / WhatsApp / Twilio / IFTTT / Kafka / MongoDB / Open Interpreter / P5.js** | Official example flows forwarding Watcher alarms ([nr]) | **Easy–Medium** | HTTP push local; destinations vary | Free (Twilio etc. paid) |
| 6 | **XiaoZhi voice assistant** | "Hey Jarvis" desk assistant: chat, roles, voices, chat history, OTA, via the web control panel ([panel], [xz]) | **Easy** on the XiaoZhi SKU | xiaozhi.me or SenseCraft cloud by default | Free tier **[unverified limits]** |
| 7 | **Visual-trigger conversation** | Camera sees a person or face, so Watcher starts talking (greeter / reception) ([vt]) | **Medium** (flash v2.0.4) | Hybrid | Free |
| 8 | **XiaoZhi MCP endpoint + Grove via MCP** | Voice → LLM calls your Python tools (APIs, DBs, sensors) ([mcp], [mcpcalc]) | **Medium** (Python) | Relay via SenseCraft, or self-hosted | Free |
| 9 | **Face recognition via MCP** | "Who is this?" Watcher snaps a photo and sends it to a face-rec API (reComputer + Hailo-8 on port 8001), then replies with name and confidence ([facerec]) | **Medium–Hard** (custom firmware branch, server) | **Local** (server on LAN) | Needs a server (any GPU box would do **[inference]**) |
| 10 | **XiaoZhi + Dify + HA MCP smart-home voice** | Voice control of HA devices via a Dify agent ([hadify]) | **Hard-ish** (Docker: Dify + xiaozhi-server) | Local infra plus an LLM API (swappable for Ollama **[inference]**) | LLM API cost or free if local |
| 11 | **Seeed use-case demos** | Visitor directions, virtual flower-shop cashier, fall detection / pet monitor / lost-property alert ([use cases][uc]) | Demo-level | Mixed | — |
| 12 | **Community: Agora voice agent** | Real-time conversational agent over Agora RTC ([agora]) | Medium (PlatformIO) | Agora cloud | Agora usage |

Community activity is modest. The main repo has ~270★ and active [GitHub Discussions][disc]. There are a handful of Hackster posts and listings on [AI Tinkerers][ait]. The XiaoZhi ecosystem (33k★ firmware, 11k★ server) is much larger and is the stronger long-term bet.

---

## 3. Ideas we could build (ranked)

The ranking weighs fit with your setup (3090 GPU box, Tailscale, Zabbix → Telegram, HA-style automation, the NRL creator team), effort, and how much the Watcher adds over a plain IP camera or phone. Effort assumes one experienced engineer.

### #1. Private "scene-understanding" alerts: Watcher trigger → local VLM on the 3090 → Telegram + HA ★ privacy-first
- **Value:** Smart-camera alerts that say what is happening ("courier left a parcel", "someone is at the server shelf", "dog on the sofa"), not just "motion". No images leave the LAN/tailnet. It reuses your existing Telegram alert channel.
- **How:**
  1. Watcher task with an on-device person (or custom) model as the gate.
  2. HTTP push to `http://<gpu-box>:PORT/v1/notification/event` on the LAN, or via a Tailscale subnet router (the Watcher itself can't run Tailscale).
  3. A small FastAPI receiver stores the image and runs a local VLM (Qwen2.5-VL-7B / InternVL / LLaVA on vLLM or Ollama; fits easily in 24 GB) with a per-camera prompt.
  4. It sends a Telegram photo + caption, publishes MQTT/HA entities, and feeds a Zabbix trapper item (event count, device heartbeat).
- **Effort:** 2–4 days for an MVP.
- **Risks:**
  - The HTTP payload schema is undocumented; capture it first.
  - Task creation still goes through the SenseCraft app/account unless you use local-secure mode or custom firmware.
  - The Watcher's own VLM step may fire before the push; the plan is to rely only on the TinyML gate.
  - 2.4 GHz Wi-Fi only.
  - Fixed focus at 3 m, so it suits room-scale scenes.

### #2. Self-hosted voice ops assistant (XiaoZhi + xiaozhi-esp32-server on the 3090) with MCP tools for Zabbix, HA and Tailscale ★ fully local
- **Value:** A desk device you can ask "anything down?", "what's the GPU box temperature?", "turn off studio lights", "ack that alert". It also *speaks* critical Zabbix problems. Fully local: FunASR/Whisper → Ollama/vLLM (Qwen 2.5/3 14–32B) → local TTS.
- **How:**
  - Run `xiaozhi-esp32-server` in Docker on the GPU box.
  - Point the Watcher's XiaoZhi firmware at it (a custom OTA/server URL; may need your own build).
  - Write FastMCP tools wrapping the Zabbix API (`problem.get`, `event.acknowledge`), the HA REST/MCP server, and `tailscale status`.
  - A Zabbix action with a webhook → server → device announcement lets the device speak alerts.
- **Effort:** 4–7 days.
- **Risks:**
  - The server is "not security-audited": keep it tailnet/LAN only and make tools read-only plus allow-listed actions.
  - Accuracy depends on Vietnamese/English ASR quality; check FunASR/Whisper for Vietnamese.
  - The SKU/firmware compatibility caveat (section 1.2).
  - Pushing announcements to the device may need firmware support **[unverified]**.

### #3. Creator-team "idea & take inbox" (push-to-talk → local Whisper → LLM → task tracker)
- **Value:** In the studio or office, hold the wheel and say "idea: lo-fi rain cover thumbnail with neon cat" or "take 3 of track X is the keeper". It gets transcribed, structured (title, tags, channel), and filed into the team's task or notes tool. A shared, zero-friction capture point for the NRL team.
- **How:** XiaoZhi with a self-hosted server. An MCP tool `create_idea(title, details, channel)` posts to your tracker's API. The display shows confirmation.
- **Effort:** 2–3 days on top of #2's server.
- **Risks:** Noisy studio audio; single mic (best at 3–10 cm per [qs]); keep tracker API tokens server-side.

### #4. Studio "On-Air" and occupancy guard
- **Value:** The display shows ON AIR / FREE for the recording room. A person-in-room model plus schedule tells HA to mute notifications, set the door light, and log studio usage hours. It alerts on Telegram if the studio is empty but gear or AC is left on after hours.
- **How:** On-device person model (no images stored) → HTTP → HA automations. Optional Grove CO₂/temperature sensor for room comfort during long sessions.
- **Effort:** 1–2 days.
- **Risks:** Person-model false negatives when people sit still. Privacy expectations of staff: announce it, and log no images.

### #5. Fully offline custom TinyML watcher (no network inference) ★ privacy-first
- **Value:** A single-purpose detector that never sends images anywhere, e.g. "UPS / router panel LED turned red", "3D printer failure", "studio door left open", "package on doormat". It sends only a tiny event (UART or HTTP) to HA/Zabbix.
- **How:** Collect ~200–500 images, train in SenseCraft AI (or Edge Impulse), deploy to the Himax chip, and create a task in local mode with HTTP/UART output.
- **Effort:** 2–4 days, mostly data collection.
- **Risks:** Small-model accuracy and lighting sensitivity. Training happens in Seeed's cloud (upload of training images), though inference is local.

### #6. Front-door / reception visitor log with opt-in face recognition (local)
- **Value:** For the NRL office: greet visitors, recognise enrolled staff (opt-in), notify the host on Telegram with a snapshot, and keep a local visitor log.
- **How:** Seeed's face-rec MCP pattern ([facerec]), but run the face API (InsightFace) on the 3090 box instead of a reComputer.
- **Effort:** 4–6 days.
- **Risks:** Legal and consent issues for biometrics (Vietnam's Decree 13/2023 on personal data treats biometric data as sensitive **[inference: verify applicability]**). Keep it enrollment-only and on local storage.

### #7. Channel stats "desk totem" for the creator team
- **Value:** A round display rotating YouTube subscriber/view counters, today's uploads and RPM. It chimes on milestones, and you can ask it "how did yesterday's upload do?"
- **How:** A server polls the YouTube Data/Analytics API and pushes to the display (custom ESP-IDF UI via LVGL, or XiaoZhi + MCP tool for the voice side).
- **Effort:** 3–5 days (the custom UI is the costly part).
- **Risks:** Custom display work needs firmware changes. API quota.

### #8. Home-lab health sentinel (physical)
- **Value:** Watches the rack or shelf: a person approaching outside working hours, smoke or odd LEDs (custom model), plus a Grove temperature/humidity sensor near the GPU box. It feeds Zabbix as items with triggers to the existing Telegram channel.
- **How:** Combines #1 and #5. Zabbix trapper items via a `zabbix_sender` call in the receiver.
- **Effort:** 1–2 days after #1.
- **Risks:** Overlaps with existing sensors. The 400 mAh battery won't cover power cuts for long.

### #9. Elderly/child/pet room monitor with fall detection ★ privacy-first
- **Value:** Fall or "no movement for N hours" alerts without a streaming camera. Only events and an optional blurred snapshot leave the device.
- **How:** On-device person model gives presence; the local VLM on the 3090 classifies posture ("lying on floor?") only when triggered; Telegram alert.
- **Effort:** 3–5 days, plus careful testing.
- **Risks:** Safety-critical false negatives, so it's a supplement, not a medical device. 120° FOV with fixed focus limits coverage.

### #10. Gesture-controlled studio remote
- **Value:** A hands-free "paper" (open palm) gesture or custom gestures to start/stop recording, switch OBS scenes, or skip tracks during sessions.
- **How:** The built-in gesture task → HTTP → receiver → OBS WebSocket / HA.
- **Effort:** 1 day.
- **Risks:** Latency (task-flow alarms have minimum intervals, [qs]) and accidental triggers. Better as a novelty than a pro tool.

---

## 4. Recommendation

**Pick these three, in this order:**
1. **#1 Private scene-understanding alerts.** Fastest path to real value, works on stock firmware, reuses Telegram/HA/Zabbix, and proves the payload and integration path everything else needs.
2. **#2 Self-hosted voice ops assistant.** The most "AI-engineer" project and fully local. It creates the shared xiaozhi-server + MCP platform.
3. **#3 Creator idea inbox.** Cheap once #2 exists, and gives direct value to the NRL team.

**Hardware purchase advice (not executed):** one **W1-B or W1-A** for #1 (stock task firmware) and one **"Watcher for XiaoZhi"** for #2/#3, because of the firmware/SKU caveat in 1.2. That is ~US$105–120 total at current Seeed prices, plus 5 V adapters.

### Concrete next-step plan for Project #1

**Goal:** Watcher detects a person, the 3090 box describes the scene locally, a Telegram photo alert arrives in under 10 s, and HA gets an entity. No image leaves the LAN/tailnet.

1. **Network prep (0.5 day)**
   - Put the Watcher on a 2.4 GHz IoT SSID/VLAN that can reach only the GPU box receiver port and HA.
   - Reserve a DHCP IP for it.
   - Decide whether the receiver sits on the GPU box directly or behind a Tailscale subnet router.
2. **Payload capture (0.5 day)**
   - Run a throwaway receiver (FastAPI or `nc`) logging raw `POST /v1/notification/event` bodies to disk.
   - Bind the Watcher in the SenseCraft app, create a "person detected" task, set HTTP Push to the receiver URL.
   - Write down the JSON schema (image encoding, `events.text`, timestamps, EUI, token header). **This closes the biggest unknown.**
3. **Keep the cloud out of the hot path (0.5 day)**
   - Configure the task to use the on-device TinyML person model as trigger. Then test which mode avoids cloud image analysis: (a) Seeed's "Local secure" or "Hybrid" mode, or (b) a minimal/generic analysis prompt.
   - Measure with a packet capture on the router/VLAN that no images go to SenseCraft endpoints.
   - If they do, fallback plan: build the SDK's `factory_firmware` with a custom HTTP uploader ([sdk]). That adds ~1 week.
4. **Receiver v1 (1 day)**
   - FastAPI service in Docker on the GPU box: save image + JSON to disk, run a VLM via vLLM/Ollama (start with Qwen2.5-VL-7B-Instruct) with a prompt like "Describe the people/objects; is this a delivery, family member, unknown person? Answer JSON."
   - Rule layer: quiet hours, dedupe window, per-camera prompts.
5. **Outputs (0.5 day)**
   - Telegram `sendPhoto` with caption, using a new bot or your existing alert bot.
   - MQTT discovery entities for HA (`binary_sensor.watcher_person`, `sensor.watcher_last_event`, camera/image entity).
   - `zabbix_sender` items: events/hour plus a heartbeat with a "no data for 15 min" trigger.
6. **Evaluate for 1 week**
   - Log false positives/negatives, end-to-end latency, VLM tokens/s on the 3090, and Wi-Fi drops.
   - Success criteria: ≥90% of real arrivals alerted, ≤2 false alerts/day, median latency ≤10 s.
7. **Harden**
   - Token auth on the receiver, image retention (e.g. 7 days), and a README in this repo.
   - Then move on to Project #2, reusing the GPU box's model server.

### Open questions to resolve early
- Exact HTTP push schema and whether the device sends images in local modes **[unverified]**.
- Whether stock firmware can run tasks with **no** SenseCraft account dependency after setup.
- Whether a W1-A/W1-B can run self-built XiaoZhi firmware (SKU gating) **[unverified]**.
- XiaoZhi ASR/TTS quality for Vietnamese with local models.
- Real battery runtime **[unverified]**.

---

## Sources

[prod]: https://www.seeedstudio.com/SenseCAP-Watcher-W1-A-p-5979.html
[hw]: https://wiki.seeedstudio.com/watcher_hardware_overview/
[qs]: https://wiki.seeedstudio.com/getting_started_with_watcher/
[taskg]: https://wiki.seeedstudio.com/getting_started_with_watcher_task/
[cnx]: https://www.cnx-software.com/2024/01/19/16-grove-vision-ai-v2-module-features-wiseeye2-hx6538-arm-cortex-m55-ethos-u55-ai-microcontroller/
[oshw]: https://github.com/Seeed-Studio/OSHW-SenseCAP-Watcher
[disc]: https://github.com/Seeed-Studio/OSHW-SenseCAP-Watcher/discussions
[sdk]: https://github.com/Seeed-Studio/SenseCAP-Watcher-Firmware
[xz]: https://github.com/78/xiaozhi-esp32
[xzs]: https://github.com/xinnan-tech/xiaozhi-esp32-server
[xzsec]: https://wiki.seeedstudio.com/sensecap_watcher_for_xiaozhi_ai/
[vt]: https://wiki.seeedstudio.com/visual_trigger_and_ai_flash/
[panel]: https://wiki.seeedstudio.com/watcher_web_control_panel/
[mcp]: https://wiki.seeedstudio.com/mcp_endpoint/
[mcpcalc]: https://github.com/78/mcp-calculator
[facerec]: https://wiki.seeedstudio.com/face_regonition_with_mcp/
[hadify]: https://wiki.seeedstudio.com/ha_dify_watcher_llms/
[agora]: https://github.com/AgoraIO-Community/agent-with-sensecap-watcher
[local]: https://wiki.seeedstudio.com/watcher_local_deploy/
[svc]: https://wiki.seeedstudio.com/watcher_software_service_framework/
[price]: https://wiki.seeedstudio.com/watcher_price/
[models]: https://wiki.seeedstudio.com/sensecraft-ai/tutorials/sensecraft-ai-pretrained-models-for-watcher/
[train]: https://wiki.seeedstudio.com/training_model_for_watcher/
[hacktiny]: https://hackster.io/abhinav123krish/custom-tinyml-model-for-sensecap-watcher-452aea
[ha]: https://wiki.seeedstudio.com/integrate_watcher_to_ha/
[hacs]: https://github.com/Seeed-Solution/SenseCraft-HomeAssistant
[nr]: https://wiki.seeedstudio.com/watcher_to_node_red/
[uart]: https://wiki.seeedstudio.com/uart_output/
[uc]: https://wiki.seeedstudio.com/use_case/
[hackster]: https://www.hackster.io/nicolaudosbrinquedos/hands-on-sensecap-watcher-6c6f99
[makiuto]: https://makiuto.github.io/p/sensecap-watcher-updates-use-self-hosted-llava.html
[make]: https://makezine.com/products/boards/seeed-studio-sensecap-watcher
[ss]: https://www.switch-science.com/products/10011
[ssxz]: https://www.switch-science.com/products/10748
[rs]: https://www.robotshop.com/products/seeedstudio-sensecap-watcher-xiaozhi
[ait]: https://aitinkerers.org/technologies/sensecap-watcher

- Seeed product page (W1-A): <https://www.seeedstudio.com/SenseCAP-Watcher-W1-A-p-5979.html>
- Seeed wiki: [Quick start](https://wiki.seeedstudio.com/getting_started_with_watcher/) · [Hardware overview](https://wiki.seeedstudio.com/watcher_hardware_overview/) · [Pricing/plans](https://wiki.seeedstudio.com/watcher_price/) · [Software service framework](https://wiki.seeedstudio.com/watcher_software_service_framework/) · [Local deploy](https://wiki.seeedstudio.com/watcher_local_deploy/) · [Home Assistant](https://wiki.seeedstudio.com/integrate_watcher_to_ha/) · [Node-RED](https://wiki.seeedstudio.com/watcher_to_node_red/) · [UART output](https://wiki.seeedstudio.com/uart_output/) · [Model training](https://wiki.seeedstudio.com/training_model_for_watcher/) · [Pretrained models](https://wiki.seeedstudio.com/sensecraft-ai/tutorials/sensecraft-ai-pretrained-models-for-watcher/) · [XiaoZhi section](https://wiki.seeedstudio.com/sensecap_watcher_for_xiaozhi_ai/) · [Visual trigger & AI flash](https://wiki.seeedstudio.com/visual_trigger_and_ai_flash/) · [Web control panel](https://wiki.seeedstudio.com/watcher_web_control_panel/) · [MCP endpoint](https://wiki.seeedstudio.com/mcp_endpoint/) · [Face recognition with MCP](https://wiki.seeedstudio.com/face_regonition_with_mcp/) · [XiaoZhi + Dify + HA](https://wiki.seeedstudio.com/ha_dify_watcher_llms/) · [Use cases](https://wiki.seeedstudio.com/use_case/)
- GitHub: [OSHW-SenseCAP-Watcher](https://github.com/Seeed-Studio/OSHW-SenseCAP-Watcher) · [SenseCAP-Watcher-Firmware](https://github.com/Seeed-Studio/SenseCAP-Watcher-Firmware) · [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) · [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) · [mcp-calculator](https://github.com/78/mcp-calculator) · [SenseCraft-HomeAssistant](https://github.com/Seeed-Solution/SenseCraft-HomeAssistant) · [Agora voice agent](https://github.com/AgoraIO-Community/agent-with-sensecap-watcher)
- Third-party: [CNX Software on HX6538](https://www.cnx-software.com/2024/01/19/16-grove-vision-ai-v2-module-features-wiseeye2-hx6538-arm-cortex-m55-ethos-u55-ai-microcontroller/) · [Hackster hands-on](https://www.hackster.io/nicolaudosbrinquedos/hands-on-sensecap-watcher-6c6f99) · [Hackster custom TinyML](https://hackster.io/abhinav123krish/custom-tinyml-model-for-sensecap-watcher-452aea) · [makiuto blog (self-hosted LLaVA update)](https://makiuto.github.io/p/sensecap-watcher-updates-use-self-hosted-llava.html) · [Make: listing](https://makezine.com/products/boards/seeed-studio-sensecap-watcher) · [Switch Science W1-B](https://www.switch-science.com/products/10011) · [Switch Science XiaoZhi](https://www.switch-science.com/products/10748) · [RobotShop XiaoZhi](https://www.robotshop.com/products/seeedstudio-sensecap-watcher-xiaozhi) · [AI Tinkerers](https://aitinkerers.org/technologies/sensecap-watcher)

---

## 5. Follow-up (2026-11-16): a Vietnamese assistant for family use

**Verdict: feasible, with XiaoZhi firmware plus a self-hosted `xiaozhi-esp32-server` on the RTX 3090 box.**
The weak points are the wake word (no Vietnamese) and the quality of Vietnamese speech recognition.

| Layer | Vietnamese status | Recommended choice | Source |
|---|---|---|---|
| Device UI | xiaozhi-esp32 ships a `vi-VN` locale; 41 locale folders exist even though the README only advertises zh/en/ja | Build the firmware with the Vietnamese locale **[unverified whether it is a menuconfig/build option]** | [locales](https://github.com/78/xiaozhi-esp32/tree/main/main/assets/locales) |
| Wake word | ESP-SR WakeNet has no Vietnamese. Custom wake-word training covers zh/en/ja/fr and needs a large corpus via Espressif | Keep an English wake word ("Jarvis" on Seeed's build, or "Hi ESP"). The scroll-wheel push-to-talk is easiest for non-technical users | [ESP-SR](https://components.espressif.com/components/espressif/esp-sr), [web panel][panel] |
| ASR (speech → text) | FunASR/SenseVoice (the server defaults) do **not** target Vietnamese. The server's `OpenaiASR` provider is `type: openai` with a configurable `base_url`, so you can point it at a local OpenAI-compatible Whisper server | Local: faster-whisper / speaches server with Whisper large-v3(-turbo) or **PhoWhisper-large** (Vietnamese fine-tune; beats large-v3 on Vietnamese in published comparisons but weaker on code-switching) | [server config.yaml](https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/main/xiaozhi-server/config.yaml), [PhoWhisper paper](https://arxiv.org/abs/2406.02555), [ViMedCSS](https://arxiv.org/pdf/2602.12911) |
| LLM | Any OpenAI-compatible or Ollama endpoint | Ollama or vLLM on the 3090 running a strong multilingual model (Qwen-family 14B–32B or Gemma-family 27B class). **Test Vietnamese quality yourself before choosing.** | [xzs] |
| TTS (text → speech) | EdgeTTS (free, but a **cloud** Microsoft service) has `vi-VN-HoaiMyNeural` (female) and `vi-VN-NamMinhNeural` (male). The server's `CustomTTS` provider does a generic POST, so any local TTS HTTP API can be plugged in | Start with EdgeTTS HoaiMy for quality. Go fully local with **VieNeu-TTS** (GGUF/ONNX, 3–5 s voice cloning, Northern and Southern preset voices; 0.5B is Apache-2.0, 0.3B is CC BY-NC). Reviewers say it struggles with mixed English–Vietnamese text | [VieNeu on PyPI](https://pypi.org/project/vieneu/), [Edge voice list](https://huggingface.co/spaces/NeoPy/vtuber_rvc_models/blob/main/edgetts_db.py) |
| Extras in the server | Plugins for Home Assistant control, local music folder playback, RSS news (the URL is configurable, so a Vietnamese news RSS feed works), web search, RAGFlow knowledge base, **voiceprint speaker recognition** (tell her voice from yours), short-term memory | Set `timezone_offset: +7` (default is +8) and write the system prompt in Vietnamese | [server config.yaml](https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/main/xiaozhi-server/config.yaml) |

**Things it could do for her:** answer questions and chat in Vietnamese; read today's news and weather; play the family's own music folder; control lights and AC through Home Assistant; recipes and cooking Q&A; a family knowledge base (RAG over notes such as "wifi password for guests" or "kids' vaccination dates").

**Effort:** about 2–3 evenings for a working version (Docker server, Whisper, EdgeTTS, Ollama, flash or OTA-point the device). Add 1–2 days for a fully local voice and Home Assistant control.

**Risks:**
- Wake-word false or missed triggers in a noisy kitchen.
- A single mic: best at 3–10 cm ([qs]).
- The quiet 1 W speaker.
- Whisper occasionally hallucinates on silence; enable VAD (Silero is built in).
- The server is not security-audited: keep it on the LAN or tailnet.
- Buy the **"for XiaoZhi" SKU** for this (see the caveat in 1.2).

## 6. Follow-up: developer-oriented apps

Ranked by coolness × usefulness for an AI engineer with a 3090 box, Tailscale and Zabbix.

1. **Coding-agent desk companion.**
   - **What:** agent hooks (e.g. "needs approval", "task finished", "tests failed") POST to a small relay. The Watcher shows a status face, chimes, and reads a one-line summary.
   - **Voice:** "what is the agent doing?" via an MCP tool that tails the session log.
   - **Effort:** 2–3 days.
   - **Risk:** keep approvals on the computer, with read-only notifications on the device.
2. **GPU / training-run monitor.**
   - **What:** nvidia-smi, the queue and training metrics (loss, steps/s, ETA) on the round display. It speaks "run X finished, eval acc 0.87" or "loss is NaN".
   - **Voice:** "how's the fine-tune going?" via an MCP tool over your metrics source.
   - **Effort:** 2 days.
3. **Voice ops for home infra** (Idea #2 above).
   - **What:** Zabbix `problem.get` / acknowledge, Tailscale status, "is the GPU box up?". It reads critical alerts aloud.
   - **Effort:** 4–7 days, shared server with section 5.
4. **Presence-aware workstation.**
   - **What:** the on-device person model (no images leave the Himax chip) sends an HTTP event to a desktop agent that locks the screen when you leave, pauses music, sets chat status to away, and logs focus time.
   - **Effort:** 1–2 days.
   - **Risk:** false "absent" when you sit very still; add a grace period.
5. **CI/PR radar.**
   - **What:** GitHub webhooks feed red/green CI status and review requests to the display.
   - **Voice:** "summarise PR 42" (fetch the diff, local LLM summary).
   - **Effort:** 2–3 days.
6. **Whiteboard → markdown.**
   - **What:** say "capture the board". A snapshot goes to a VLM on the 3090, which turns it into markdown or a mermaid diagram committed to a notes repo.
   - **Effort:** 2 days.
   - **Risk:** fixed 3 m focus and unknown capture resolution may limit legibility. Test first.
7. **Edge-AI lab / content.**
   - **What:** train custom TinyML models (SenseCraft AI or Edge Impulse) and benchmark Himax on-device latency against the 3090 VLM.
   - **Bonus:** good material for a technical video series.
   - **Effort:** ongoing.
8. **Voice front-end to your own agents.**
   - **What:** expose your local agent or RAG stack as one MCP tool ("ask the research agent…"). It answers short questions and pushes the long answer to Telegram.
   - **Effort:** 1–2 days on top of the server.

**Shared foundation:** one `xiaozhi-esp32-server` instance on the 3090 (Whisper, local LLM, TTS) plus one FastAPI "watcher-gateway" for HTTP pushes. That covers both her assistant (section 5) and every app above.

**Suggested purchase:** 2 × "Watcher for XiaoZhi", one for the kitchen or living room and one for the desk. Add 1 × W1-A/B only if you also want the camera-alert project (#1).
