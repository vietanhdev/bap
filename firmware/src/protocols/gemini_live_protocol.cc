#include "gemini_live_protocol.h"

#include "ticket_ssl.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <functional>
#include <vector>

#include "application.h"
#include "assets/lang_config.h"
#include "assistant/assistant.h"
#include "assistant/assistant_config.h"
#include "assistant/assistant_face.h"
#include "board.h"
#include "display/display.h"
#include "mcp_server.h"

#define TAG "GeminiLive"

static constexpr const char* kGeminiUrl =
    "wss://generativelanguage.googleapis.com/ws/"
    "google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent";
static constexpr EventBits_t kSetupDoneBit = BIT0;
static constexpr EventBits_t kSetupFailedBit = BIT1;

GeminiLiveProtocol* GeminiLiveProtocol::instance_ = nullptr;
static constexpr int kDownlinkRate = 24000;
static constexpr size_t kDownlinkChunkBytes = kDownlinkRate / 1000 * 60 * sizeof(int16_t);  // 60 ms
static constexpr const char* kEndConversationFn = "end_conversation";
static constexpr size_t kUplinkBatchBytes = 16000 * 2 * 120 / 1000;  // 120 ms of 16 kHz PCM16
// Free-tier Live quota is input tokens per minute, so the free key rests a minute after a
// rejection, doubling while it keeps being rejected.
static constexpr int kFreeRestMinS = 60;
static constexpr int kFreeRestMaxS = 900;

namespace {

int64_t NowMs() { return esp_timer_get_time() / 1000; }

std::string Base64Encode(const uint8_t* data, size_t len) {
    size_t out_len = 0;
    mbedtls_base64_encode(nullptr, 0, &out_len, data, len);
    std::string out(out_len, '\0');
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out.data()), out.size(), &out_len,
                              data, len) != 0) {
        return {};
    }
    out.resize(out_len);
    return out;
}

std::vector<uint8_t> Base64Decode(const char* b64) {
    size_t len = strlen(b64);
    size_t out_len = 0;
    mbedtls_base64_decode(nullptr, 0, &out_len, reinterpret_cast<const unsigned char*>(b64), len);
    std::vector<uint8_t> out(out_len);
    if (mbedtls_base64_decode(out.data(), out.size(), &out_len,
                              reinterpret_cast<const unsigned char*>(b64), len) != 0) {
        return {};
    }
    out.resize(out_len);
    return out;
}

// Gemini function names: letters, digits, underscore, dash (max 64). MCP uses dots.
std::string ToFunctionName(const std::string& tool) {
    std::string fn = tool;
    for (auto& c : fn) {
        if (c == '.')
            c = '_';
    }
    return fn.substr(0, 64);
}

bool EndsSentence(const std::string& s, size_t& cut) {
    // Cut after the last sentence terminator so a full sentence goes to the screen at once.
    for (size_t i = s.size(); i > 0; --i) {
        char c = s[i - 1];
        if (c == '.' || c == '!' || c == '?' || c == '\n') {
            cut = i;
            return true;
        }
    }
    return false;
}

std::string Lower(const std::string& s) {
    std::string out = s;
    for (auto& c : out)
        c = tolower(static_cast<unsigned char>(c));
    return out;
}

}  // namespace

GeminiLiveProtocol::GeminiLiveProtocol() {
    event_group_ = xEventGroupCreate();
    server_sample_rate_ = kDownlinkRate;
    server_frame_duration_ = 60;
    instance_ = this;
}

GeminiLiveProtocol::~GeminiLiveProtocol() {
    instance_ = nullptr;
    websocket_.reset();
    vEventGroupDelete(event_group_);
}

bool GeminiLiveProtocol::Start() {
    // No OTA server provides time, so sync it ourselves (Vietnam time for the prompt context).
    setenv("TZ", "ICT-7", 1);
    tzset();
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&config) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed");
    }
    esp_ota_mark_app_valid_cancel_rollback();  // no OTA server marks it for us in direct mode
    assistant::InstallFontFallback();
    assistant::SyncClock();
    assistant::RegisterTools();
    assistant::StartScheduler();
    assistant::StartWebServer();
    if (assistant::LoadConfig().api_key.empty()) {
        ESP_LOGE(TAG, "Gemini API key is empty");
        SetError(Lang::Strings::SERVER_NOT_CONNECTED,
                 "Chưa có Gemini API key. Mở http://bap.local để nhập.");
        return false;
    }
    return true;
}

