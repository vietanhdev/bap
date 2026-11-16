// Status cards pushed from computers on the LAN (coding-agent limits, CI, home lab alerts):
// POST /api/notify {"title","text","emotion","speak"}. The card shows on the resting screen;
// "speak" (optional) is announced by the active character.
#include <esp_http_server.h>

#include "assistant_auth.h"
#include <esp_timer.h>
#include <cJSON.h>

#include <mutex>
#include <string>

#include "application.h"
#include "assistant.h"
#include "board.h"
#include "display/display.h"

namespace assistant {
namespace {

std::mutex g_mutex;
std::string g_title, g_text, g_emotion;
int64_t g_at_us = 0;
constexpr int64_t kFreshUs = 15LL * 60 * 1000000;  // a card stays relevant for 15 minutes

std::string Field(const cJSON* obj, const char* key, size_t max) {
    auto v = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsString(v))
        return "";
    std::string s = v->valuestring;
    if (s.size() > max)
        s.resize(max);
    return s;
}

esp_err_t HandleNotify(httpd_req_t* req) {
    char header[8];
    if (httpd_req_get_hdr_value_str(req, "X-Bap", header, sizeof(header)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "missing header");
    }
    if (req->content_len == 0 || req->content_len > 2048) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "bad size");
    }
    std::string body(req->content_len, '\0');
    size_t got = 0;
    while (got < body.size()) {
        int n = httpd_req_recv(req, body.data() + got, body.size() - got);
        if (n <= 0)
            return ESP_FAIL;
        got += n;
    }
    cJSON* in = cJSON_Parse(body.c_str());
    std::string title = Field(in, "title", 40), text = Field(in, "text", 200);
    std::string emotion = Field(in, "emotion", 20), speak = Field(in, "speak", 300);
    cJSON_Delete(in);
    if (text.empty()) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "missing text");
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_title = title;
        g_text = text;
        g_emotion = emotion.empty() ? "happy" : emotion;
        g_at_us = esp_timer_get_time();
    }
    auto& app = Application::GetInstance();
    app.Schedule([title, text, emotion = g_emotion]() {
        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle)
            return;
        auto display = Board::GetInstance().GetDisplay();
        if (!title.empty())
            display->SetStatus(title.c_str());
        display->SetEmotion(emotion.c_str());
        display->SetChatMessage("system", text.c_str());
    });
    if (!speak.empty()) {
        Say("<hệ thống>Máy tính của người dùng gửi thông báo: \"" + speak +
            "\". Hãy báo lại ngắn gọn bằng giọng của bạn rồi gọi end_conversation.</hệ thống>");
    }
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

}  // namespace

std::string LatestCard() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_text.empty() || esp_timer_get_time() - g_at_us > kFreshUs)
        return "";
    return g_text;
}

void RegisterNotifyRoutes(void* server) {
    static const httpd_uri_t route = {
        .uri = "/api/notify", .method = HTTP_POST, .handler = HandleNotify, .user_ctx = nullptr};
    RegisterGuarded(server, route);
}

}  // namespace assistant
