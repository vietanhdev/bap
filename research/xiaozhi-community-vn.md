# Xiaozhi community (VN focus), keyless VN data sources, realtime providers, wake words

Research date: 2026-11-16. All URLs in section 2 were curl-tested on that date from a dev host (not from the device, and probably not from a VN IP).
Legend: **[V]** verified by me (curl, or read the source code on GitHub). **[D]** taken from official docs. **[U]** unverified: third-party claim or inferred.
"On-device" means an ESP32-S3 can do it with HTTPS GET plus a small JSON/XML/HTML parse, with no relay server.

---

## 1. Vietnamese Xiaozhi community: apps, plugins, MCP tools

The Vietnamese scene is small. It consists mostly of firmware forks and flasher sites, plus a few music servers. I found no Vietnamese plugins for xiaozhi-esp32-server and no published Vietnamese MCP tool packs, apart from what is listed below.

| Project | What it does | Data source / API | Key? | On-device? |
|---|---|---|---|---|
| **KhanhDev54/xiaozhi-es3c28p-vn** [V] ([repo](https://github.com/KhanhDev54/xiaozhi-es3c28p-vn), active 2026-11) | The closest match to our feature set. It is a VN launcher UI on top of xiaozhi v2.5.3 and adds these device MCP tools: weather + AQI, VnExpress news by category, SJC gold, Vietcombank FX, **VOV radio**, Radio Browser search, alarms/timers, lunar calendar with can chi and giờ hoàng đạo, night mode. It also parses bank notifications (biến động số dư, VietQR planned). Wake word: `wn9_sophia_tts`. | Open-Meteo (forecast, air-quality, geocoding), ipwho.is, `vnexpress.net/rss/*.rss`, `sjc.com.vn/GoldPrice/Services/PriceService.ashx`, Vietcombank `pXML.aspx`, `stream.vovmedia.vn/vov-1` (AAC/ADTS), `api.radio-browser.info`. Lunar code follows Hồ Ngọc Đức's amlich. | No | **Yes**: everything runs on the ESP32 and uses `esp_audio_codec` for AAC/MP3. Worth reading `radio_player.*`, `info_services.cc`, `bank_parser.*`. |
| **ryannguyentran/xiaozhi-music-vn-fixed** ("xiaozhi-esp32-musicVN") [V] ([repo](https://github.com/ryannguyentran/xiaozhi-music-vn-fixed), 9★/16 forks) | xiaozhi music fork for Vietnamese songs, with lyrics display and a YouTube tutorial. | `http://www.xiaozhishop.xyz:5005/stream_pcm?song=&artist=&prefer_language=vietnamese`. A third-party server finds the song and transcodes it to PCM. Auth uses headers derived from MAC and chip ID with a hard-coded secret. | De-facto (shared secret) | **No**: depends on a third-party server you cannot inspect, plain HTTP. |
| **volume9/xiaozhi_zingmp3_music_server** [V] ([repo](https://github.com/volume9/xiaozhi_zingmp3_music_server)) | A Zing MP3 music server for Xiaozhi, repackaged as a Home Assistant add-on (the README says it is a rebuild of a container shared in the community). | Zing MP3 private API. `zingmp3.vn/api/v2/...` returns `-403` without signed params [V]. Community libraries do the signing: [zingmp3-api-full](https://github.com/phamhiep2506/zingmp3-api-full), [nhaccuatui-api-full](https://github.com/phongna07/nhaccuatui-api-full). | Signed requests (HMAC secret taken from the web JS) [U] | **Server needed** in practice. Signing plus search could be done on-device, but it is fragile and a ToS risk. Streams are MP3, which the ESP32 can decode. |
| **tranhung84de-art/xiaozhi-music-mcp** [V] ([repo](https://github.com/tranhung84de-art/xiaozhi-music-mcp)) | Python FastMCP `search_music` that calls a private PHP endpoint. Toy-level. | `hungtran.lovestoblog.com/xiaozhi-music/api/search.php` | No | Server (MCP over xiaozhi endpoint) |
| **nguyenduchoai/Server-Xiaozhi-PY-Vietnam** ("Xiaozhi CE") [V] ([repo](https://github.com/nguyenduchoai/Server-Xiaozhi-PY-Vietnam)) | Self-hosted VN xiaozhi server: agents, OTA, web flasher, RAG (pgvector), EMQX MQTT, "app market". Providers: OpenAI, Gemini, DeepSeek, Qwen, **Edge TTS (free, Vietnamese voices)**. | Edge TTS: `vi-VN-HoaiMyNeural` / `vi-VN-NamMinhNeural` [U on exact voice names in this repo] | BYOK | Server |
| **vdlaptrinh/py_xiaozhi_vietnamese**, **vdlaptrinh/Xiaozhi_AI_VN** [V] ([1](https://github.com/vdlaptrinh/py_xiaozhi_vietnamese), [2](https://github.com/vdlaptrinh/Xiaozhi_AI_VN)) | Raspberry Pi client in Vietnamese with wake word "alexa", plus a KiCad ESP32-S3 kit and prebuilt binaries. | xiaozhi.me | - | n/a |
| **ngtruongmetin/xiaozhi-mcp** [V] ([repo](https://github.com/ngtruongmetin/xiaozhi-mcp)) | "Chatbot Lịch Sử": Vietnamese history Q&A using semantic search over a CSV, connected to a xiaozhi MCP endpoint. Kids/education example. | Local SQLite + embeddings | No | Server |
| **hoangnamdz1998-max/xiaozhi-esp32** [V, README only] ([repo](https://github.com/hoangnamdz1998-max/xiaozhi-esp32)) | Described as a "Vietnamese customized firmware: music player, FM radio, quick settings, calendar, weather". Pushed 2026-11-16. The README is still upstream's. | ? | ? | ? |
| Flasher/vendor sites: [xiaozhi.vn](https://xiaozhi.vn/), [dienthongminhesmart.com/xiaozhi](https://dienthongminhesmart.com/xiaozhi/), [loaai.me guide](https://loaai.me/bai-viet/huong-dan-tu-lam-chatbot-ai-xiaozhi-tren-esp32-s3-phan-cung-esp-idf-va-nap-firmware), [sites.google.com/view/ailamsan](https://sites.google.com/view/ailamsan/n%E1%BA%A1p-firmware) | Web-flash VN firmware. xiaozhi.vn "FULL" advertises music, radio, podcast, "cải lương, truyện cổ tích, tin tức" and runs Facebook group plus Zalo communities [U: backends not disclosed]. | ? | - | - |
| **Upstream 78/xiaozhi-esp32** [V] | Has a `vi-VN` locale with prompt sounds and `language.json`. Release v2.6.0 (2026-11-13) now **requires ESP-IDF ≥ 6.0.1** and adds a `"realtime"` listen mode for end-to-end models through the xiaozhi server. Opus 16 kHz up / 24 kHz down, 60 ms frames. | xiaozhi.me | - | - |

### Deep dive: TienHuyIoT/xiaozhi-esp32_vietnam [V code]

[Repo](https://github.com/TienHuyIoT/xiaozhi-esp32_vietnam): 114★, 77 forks, MIT. This is the largest Vietnamese fork. The default branch `develop_vn` was last pushed on 2026-11-08, but it is ~509 commits behind upstream (it is still on IDF 5.x and xiaozhi v1/v2-era code). The community runs through the "Xiaozhi AI-IoT Vietnam" [Facebook group](https://www.facebook.com/groups/2655614131443031) and [fanpage](https://www.facebook.com/XiaozhiAI.IoTVietnam/). The Zalo groups are paused. It also provides a [web flasher](https://tienhuyiot.github.io/esp_web_flasher/). All of its code lives in `main/features/`.

**Worth reusing or copying:**
- **`music/audio_stream_player.cc`** (~1,070 lines). A generic HTTP stream player that handles MP3, AAC and WAV:
  - It uses `esp_audio_simple_dec` and puts two task stacks in PSRAM.
  - It uses a semaphore ring buffer, mixes stereo down to mono, and changes the codec output sample rate on the fly.
  - It is a ready-made base for radio, SD playback and sound effects.
- **`music/esp32_radio.cc`**. It has the most complete VOV list I found, 14 stations, all `https://stream.vovmedia.vn/…` AAC:
  - VOV1/2/3/5 and VOV Giao thông HN/HCM.
  - VOV Mekong and the six regional VOV4 channels (`vov4mt`, `vov4tb`, `vov4db`, `vov4tn`, `vov4dbscl`, `vov4hcm`).
  - VOV5 English 24/7 (`vov247`).
  - It picks the decoder from the URL (vovmedia means AAC).
- **Tool descriptions that cover speech-recognition errors.** The `self.radio.play_station` description says: *"VOV mộc/mốc/mốt/mậu/máu/một/mút/mót/mục means VOV1"*. This cheap trick is worth copying into our Vietnamese tool descriptions: list common mishearings of names such as Bông, Bắp, PNJ and "xổ số miền Bắc".
- **`spectrum/`**. A real-time FFT spectrum (`espressif/dl_fft`, `dl_rfft_f32`, 512-point, 40 bars, LVGL canvas, with docs in `spectrum.md`). It would look good as a ring visualizer on the round 412×412 screen while the character "dances" to radio.
- **SD-card music library** (`esp32_sd_music.cc`, tools `self.sdmusic.search/genre/suggest/playback/progress/...`, upstream [NTC95 Xiaozhi_NTC_SDCARD](https://github.com/NTC95-Xiaozhi-Esp32/Xiaozhi_NTC_SDCARD)). Offline kids' songs and audio fairy tales (truyện cổ tích) with **no server and no copyright API**. The Watcher has a microSD slot (SPI, FAT32, up to 32 GB according to the [Seeed hardware overview](https://wiki.seeedstudio.com/watcher_hardware_overview/)) [D]. This is the clean answer to "Vietnamese music" for a family device.
- **SD video** (`video/`, AVI with MJPEG and PCM via `espressif/avi_player`; `mp4/` via an extractor). Short cartoon clips are possible, but this is low priority.
- **On-device OTA web page** (`ota_server.cc`, `assets/web_browser/ota_index.html`). You upload a `.bin` to `http://<device-ip>/ota`. There is also an OTA server on Cloudflare Workers.
- **QR display** (`QRCode/qrcode_display.cc`). It is the building block for VietQR.
- The music server URL is stored in NVS (`Settings("wifi").music_url`), so it can be changed without rebuilding.

**Not worth copying:**
- **Music.** It uses the same opaque `http://www.xiaozhishop.xyz:5005/stream_pcm` server with the hard-coded `"your-esp32-secret-key-2024"` HMAC as the music fork above. That is a privacy and availability risk.
- **Weather** needs an OpenWeatherMap key. Our keyless Open-Meteo is better.
- **Gold, stocks, VietQR, alarms, iPhone ANCS notifications and scheduled proactive wake-up** are only "planned" or "in progress".
- The conversation path is the stock xiaozhi.me pipeline (Qwen/DeepSeek), so there is nothing to learn for Gemini Live.

**Home Assistant integrations** (Chinese/international community, all server-side MCP bridges):
- [c1pher-cn/ha-mcp-for-xiaozhi](https://github.com/c1pher-cn/ha-mcp-for-xiaozhi) (285★, HACS) [V]
- [mac8005/xiaozhi-mcp-ha](https://github.com/mac8005/xiaozhi-mcp-ha) (114★) [V]
- [huangjunsen0406/xiaozhi-mcphub](https://github.com/huangjunsen0406/xiaozhi-mcphub) (203★) [V]

All three connect to the **xiaozhi.me MCP endpoint**. Because we connect directly to Gemini, the equivalent for us is for the device to call the HA REST API (`/api/services/...`) or HA's built-in MCP server on the LAN with a long-lived token. That is a plain HTTP(S) call, so it runs on-device. [U: not tested]

**xiaozhi-esp32-server built-in plugins** [V] ([plugins_func/functions](https://github.com/xinnan-tech/xiaozhi-esp32-server/tree/main/main/xiaozhi-server/plugins_func/functions)): `get_weather`, `get_news_from_chinanews`, `get_news_from_newsnow`, `play_music`, `hass_*`, `web_search`, `search_from_ragflow`, `change_role`. None are Vietnamese. The project has a Vietnamese README only.

**Vietnamese TTS (relevant only if we ever add a cascaded pipeline):**
- Edge TTS vi-VN voices (free, unofficial).
- [pnnbao97/VieNeu-TTS](https://github.com/pnnbao97/VieNeu-TTS) (3.4k★, CPU real-time, voice cloning; a server is needed) [V repo].
- Piper `vi_VN` voices `25hours_single`, `vais1000`, `vivos` ([HF](https://huggingface.co/rhasspy/piper-voices/tree/main/vi/vi_VN)) [V].
- With Gemini Live we already get Vietnamese (`vi` is in the Live API language table [D]). Kid-friendly prebuilt voices: Leda (Youthful), Puck / Laomedeia (Upbeat), Zephyr / Autonoe (Bright), Achird (Friendly), Vindemiatrix (Gentle) [D].

---

## 2. Keyless VN data sources an ESP32 can call (curl-tested 2026-11-16)

| Topic | URL | Result | Size / format | ESP32 notes |
|---|---|---|---|---|
| **AQI Hanoi/HCMC** | `https://air-quality-api.open-meteo.com/v1/air-quality?latitude=21.03&longitude=105.85&current=us_aqi,pm2_5,pm10` | 200 [V] (Hanoi us_aqi 72) | ~350 B JSON | Easiest win. Model data (CAMS), not station readings. Station data from WAQI needs a free token; its `demo` token returns Shanghai [V]. |
| **Fuel prices (xăng dầu)** | `https://webgia.com/gia-xang-dau/petrolimex/` | 200 [V]. Table shows E10 RON95-V 21.840, E5 RON92 19.390, DO 0,001S 28.620 … (Vùng 1/2) | 76 KB HTML, table at ~51 KB offset | Stream-parse and stop after the `<table>`. Petrolimex.com.vn loads prices through RSA/crypto-js (not ESP-friendly) [V]. PVOIL sits behind a Cloudflare challenge (403) [V]. |
| **VN-Index** | `https://iboard-query.ssi.com.vn/exchange-index/VNINDEX?hasHistory=false` | 200 [V] (`indexValue` 1736.89, `change`, `changePercent`) | ~700 B JSON | Unofficial API, could break. Alternative: `https://bgapidatafeed.vps.com.vn/getlistindexdetail/10,11,02,03` (200, 1.2 KB, VN-Index/HNX/UPCoM) [V]. TCBS: Cloudflare 403. VNDirect: timed out [V]. |
| **V-League 1 fixtures/results/table** | `https://www.thesportsdb.com/api/v1/json/123/eventsnextleague.php?id=4803` (also `eventspastleague.php`, `lookuptable.php?l=4803&s=2026-2027`) | 200 [V] (e.g. "Thanh Hóa vs Hoàng Anh Gia Lai 2026-11-20") | 0.7–2.6 KB JSON | `123` is TheSportsDB's public test key (the old `3` is disabled) and returns only a few events per query. V.League 2 = 5214, Cup = 5200. ESPN has no `vie.1` (400) [V]. VnExpress `bong-da.rss` is empty; use `the-thao.rss` (66 KB) [V]. |
| **Holidays** | `https://date.nager.at/api/v3/PublicHolidays/2026/VN` | 200 [V] | 765 B JSON | Lists only 4 solar holidays (1/1, 30/4, 1/5, 2/9). Tết, Giỗ Tổ, Trung Thu, Rằm etc. must come from the on-device lunar calendar, which we already have. |
| **Traffic** | VOV Giao thông HLS: `https://play.vovgiaothong.vn/live/gthn/playlist.m3u8`, `.../gthcm/...` (from Radio Browser [V]) | HLS | AAC in TS segments | No keyless traffic JSON API found. HLS on ESP32 needs m3u8 plus TS demux, which is extra work. The `stream.vovmedia.vn/vovgt-hn` AAC stream used by KhanhDev54 is the simpler path. |
| **Radio (VOV1/2/3/5, VOV GT, Mekong)** | `https://stream.vovmedia.vn/vov-1` etc. | **TLS alert from my host** [V failure]. HTTP 308 → HTTPS. KhanhDev54 ships these, so they likely work from VN (geo/SNI) [U]. | AAC/ADTS stream | Test from the device in VN. Fallback: Radio Browser `https://de1.api.radio-browser.info/json/stations/search?countrycode=VN&limit=…` (200, ~1 KB per station) [V]. |
| **Wikipedia (kids "Bé hỏi")** | `https://vi.wikipedia.org/api/rest_v1/page/summary/H%C3%A0_N%E1%BB%99i` or `w/api.php?action=query&prop=extracts&exintro=1&explaintext=1&exchars=400&titles=…` | 200 [V] | 6 KB / 0.5 KB JSON | Use the `exchars` variant (tiny). "On this day" is **not** supported for `vi` (404) [V]. |
| Gold (other brands) | PNJ `https://edge-api.pnj.io/ecom-frontend/v1/get-gold-price?zone=00` | 200 [V] (2 KB, includes SJC bar) | JSON | Already used. SJC `PriceService.ashx` returns a Cloudflare 403 from my host [V]. BTMC public-key API works but is **421 KB** (too big) [V]. DOJI old XML returns 404 [V]. |
| FX fallback | `https://open.er-api.com/v6/latest/USD` | 200 [V] (1.7 KB) | JSON | Mid-market rates, daily. Vietcombank XML (2.5 KB, "one request every 5 minutes") stays primary [V]. |
| Lottery fallback | `https://xskt.com.vn/rss-feed/mien-bac-xsmb.rss` | 200 [V] (4.2 KB) | RSS | Backup for xosodaiphat (4 KB) [V]. |
| News alternatives | Tuổi Trẻ `tuoitre.vn/rss/tin-moi-nhat.rss` 41 KB, Dân Trí 127 KB, Google News vi 83 KB [V] | RSS | | VnExpress `tin-moi-nhat.rss` is 60 KB. Stream-parse only the first N `<item>`s. |
| Bank list (VietQR) | `https://api.vietqr.io/v2/banks` | 200 [V] (3.6 KB) | JSON | For a "show QR to receive money" screen. QR is rendered locally. |
| Horoscope / tử vi | `https://horoscope-app-api.vercel.app/api/v1/get-horoscope/daily?sign=Aries&day=TODAY` | 200 [V] | 0.4 KB, **English, Western zodiac** | No keyless Vietnamese tử vi API found. Better: compute can chi, ngày hoàng đạo/hắc đạo and giờ tốt on-device, and let Gemini phrase it playfully. |
| Proverbs / riddles / fairy tales | No good open dataset found. GitHub/HF hits are tiny student repos [V search]. | - | - | Ship a curated JSON (a few hundred ca dao, tục ngữ, câu đố, with answers) in LittleFS, or let Gemini generate them. The local file avoids hallucinated "proverbs". |

General ESP32 caveats:
- Several VN sites sit behind Cloudflare bot challenges (SJC, PVOIL, TCBS), so they are unusable without a server.
- Prefer sources under ~10 KB, or stream-parse with an early stop.
- Keep `User-Agent` set. Some endpoints 403 without it.

---

## 3. Multi-provider realtime support on ESP32

**How the reference projects do it**
- **ciniml/stackchan-idf** [V code] ([repo](https://github.com/ciniml/stackchan-idf), BSL-1.0, ESP-IDF 5.5): a `components/conversation/` with `gemini_live_client.cpp`, `openai_realtime_client.cpp` and `xiaozhi_client.cpp` behind one `conversation_service`. **The device connects directly.**
  - Tools such as set_expression and memory are declared per provider. For XiaoZhi they are exposed as device MCP.
  - The Gemini client defaults to `models/gemini-2.0-flash-live-001` and sets `thinkingBudget:0` on 2.5 native-audio models, because thinking caused spoken "tool_code" output instead of functionCalls.
  - It handles `goAway` plus `sessionResumption`.
  - The OpenAI client uses **G.711 µ-law 8 kHz** on the wire to save bandwidth and survive Wi-Fi/BLE coexistence, and `server_vad`.
- **espressif/esp-webrtc-solution `openai_demo`** (successor to [openai-realtime-embedded-sdk](https://github.com/openai/openai-realtime-embedded-sdk), which now just points there) [V]: OpenAI Realtime over **WebRTC** (Opus, `/v1/realtime/calls`) directly from ESP32.
- **ElatoAI** [V README] ([repo](https://github.com/akdeb/ElatoAI), 2.1k★): ESP32 (Arduino) → secure WebSocket → **Deno edge server** → OpenAI Realtime / Gemini Live / xAI Grok / ElevenLabs / Hume. Server-relayed, which keeps keys off the device and allows >20 min sessions.
- **xiaozhi (upstream/server)**: the device always speaks the xiaozhi protocol (Opus). End-to-end models such as Doubao and Qwen-Omni run behind the server (`listen mode: "realtime"`) [V docs].
- **rudyll/stackchan_ha_addons**: local server that impersonates the xiaozhi cloud and bridges to OpenAI/Gemini [U, search result only].

**Protocol differences that matter for an ESP32 client**

| | Gemini Live [D] | OpenAI Realtime [D/V code] | Qwen-Omni Realtime [D, partly U] | Doubao realtime dialogue [V code, U docs] |
|---|---|---|---|---|
| Endpoint | `wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=…` | `wss://api.openai.com/v1/realtime?model=…` (also WebRTC `/v1/realtime/calls`) | `wss://dashscope-intl.aliyuncs.com/api-ws/v1/realtime?model=…` | `wss://openspeech.bytedance.com/api/v3/realtime/dialogue` |
| Auth | API key in the query string. **Ephemeral token** via `POST v1beta/auth_tokens`, then `…BidiGenerateContentConstrained?access_token=`. Defaults: 1 min to start a session, 30 min `expireTime`, `uses:1`, and reconnects allowed with the same token. | `Authorization: Bearer` (no `OpenAI-Beta` header in GA). Ephemeral via `POST /v1/realtime/client_secrets`. | Bearer DashScope key | Headers `X-Api-App-ID`, `X-Api-Access-Key`, `X-Api-Resource-Id: volc.speech.dialog`, `X-Api-App-Key`. **Custom binary framing**, not JSON. |
| Audio | In: PCM16 LE **16 kHz**. Out: PCM16 **24 kHz**. Base64 inside JSON. | `audio/pcm` 24 kHz, or `audio/pcmu`/`pcma` 8 kHz. Base64 JSON. | In: PCM16 16 kHz. Out: PCM 24 kHz. | In: PCM 16 kHz. Out: pcm_s16le (24 kHz typical) or Opus/OGG [U]. |
| VAD | Server-side automatic VAD, configurable via `realtimeInputConfig`, or manual activityStart/End. | `server_vad` / `semantic_vad` / none | `server_vad` / `semantic_vad` / manual | Server-side |
| Tool calls | `toolCall.functionCalls[]` → reply `toolResponse.functionResponses[]`. On **gemini-3.8-live, `NON_BLOCKING` (async) is the default**; responses can carry `scheduling: INTERRUPT / WHEN_IDLE / SILENT`. Set `behavior: BLOCKING` to keep old semantics. | `response.function_call_arguments.done` → `conversation.item.create` `{type:function_call_output}` + `response.create` | OpenAI-compatible event shapes; function calling + remote MCP supported | Limited (mainly Chinese assistant use) [U] |
| Session limits | Audio-only **15 min** without compression, audio+video 2 min. Connection ~10 min. Use `contextWindowCompression.slidingWindow` + `sessionResumption` + handle `goAway`. | Not stated on the pages I read [U: 60 min commonly cited] | ~30 min per WebSocket session | Drops after 2 min idle without ping/audio [U, Volcengine gateway doc] |
| Vietnamese | Yes (`vi` listed) | Yes (multilingual) [U quality] | ASR 113 / TTS 36 languages, `vi` not confirmed [U] | Mainly zh/en [U]. Not a fit. |

**Current models and prices (per 1M tokens, USD)**
- **Gemini** ([pricing](https://aistudio.google.com/docs/pricing)) [D]:
  - `gemini-3.8-live` is **stable**, latest update Sept 2026, and is the recommended default. Pricing: audio in **$3.00 (≈$0.005/min)**, audio out **$12.00 (≈$0.018/min)**, text in $0.75 / out $4.50. **Free tier available.**
  - Same price: `gemini-3.8-live-extended-thinking` and `gemini-3.1-flash-live-preview`.
  - Older `gemini-2.5-flash-native-audio-preview-12-2025`: $3 audio in / $12 audio out.
  - Google Search grounding: free quota, then $14 per 1,000 requests.
  - Migration notes when moving to 3.8:
    - Remove `thinking_config`.
    - Remove `enable_affective_dialog`.
    - Do not send `proactive_audio:false`, which now errors.
    - Expect async tool calls by default.
- **OpenAI** ([pricing](https://developers.openai.com/api/docs/pricing)) [D]: `gpt-realtime-2.1` and `gpt-realtime-2` cost $32 audio in / $64 audio out / $4 text in / $24 text out. `gpt-realtime-2.1-mini` and `gpt-realtime-mini` cost $10 / $20 / $0.60 / $2.40. Docs examples use `gpt-realtime-2.1`. A new "GPT-Live" WebSocket also exists (`wss://api.openai.com/v1/live/sessions`, `session.start`, supports 16 kHz PCM) [D].
- **Qwen**: docs examples use `qwen3.8-omni-flash-realtime` (also `qwen3.5-omni-plus-realtime`). Price is not on the doc page. A third party quotes ~$0.52 in / $1.99 out per 1M for the older `qwen3-omni-flash-realtime` **[U]**.
- **Doubao**: no public international price found **[U]**.

Takeaway for us: stay Gemini-direct and upgrade to `gemini-3.8-live`. Adding OpenAI is ~1 extra client file (stackchan-idf is a good template), but it costs about 5–10× more per audio minute. Qwen is cheap but Vietnamese is unconfirmed. Doubao is not worth it.

For a real product, have a tiny token service mint Gemini ephemeral tokens rather than storing the API key in NVS. With direct-to-Gemini this is optional, but it is the one server piece worth having.

---

## 4. Friendlier wake words

**Prebuilt ESP-SR WakeNet** [V, list from [esp-sr/model/wakenet_model](https://github.com/espressif/esp-sr/tree/master/model/wakenet_model)]
- English or neutral options: `wn9_sophia_tts` (used by the VN fork above), `wn9_hijolly_tts2`, `wn9_hifairy_tts2`, `wn9_hijoy_tts`, `wn9_hilili_tts`, `wn9_hiandy_tts2`, `wn9_hiwalle_tts2`, `wn9_heyily/heyivy`, `wn9_hijason_tts2`, `wn9_jarvis_tts`, `wn9_computer_tts`, `wn9_histackchan_tts3`, `wn9_alexa` / `wn10_alexa`, `wn10_heybuddy`, `wn10_heynova`, `wn10_himuse`, `wn10_sophia`.
- Several Chinese options, e.g. `wn9_himiaomiao_tts`, `wn9_xiaoluxiaolu_tts2`.
- **No Vietnamese model exists.**
- Easy for Vietnamese kids to say: "Hi Lili", "Hi Joy", "Hi Fairy", "Sophia", "Hey Buddy". Two open syllables and no hard English clusters.

**MultiNet custom phrase** [V Kconfig] (`CONFIG_USE_CUSTOM_WAKE_WORD`, ESP32-S3 + PSRAM)
- Works with **any phrase written as pinyin (mn7_cn) or English phonemes (mn7_en)**, with no training. xiaozhi's [assets generator](https://github.com/78/xiaozhi-assets-generator) can set it online.
- Vietnamese names can be approximated in pinyin, e.g. Mochi → `mo qi`, Bắp → `ba bo` [U: accuracy for Vietnamese speakers untested].
- Trade-offs:
  - Higher false-accept and miss rates than WakeNet.
  - More RAM/CPU.
  - Threshold tuning needed (default 20%).

**Espressif custom WakeNet training** [D] ([docs](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html))
- Three options:
  - TTS-only.
  - Crowdsourced (~500 speakers) + TTS.
  - Your own corpus: ≥20,000 utterances, ≥500 speakers incl. ≥100 children, 16 kHz.
- One-time fee negotiated with sales@espressif.com. 2–3 weeks.
- A Vietnamese "Bông ơi" would most likely need the crowdsourced or own-corpus route. Highest quality, but it costs money and time.

**microWakeWord / openWakeWord** [V repos]
- microWakeWord (TFLite Micro, ~60 KB models) runs on ESP32-S3 in ESPHome.
- Community trainers: [TaterTotterson/microWakeWord-Trainer-Nvidia-Docker](https://github.com/TaterTotterson/microWakeWord-Trainer-Nvidia-Docker), [example "Hey Donut"](https://github.com/nyc-esq/hey-donut).
- Training uses synthetic TTS data (Piper sample generator, English by default).
- A Vietnamese phrase could use the Piper `vi_VN` voices (`vivos` is multi-speaker) plus real family recordings [U: no one has published a Vietnamese mWW model].
- Porting into xiaozhi/ESP-IDF needs esp-tflite-micro plus the microfrontend feature extractor, and it competes with the AFE for CPU.
- openWakeWord is too heavy for ESP32. It is server/RPi only.

**Recommendation**
- Short term: switch to an easy prebuilt word ("Hi Lili"/"Hi Joy"/"Sophia") or a MultiNet pinyin approximation of a character name. Keep the scroll wheel / tap as the main trigger for kids.
- Long term: train a microWakeWord "Bông ơi" with Piper vi + ~50 family samples.

---

## Top 10 to integrate next

| # | Item | Value (VN family + dev) | Effort | On-device |
|---|---|---|---|---|
| 1 | **Upgrade to `gemini-3.8-live`.** Enable `googleSearch` grounding (covers football, traffic news, "what happened today"), `contextWindowCompression` + `sessionResumption`, and handle the async NON_BLOCKING tool default. | Very high: fewer bespoke tools, longer chats | S | Yes (config) |
| 2 | **Radio: 14 VOV stations (incl. VOV Giao thông HN/HCM and VOV4 regional), Radio Browser VN.** Reuse TienHuyIoT's `audio_stream_player` + `esp32_radio` or KhanhDev54's `radio_player`, and add an FFT ring visualizer. Doubles as the traffic feature. SD-card kids' songs and fairy tales can share the same player. | High (grandparents, commute, kids) | M (AAC decode, stop on wake) | Yes (verify the vovmedia TLS from VN) |
| 3 | **Friendlier wake word:** prebuilt `wn9_hilili_tts`/`wn9_hijoy_tts`/`wn9_sophia_tts`, or a MultiNet pinyin character name. | High for kids | S | Yes |
| 4 | **Air quality** (Open-Meteo AQI, with a colored ring on the round display). | High in Hanoi | S | Yes |
| 5 | **Lunar festival reminders:** Tết, Rằm, mùng 1, Giỗ Tổ, Trung Thu, family giỗ dates, plus Nager solar holidays. | High (cultural) | S (lunar code already exists) | Yes |
| 6 | **Kids pack:** "Bé hỏi" vi.wikipedia summaries, plus an offline JSON of câu đố / ca dao / tục ngữ for a riddle quiz with scroll-wheel answers. | High for kids, educational | M (content curation) | Yes |
| 7 | **Fuel prices** (webgia Petrolimex table, stream-parse). | Medium | S–M | Yes (HTML scrape, fragile) |
| 8 | **V-League schedule/results/table** (TheSportsDB test key 123). | Medium (dads) | S | Yes (test key, limited) |
| 9 | **VN-Index / HNX** (SSI or VPS JSON). | Medium for adults | S | Yes (unofficial API) |
| 10 | **Home Assistant on LAN** (REST/MCP with a long-lived token; lights, AC, cửa cuốn). | High if the family has HA | M | Yes (LAN HTTP) |

Not now:
- **Vietnamese music (Zing MP3/NhacCuaTui)**: needs a self-hosted server (signed API, ToS risk). Existing community servers are opaque third parties.
- **Bank-notification/VietQR**: more of a shop-owner feature.
- **OpenAI/Qwen providers**: cost or unconfirmed Vietnamese, with no family-visible gain over Gemini.