bool GeminiLiveProtocol::FreeKeyResting() const { return NowMs() < free_retry_ms_.load(); }

void GeminiLiveProtocol::RestFreeKey() {
    int rest = free_rest_s_ == 0 ? kFreeRestMinS : std::min(free_rest_s_ * 2, kFreeRestMaxS);
    free_rest_s_ = rest;
    free_retry_ms_ = NowMs() + rest * 1000LL;
    ESP_LOGW(TAG, "Free key rejected; using the paid key for the next %d s", rest);
}

GeminiLiveProtocol::KeyStats GeminiLiveProtocol::GetKeyStats() const {
    int64_t left = free_retry_ms_.load() - NowMs();
    return {use_paid_.load(), free_sessions_.load(), paid_sessions_.load(),
            left > 0 ? static_cast<int>((left + 999) / 1000) : 0};
}

bool GeminiLiveProtocol::OpenAudioChannel() {
    auto cfg = assistant::LoadConfig();
    idle_timeout_s_ = cfg.idle_s;
    // Free key first. The paid key (a separate billing-enabled project) only takes over when the
    // free one is rejected, and only until the free key's rest ends.
    const bool have_free = !cfg.api_key.empty();
    const bool have_paid = !cfg.api_key_paid.empty() && cfg.api_key_paid != cfg.api_key;
    bool paid = have_paid && (!have_free || FreeKeyResting());
    auto result = TryOpen(cfg, paid);
    if (result != OpenResult::kOpened && !paid && have_paid) {
        ESP_LOGW(TAG, "Free key failed; trying the paid key");
        paid = true;
        result = TryOpen(cfg, true);
        if (result == OpenResult::kOpened)
            RestFreeKey();  // the paid key got through, so the free key was the problem
    }
    if (result == OpenResult::kRejected && cfg.search) {
        // Google Search grounding is not in the free quota: turn it off and retry once.
        ESP_LOGW(TAG, "Session rejected with Google Search on; disabling it and retrying");
        cfg.search = false;
        assistant::SaveConfig(cfg);
        return OpenAudioChannel();
    }
    if (result == OpenResult::kConnectFailed) {
        SetError(Lang::Strings::SERVER_NOT_CONNECTED, "Gemini Live");
        return false;
    }
    if (result == OpenResult::kRejected) {
        ESP_LOGE(TAG, "Gemini rejected the session (check API key, model, quota)");
        SetError(Lang::Strings::SERVER_NOT_CONNECTED,
                 "Gemini từ chối: kiểm tra API key, model, hạn mức");
        return false;
    }
    if (paid) {
        paid_sessions_++;
    } else {
        free_sessions_++;
        free_rest_s_ = 0;  // free key works again: the next rejection rests it only briefly
    }

    last_activity_ = std::chrono::steady_clock::now();
    last_incoming_time_ = last_activity_;
    channel_open_ = true;
    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }
    if (!pending_text_.empty()) {
        SendUserText(pending_text_);
        pending_text_.clear();
    }
    return true;
}

GeminiLiveProtocol::OpenResult GeminiLiveProtocol::TryOpen(const assistant::Config& cfg,
                                                           bool paid) {
    error_occurred_ = false;
    setup_done_ = false;
    speaking_ = false;
    drop_audio_ = false;
    close_after_turn_ = false;
    assistant_text_.clear();
    user_text_.clear();
    xEventGroupClearBits(event_group_, kSetupDoneBit | kSetupFailedBit);
    pending_audio_.clear();
    use_paid_ = paid;

    // TLS session tickets: reconnects skip the full handshake (see ticket_ssl.h).
    static TicketNetwork ticket_network(Board::GetInstance().GetNetwork());
    websocket_ = ticket_network.CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        return OpenResult::kConnectFailed;
    }
    websocket_->SetHeader("x-goog-api-key", (paid ? cfg.api_key_paid : cfg.api_key).c_str());
    websocket_->OnData([this](const char* data, size_t len, bool) {
        last_incoming_time_ = std::chrono::steady_clock::now();
        HandleMessage(data, len);
    });
    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Disconnected");
        xEventGroupSetBits(event_group_, kSetupFailedBit);
        if (channel_open_.exchange(false) && on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    ESP_LOGI(TAG, "Connecting to Gemini Live (%s, %s key)", cfg.model.c_str(),
             paid ? "paid" : "free");
    if (auto connected = websocket_->Connect(kGeminiUrl); !connected) {
        // Also how a quota rejection of the WebSocket upgrade (HTTP 429) shows up.
        ESP_LOGE(TAG, "Connect failed: %s", connected.error().ToString().c_str());
        websocket_.reset();
        return OpenResult::kConnectFailed;
    }

    if (!SendText(BuildSetupMessage())) {
        websocket_.reset();
        return OpenResult::kConnectFailed;
    }
    EventBits_t bits = xEventGroupWaitBits(event_group_, kSetupDoneBit | kSetupFailedBit, pdTRUE,
                                           pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & kSetupDoneBit)) {
        // Gemini closes the socket when it rejects the key, model, quota or a tool.
        websocket_.reset();
        if (resuming_) {
            ESP_LOGW(TAG, "Resume handle rejected; starting a fresh session");
            resume_handle_.clear();
            return TryOpen(cfg, paid);
        }
        return OpenResult::kRejected;
    }
    return OpenResult::kOpened;
}

