// On-screen keyboard. The keys are painted by one LVGL object, which costs far less memory than
// one object per key, and only the keys that change are redrawn.
// The typed text is a password: it is never logged and is wiped when the keyboard closes.
#include "assistant_keyboard.h"

#include <lvgl.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "application.h"
#include "assistant_config.h"
#include "board.h"
#include "display/display.h"
#include "keyboard_layout.h"

LV_FONT_DECLARE(font_chat_24);

namespace assistant {
namespace {

constexpr uint32_t kBg = 0x15131F;
constexpr uint32_t kKey = 0x2A2640;
constexpr uint32_t kKeyWide = 0x3A3460;
constexpr uint32_t kKeyActive = 0x9D86FF;
constexpr uint32_t kText = 0xF4F0FF;
constexpr uint32_t kMuted = 0x8C86A8;
constexpr uint32_t kDanger = 0xFF7A90;
constexpr size_t kFieldChars = 18;

struct State {
    std::vector<kb::Key> keys;
    kb::Editor editor;
    std::function<void(std::string)> on_done;
    int pressed = -1, focus = -1;
    bool touching = false, field_touch = false, reveal = false;
    lv_obj_t* root = nullptr;
    lv_obj_t* board = nullptr;
    lv_obj_t* field = nullptr;
    lv_obj_t* hint = nullptr;
    lv_obj_t* bubble = nullptr;
};

State* g_state = nullptr;
std::atomic<bool> g_open{false};

lv_area_t AreaOf(const kb::Key& k) {
    lv_area_t a;
    a.x1 = k.x + 2;
    a.y1 = k.y + 2;
    a.x2 = k.x + k.w - 3;
    a.y2 = k.y + k.h - 3;
    return a;
}

void InvalidateKey(int index) {
    if (g_state == nullptr || index < 0 || index >= (int)g_state->keys.size())
        return;
    lv_area_t a = AreaOf(g_state->keys[index]);
    lv_obj_invalidate_area(g_state->board, &a);
}

void PaintKeys(lv_event_t* e) {
    if (g_state == nullptr)
        return;
    lv_layer_t* layer = lv_event_get_layer(e);
    const int page = g_state->editor.page;
    for (size_t i = 0; i < g_state->keys.size(); ++i) {
        const kb::Key& k = g_state->keys[i];
        const bool active = (int)i == g_state->pressed || (int)i == g_state->focus;
        const bool shift_on = k.code == kb::kShift && page == kb::kUpper;
        lv_area_t a = AreaOf(k);

        lv_draw_rect_dsc_t rect;
        lv_draw_rect_dsc_init(&rect);
        rect.radius = 8;
        rect.bg_opa = LV_OPA_COVER;
        rect.bg_color = lv_color_hex(active || shift_on ? kKeyActive
                                     : k.code < ' '     ? kKeyWide
                                                        : kKey);
        lv_draw_rect(layer, &rect, &a);

        const std::string label = kb::KeyLabel(k, page);
        if (label.empty())
            continue;
        lv_draw_label_dsc_t text;
        lv_draw_label_dsc_init(&text);
        text.font = &font_chat_24;
        text.color = lv_color_hex(active || shift_on ? kBg : kText);
        text.align = LV_TEXT_ALIGN_CENTER;
        text.text = label.c_str();
        text.text_local = 1;  // LVGL paints later, after this string is gone
        a.y1 += (lv_area_get_height(&a) - lv_font_get_line_height(&font_chat_24)) / 2;
        lv_draw_label(layer, &text, &a);
    }
}

void UpdateField() {
    const std::string& text = g_state->editor.text;
    std::string shown;
    if (g_state->reveal) {
        shown = text;
    } else if (!text.empty()) {
        // Like a phone: only the last character stays readable.
        shown.assign(text.size() - 1, '*');
        shown.push_back(text.back());
    }
    if (shown.size() > kFieldChars)
        shown = shown.substr(shown.size() - kFieldChars);
    lv_label_set_text(g_state->field, shown.c_str());
    std::fill(shown.begin(), shown.end(), '\0');
}

void SetHint(const char* vietnamese, uint32_t color) {
    lv_label_set_text(g_state->hint, Localize(vietnamese).c_str());
    lv_obj_set_style_text_color(g_state->hint, lv_color_hex(color), LV_PART_MAIN);
}

void ShowBubble(int index) {
    if (index < 0 || g_state->keys[index].code < ' ' || g_state->keys[index].code == ' ') {
        lv_obj_add_flag(g_state->bubble, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const kb::Key& k = g_state->keys[index];
    const char text[2] = {k.code, '\0'};
    lv_label_set_text(g_state->bubble, text);
    lv_obj_set_pos(g_state->bubble, k.x + k.w / 2 - 28, std::max(k.y - 62, 40));
    lv_obj_remove_flag(g_state->bubble, LV_OBJ_FLAG_HIDDEN);
}

void Press(int index) {
    if (index < 0 || index >= (int)g_state->keys.size())
        return;
    const char code = g_state->keys[index].code;
    const int page = g_state->editor.page;
    const bool finished = g_state->editor.Press(code);
    if (code == kb::kDone && !finished) {
        SetHint("Cần 8 đến 64 ký tự", kDanger);
        return;
    }
    if (finished) {
        auto on_done = std::move(g_state->on_done);
        std::string text = g_state->editor.text;
        KeyboardClose();
        Application::GetInstance().Schedule([on_done = std::move(on_done), text]() mutable {
            if (on_done)
                on_done(text);
            std::fill(text.begin(), text.end(), '\0');
        });
        return;
    }
    if (g_state->editor.page != page) {
        g_state->keys = kb::Layout(g_state->editor.page);
        g_state->focus = std::min(g_state->focus, (int)g_state->keys.size() - 1);
        lv_obj_invalidate(g_state->board);
    }
    SetHint("Chạm đây để hiện chữ", kMuted);
    UpdateField();
}

void SetPressed(int index) {
    if (index == g_state->pressed)
        return;
    InvalidateKey(g_state->pressed);
    g_state->pressed = index;
    InvalidateKey(index);
    ShowBubble(index);
}

lv_obj_t* AddLabel(lv_obj_t* parent, int width, uint32_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &font_chat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(l, lv_font_get_line_height(&font_chat_24));
    lv_label_set_text(l, "");
    return l;
}

}  // namespace

bool KeyboardIsOpen() { return g_open; }

void KeyboardOpen(const std::string& title, std::function<void(std::string)> on_done) {
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if (g_state != nullptr)
        return;
    g_state = new State();
    g_state->on_done = std::move(on_done);
    g_state->keys = kb::Layout(kb::kLower);

    lv_obj_t* root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(root, lv_color_hex(kBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_state->root = root;

    lv_obj_t* name = AddLabel(root, 200, kKeyActive);
    lv_label_set_text(name, title.c_str());
    lv_obj_set_pos(name, (LV_HOR_RES - 200) / 2, 32);

    g_state->field = AddLabel(root, 290, kText);
    lv_obj_set_pos(g_state->field, (LV_HOR_RES - 290) / 2, 62);

    g_state->hint = AddLabel(root, 250, kMuted);
    lv_obj_set_pos(g_state->hint, (LV_HOR_RES - 250) / 2, 92);
    SetHint("Nhập mật khẩu Wi-Fi", kMuted);

    g_state->board = lv_obj_create(root);
    lv_obj_remove_style_all(g_state->board);
    lv_obj_set_size(g_state->board, LV_HOR_RES, LV_VER_RES);
    lv_obj_remove_flag(g_state->board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(g_state->board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_state->board, PaintKeys, LV_EVENT_DRAW_MAIN, nullptr);

    g_state->bubble = lv_label_create(root);
    lv_obj_set_size(g_state->bubble, 56, 56);
    lv_obj_set_style_radius(g_state->bubble, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_state->bubble, lv_color_hex(kKeyActive), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_state->bubble, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_state->bubble, lv_color_hex(kBg), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_state->bubble, &font_chat_24, LV_PART_MAIN);
    lv_obj_set_style_text_align(g_state->bubble, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_top(g_state->bubble, 14, LV_PART_MAIN);
    lv_obj_add_flag(g_state->bubble, LV_OBJ_FLAG_HIDDEN);
    g_open = true;
}

void KeyboardClose() {
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if (g_state == nullptr)
        return;
    g_open = false;
    lv_label_set_text(g_state->field, "");
    lv_obj_delete(g_state->root);
    std::fill(g_state->editor.text.begin(), g_state->editor.text.end(), '\0');
    delete g_state;
    g_state = nullptr;
}

void KeyboardTouch(TouchEvent event, int x, int y) {
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if (g_state == nullptr)
        return;
    if (event == TouchEvent::kDown) {
        g_state->touching = true;
        g_state->field_touch = y < kb::kTop - 8;
        InvalidateKey(g_state->focus);
        g_state->focus = -1;  // the finger takes over from the wheel
    }
    if (!g_state->touching)
        return;  // the finger that opened the keyboard is still lifting
    if (event == TouchEvent::kUp) {
        const int index = g_state->pressed;
        const bool field = g_state->field_touch && y < kb::kTop - 8;
        g_state->touching = false;
        SetPressed(-1);
        if (field) {
            g_state->reveal = !g_state->reveal;
            UpdateField();
        } else {
            Press(index);
        }
        return;
    }
    if (!g_state->field_touch)
        SetPressed(kb::NearestKey(g_state->keys, x, y));  // sliding corrects a near miss
}

void KeyboardRotate(bool forward) {
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if (g_state == nullptr)
        return;
    const int n = (int)g_state->keys.size();
    InvalidateKey(g_state->focus);
    g_state->focus = g_state->focus < 0 ? 0 : (g_state->focus + (forward ? 1 : n - 1)) % n;
    InvalidateKey(g_state->focus);
}

void KeyboardClick() {
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if (g_state != nullptr && g_state->focus >= 0)
        Press(g_state->focus);
}

}  // namespace assistant
