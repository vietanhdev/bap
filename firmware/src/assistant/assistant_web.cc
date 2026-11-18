// Settings UI served by the device itself on port 80 (LAN only, no cloud).
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <mdns.h>
#include <cstring>

#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/semphr.h>
#include <atomic>
#include <memory>
#include "application.h"
#include "assets.h"
#include "assistant.h"
#include "assistant_auth.h"
#include "assistant_config.h"
#include "assistant_face.h"
#include "assistant_menu.h"
#include "audio/audio_codec.h"
#include "board.h"
#include "boards/common/backlight.h"
#include "boards/common/wifi_board.h"
#include "display/display.h"
#include "mcp_server.h"
#include "protocols/gemini_live_protocol.h"
#include "settings.h"
#include "ssid_manager.h"
#include "web_page.h"
#include "wifi_manager.h"

#define TAG "AssistantWeb"

namespace assistant {
namespace {

esp_err_t SendJson(httpd_req_t* req, cJSON* root) {
    char* txt = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, txt);
    cJSON_free(txt);
    cJSON_Delete(root);
    return err;
}

cJSON* ReadJson(httpd_req_t* req) {
    if (req->content_len == 0 || req->content_len > 8192)
        return nullptr;
    std::string body(req->content_len, '\0');
    size_t got = 0;
    while (got < body.size()) {
        int n = httpd_req_recv(req, body.data() + got, body.size() - got);
        if (n <= 0)
            return nullptr;
        got += n;
    }
    return cJSON_Parse(body.c_str());
}

// Browsers send a custom header only for same-origin fetches; this blocks cross-site form posts.
bool SameOrigin(httpd_req_t* req) {
    char v[8];
    return httpd_req_get_hdr_value_str(req, "X-Bap", v, sizeof(v)) == ESP_OK;
}

esp_err_t Reject(httpd_req_t* req, const char* msg) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, msg);
}

std::string MaskKey(const std::string& key) {
    if (key.size() < 10)
        return key.empty() ? "" : "••••";
    return key.substr(0, 4) + "••••••••" + key.substr(key.size() - 4);
}

esp_err_t HandleIndex(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, kWebPage, sizeof(kWebPage) - 1);
}

esp_err_t HandleGetConfig(httpd_req_t* req) {
    auto c = LoadConfig();
    auto& board = Board::GetInstance();
    cJSON* r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "api_key_masked", MaskKey(c.api_key).c_str());
    cJSON_AddBoolToObject(r, "has_password", HasWebPassword());
    cJSON_AddStringToObject(r, "api_key_paid_masked", MaskKey(c.api_key_paid).c_str());
    if (auto gl = GeminiLiveProtocol::instance()) {
        auto ks = gl->GetKeyStats();
        cJSON* k = cJSON_AddObjectToObject(r, "keys");
        cJSON_AddBoolToObject(k, "last_paid", ks.last_paid);
        cJSON_AddNumberToObject(k, "free_sessions", ks.free_sessions);
        cJSON_AddNumberToObject(k, "paid_sessions", ks.paid_sessions);
        cJSON_AddNumberToObject(k, "free_rest_s", ks.free_rest_s);
    }
    cJSON_AddStringToObject(r, "model", c.model.c_str());
    cJSON_AddStringToObject(r, "character", c.character.c_str());
    cJSON* chars = cJSON_AddArrayToObject(r, "characters");
    for (auto& ch : Characters()) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", ch.id.c_str());
        cJSON_AddStringToObject(o, "name", ch.name.c_str());
        cJSON_AddStringToObject(o, "kind", ch.kind.c_str());
        cJSON_AddStringToObject(o, "tagline", ch.tagline.c_str());
        cJSON_AddStringToObject(o, "voice", ch.voice.c_str());
        cJSON_AddStringToObject(o, "persona", ch.persona.c_str());
        cJSON_AddStringToObject(o, "default_name", ch.default_name.c_str());
        cJSON_AddStringToObject(o, "default_voice", ch.default_voice.c_str());
        cJSON_AddStringToObject(o, "default_persona", ch.default_persona.c_str());
        cJSON_AddItemToArray(chars, o);
    }
    cJSON* apps = cJSON_AddArrayToObject(r, "apps");
    for (auto& app : Apps()) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", app.id.c_str());
        cJSON_AddStringToObject(o, "title", app.title.c_str());
        cJSON_AddStringToObject(o, "desc", app.desc.c_str());
        cJSON_AddItemToArray(apps, o);
    }
    cJSON* stations = cJSON_AddArrayToObject(r, "stations");
    for (auto& st : Stations()) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", st.id.c_str());
        cJSON_AddStringToObject(o, "name", st.name.c_str());
        cJSON_AddItemToArray(stations, o);
    }
    cJSON_AddStringToObject(r, "city", c.city.c_str());
    cJSON_AddNumberToObject(r, "lat", c.lat);
    cJSON_AddNumberToObject(r, "lon", c.lon);
    cJSON_AddNumberToObject(r, "idle_s", c.idle_s);
    cJSON_AddBoolToObject(r, "search", c.search);
    cJSON_AddBoolToObject(r, "greet", c.greet);
    cJSON_AddBoolToObject(r, "night_dim", c.night_dim);
    cJSON_AddBoolToObject(r, "presence", c.presence);
    cJSON_AddBoolToObject(r, "morning", c.morning);
    cJSON_AddBoolToObject(r, "quiet", c.quiet);
    cJSON_AddBoolToObject(r, "rest", c.rest);
    cJSON_AddBoolToObject(r, "wake_word", c.wake_word);
    cJSON_AddStringToObject(r, "language", c.language.c_str());
    cJSON* langs = cJSON_AddArrayToObject(r, "languages");
    for (auto& l : Languages()) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", l.id.c_str());
        cJSON_AddStringToObject(o, "name", l.name.c_str());
        cJSON_AddItemToArray(langs, o);
    }
    cJSON_AddItemToObject(r, "hooks", cJSON_Parse(LoadList("hooks").c_str()));
    cJSON_AddNumberToObject(r, "volume", board.GetAudioCodec()->output_volume());
    if (auto bl = board.GetBacklight())
        cJSON_AddNumberToObject(r, "brightness", bl->brightness());
    cJSON_AddItemToObject(r, "notes", cJSON_Parse(LoadList("notes").c_str()));
    cJSON_AddItemToObject(r, "memory", cJSON_Parse(LoadList("memory").c_str()));
    return SendJson(req, r);
}

