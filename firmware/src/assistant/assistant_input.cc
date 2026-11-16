// Scroll wheel behaviour:
//   resting: rotate = pick a character, double click = app launcher (rotate to choose, click to
//   start) in a conversation: rotate = volume (handled by the board), click = stop/start listening
#include <esp_timer.h>

#include <mutex>
#include <string>

#include "application.h"
#include "assets/lang_config.h"
#include "assistant.h"
#include "assistant_config.h"
#include "assistant_menu.h"
#include "board.h"
#include "display/display.h"

namespace assistant {
namespace {

constexpr int64_t kMenuTimeoutUs = 8 * 1000000LL;
std::mutex g_mutex;
bool g_apps_menu = false;
int g_app_index = 0;
int64_t g_last_input_us = 0;
bool g_preview_shown = false;  // a character/app preview is on screen and must be cleared later

void Show(const std::string& status, const std::string& line, const char* emotion) {
    Application::GetInstance().Schedule([status, line, emotion]() {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(status.c_str());
        display->SetEmotion(emotion);
        display->SetChatMessage("system", line.c_str());
    });
}

void ShowApp() {
    auto apps = Apps();
    if (apps.empty())
        return;
    g_app_index = (g_app_index % (int)apps.size() + apps.size()) % apps.size();
    Show("Ứng dụng " + std::to_string(g_app_index + 1) + "/" + std::to_string(apps.size()),
         apps[g_app_index].title + " · bấm để mở", "thinking");
}

}  // namespace

bool OnWheelRotate(bool clockwise) {
    auto& app = Application::GetInstance();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last_input_us = esp_timer_get_time();
    if (g_apps_menu) {
        g_app_index += clockwise ? 1 : -1;
        ShowApp();
        return true;
    }
    if (app.GetDeviceState() != kDeviceStateIdle)
        return false;  // conversation: volume
    auto chars = Characters();
    if (chars.empty())
        return false;
    std::string active = LoadConfig().character;
    int index = 0;
    for (size_t i = 0; i < chars.size(); ++i) {
        if (chars[i].id == active)
            index = (int)i;
    }
    index = (index + (clockwise ? 1 : -1) + chars.size()) % chars.size();
    SetActiveCharacter(chars[index].id);
    g_preview_shown = true;
    Show(chars[index].name, chars[index].tagline, "happy");
    return true;
}

bool OnWheelClick() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_apps_menu)
        return false;
    g_apps_menu = false;
    g_preview_shown = false;
    auto apps = Apps();
    if (g_app_index < (int)apps.size()) {
        auto& a = apps[g_app_index];
        Say("<hệ thống>Người dùng vừa mở ứng dụng \"" + a.title + "\". " + a.prompt +
            "</hệ thống>");
    }
    return true;
}

void OnWheelDoubleClick() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle)
        return;
    g_apps_menu = true;
    g_preview_shown = true;
    g_last_input_us = esp_timer_get_time();
    Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
    ShowApp();
}

void WheelTick() {
    MenuTick();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_preview_shown || esp_timer_get_time() - g_last_input_us < kMenuTimeoutUs)
        return;
    g_apps_menu = false;
    g_preview_shown = false;
    if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
        Show(Lang::Strings::STANDBY, IdleLine(), "relaxed");
    }
}

}  // namespace assistant