void GeminiLiveProtocol::CloseAudioChannel(bool) {
    std::shared_ptr<WebSocket> ws;
    {
        std::lock_guard<std::recursive_mutex> lock(send_mutex_);
        ws = std::move(websocket_);
    }
    ws.reset();
    if (channel_open_.exchange(false) && on_audio_channel_closed_ != nullptr) {
        on_audio_channel_closed_();
    }
}

bool GeminiLiveProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && channel_open_ && !error_occurred_;
}

bool GeminiLiveProtocol::SendText(const std::string& text) {
    std::lock_guard<std::recursive_mutex> lock(send_mutex_);
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }
    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Send failed");
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }
    return true;
}

bool GeminiLiveProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (!setup_done_ || packet->payload.empty()) {
        return false;
    }
    auto idle = std::chrono::steady_clock::now() - last_activity_;
    if (!speaking_ && idle > std::chrono::seconds(idle_timeout_s_)) {
        ESP_LOGI(TAG, "Idle for %ds, closing session", idle_timeout_s_);
        last_activity_ = std::chrono::steady_clock::now();  // schedule once
        Application::GetInstance().Schedule([this]() { CloseAudioChannel(); });
        return false;
    }
    // Batch two 60 ms frames per message: half the JSON/TLS overhead, still low latency.
    pending_audio_.insert(pending_audio_.end(), packet->payload.begin(), packet->payload.end());
    if (pending_audio_.size() < kUplinkBatchBytes)
        return true;
    return FlushAudio();
}

bool GeminiLiveProtocol::FlushAudio() {
    if (pending_audio_.empty())
        return true;
    std::string b64 = Base64Encode(pending_audio_.data(), pending_audio_.size());
    pending_audio_.clear();
    std::string msg;
    msg.reserve(b64.size() + 96);
    msg += R"({"realtimeInput":{"audio":{"mimeType":"audio/pcm;rate=16000","data":")";
    msg += b64;
    msg += R"("}}})";
    return SendText(msg);
}

void GeminiLiveProtocol::SendWakeWordDetected(const std::string& wake_word) {
    // Real wake words carry no content. Synthetic ones (camera "<detect>", timers, web UI text)
    // are forwarded so the assistant reacts to them.
    if (wake_word.empty() || wake_word[0] != '<')
        return;
    if (wake_word.rfind("<say>", 0) == 0) {
        SendUserText(wake_word.substr(5));
    } else if (wake_word.rfind("<detect>", 0) == 0) {
        SendUserText(
            "<hệ thống>Camera vừa thấy có người đến gần. Hãy chủ động chào một câu thật ngắn và dễ "
            "thương.</hệ thống>");
    } else {
        SendUserText(wake_word);
    }
}

bool GeminiLiveProtocol::SendUserText(const std::string& text) {
    if (!setup_done_ || !IsAudioChannelOpened()) {
        pending_text_ = text;
        return false;
    }
    last_activity_ = std::chrono::steady_clock::now();
    if (!text.empty() && text[0] != '<')
        assistant::LogTurn("user", text);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(cJSON_AddObjectToObject(root, "realtimeInput"), "text", text.c_str());
    char* s = cJSON_PrintUnformatted(root);
    bool ok = SendText(s);
    cJSON_free(s);
    cJSON_Delete(root);
    return ok;
}

bool GeminiLiveProtocol::SendImage(const uint8_t* jpeg, size_t len) {
    if (!setup_done_ || jpeg == nullptr || len == 0)
        return false;
    std::string msg = R"({"realtimeInput":{"video":{"mimeType":"image/jpeg","data":")";
    msg += Base64Encode(jpeg, len);
    msg += R"("}}})";
    return SendText(msg);
}
void GeminiLiveProtocol::SendStartListening(ListeningMode) {
    last_activity_ = std::chrono::steady_clock::now();
}