esp_err_t HandlePostConfig(httpd_req_t* req) {
    if (!SameOrigin(req))
        return Reject(req, "missing header");
    cJSON* in = ReadJson(req);
    if (in == nullptr)
        return Reject(req, "bad json");
    auto c = LoadConfig();
    auto str = [in](const char* k, std::string& dst, size_t max) {
        auto v = cJSON_GetObjectItem(in, k);
        if (cJSON_IsString(v) && strlen(v->valuestring) <= max)
            dst = v->valuestring;
    };
    std::string new_key;
    str("api_key", new_key, 100);
    if (!new_key.empty())
        c.api_key = new_key;  // empty = keep the current key
    if (auto pw = cJSON_GetObjectItem(in, "web_password");
        cJSON_IsString(pw) && strlen(pw->valuestring) <= 64) {
        SetWebPassword(pw->valuestring);  // "" removes it
    }
    std::string new_paid;
    str("api_key_paid", new_paid, 100);
    if (new_paid == "-")
        c.api_key_paid.clear();  // "-" = remove the paid key
    else if (!new_paid.empty())
        c.api_key_paid = new_paid;
    str("model", c.model, 80);
    std::string want_character;
    str("character", want_character, 20);
    for (auto& ch : Characters()) {
        if (ch.id == want_character)
            c.character = ch.id;
    }
    // Per-character overrides: [{"id","name","voice","persona"}]; a value equal to the built-in
    // one (or empty) clears the override.
    cJSON* item = nullptr;
    cJSON_ArrayForEach (item, cJSON_GetObjectItem(in, "characters")) {
        auto get = [item](const char* k) {
            auto v = cJSON_GetObjectItem(item, k);
            return std::string(cJSON_IsString(v) ? v->valuestring : "");
        };
        for (auto& ch : Characters()) {
            if (ch.id != get("id"))
                continue;
            std::string name = get("name"), voice = get("voice"), persona = get("persona");
            if (name.size() > 40 || voice.size() > 40 || persona.size() > 3000)
                continue;
            SaveCharacterOverride(ch.id, name == ch.default_name ? "" : name,
                                  voice == ch.default_voice ? "" : voice,
                                  persona == ch.default_persona ? "" : persona);
        }
    }
    str("city", c.city, 60);
    auto num = [in](const char* k) { return cJSON_GetObjectItem(in, k); };
    if (cJSON_IsNumber(num("lat")))
        c.lat = num("lat")->valuedouble;
    if (cJSON_IsNumber(num("lon")))
        c.lon = num("lon")->valuedouble;
    if (cJSON_IsNumber(num("idle_s")))
        c.idle_s = std::max(10, std::min(300, num("idle_s")->valueint));
    if (cJSON_IsBool(num("search")))
        c.search = cJSON_IsTrue(num("search"));
    if (cJSON_IsBool(num("greet")))
        c.greet = cJSON_IsTrue(num("greet"));
    if (cJSON_IsBool(num("night_dim")))
        c.night_dim = cJSON_IsTrue(num("night_dim"));
    if (cJSON_IsBool(num("presence")))
        c.presence = cJSON_IsTrue(num("presence"));
    if (cJSON_IsBool(num("morning")))
        c.morning = cJSON_IsTrue(num("morning"));
    if (cJSON_IsBool(num("quiet")))
        c.quiet = cJSON_IsTrue(num("quiet"));
    if (cJSON_IsBool(num("rest")))
        c.rest = cJSON_IsTrue(num("rest"));
    if (cJSON_IsBool(num("wake_word")))
        c.wake_word = cJSON_IsTrue(num("wake_word"));
    if (auto l = cJSON_GetObjectItem(in, "language"); cJSON_IsString(l)) {
        for (auto& lang : Languages())
            if (lang.id == l->valuestring)
                c.language = lang.id;
    }
    if (cJSON_IsArray(num("hooks")) && cJSON_GetArraySize(num("hooks")) <= 8) {
        char* t = cJSON_PrintUnformatted(num("hooks"));
        if (strlen(t) < 3000)
            SaveList("hooks", t);
        cJSON_free(t);
    }
    SaveConfig(c);

    auto& board = Board::GetInstance();
    if (cJSON_IsNumber(num("volume"))) {
        board.GetAudioCodec()->SetOutputVolume(std::max(0, std::min(100, num("volume")->valueint)));
    }
    if (cJSON_IsNumber(num("brightness"))) {
        if (auto bl = board.GetBacklight())
            bl->SetBrightness(std::max(5, std::min(100, num("brightness")->valueint)), true);
    }
    if (cJSON_IsArray(num("notes"))) {
        char* t = cJSON_PrintUnformatted(num("notes"));
        SaveList("notes", t);
        cJSON_free(t);
    }
    if (cJSON_IsArray(num("memory"))) {
        char* t = cJSON_PrintUnformatted(num("memory"));
        SaveList("memory", t);
        cJSON_free(t);
    }
    cJSON_Delete(in);
    cJSON* r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return SendJson(req, r);
}

