// Scroll wheel behaviour:
//   resting     : rotate = volume, double click = app launcher (rotate to choose, click to start)
//   conversation: rotate = volume (handled by the board), click = stop / start listening
// Touch screen:
//   resting: tap = the character giggles, double tap = talk, hold = menu
//   talking: tap = stop / interrupt; music or radio: double tap = stop
//   menu   : drag up/down = the list follows the finger, tap = open, hold = back
#include <esp_log.h>
#include <esp_timer.h>

#include <atomic>
#include <cstdlib>
#include <ctime>

#include <mutex>
#include <string>

#include "application.h"
#include "assets/lang_config.h"
#include "assistant.h"
#include "assistant_config.h"
#include "assistant_menu.h"
#include "board.h"
#include "boards/common/backlight.h"
#include "display/display.h"

namespace assistant {
namespace {

constexpr int64_t kMenuTimeoutUs = 8 * 1000000LL;
std::mutex g_mutex;
bool g_apps_menu = false;
int g_app_index = 0;
int64_t g_last_input_us = 0;
std::atomic<int64_t> g_activity_us{0};
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
    Show(Localize("Ứng dụng") + " " + std::to_string(g_app_index + 1) + "/" +
             std::to_string(apps.size()),
         apps[g_app_index].title + " · " + Localize("bấm để mở"), "thinking");
}

}  // namespace

std::atomic<bool> g_screen_off{false};
static void (*g_panel_recovery)() = nullptr;

void SetScreenOff(bool off) { g_screen_off = off; }
bool ScreenIsOff() { return g_screen_off; }

void NoteActivity() {
    g_activity_us = esp_timer_get_time();
    // Wake the backlight at once (the scheduler only checks once a second).
    // Only the backlight goes dark while resting. Waking sets the panel up again first, so the
    // backlight never comes back over a blank panel.
    if (g_screen_off.exchange(false)) {
        Application::GetInstance().Schedule([]() {
            if (g_panel_recovery != nullptr)
                g_panel_recovery();
            if (auto backlight = Board::GetInstance().GetBacklight())
                backlight->RestoreBrightness();
        });
    }
}

void SetPanelRecovery(void (*recovery)()) { g_panel_recovery = recovery; }

int SecondsSinceActivity() { return (int)((esp_timer_get_time() - g_activity_us) / 1000000); }

bool OnWheelRotate(bool clockwise) {
    if (ScreenIsOff()) {
        NoteActivity();  // the first turn or press only wakes the screen
        return true;
    }
    NoteActivity();
    auto& app = Application::GetInstance();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last_input_us = esp_timer_get_time();
    if (g_apps_menu) {
        g_app_index += clockwise ? 1 : -1;
        ShowApp();
        return true;
    }
    return false;  // resting or talking: volume (the board handles it)
}

bool OnWheelClick() {
    if (ScreenIsOff()) {
        NoteActivity();  // the first turn or press only wakes the screen
        return true;
    }
    NoteActivity();
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
    NoteActivity();
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
    bool dragging = false;
    bool edge = false;  // started on the outer rim of the round glass
};
Gesture g_touch;
esp_timer_handle_t g_giggle_timer = nullptr;

void Giggle() {
    auto& app = Application::GetInstance();
    app.Schedule([]() { Board::GetInstance().GetDisplay()->SetEmotion("laughing"); });
    // Called from the touch task and the camera task: create the timer exactly once.
    static std::once_flag once;
    std::call_once(once, []() {
        esp_timer_create_args_t args = {};
        args.callback = [](void*) {
            Application::GetInstance().Schedule([]() {
                if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle)
                    Board::GetInstance().GetDisplay()->SetEmotion("relaxed");
            });
        };
        args.name = "giggle";
        esp_timer_create(&args, &g_giggle_timer);
    });
    esp_timer_stop(g_giggle_timer);
    esp_timer_start_once(g_giggle_timer, 1500 * 1000);
}
}  // namespace