void GeminiLiveProtocol::SendStopListening() {
    FlushAudio();
    // Manual mode: tell Gemini the user finished speaking.
    SendText(R"({"realtimeInput":{"audioStreamEnd":true}})");
}

void GeminiLiveProtocol::SendAbortSpeaking(AbortReason) {
    // Gemini keeps generating this turn; drop its audio until the turn ends.
    drop_audio_ = true;
}

std::string GeminiLiveProtocol::BuildToolDeclarations() {
    // Ask the on-device MCP server for its tools. tools/list is paginated (nextCursor).
    cJSON* decls = cJSON_CreateArray();
    fn_to_tool_.clear();
    std::string cursor;
    for (int page = 0; page < 10; ++page) {
        std::string reply;
        std::string request = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{)";
        if (!cursor.empty())
            request += "\"cursor\":\"" + cursor + "\"";
        request += "}}";
        McpServer::GetInstance().ParseMessage(
            request, [&reply](const std::string& payload) { reply = payload; });
        cJSON* root = cJSON_Parse(reply.c_str());
        cJSON* result = cJSON_GetObjectItem(root, "result");
        cJSON* tool = nullptr;
        cJSON_ArrayForEach (tool, cJSON_GetObjectItem(result, "tools")) {
            auto name = cJSON_GetObjectItem(tool, "name");
            auto desc = cJSON_GetObjectItem(tool, "description");
            auto schema = cJSON_GetObjectItem(tool, "inputSchema");
            if (!cJSON_IsString(name))
                continue;
            // Upstream tools that do not help a voice assistant here (camera model tuning, theme:
            // the UI is always dark). Hiding them saves ~1.9 KB of tokens on every session.
            static const char* const kHidden[] = {"self.model.param_get", "self.model.param_set",
                                                  "self.model.enable", "self.screen.set_theme"};
            bool hidden = false;
            for (auto* h : kHidden)
                hidden = hidden || strcmp(name->valuestring, h) == 0;
            if (hidden)
                continue;
            std::string fn = ToFunctionName(name->valuestring);
            fn_to_tool_[fn] = name->valuestring;
            cJSON* d = cJSON_CreateObject();
            cJSON_AddStringToObject(d, "name", fn.c_str());
            cJSON_AddStringToObject(d, "description",
                                    cJSON_IsString(desc) ? desc->valuestring : "");
            if (cJSON_IsObject(schema)) {
                cJSON_AddItemToObject(d, "parametersJsonSchema", cJSON_Duplicate(schema, true));
            }
            cJSON_AddItemToArray(decls, d);
        }
        auto next = cJSON_GetObjectItem(result, "nextCursor");
        cursor = cJSON_IsString(next) ? next->valuestring : "";
        cJSON_Delete(root);
        if (cursor.empty())
            break;
    }

    cJSON* end = cJSON_CreateObject();
    cJSON_AddStringToObject(end, "name", kEndConversationFn);
    cJSON_AddStringToObject(end, "description",
                            "Kết thúc cuộc trò chuyện. Gọi khi người dùng chào tạm biệt, chúc ngủ "
                            "ngon hoặc bảo bạn nghỉ. Hãy chào lại trước khi gọi.");
    cJSON_AddItemToArray(decls, end);

    char* s = cJSON_PrintUnformatted(decls);
    std::string out(s);
    cJSON_free(s);
    cJSON_Delete(decls);
    ESP_LOGI(TAG, "Exposed %d device tools to Gemini", (int)fn_to_tool_.size());
    return out;
}