esp_err_t HandleStatus(httpd_req_t* req) {
    auto& board = Board::GetInstance();
    static const char* states[] = {"unknown",    "starting",   "wifi_configuring", "idle",
                                   "connecting", "listening",  "speaking",         "notifying",
                                   "upgrading",  "activating", "audio_testing",    "fatal_error"};
    int st = Application::GetInstance().GetDeviceState();
    cJSON* r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "state", st >= 0 && st < 12 ? states[st] : "unknown");
    cJSON_AddStringToObject(r, "now_playing", Application::GetInstance().NowPlaying().c_str());
    cJSON_AddNumberToObject(r, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    {
        uint8_t mac[6] = {};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char host[24];
        snprintf(host, sizeof(host), "bap-%02x%02x.local", mac[4], mac[5]);
        cJSON_AddStringToObject(r, "host", host);
    }
    cJSON_AddNumberToObject(r, "free_heap", (double)esp_get_free_heap_size());
    cJSON_AddItemToObject(r, "device", cJSON_Parse(board.GetDeviceStatusJson().c_str()));
    cJSON_AddItemToObject(r, "log", cJSON_Parse(LogJson().c_str()));
    cJSON_AddStringToObject(r, "alarms", AlarmsText().c_str());
    cJSON_AddStringToObject(r, "version", esp_app_get_description()->version);
    return SendJson(req, r);
}

esp_err_t HandleSay(httpd_req_t* req) {
    if (!SameOrigin(req))
        return Reject(req, "missing header");
    cJSON* in = ReadJson(req);
    auto text = cJSON_GetObjectItem(in, "text");
    if (!cJSON_IsString(text) || text->valuestring[0] == '\0') {
        cJSON_Delete(in);
        return Reject(req, "missing text");
    }
    Say(text->valuestring);
    cJSON_Delete(in);
    cJSON* r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return SendJson(req, r);
}

