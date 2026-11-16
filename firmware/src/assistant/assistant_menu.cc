// On-device menu for the scroll wheel, drawn over everything on the round screen.
//   hold the wheel  : open the menu / go back one level
//   rotate          : move between items (or change a value)
//   click           : open the item
// Layout (412x412 round): an arc along the rim shows the position in the list (or the value of a
// slider), the title sits at the top, the selected item is in a pill in the middle with its
// neighbours faded above and below, and a short hint sits at the bottom.
#include "assistant_menu.h"

#include <esp_app_desc.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <lvgl.h>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "application.h"
#include "assistant.h"
#include "assistant_config.h"
#include "audio/audio_codec.h"
#include "board.h"
#include "boards/common/backlight.h"
#include "boards/common/wifi_board.h"
#include "display/display.h"
#include "wifi_manager.h"

LV_FONT_DECLARE(font_chat_24);

namespace assistant {
namespace {

constexpr int64_t kIdleCloseUs = 20 * 1000000LL;
constexpr int kArcStart = 120;  // degrees, LVGL: 0 = 3 o'clock, clockwise
constexpr int kArcSweep = 300;  // open at the bottom where the hint sits

constexpr uint32_t kBg = 0x15131F;
constexpr uint32_t kText = 0xF4F0FF;
constexpr uint32_t kMuted = 0x8C86A8;
constexpr uint32_t kDim = 0x6E6890;
constexpr uint32_t kAccent = 0x9D86FF;
constexpr uint32_t kPill = 0x2E2850;
constexpr uint32_t kTrack = 0x262238;
constexpr uint32_t kDanger = 0xFF7A90;

struct Item {
    std::string label;
    std::function<void()> on_click;
    bool danger = false;
    std::function<std::string()> value;  // shown after the label (toggles)
};

struct Level {
    std::string title;
    std::vector<Item> items;
    int index = 0;
    // Slider mode (volume / brightness): rotation changes the value instead of the selection.
    std::function<int()> get_value;
    std::function<void(int)> set_value;
    std::string info;  // read-only text page
};

std::recursive_mutex g_mutex;
std::vector<Level> g_stack;
std::function<bool()> g_power_off;
int64_t g_last_us = 0;

lv_obj_t* g_overlay = nullptr;
lv_obj_t* g_arc = nullptr;
lv_obj_t* g_title = nullptr;
lv_obj_t* g_pill = nullptr;
lv_obj_t* g_prev = nullptr;
lv_obj_t* g_current = nullptr;
lv_obj_t* g_next = nullptr;
lv_obj_t* g_counter = nullptr;
lv_obj_t* g_hint = nullptr;
lv_obj_t* g_info = nullptr;

lv_obj_t* Label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int width) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return l;
}