std::string GeminiLiveProtocol::BuildSetupMessage() {
    auto cfg = assistant::LoadConfig();
    auto character = assistant::ActiveCharacter();
    std::string prompt = assistant::SystemPrompt();
    prompt += "\nNgười dùng đang ở " + cfg.city + ", Việt Nam.";
    {
        cJSON* mem = cJSON_Parse(assistant::LoadList("memory").c_str());
        if (cJSON_GetArraySize(mem) > 0) {
            prompt += "\nNhững điều bạn đã ghi nhớ về người dùng:";
            cJSON* it = nullptr;
            cJSON_ArrayForEach (it, mem) {
                if (cJSON_IsString(it))
                    prompt += std::string("\n- ") + it->valuestring;
            }
        }
        cJSON_Delete(mem);
    }
    time_t now = time(nullptr);
    if (now > 1700000000) {
        char buf[96];
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        static const char* kWeekdays[] = {"Chủ nhật", "Thứ hai", "Thứ ba", "Thứ tư",
                                          "Thứ năm",  "Thứ sáu", "Thứ bảy"};
        snprintf(buf, sizeof(buf), "\nBây giờ là %02d:%02d, %s, ngày %d/%d/%d (giờ Việt Nam).",
                 tm_info.tm_hour, tm_info.tm_min, kWeekdays[tm_info.tm_wday], tm_info.tm_mday,
                 tm_info.tm_mon + 1, tm_info.tm_year + 1900);
        prompt += buf;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* setup = cJSON_AddObjectToObject(root, "setup");
    std::string model = "models/" + cfg.model;
    cJSON_AddStringToObject(setup, "model", model.c_str());

    cJSON* gen = cJSON_AddObjectToObject(setup, "generationConfig");
    cJSON* modalities = cJSON_AddArrayToObject(gen, "responseModalities");
    cJSON_AddItemToArray(modalities, cJSON_CreateString("AUDIO"));
    cJSON* speech = cJSON_AddObjectToObject(gen, "speechConfig");
    cJSON* voice = cJSON_AddObjectToObject(cJSON_AddObjectToObject(speech, "voiceConfig"),
                                           "prebuiltVoiceConfig");
    cJSON_AddStringToObject(voice, "voiceName", character.voice.c_str());

    cJSON* sys = cJSON_AddObjectToObject(setup, "systemInstruction");
    cJSON* part = cJSON_CreateObject();
    cJSON_AddStringToObject(part, "text", prompt.c_str());
    cJSON_AddItemToArray(cJSON_AddArrayToObject(sys, "parts"), part);

    // Long sessions: sliding-window compression, and resumable sessions so a conversation picked
    // up again within a few minutes keeps its context.
    cJSON_AddObjectToObject(cJSON_AddObjectToObject(setup, "contextWindowCompression"),
                            "slidingWindow");
    cJSON* resumption = cJSON_AddObjectToObject(setup, "sessionResumption");
    // A handle only resumes on the key (project) that issued it.
    // A different character or language must start a fresh session, not resume the old persona.
    setup_hash_ = std::hash<std::string>{}(prompt + character.voice);
    if (!resume_handle_.empty() && resume_paid_ == use_paid_ && resume_setup_hash_ == setup_hash_ &&
        std::chrono::steady_clock::now() - resume_handle_time_ < std::chrono::minutes(10)) {
        cJSON_AddStringToObject(resumption, "handle", resume_handle_.c_str());
        resuming_ = true;
    } else {
        resuming_ = false;
    }
    // Wait for a real pause before answering: Vietnamese speakers often pause briefly mid-sentence,
    // and the default end-of-speech detection cut them off.
    cJSON* vad = cJSON_AddObjectToObject(cJSON_AddObjectToObject(setup, "realtimeInputConfig"),
                                         "automaticActivityDetection");
    cJSON_AddStringToObject(vad, "startOfSpeechSensitivity", "START_SENSITIVITY_LOW");
    cJSON_AddStringToObject(vad, "endOfSpeechSensitivity", "END_SENSITIVITY_LOW");
    cJSON_AddNumberToObject(vad, "prefixPaddingMs", 300);
    cJSON_AddNumberToObject(vad, "silenceDurationMs", 900);
    cJSON_AddObjectToObject(setup, "inputAudioTranscription");
    cJSON_AddObjectToObject(setup, "outputAudioTranscription");

    cJSON* tools = cJSON_AddArrayToObject(setup, "tools");
    cJSON* fn_tool = cJSON_CreateObject();
    cJSON_AddItemToObject(fn_tool, "functionDeclarations",
                          cJSON_Parse(BuildToolDeclarations().c_str()));
    cJSON_AddItemToArray(tools, fn_tool);
    if (cfg.search) {
        cJSON* search = cJSON_CreateObject();
        cJSON_AddObjectToObject(search, "googleSearch");
        cJSON_AddItemToArray(tools, search);
    }

    char* s = cJSON_PrintUnformatted(root);
    std::string out(s);
    cJSON_free(s);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Setup message: %u bytes (prompt %u)", (unsigned)out.size(),
             (unsigned)prompt.size());
    return out;
}