esp_err_t HandleAction(httpd_req_t* req) {
    if (!SameOrigin(req))
        return Reject(req, "missing header");
    cJSON* in = ReadJson(req);
    auto name = cJSON_GetObjectItem(in, "action");
    std::string action = cJSON_IsString(name) ? name->valuestring : "";
    cJSON_Delete(in);
    auto& app = Application::GetInstance();
    if (action == "talk") {
        app.Schedule([&app]() { app.ToggleChatState(); });
    } else if (action == "stop") {
        app.StopAll();
    } else if (action == "wheel_hold") {  // remote wheel, same as the physical one
        MenuIsOpen() ? MenuBack() : MenuOpen();
    } else if (action == "wheel_left" || action == "wheel_right") {
        bool clockwise = action == "wheel_right";
        if (MenuIsOpen()) {
            MenuRotate(clockwise);
        } else if (!OnWheelRotate(clockwise)) {  // resting or talking: volume, like the real wheel
            auto codec = Board::GetInstance().GetAudioCodec();
            int volume = std::max(0, std::min(100, codec->output_volume() + (clockwise ? -5 : 5)));
            codec->SetOutputVolume(volume);
            Board::GetInstance().GetDisplay()->ShowNotification(Localize("Âm lượng") + ": " +
                                                                std::to_string(volume));
        }
    } else if (action == "wheel_click") {
        if (MenuIsOpen())
            MenuClick();
        else if (!OnWheelClick())
            app.Schedule([&app]() { app.ToggleChatState(); });
    } else if (action == "wifi_setup") {
        app.Schedule([]() { static_cast<WifiBoard&>(Board::GetInstance()).EnterWifiConfigMode(); });
    } else if (action == "reboot") {
        app.Schedule([&app]() { app.Reboot(); });
    } else {
        return Reject(req, "unknown action");
    }
    cJSON* r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return SendJson(req, r);
}

