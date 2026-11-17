#ifndef GEMINI_LIVE_PROTOCOL_H
#define GEMINI_LIVE_PROTOCOL_H

// Direct device -> Gemini Live API (BidiGenerateContent over WebSocket). No relay server.
// Uplink: raw PCM16 16 kHz mono (AudioService raw mode). Downlink: raw PCM16 24 kHz.
// Translates Gemini events into the XiaoZhi JSON messages the Application already handles
// (tts/stt/llm), and bridges the on-device MCP tools to Gemini function calling.

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "protocol.h"
#include "web_socket.h"

namespace assistant {
struct Config;
}

class GeminiLiveProtocol : public Protocol {
public:
    GeminiLiveProtocol();
    ~GeminiLiveProtocol() override;

    bool Start() override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel(bool send_goodbye = true) override;
    bool IsAudioChannelOpened() const override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;

    void SendWakeWordDetected(const std::string& wake_word) override;
    void SendStartListening(ListeningMode mode) override;
    void SendStopListening() override;
    void SendAbortSpeaking(AbortReason reason) override;
    void SendMcpMessage(const std::string& message) override;

    static GeminiLiveProtocol* instance() { return instance_; }
    // Inject text as if the user said it (system events, web UI, camera detection).
    bool SendUserText(const std::string& text);
    // Send one JPEG frame for the model to look at.
    bool SendImage(const uint8_t* jpeg, size_t len);
    // Attaches a JPEG to the next tool response (a multimodal function response). Gemini Live
    // ignores a lone video frame sent while it waits for a tool, but sees an image inside the
    // functionResponse itself.
    bool AttachImageToNextToolResponse(const uint8_t* jpeg, size_t len);

    // Sessions use the free key; the paid key only while the free one rests after a rejection.
    // Free-tier Live quota is tokens per minute, so a rest is minutes, not the rest of the day.
    struct KeyStats {
        bool last_paid;     // the latest session used the paid key
        int free_sessions;  // sessions opened since boot, per key
        int paid_sessions;
        int free_rest_s;  // seconds until the free key is tried again (0 = free key in use)
    };
    KeyStats GetKeyStats() const;

private:
    bool SendText(const std::string& text) override;

    enum class OpenResult { kOpened, kConnectFailed, kRejected };
    OpenResult TryOpen(const assistant::Config& cfg, bool paid);  // one key; retries stale resume
    void RestFreeKey();
    bool FreeKeyResting() const;

    std::string BuildSetupMessage();
    std::string BuildToolDeclarations();
    void HandleMessage(const char* data, size_t len);
    void HandleServerContent(const cJSON* content);
    void HandleToolCall(const cJSON* tool_call);
    void DeliverAudio(const std::string& b64);
    void FinishTurn(bool interrupted);
    void FlushAssistantText(bool force);
    bool FlushAudio();
    void ShowSubtitle(const std::string& text, size_t max_bytes);

    void EmitTts(const char* state, const std::string& text = "");
    void EmitStt(const std::string& text);
    void EmitEmotion(const char* emotion);
    const char* GuessEmotion(const std::string& sentence) const;
    void SendToolResponse(const std::string& call_id, const std::string& name,
                          const std::string& result_json);

    static GeminiLiveProtocol* instance_;
    int idle_timeout_s_ = 30;
    std::string pending_text_;  // text to send right after the session opens

    std::shared_ptr<WebSocket> websocket_;
    EventGroupHandle_t event_group_;
    std::recursive_mutex send_mutex_;

    std::atomic<bool> setup_done_{false};
    std::atomic<bool> speaking_{false};
    std::atomic<bool> drop_audio_{false};
    std::atomic<bool> close_after_turn_{false};
    std::atomic<bool> channel_open_{false};
    std::atomic<bool> closing_{false};  // we are closing on purpose (timeout, user, new session)
    int64_t last_auto_resume_us_ = 0;
    void ResumeAfterDrop();

    // Key choice (see KeyStats). Touched from the main task and the websocket task.
    std::atomic<bool> use_paid_{false};
    std::atomic<int64_t> free_retry_ms_{0};  // esp_timer ms when the free key may be tried again
    std::atomic<int> free_rest_s_{0};        // current rest length, doubles on repeated rejections
    std::atomic<int> free_sessions_{0};
    std::atomic<int> paid_sessions_{0};
    bool resume_paid_ = false;  // key that issued resume_handle_ (handles are per project)

    std::string assistant_text_;  // pending sentence for display
    std::string last_subtitle_;
    std::vector<uint8_t> pending_audio_;  // uplink PCM waiting to be batched
    std::string resume_handle_;           // Gemini session resumption handle
    size_t resume_setup_hash_ = 0;        // prompt+voice the handle was issued for
    size_t setup_hash_ = 0;               // prompt+voice of the session being opened
    std::chrono::steady_clock::time_point resume_handle_time_;
    bool resuming_ = false;
    std::string user_text_;  // accumulated input transcription for this turn
    std::chrono::steady_clock::time_point last_activity_;

    // MCP bridge: gemini function name <-> MCP tool name, pending calls by MCP request id
    std::map<std::string, std::string> fn_to_tool_;
    std::mutex calls_mutex_;
    std::string pending_image_b64_;                                     // guarded by calls_mutex_
    std::map<int, std::pair<std::string, std::string>> pending_calls_;  // id -> (call_id, fn)
    int next_mcp_id_ = 1000;
};

#endif  // GEMINI_LIVE_PROTOCOL_H