void BuildOverlay() {
    if (g_overlay != nullptr) return;
    const int w = LV_HOR_RES, h = LV_VER_RES;
    const lv_font_t* big = lv_obj_get_style_text_font(lv_screen_active(), LV_PART_MAIN);
    g_overlay = lv_obj_create(lv_screen_active());  // on the screen (not layer_top) so /screen.jpg sees it
    lv_obj_remove_style_all(g_overlay);
    lv_obj_set_size(g_overlay, w, h);
    lv_obj_set_style_bg_color(g_overlay, lv_color_hex(kBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);

    g_arc = lv_arc_create(g_overlay);
    lv_obj_set_size(g_arc, w - 16, h - 16);
    lv_obj_center(g_arc);
    lv_arc_set_bg_angles(g_arc, kArcStart, kArcStart + kArcSweep);
    lv_obj_remove_style(g_arc, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(g_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(g_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_arc, lv_color_hex(kTrack), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_arc, lv_color_hex(kAccent), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_arc, true, LV_PART_INDICATOR);

    g_title = Label(g_overlay, &font_chat_24, kAccent, w * 55 / 100);
    lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, h * 13 / 100);

    g_pill = lv_obj_create(g_overlay);
    lv_obj_remove_style_all(g_pill);
    lv_obj_set_size(g_pill, w * 74 / 100, big->line_height + 28);
    lv_obj_center(g_pill);
    lv_obj_set_style_radius(g_pill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_pill, lv_color_hex(kPill), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_pill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_pill, lv_color_hex(kAccent), LV_PART_MAIN);
    lv_obj_set_style_border_width(g_pill, 2, LV_PART_MAIN);

    const int pill_half = (big->line_height + 28) / 2;
    g_current = Label(g_overlay, big, kText, w * 68 / 100);
    lv_obj_align(g_current, LV_ALIGN_CENTER, 0, 0);
    g_prev = Label(g_overlay, &font_chat_24, kMuted, w * 62 / 100);
    lv_obj_set_height(g_prev, font_chat_24.line_height);
    lv_obj_align(g_prev, LV_ALIGN_CENTER, 0, -(pill_half + 26));
    g_next = Label(g_overlay, &font_chat_24, kMuted, w * 62 / 100);
    lv_obj_set_height(g_next, font_chat_24.line_height);
    lv_obj_align(g_next, LV_ALIGN_CENTER, 0, pill_half + 26);
    g_counter = Label(g_overlay, &font_chat_24, kDim, w * 30 / 100);
    lv_obj_align(g_counter, LV_ALIGN_TOP_MID, 0, h * 13 / 100 + 30);
    g_hint = Label(g_overlay, &font_chat_24, kDim, w * 68 / 100);
    lv_obj_set_height(g_hint, font_chat_24.line_height);  // one line, never wraps
    lv_obj_align(g_hint, LV_ALIGN_BOTTOM_MID, 0, -h * 10 / 100);

    g_info = Label(g_overlay, &font_chat_24, kText, w * 70 / 100);
    lv_label_set_long_mode(g_info, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_line_space(g_info, 6, LV_PART_MAIN);
    lv_obj_align(g_info, LV_ALIGN_CENTER, 0, 0);

    lv_obj_fade_in(g_overlay, 120, 0);
}

void Show(lv_obj_t* obj, bool visible) {
    if (visible)
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

std::string LabelOf(const Item& item) {
    return item.value ? item.label + ": " + item.value() : item.label;
}

void Render() {
    auto display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);
    std::lock_guard<std::recursive_mutex> guard(g_mutex);
    if (g_stack.empty()) {
        if (g_overlay != nullptr) {
            lv_obj_delete(g_overlay);
            g_overlay = nullptr;
        }
        return;
    }
    BuildOverlay();
    lv_obj_move_foreground(g_overlay);
    Level& level = g_stack.back();
    const bool info = !level.info.empty();
    const bool slider = static_cast<bool>(level.get_value);
    lv_label_set_text(g_title, level.title.c_str());
    Show(g_info, info);
    Show(g_pill, !info);
    Show(g_current, !info);
    Show(g_prev, !info && !slider);
    Show(g_next, !info && !slider);
    Show(g_counter, !info && !slider);
    const char* back = g_stack.size() > 1 ? "giữ quay lại" : "giữ thoát";

    if (info) {
        lv_label_set_text(g_info, level.info.c_str());
        lv_arc_set_angles(g_arc, kArcStart, kArcStart + kArcSweep);
        lv_label_set_text_fmt(g_hint, "bấm xong · %s", back);
        return;
    }
    if (slider) {
        int v = level.get_value();
        lv_label_set_text_fmt(g_current, "%d%%", v);
        lv_obj_set_style_text_color(g_current, lv_color_hex(kText), LV_PART_MAIN);
        lv_obj_set_style_border_color(g_pill, lv_color_hex(kAccent), LV_PART_MAIN);
        lv_arc_set_angles(g_arc, kArcStart, kArcStart + kArcSweep * v / 100);
        lv_label_set_text(g_hint, "xoay chỉnh · bấm xong");
        return;
    }
    const int n = (int)level.items.size();
    auto at = [&](int i) { return LabelOf(level.items[((i % n) + n) % n]); };
    const Item& item = level.items[level.index];
    lv_label_set_text(g_prev, n > 2 ? at(level.index - 1).c_str() : "");
    lv_label_set_text(g_current, LabelOf(item).c_str());
    lv_label_set_text(g_next, n > 1 ? at(level.index + 1).c_str() : "");
    lv_label_set_text_fmt(g_counter, "%d / %d", level.index + 1, n);
    uint32_t accent = item.danger ? kDanger : kAccent;
    lv_obj_set_style_text_color(g_current, lv_color_hex(item.danger ? kDanger : kText), LV_PART_MAIN);
    lv_obj_set_style_border_color(g_pill, lv_color_hex(accent), LV_PART_MAIN);
    // One segment of the rim per item; the lit one is the selection.
    int seg = kArcSweep / n;
    int from = kArcStart + seg * level.index;
    lv_arc_set_angles(g_arc, from, from + (seg > 8 ? seg - 2 : seg));
    lv_obj_set_style_arc_color(g_arc, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_label_set_text_fmt(g_hint, "bấm chọn · %s", back);
}

void Push(Level level) {
    {
        std::lock_guard<std::recursive_mutex> guard(g_mutex);
        g_stack.push_back(std::move(level));
        g_last_us = esp_timer_get_time();
    }
    Render();
}

void CloseAll() {
    {
        std::lock_guard<std::recursive_mutex> guard(g_mutex);
        g_stack.clear();
    }
    Render();
}

// Runs an action after the menu has closed (on the main task).
std::function<void()> Then(std::function<void()> action) {
    return [action]() {
        CloseAll();
        Application::GetInstance().Schedule(std::function<void()>(action));
    };
}

Level Confirm(const std::string& question, std::function<void()> yes) {
    Level l{.title = question};
    l.items.push_back({"Thôi", []() { MenuBack(); }});
    l.items.push_back({"Đồng ý", Then(std::move(yes)), true});
    return l;
}

Item Toggle(const std::string& label, bool Config::* field) {
    return Item{label,
                [field]() {
                    auto c = LoadConfig();
                    c.*field = !(c.*field);
                    SaveConfig(c);
                    Render();
                },
                false, [field]() { return std::string(LoadConfig().*field ? "Bật" : "Tắt"); }};
}

std::string DeviceInfo() {
    auto& wifi = WifiManager::GetInstance();
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    int battery = -1;
    bool charging = false, discharging = false;
    Board::GetInstance().GetBatteryLevel(battery, charging, discharging);
    char buf[300];
    snprintf(buf, sizeof(buf), "bap-%02x%02x.local\n%s\nWi-Fi: %s\nPin %d%%%s · bản %s", mac[4], mac[5],
             wifi.GetIpAddress().c_str(), wifi.IsConnected() ? wifi.GetSsid().c_str() : "chưa nối", battery,
             charging ? " (đang sạc)" : "", esp_app_get_description()->version);
    return buf;
}

Level Slider(const std::string& title, std::function<int()> get, std::function<void(int)> set) {
    Level l{.title = title};
    l.get_value = std::move(get);
    l.set_value = std::move(set);
    return l;
}

Level SettingsMenu() {
    Level m{.title = "Thiết lập"};
    m.items.push_back({"Âm lượng", []() {
                           Push(Slider(
                               "Âm lượng", []() { return Board::GetInstance().GetAudioCodec()->output_volume(); },
                               [](int v) { Board::GetInstance().GetAudioCodec()->SetOutputVolume(v); }));
                       }});
    m.items.push_back({"Độ sáng", []() {
                           Push(Slider(
                               "Độ sáng",
                               []() {
                                   auto bl = Board::GetInstance().GetBacklight();
                                   return bl ? (int)bl->brightness() : 0;
                               },
                               [](int v) {
                                   if (auto bl = Board::GetInstance().GetBacklight()) bl->SetBrightness(v, true);
                               }));
                       }});
    m.items.push_back(Toggle("Chế độ đêm", &Config::night_dim));
    m.items.push_back(Toggle("Thấy người thì chào", &Config::greet));
    m.items.push_back({"Thông tin máy", []() {
                           Level l{.title = "Thông tin máy"};
                           l.info = DeviceInfo();
                           Push(std::move(l));
                       }});
    m.items.push_back({"Cài Wi-Fi mới", []() {
                           Push(Confirm("Cài Wi-Fi mới?", []() {
                               static_cast<WifiBoard&>(Board::GetInstance()).EnterWifiConfigMode();
                           }));
                       }});
    m.items.push_back({"Khởi động lại", []() {
                           Push(Confirm("Khởi động lại?", []() { Application::GetInstance().Reboot(); }));
                       }});
    return m;
}

Level MainMenu() {
    Level m{.title = "Menu"};
    m.items.push_back({"Trò chuyện", Then([]() { Application::GetInstance().ToggleChatState(); })});
    m.items.push_back({"Nhân vật", []() {
                           Level l{.title = "Nhân vật"};
                           std::string active = LoadConfig().character;
                           int i = 0;
                           for (auto& c : Characters()) {
                               if (c.id == active) l.index = i;
                               ++i;
                               l.items.push_back({c.name, Then([id = c.id]() {
                                                      SetActiveCharacter(id);
                                                      Board::GetInstance().GetDisplay()->SetEmotion("happy");
                                                  })});
                           }
                           if (!l.items.empty()) Push(std::move(l));
                       }});
    m.items.push_back({"Ứng dụng", []() {
                           Level l{.title = "Ứng dụng"};
                           for (auto& a : Apps()) {
                               l.items.push_back({a.title, Then([title = a.title, prompt = a.prompt]() {
                                                      Say("<hệ thống>Người dùng vừa mở ứng dụng \"" + title +
                                                          "\". " + prompt + "</hệ thống>");
                                                  })});
                           }
                           if (!l.items.empty()) Push(std::move(l));
                       }});
    m.items.push_back({"Radio", []() {
                           Level l{.title = "Radio"};
                           for (auto& st : Stations()) {
                               l.items.push_back({st.name, Then([url = st.url, name = st.name]() {
                                                      Application::GetInstance().PlayStream("radio:" + url, name);
                                                  })});
                           }
                           if (!l.items.empty()) Push(std::move(l));
                       }});
    m.items.push_back({"Thiết lập", []() { Push(SettingsMenu()); }});
    m.items.push_back({"Tắt máy", []() {
                           Push(Confirm("Tắt máy?", []() {
                               if (!g_power_off || !g_power_off()) {
                                   Board::GetInstance().GetDisplay()->ShowNotification(
                                       "Đang cắm điện: rút cáp để tắt hẳn", 4000);
                               }
                           }));
                       },
                       true});
    return m;
}

}  // namespace

bool MenuIsOpen() {
    std::lock_guard<std::recursive_mutex> guard(g_mutex);
    return !g_stack.empty();
}

void MenuOpen() {
    if (MenuIsOpen()) return;
    Push(MainMenu());
}

void MenuBack() {
    {
        std::lock_guard<std::recursive_mutex> guard(g_mutex);
        if (!g_stack.empty()) g_stack.pop_back();
        g_last_us = esp_timer_get_time();
    }
    Render();
}

void MenuRotate(bool forward) {
    {
        std::lock_guard<std::recursive_mutex> guard(g_mutex);
        if (g_stack.empty()) return;
        g_last_us = esp_timer_get_time();
        Level& level = g_stack.back();
        if (level.get_value) {
            // Same direction as the board's volume knob.
            int v = level.get_value() + (forward ? -5 : 5);
            level.set_value(v < 0 ? 0 : (v > 100 ? 100 : v));
        } else if (!level.items.empty()) {
            int n = (int)level.items.size();
            level.index = ((level.index + (forward ? 1 : -1)) % n + n) % n;
        }
    }
    Render();
}

void MenuClick() {
    std::function<void()> action;
    {
        std::lock_guard<std::recursive_mutex> guard(g_mutex);
        if (g_stack.empty()) return;
        g_last_us = esp_timer_get_time();
        Level& level = g_stack.back();
        if (level.get_value || !level.info.empty()) {
            action = []() { MenuBack(); };
        } else if (!level.items.empty()) {
            action = level.items[level.index].on_click;
        }
    }
    if (action) action();
}

void MenuTick() {
    bool expired;
    {
        std::lock_guard<std::recursive_mutex> guard(g_mutex);
        expired = !g_stack.empty() && esp_timer_get_time() - g_last_us > kIdleCloseUs;
    }
    if (expired) CloseAll();
}

void SetPowerOffHandler(std::function<bool()> handler) { g_power_off = std::move(handler); }

}  // namespace assistant