// Call a device tool directly (same tools the assistant uses). Handy for testing and automation.
esp_err_t HandleTool(httpd_req_t* req) {
    if (!SameOrigin(req))
        return Reject(req, "missing header");
    cJSON* in = ReadJson(req);
    auto name = cJSON_GetObjectItem(in, "name");
    if (!cJSON_IsString(name)) {
        cJSON_Delete(in);
        return Reject(req, "missing name");
    }
    cJSON* rpc = cJSON_CreateObject();
    cJSON_AddStringToObject(rpc, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(rpc, "id", 1);
    cJSON_AddStringToObject(rpc, "method", "tools/call");
    cJSON* params = cJSON_AddObjectToObject(rpc, "params");
    cJSON_AddStringToObject(params, "name", name->valuestring);
    auto args = cJSON_GetObjectItem(in, "arguments");
    cJSON_AddItemToObject(
        params, "arguments",
        cJSON_IsObject(args) ? cJSON_Duplicate(args, true) : cJSON_CreateObject());
    char* txt = cJSON_PrintUnformatted(rpc);
    std::string request(txt);
    cJSON_free(txt);
    cJSON_Delete(rpc);
    cJSON_Delete(in);

    struct Pending {
        SemaphoreHandle_t done = xSemaphoreCreateBinary();
        std::string reply;
        ~Pending() { vSemaphoreDelete(done); }
    };
    auto pending = std::make_shared<Pending>();
    McpServer::GetInstance().ParseMessage(request, [pending](const std::string& payload) {
        pending->reply = payload;
        xSemaphoreGive(pending->done);
    });
    httpd_resp_set_type(req, "application/json");
    if (xSemaphoreTake(pending->done, pdMS_TO_TICKS(20000)) != pdTRUE) {
        return httpd_resp_sendstr(req, "{\"error\":\"timeout\"}");
    }
    return httpd_resp_sendstr(req, pending->reply.c_str());
}

// Firmware update: the browser uploads xiaozhi.bin as the raw request body.
esp_err_t HandleOta(httpd_req_t* req) {
    if (!SameOrigin(req))
        return Reject(req, "missing header");
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (part == nullptr || req->content_len < 100000 || req->content_len > part->size) {
        return Reject(req, "File không hợp lệ");
    }
    esp_ota_handle_t handle = 0;
    // Erase exactly the image size up front (in yielding chunks) instead of sector-by-sector
    // between network reads: roughly halves the upload time.
    if (esp_ota_begin(part, req->content_len, &handle) != ESP_OK)
        return Reject(req, "Không bắt đầu được");
    ESP_LOGI(TAG, "OTA upload: %u bytes", (unsigned)req->content_len);
    Board::GetInstance().SetPowerSaveLevel(
        PowerSaveLevel::PERFORMANCE);  // modem sleep drops big uploads
    // Flash writes stall the other core; the wake-word engine then backs up and starves Wi-Fi.
    // Pause it for the upload (the device reboots afterwards anyway).
    auto& audio = Application::GetInstance().GetAudioService();
    audio.EnableWakeWordDetection(false);
    ApplyVision(false);  // camera inference floods SPI while flash writes stall the CPU
    SetOtaInProgress(true);
    Board::GetInstance().GetDisplay()->SetChatMessage(
        "system", Localize("Đang cập nhật phần mềm...").c_str());
    std::string buf(16384, '\0');  // PSRAM; fewer, larger flash writes
    size_t remaining = req->content_len;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf.data(), std::min(buf.size(), remaining));
        if (n == HTTPD_SOCK_ERR_TIMEOUT)
            continue;
        if (n <= 0 || esp_ota_write(handle, buf.data(), n) != ESP_OK) {
            esp_ota_abort(handle);
            Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
            audio.EnableWakeWordDetection(true);
            SetOtaInProgress(false);
            {
                auto cfg = LoadConfig();
                ApplyVision(cfg.greet || cfg.presence);
            }
            return Reject(req, "Tải lên bị gián đoạn");
        }
        remaining -= n;
        vTaskDelay(1);  // let the idle task feed the watchdog during the long upload
    }
    if (esp_ota_end(handle) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        SetOtaInProgress(false);
        audio.EnableWakeWordDetection(true);
        auto cfg = LoadConfig();
        ApplyVision(cfg.greet || cfg.presence);
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        return Reject(req, "Firmware không hợp lệ hoặc không có chữ ký đúng");
    }
    httpd_resp_sendstr(req, "{\"ok\":true}");
    auto& app = Application::GetInstance();
    app.Schedule([&app]() {
        vTaskDelay(pdMS_TO_TICKS(800));
        app.Reboot();
    });
    return ESP_OK;
}

// Animated face of a character, straight from the assets partition: /face?c=bap&e=happy
esp_err_t HandleFace(httpd_req_t* req) {
    char query[64] = {}, id[20] = {}, emotion[24] = "happy";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "c", id, sizeof(id));
    httpd_query_key_value(query, "e", emotion, sizeof(emotion));
    std::string name = std::string(id) + "_" + emotion + ".gif";
    void* ptr = nullptr;
    size_t size = 0;
    if (!Assets::GetInstance().GetAssetData(name, ptr, size)) {
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_sendstr(req, "not found");
    }
    httpd_resp_set_type(req, "image/gif");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, static_cast<const char*>(ptr), size);
}

// Debug: preview a face expression without talking. {"emotion":"happy"} and/or {"talking":true}.
// The answer carries the LVGL heap usage measured after the previous request.
esp_err_t HandleFacePreview(httpd_req_t* req) {
    static volatile uint32_t lv_used = 0, lv_free = 0;
    if (!SameOrigin(req))
        return Reject(req, "missing header");
    cJSON* in = ReadJson(req);
    if (in == nullptr)
        return Reject(req, "bad json");
    auto em = cJSON_GetObjectItem(in, "emotion");
    auto talk = cJSON_GetObjectItem(in, "talking");
    std::string emotion = cJSON_IsString(em) && strlen(em->valuestring) < 24 ? em->valuestring : "";
    int talking = cJSON_IsBool(talk) ? (cJSON_IsTrue(talk) ? 1 : 0) : -1;
    cJSON_Delete(in);
    Application::GetInstance().Schedule([emotion, talking]() {
        auto display = Board::GetInstance().GetDisplay();
        if (!emotion.empty())
            display->SetEmotion(emotion.c_str());
        DisplayLockGuard lock(display);
        if (talking >= 0)
            FaceSetTalking(talking == 1);
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        lv_used = mon.total_size - mon.free_size;
        lv_free = mon.free_size;
    });
    cJSON* r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddNumberToObject(r, "lv_used", lv_used);
    cJSON_AddNumberToObject(r, "lv_free", lv_free);
    return SendJson(req, r);
}