void OnTouch(TouchEvent event, int x, int y) {
    constexpr int64_t kHoldUs = 700 * 1000, kDoubleTapUs = 380 * 1000;
    // Holding the Watcher in a hand rests fingers on the rim of the glass: on device logs every
    // such contact sat 165-200 px from the centre of the 412x412 screen, while deliberate taps
    // land on the face. Still touches and holds that start on the rim are ignored; swipes from
    // the rim still count.
    constexpr int kCenter = 206, kRimRadius = 165;
    constexpr int kSwipe = 60, kSlop = 25;
    auto& app = Application::GetInstance();
    const int64_t now = esp_timer_get_time();
    Gesture& t = g_touch;
    if (event == TouchEvent::kDown) {
        t.down_us = now;
        t.x0 = t.x = x;
        t.y0 = t.y = y;
        t.held = false;
        t.dragging = false;
        t.edge =
            (x - kCenter) * (x - kCenter) + (y - kCenter) * (y - kCenter) > kRimRadius * kRimRadius;
        if (!t.edge)
            NoteActivity();
        return;
    }
    t.x = x;
    t.y = y;
    const int dx = t.x - t.x0, dy = t.y - t.y0;
    const bool still = abs(dx) < kSlop && abs(dy) < kSlop;
    if (t.edge && still && !t.dragging)
        return;  // a finger resting on the rim
    if (event == TouchEvent::kUp && ScreenIsOff()) {
        NoteActivity();  // a dark screen: this touch only wakes it
        return;
    }
    NoteActivity();
    if (event == TouchEvent::kMove) {
        if (!t.held && !still && MenuIsOpen() && (t.dragging || abs(dy) >= abs(dx))) {
            t.dragging = true;
            MenuDrag(dy);  // the list follows the finger
            return;
        }
        if (!t.held && !t.dragging && still && now - t.down_us > kHoldUs) {
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
    if (t.held)
        return;
    ESP_LOGI("Touch", "up at %d,%d (moved %d,%d)", x, y, dx, dy);
    const bool vertical = abs(dy) > kSwipe && abs(dy) > abs(dx);
    const bool sideways = abs(dx) > kSwipe && abs(dx) > abs(dy);
    if (t.dragging) {
        t.dragging = false;
        MenuDragEnd();
        return;
    }
    if (MenuIsOpen()) {
        if (vertical)
            MenuRotate(dy < 0);
        else if (sideways)
            MenuRotate(dx < 0);
        else if (still && y < 150)
            MenuRotate(false);  // tap the faded item above: select it
        else if (still && y > 262)
            MenuRotate(true);  // ... or the one below
        else if (still)
            MenuClick();  // tap the highlighted pill: open
        return;
    }
    switch (app.GetDeviceState()) {
        case kDeviceStateIdle:
            if (still) {
                if (now - t.last_tap_us < kDoubleTapUs) {
                    t.last_tap_us = 0;
                    app.Schedule([]() { Application::GetInstance().ToggleChatState(); });
                } else {
                    t.last_tap_us = now;
                    Giggle();
                    // With voice wake-up off by default, a single tap teaches how to start.
                    static int64_t last_hint_us = -60LL * 1000 * 1000;
                    if (now - last_hint_us > 20LL * 1000 * 1000) {
                        last_hint_us = now;
                        app.Schedule([]() {
                            Board::GetInstance().GetDisplay()->ShowNotification(
                                Localize("Chạm 2 lần để nói chuyện"), 1800);
                        });
                    }
                }
            }
            break;
        case kDeviceStateListening:
        case kDeviceStateSpeaking:
            // A stray touch must not end the conversation: one tap interrupts the answer (the
            // session keeps listening), two taps end the session.
            if (!still)
                break;
            if (now - t.last_tap_us < kDoubleTapUs) {
                t.last_tap_us = 0;
                app.Schedule([]() { Application::GetInstance().ToggleChatState(); });
            } else {
                t.last_tap_us = now;
                bool speaking = app.GetDeviceState() == kDeviceStateSpeaking;
                app.Schedule([speaking]() {
                    auto& a = Application::GetInstance();
                    if (speaking && a.GetDeviceState() == kDeviceStateSpeaking)
                        a.AbortSpeaking(kAbortReasonNone);
                    Board::GetInstance().GetDisplay()->ShowNotification(
                        Localize("Chạm 2 lần để kết thúc"), 1500);
                });
            }
            break;
        case kDeviceStateNotifying:
            // Music: a stray touch should not stop it; double tap does.
            if (!still)
                break;
            if (now - t.last_tap_us < kDoubleTapUs) {
                t.last_tap_us = 0;
                app.StopAll();
            } else {
                t.last_tap_us = now;
                app.Schedule([]() {
                    Board::GetInstance().GetDisplay()->ShowNotification(
                        Localize("Chạm 2 lần để dừng nhạc"), 2000);
                });
            }
            break;
        default:
            break;
    }
}

namespace {
void (*g_wake_handler)() = nullptr;
int64_t g_last_present_us = 0;
}  // namespace

void SetWakeHandler(void (*handler)()) { g_wake_handler = handler; }

namespace {
void (*g_vision_handler)(bool) = nullptr;
}
void SetVisionHandler(void (*handler)(bool)) { g_vision_handler = handler; }
void ApplyVision(bool enabled) {
    if (g_vision_handler != nullptr)
        g_vision_handler(enabled);
}

static bool NightNow() {
    time_t now = time(nullptr);
    if (now < 1700000000)
        return false;
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_hour >= 22 || t.tm_hour < 6;
}

void OnPersonPresent() {
    NoteActivity();
    int64_t now = esp_timer_get_time();
    if (now - g_last_present_us < 2000000)
        return;  // at most every 2 s
    g_last_present_us = now;
    if (g_wake_handler != nullptr && !NightNow() && LoadConfig().presence)
        g_wake_handler();
}

// Morning briefing once a day, the first time someone shows up between 6:00 and 10:00.
static bool MorningBriefing() {
    static int done_yday = -1;
    time_t now = time(nullptr);
    if (now < 1700000000)
        return false;
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_hour < 6 || t.tm_hour >= 10 || t.tm_yday == done_yday)
        return false;
    done_yday = t.tm_yday;
    for (auto& a : Apps()) {
        if (a.id != "morning_news")
            continue;
        Say("<hệ thống>Người dùng vừa xuất hiện trước máy lần đầu trong buổi sáng hôm nay. " +
            a.prompt + "</hệ thống>");
        return true;
    }
    return false;
}

bool OnPersonArrived(int count) {
    auto c = LoadConfig();
    if (NightNow())
        return false;  // never wake anyone up at night
    if (c.presence && c.morning &&
        Application::GetInstance().GetDeviceState() == kDeviceStateIdle && MorningBriefing())
        return false;  // the briefing is the greeting
    if (c.presence) {
        if (g_wake_handler != nullptr)
            g_wake_handler();
        Application::GetInstance().Schedule([count]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() != kDeviceStateIdle)
                return;
            auto display = Board::GetInstance().GetDisplay();
            display->SetEmotion("happy");
            std::string hi = Localize("Chào bạn!");
            if (count > 1)
                hi = Localize("Chào cả nhà!");
            display->SetChatMessage("system", hi.c_str());
        });
        Giggle();  // back to the resting face after a moment
    }
    return c.greet;
}

bool QuietHours() {
    if (!NightNow())
        return false;
    return LoadConfig().quiet;
}

namespace {
std::atomic<int> g_wake_word{-1};  // -1 = not read from NVS yet
}

bool WakeWordAllowed() {
    if (g_wake_word < 0)
        g_wake_word = LoadConfig().wake_word ? 1 : 0;
    return g_wake_word == 1 && !QuietHours();
}

void ApplyWakeWord(bool enabled) {
    int want = enabled ? 1 : 0;
    if (g_wake_word.exchange(want) == want)
        return;
    Application::GetInstance().Schedule([]() {
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateIdle)
            app.GetAudioService().EnableWakeWordDetection(WakeWordAllowed());
    });
}

namespace {
std::atomic<bool> g_ota_busy{false};
}
bool OtaInProgress() { return g_ota_busy; }
void SetOtaInProgress(bool busy) {
    g_ota_busy = busy;
    if (busy)
        SetBusy(true);
}

}  // namespace assistant
