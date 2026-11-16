// Scroll wheel behaviour:
//   resting: rotate = pick a character, double click = app launcher (rotate to choose, click to
//   start) in a conversation: rotate = volume (handled by the board), click = stop/start listening
// Touch screen:
//   resting: tap = the character giggles, double tap = talk, swipe sideways = next character,
//            hold = menu
//   talking: tap = stop / interrupt; music or radio: tap = stop
//   menu   : swipe up/down = move, tap = open, hold = back
#include <esp_log.h>
#include <esp_timer.h>

#include <cstdlib>

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

namespace {
struct Gesture {
    int64_t down_us = 0, last_tap_us = 0;
    int x0 = 0, y0 = 0, x = 0, y = 0;
    bool held = false;
};
Gesture g_touch;
esp_timer_handle_t g_giggle_timer = nullptr;

void Giggle() {
    auto& app = Application::GetInstance();
    app.Schedule([]() { Board::GetInstance().GetDisplay()->SetEmotion("laughing"); });
    if (g_giggle_timer == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = [](void*) {
            Application::GetInstance().Schedule([]() {
                if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle)
                    Board::GetInstance().GetDisplay()->SetEmotion("relaxed");
            });
        };
        args.name = "giggle";
        esp_timer_create(&args, &g_giggle_timer);
    }
    esp_timer_stop(g_giggle_timer);
    esp_timer_start_once(g_giggle_timer, 1500 * 1000);
}
}  // namespace

void OnTouch(TouchEvent event, int x, int y) {
    constexpr int64_t kHoldUs = 700 * 1000, kDoubleTapUs = 380 * 1000;
    constexpr int kSwipe = 60, kSlop = 25;
    auto& app = Application::GetInstance();
    const int64_t now = esp_timer_get_time();
    Gesture& t = g_touch;
    if (event == TouchEvent::kDown) {
        t.down_us = now;
        t.x0 = t.x = x;
        t.y0 = t.y = y;
        t.held = false;
        return;
    }
    t.x = x;
    t.y = y;
    const int dx = t.x - t.x0, dy = t.y - t.y0;
    const bool still = abs(dx) < kSlop && abs(dy) < kSlop;
    if (event == TouchEvent::kMove) {
        if (!t.held && still && now - t.down_us > kHoldUs) {
            t.held = true;
            if (MenuIsOpen()) {
                MenuBack();
            } else if (app.GetDeviceState() == kDeviceStateIdle) {
                app.PlaySound(Lang::Sounds::OGG_POPUP);
                MenuOpen();
            }
        }
        return;
    }
    // kUp
    if (t.held) return;
    ESP_LOGI("Touch", "up at %d,%d (moved %d,%d)", x, y, dx, dy);
    const bool vertical = abs(dy) > kSwipe && abs(dy) > abs(dx);
    const bool sideways = abs(dx) > kSwipe && abs(dx) > abs(dy);
    if (MenuIsOpen()) {
        if (vertical) MenuRotate(dy < 0);
        else if (sideways) MenuRotate(dx < 0);
        else if (still) MenuClick();
        return;
    }
    switch (app.GetDeviceState()) {
        case kDeviceStateIdle:
            if (sideways) {
                OnWheelRotate(dx < 0);
            } else if (still) {
                if (now - t.last_tap_us < kDoubleTapUs) {
                    t.last_tap_us = 0;
                    app.Schedule([]() { Application::GetInstance().ToggleChatState(); });
                } else {
                    t.last_tap_us = now;
                    Giggle();
                }
            }
            break;
        case kDeviceStateListening:
        case kDeviceStateSpeaking:
            if (still) app.Schedule([]() { Application::GetInstance().ToggleChatState(); });
            break;
        case kDeviceStateNotifying:
            if (still) app.StopAll();
            break;
        default:
            break;
    }
}

}  // namespace assistant