// Live screenshot of the round display (JPEG), used by the web UI preview and for debugging.
// Rendering the screen into a JPEG runs the whole LVGL draw pipeline, which needs far more stack
// than the HTTP server task has (6 KB). It runs in a short-lived task with a 16 KB stack in PSRAM,
// one snapshot at a time; the shared state outlives a request that gave up waiting.
struct ScreenJob {
    std::string jpeg;
    bool ok = false;
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    ~ScreenJob() { vSemaphoreDelete(done); }
};
std::atomic<bool> g_screen_busy{false};

esp_err_t HandleScreen(httpd_req_t* req) {
    auto display = Board::GetInstance().GetDisplay();
    // The JPEG encoder takes ~13 KB of internal RAM for its working buffers (measured: one
    // screenshot moved the internal-RAM low-water mark from 33 KB to 20 KB). Internal RAM is what
    // TLS and audio need, so only take a screenshot when there is room for it.
    // 48 KB keeps the low-water mark above ~35 KB after the encoder's 13 KB; a busy device (a
    // session, boot) is below that and simply answers "try again".
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 48 * 1024 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 20 * 1024) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_hdr(req, "Retry-After", "2");
        return httpd_resp_sendstr(req, "low memory, try again");
    }
    if (display == nullptr || g_screen_busy.exchange(true)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "snapshot busy");
    }
    auto job = new std::shared_ptr<ScreenJob>(std::make_shared<ScreenJob>());
    auto keep = *job;
    BaseType_t created = xTaskCreateWithCaps(
        [](void* arg) {
            auto owned = static_cast<std::shared_ptr<ScreenJob>*>(arg);
            auto job = *owned;
            delete owned;
            job->ok = Board::GetInstance().GetDisplay()->SnapshotToJpeg(job->jpeg, 85);
            g_screen_busy = false;
            xSemaphoreGive(job->done);
            job.reset();
            vTaskDeleteWithCaps(nullptr);
        },
        "screen_jpg", 16 * 1024, job, 2, nullptr, MALLOC_CAP_SPIRAM);
    if (created != pdPASS) {
        delete job;
        g_screen_busy = false;
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "snapshot failed");
    }
    if (xSemaphoreTake(keep->done, pdMS_TO_TICKS(8000)) != pdTRUE || !keep->ok) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "snapshot failed");
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, keep->jpeg.data(), keep->jpeg.size());
}

esp_err_t HandleApp(httpd_req_t* req) {
    if (!SameOrigin(req))
        return Reject(req, "missing header");
    cJSON* in = ReadJson(req);
    auto id = cJSON_GetObjectItem(in, "id");
    std::string want = cJSON_IsString(id) ? id->valuestring : "";
    cJSON_Delete(in);
    for (auto& app : Apps()) {
        if (app.id != want)
            continue;
        Say("<hệ thống>Người dùng vừa mở ứng dụng \"" + app.title + "\". " + app.prompt +
            "</hệ thống>");
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    }
    return Reject(req, "unknown app");
}

// Saved Wi-Fi networks. The device joins whichever saved network it finds (up to 10).
esp_err_t HandleGetWifi(httpd_req_t* req) {
    auto& wifi = WifiManager::GetInstance();
    cJSON* r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "current", wifi.IsConnected() ? wifi.GetSsid().c_str() : "");
    cJSON_AddNumberToObject(r, "rssi", wifi.IsConnected() ? wifi.GetRssi() : 0);
    cJSON_AddStringToObject(r, "ip", wifi.GetIpAddress().c_str());
    cJSON* list = cJSON_AddArrayToObject(r, "saved");
    for (auto& item : SsidManager::GetInstance().GetSsidList()) {
        cJSON_AddItemToArray(list,
                             cJSON_CreateString(item.ssid.c_str()));  // never return passwords
    }
    return SendJson(req, r);
}

