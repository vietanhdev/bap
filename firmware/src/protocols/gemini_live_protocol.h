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

#include "protocol.h"
#include "web_socket.h"

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

private:
    bool SendText(const std::string& text) override;

    std::string BuildSetupMessage();
    std::string BuildToolDeclarations();
    void HandleMessage(const char* data, size_t len);
    void HandleServerContent(const cJSON* content);
    void HandleToolCall(const cJSON* tool_call);
    void DeliverAudio(const std::string& b64);
    void FinishTurn(bool interrupted);
    void FlushAssistantText(bool force);
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

    std::string assistant_text_;   // pending sentence for display
    std::string last_subtitle_;
    std::string user_text_;        // accumulated input transcription for this turn
    std::chrono::steady_clock::time_point last_activity_;

    // MCP bridge: gemini function name <-> MCP tool name, pending calls by MCP request id
    std::map<std::string, std::string> fn_to_tool_;
    std::mutex calls_mutex_;
    std::map<int, std::pair<std::string, std::string>> pending_calls_;  // id -> (call_id, fn)
    int next_mcp_id_ = 1000;
};

#endif  // GEMINI_LIVE_PROTOCOL_H