void GeminiLiveProtocol::HandleMessage(const char* data, size_t len) {
    cJSON* root = cJSON_ParseWithLength(data, len);
    if (root == nullptr) {
        ESP_LOGW(TAG, "Bad JSON (%u bytes)", (unsigned)len);
        return;
    }
    if (cJSON_GetObjectItem(root, "setupComplete")) {
        setup_done_ = true;
        xEventGroupSetBits(event_group_, kSetupDoneBit);
    }
    if (auto content = cJSON_GetObjectItem(root, "serverContent")) {
        HandleServerContent(content);
    }
    if (auto tool_call = cJSON_GetObjectItem(root, "toolCall")) {
        HandleToolCall(tool_call);
    }
    if (auto update = cJSON_GetObjectItem(root, "sessionResumptionUpdate")) {
        auto handle = cJSON_GetObjectItem(update, "newHandle");
        if (cJSON_IsTrue(cJSON_GetObjectItem(update, "resumable")) && cJSON_IsString(handle)) {
            resume_handle_ = handle->valuestring;
            resume_handle_time_ = std::chrono::steady_clock::now();
            resume_setup_hash_ = setup_hash_;
            resume_paid_ = use_paid_;
        }
    }
    if (cJSON_GetObjectItem(root, "goAway")) {
        ESP_LOGW(TAG, "Server sent goAway; session will end soon");
    }
    cJSON_Delete(root);
}

void GeminiLiveProtocol::HandleServerContent(const cJSON* content) {
    auto input = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "inputTranscription"), "text");
    if (cJSON_IsString(input) && input->valuestring[0] != '\0') {
        last_activity_ = std::chrono::steady_clock::now();
        if (!speaking_) {
            if (user_text_.empty())
                EmitEmotion("thinking");
            user_text_ += input->valuestring;
            assistant::LogTurn("user", user_text_);
            EmitStt(user_text_);
        }
    }

    auto parts = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "modelTurn"), "parts");
    cJSON* part = nullptr;
    cJSON_ArrayForEach (part, parts) {
        auto data = cJSON_GetObjectItem(cJSON_GetObjectItem(part, "inlineData"), "data");
        if (cJSON_IsString(data)) {
            DeliverAudio(data->valuestring);
        }
    }

    auto output = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "outputTranscription"), "text");
    if (cJSON_IsString(output)) {
        assistant_text_ += output->valuestring;
        FlushAssistantText(false);
    }

    if (cJSON_IsTrue(cJSON_GetObjectItem(content, "interrupted"))) {
        FinishTurn(true);
    } else if (cJSON_IsTrue(cJSON_GetObjectItem(content, "turnComplete"))) {
        FinishTurn(false);
    }
}

void GeminiLiveProtocol::DeliverAudio(const std::string& b64) {
    if (drop_audio_ || on_incoming_audio_ == nullptr) {
        return;
    }
    auto pcm = Base64Decode(b64.c_str());
    if (pcm.empty()) {
        return;
    }
    auto& app = Application::GetInstance();
    if (!speaking_.exchange(true)) {
        user_text_.clear();
        EmitTts("start");
        EmitEmotion("happy");
        // The Application only plays audio in the speaking state, which it enters asynchronously.
        for (int i = 0; i < 30 && app.GetDeviceState() != kDeviceStateSpeaking; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    last_activity_ = std::chrono::steady_clock::now();
    for (size_t off = 0; off < pcm.size() && !drop_audio_; off += kDownlinkChunkBytes) {
        size_t n = std::min(kDownlinkChunkBytes, pcm.size() - off);
        auto packet = std::make_unique<AudioStreamPacket>();
        packet->sample_rate = kDownlinkRate;
        packet->frame_duration = 0;  // 0 = raw PCM16 (see AudioService raw mode)
        packet->payload.assign(pcm.begin() + off, pcm.begin() + off + n);
        on_incoming_audio_(std::move(packet));  // blocks while the decode queue is full
    }
}

void GeminiLiveProtocol::FinishTurn(bool interrupted) {
    FlushAssistantText(true);
    const bool spoke = speaking_;
    if (speaking_) {
        auto& audio = Application::GetInstance().GetAudioService();
        // Let queued speech finish before telling the Application the reply is over.
        for (int i = 0; !interrupted && !drop_audio_ && i < 1200 && !audio.IsPlaybackIdle(); ++i) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        speaking_ = false;
        EmitTts("stop");
    }
    user_text_.clear();
    drop_audio_ = false;
    last_activity_ = std::chrono::steady_clock::now();
    // Tool calls are asynchronous: the turn holding end_conversation can finish before the goodbye
    // is spoken. Close after a spoken turn, or after a short grace period if none follows.
    if (close_after_turn_ && spoke) {
        close_after_turn_ = false;
        Application::GetInstance().Schedule([this]() { CloseAudioChannel(); });
    } else if (close_after_turn_) {
        xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<GeminiLiveProtocol*>(arg);
                vTaskDelay(pdMS_TO_TICKS(8000));
                if (self->close_after_turn_.exchange(false) && !self->speaking_) {
                    Application::GetInstance().Schedule([self]() { self->CloseAudioChannel(); });
                }
                vTaskDelete(nullptr);
            },
            "gl_close", 3072, this, 2, nullptr);
    }
}