esp_err_t HandlePostWifi(httpd_req_t* req) {
    if (!SameOrigin(req))
        return Reject(req, "missing header");
    cJSON* in = ReadJson(req);
    auto action = cJSON_GetObjectItem(in, "action");
    auto ssid = cJSON_GetObjectItem(in, "ssid");
    auto password = cJSON_GetObjectItem(in, "password");
    auto index = cJSON_GetObjectItem(in, "index");
    std::string act = cJSON_IsString(action) ? action->valuestring : "";
    auto& ssids = SsidManager::GetInstance();
    esp_err_t result;
    if (act == "add" && cJSON_IsString(ssid) && strlen(ssid->valuestring) > 0 &&
        strlen(ssid->valuestring) <= 32) {
        std::string pass = cJSON_IsString(password) ? password->valuestring : "";
        if (pass.size() > 64 || (!pass.empty() && pass.size() < 8)) {
            cJSON_Delete(in);
            return Reject(req, "Mật khẩu Wi-Fi phải từ 8 đến 64 ký tự");
        }
        ssids.AddSsid(ssid->valuestring, pass);
        result = httpd_resp_sendstr(req, "{\"ok\":true}");
    } else if (act == "remove" && cJSON_IsNumber(index) && index->valueint >= 0 &&
               index->valueint < (int)ssids.GetSsidList().size()) {
        if (ssids.GetSsidList().size() <= 1) {
            cJSON_Delete(in);
            return Reject(req, "Phải giữ lại ít nhất một mạng Wi-Fi");
        }
        ssids.RemoveSsid(index->valueint);
        result = httpd_resp_sendstr(req, "{\"ok\":true}");
    } else if (act == "prefer" && cJSON_IsNumber(index) && index->valueint >= 0 &&
               index->valueint < (int)ssids.GetSsidList().size()) {
        ssids.SetDefaultSsid(index->valueint);
        result = httpd_resp_sendstr(req, "{\"ok\":true}");
    } else {
        result = Reject(req, "Yêu cầu không hợp lệ");
    }
    cJSON_Delete(in);
    return result;
}

}  // namespace

void StartWebServer() {
    static httpd_handle_t server = nullptr;
    if (server != nullptr)
        return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;  // peak use ~2.7 KB (see /api/perf stack_free_min)
    config.max_uri_handlers = 24;
    config.recv_wait_timeout = 15;
    config.lru_purge_enable = true;
    config.max_open_sockets = 4;  // the browser reuses connections; leave sockets for Gemini/music
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start settings web server");
        server = nullptr;
        return;
    }
    static const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = HandleIndex, .user_ctx = nullptr},
        {.uri = "/api/config", .method = HTTP_GET, .handler = HandleGetConfig, .user_ctx = nullptr},
        {.uri = "/api/config",
         .method = HTTP_POST,
         .handler = HandlePostConfig,
         .user_ctx = nullptr},
        {.uri = "/api/status", .method = HTTP_GET, .handler = HandleStatus, .user_ctx = nullptr},
        {.uri = "/api/say", .method = HTTP_POST, .handler = HandleSay, .user_ctx = nullptr},
        {.uri = "/api/action", .method = HTTP_POST, .handler = HandleAction, .user_ctx = nullptr},
        {.uri = "/api/tool", .method = HTTP_POST, .handler = HandleTool, .user_ctx = nullptr},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = HandleOta, .user_ctx = nullptr},
        {.uri = "/api/app", .method = HTTP_POST, .handler = HandleApp, .user_ctx = nullptr},
        {.uri = "/face", .method = HTTP_GET, .handler = HandleFace, .user_ctx = nullptr},
        {.uri = "/screen.jpg", .method = HTTP_GET, .handler = HandleScreen, .user_ctx = nullptr},
        {.uri = "/api/face",
         .method = HTTP_POST,
         .handler = HandleFacePreview,
         .user_ctx = nullptr},
        {.uri = "/api/wifi", .method = HTTP_GET, .handler = HandleGetWifi, .user_ctx = nullptr},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = HandlePostWifi, .user_ctx = nullptr},
    };
    for (auto& r : routes)
        RegisterGuarded(server, r);
    RegisterNotifyRoutes(server);
    RegisterPerfRoutes(server);
    // http://bap.local on networks and devices that support mDNS
    if (mdns_init() == ESP_OK) {
        // One name per device so several Bắp on the same network do not clash: bap-1a2b.local
        uint8_t mac[6] = {};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char host[16];
        snprintf(host, sizeof(host), "bap-%02x%02x", mac[4], mac[5]);
        mdns_hostname_set(host);
        mdns_instance_name_set("Bap assistant");
        mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    }
    ESP_LOGI(TAG, "Settings UI ready on port 80 (http://bap-xxxx.local, see router or screen)");
}

}  // namespace assistant