void GeminiLiveProtocol::FlushAssistantText(bool force) {
    // Subtitles follow the voice: the transcription streams in step with the audio, so show the
    // tail of the current sentence (about three lines on the round screen) as words arrive.
    static constexpr size_t kMaxSubtitle = 64;
    size_t cut = 0;
    while (EndsSentence(assistant_text_, cut) || (force && !assistant_text_.empty())) {
        if (!EndsSentence(assistant_text_, cut))
            cut = assistant_text_.size();
        std::string sentence = assistant_text_.substr(0, cut);
        assistant_text_.erase(0, cut);
        size_t start = sentence.find_first_not_of(" \n");
        if (start == std::string::npos)
            continue;
        sentence = sentence.substr(start);
        assistant::LogTurn("assistant", sentence);
        EmitEmotion(GuessEmotion(sentence));
        if (assistant_text_.find_first_not_of(" \n") == std::string::npos) {
            ShowSubtitle(sentence, kMaxSubtitle);  // keep the finished sentence on screen
        }
    }
    if (!assistant_text_.empty())
        ShowSubtitle(assistant_text_, kMaxSubtitle);
}

void GeminiLiveProtocol::ShowSubtitle(const std::string& text, size_t max_bytes) {
    std::string shown = text;
    size_t start = shown.find_first_not_of(" \n");
    if (start == std::string::npos)
        return;
    shown = shown.substr(start);
    if (shown.size() > max_bytes) {
        size_t at = shown.find(' ', shown.size() - max_bytes);  // start at a word boundary
        shown = "…" + shown.substr(at == std::string::npos ? shown.size() - max_bytes : at + 1);
    }
    if (shown == last_subtitle_)
        return;
    last_subtitle_ = shown;
    EmitTts("sentence_start", shown);
}

const char* GeminiLiveProtocol::GuessEmotion(const std::string& sentence) const {
    std::string s = Lower(sentence);
    auto has = [&s](const char* w) { return s.find(w) != std::string::npos; };
    if (has("haha") || has("hihi") || has("hì hì") || has("buồn cười"))
        return "laughing";
    if (has("xin lỗi") || has("tiếc") || has("buồn"))
        return "sad";
    if (has("yêu") || has("thương") || has("ôm"))
        return "loving";
    if (has("wow") || has("ồ") || has("ôi") || has("thật sao") || has("trời ơi"))
        return "surprised";
    if (has("ngủ ngon") || has("buồn ngủ"))
        return "sleepy";
    if (has("ngon"))
        return "delicious";
    if (has("hừm") || has("để xem") || has("để mình nghĩ"))
        return "thinking";
    if (!s.empty() && s.back() == '?')
        return "winking";
    if (!s.empty() && s.back() == '!')
        return "laughing";
    return "happy";
}

void GeminiLiveProtocol::HandleToolCall(const cJSON* tool_call) {
    auto calls = cJSON_GetObjectItem(tool_call, "functionCalls");
    cJSON* call = nullptr;
    cJSON_ArrayForEach (call, calls) {
        auto id = cJSON_GetObjectItem(call, "id");
        auto name = cJSON_GetObjectItem(call, "name");
        auto args = cJSON_GetObjectItem(call, "args");
        if (!cJSON_IsString(name))
            continue;
        std::string call_id = cJSON_IsString(id) ? id->valuestring : "";
        std::string fn = name->valuestring;
        ESP_LOGI(TAG, "Tool call: %s", fn.c_str());

        if (fn == kEndConversationFn) {
            close_after_turn_ = true;
            SendToolResponse(call_id, fn, R"({"result":"ok"})");
            continue;
        }
        auto it = fn_to_tool_.find(fn);
        if (it == fn_to_tool_.end()) {
            SendToolResponse(call_id, fn, R"({"error":"unknown tool"})");
            continue;
        }
        int mcp_id;
        {
            std::lock_guard<std::mutex> lock(calls_mutex_);
            mcp_id = next_mcp_id_++;
            pending_calls_[mcp_id] = {call_id, fn};
        }
        cJSON* req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "jsonrpc", "2.0");
        cJSON_AddNumberToObject(req, "id", mcp_id);
        cJSON_AddStringToObject(req, "method", "tools/call");
        cJSON* params = cJSON_AddObjectToObject(req, "params");
        cJSON_AddStringToObject(params, "name", it->second.c_str());
        cJSON_AddItemToObject(
            params, "arguments",
            cJSON_IsObject(args) ? cJSON_Duplicate(args, true) : cJSON_CreateObject());
        char* s = cJSON_PrintUnformatted(req);
        std::string req_str(s);
        cJSON_free(s);
        cJSON_Delete(req);
        McpServer::GetInstance().ParseMessage(
            req_str, [this](const std::string& payload) { SendMcpMessage(payload); });
    }
}

void GeminiLiveProtocol::SendMcpMessage(const std::string& message) {
    // MCP JSON-RPC response from the device -> Gemini toolResponse.
    cJSON* root = cJSON_Parse(message.c_str());
    auto id = cJSON_GetObjectItem(root, "id");
    if (!cJSON_IsNumber(id)) {
        cJSON_Delete(root);
        return;
    }
    std::pair<std::string, std::string> call;
    {
        std::lock_guard<std::mutex> lock(calls_mutex_);
        auto it = pending_calls_.find(id->valueint);
        if (it == pending_calls_.end()) {
            cJSON_Delete(root);
            return;
        }
        call = it->second;
        pending_calls_.erase(it);
    }
    cJSON* response = cJSON_CreateObject();
    if (auto result = cJSON_GetObjectItem(root, "result")) {
        cJSON_AddItemToObject(response, "result", cJSON_Duplicate(result, true));
    } else {
        ESP_LOGW(TAG, "Tool %s failed: %s", call.second.c_str(), message.c_str());
        auto error = cJSON_GetObjectItem(root, "error");
        cJSON_AddItemToObject(response, "error",
                              error ? cJSON_Duplicate(error, true) : cJSON_CreateString("failed"));
    }
    char* s = cJSON_PrintUnformatted(response);
    SendToolResponse(call.first, call.second, s);
    cJSON_free(s);
    cJSON_Delete(response);
    cJSON_Delete(root);
}

void GeminiLiveProtocol::SendToolResponse(const std::string& call_id, const std::string& name,
                                          const std::string& result_json) {
    cJSON* root = cJSON_CreateObject();
    cJSON* tr = cJSON_AddObjectToObject(root, "toolResponse");
    cJSON* fr = cJSON_CreateObject();
    if (!call_id.empty())
        cJSON_AddStringToObject(fr, "id", call_id.c_str());
    cJSON_AddStringToObject(fr, "name", name.c_str());
    cJSON* resp = cJSON_Parse(result_json.c_str());
    cJSON_AddItemToObject(fr, "response", resp ? resp : cJSON_CreateObject());
    cJSON_AddItemToArray(cJSON_AddArrayToObject(tr, "functionResponses"), fr);
    char* s = cJSON_PrintUnformatted(root);
    SendText(s);
    cJSON_free(s);
    cJSON_Delete(root);
}

void GeminiLiveProtocol::EmitTts(const char* state, const std::string& text) {
    if (strcmp(state, "start") == 0 || strcmp(state, "stop") == 0) {
        bool talking = state[2] == 'a';  // "start"
        Application::GetInstance().Schedule([talking]() {
            auto display = Board::GetInstance().GetDisplay();
            DisplayLockGuard lock(display);
            assistant::FaceSetTalking(talking);
        });
    }
    if (on_incoming_json_ == nullptr)
        return;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "tts");
    cJSON_AddStringToObject(root, "state", state);
    if (!text.empty())
        cJSON_AddStringToObject(root, "text", text.c_str());
    on_incoming_json_(root);
    cJSON_Delete(root);
}

void GeminiLiveProtocol::EmitStt(const std::string& text) {
    if (on_incoming_json_ == nullptr)
        return;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "stt");
    cJSON_AddStringToObject(root, "text", text.c_str());
    on_incoming_json_(root);
    cJSON_Delete(root);
}

void GeminiLiveProtocol::EmitEmotion(const char* emotion) {
    if (on_incoming_json_ == nullptr)
        return;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "llm");
    cJSON_AddStringToObject(root, "emotion", emotion);
    on_incoming_json_(root);
    cJSON_Delete(root);
}
