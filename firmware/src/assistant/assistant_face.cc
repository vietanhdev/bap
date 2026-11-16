// Character faces drawn natively with LVGL (no bitmaps): flat body shape, eyes, mouth, cheeks
// and small animated accents. Smooth on the round display and a few hundred bytes per character.
#include "assistant_face.h"

#include <cstring>
#include <string>

#include "assistant_config.h"

namespace assistant {
namespace {

constexpr int kDesign = 320;      // all coordinates below are in a 320x320 design space
int g_size = 264;                 // actual pixel size of the face area
lv_obj_t* g_root = nullptr;       // body + features container
lv_obj_t* g_body = nullptr;
lv_obj_t* g_features = nullptr;
lv_obj_t* g_mouth = nullptr;      // current mouth object (replaced while talking)
const lv_font_t* g_font = nullptr;
std::string g_character, g_emotion;
bool g_talking = false;
Look g_look;

int Px(int v) { return v * g_size / kDesign; }
lv_color_t Hex(const std::string& s, uint32_t fallback) {
    if (s.size() == 7 && s[0] == '#') return lv_color_hex(strtoul(s.c_str() + 1, nullptr, 16));
    return lv_color_hex(fallback);
}

lv_obj_t* Plain(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return o;
}

// Filled rounded shape centered at (cx, cy) in design units. radius < 0 = fully round.
lv_obj_t* Shape(lv_obj_t* parent, int cx, int cy, int w, int h, lv_color_t color, int radius = -1) {
    lv_obj_t* o = Plain(parent);
    lv_obj_set_size(o, Px(w), Px(h));
    lv_obj_set_pos(o, Px(cx - w / 2), Px(cy - h / 2));
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius < 0 ? LV_RADIUS_CIRCLE : Px(radius), 0);
    return o;
}

// Stroke-only arc centered at (cx, cy): 0 deg = 3 o'clock, clockwise.
lv_obj_t* Arc(lv_obj_t* parent, int cx, int cy, int diameter, int start, int end, int width, lv_color_t color) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(a, Px(diameter), Px(diameter));
    lv_obj_set_pos(a, Px(cx - diameter / 2), Px(cy - diameter / 2));
    lv_arc_set_bg_angles(a, start, end);
    lv_obj_set_style_arc_color(a, color, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, LV_MAX(2, Px(width)), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    return a;
}

lv_obj_t* Text(lv_obj_t* parent, int cx, int cy, const char* text, lv_color_t color) {
    lv_obj_t* l = lv_label_create(parent);
    if (g_font != nullptr) lv_obj_set_style_text_font(l, g_font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    lv_obj_update_layout(l);
    lv_obj_set_pos(l, Px(cx) - lv_obj_get_width(l) / 2, Px(cy) - lv_obj_get_height(l) / 2);
    return l;
}

void SetY(void* obj, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t*)obj, v, 0); }
void SetOpa(void* obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0); }
void SetHeightCentered(void* obj, int32_t h) {
    auto* o = (lv_obj_t*)obj;
    int32_t base = (int32_t)(intptr_t)lv_obj_get_user_data(o);  // resting height
    lv_obj_set_height(o, h);
    lv_obj_set_style_translate_y(o, (base - h) / 2, 0);
}

void Animate(lv_obj_t* obj, lv_anim_exec_xcb_t cb, int from, int to, int ms, int delay = 0,
             int repeat_delay = 0, bool reverse = true) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    if (reverse) lv_anim_set_reverse_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_repeat_delay(&a, repeat_delay);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

// ---------- bodies ----------

struct Anchor { int x, y; };  // face center in design units

Anchor BuildBody(lv_obj_t* p, const Look& look) {
    lv_color_t body = Hex(look.body, 0xFFFFFF), accent = Hex(look.accent, 0xDDDDDD);
    if (look.shape == "corn") {
        lv_color_t kernel = lv_color_mix(lv_color_white(), body, 110);
        Shape(p, 160, 152, 196, 288, body, 98);
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < (row ? 4 : 3); ++col) {
                Shape(p, (row ? 115 : 130) + col * 30, 36 + row * 28, 20, 20, kernel);
            }
        }
        Shape(p, 84, 254, 112, 124, lv_color_darken(accent, 40));
        Shape(p, 236, 254, 112, 124, lv_color_darken(accent, 40));
        Shape(p, 160, 282, 220, 68, accent, 34);
        return {160, 142};
    }
    if (look.shape == "avocado") {
        lv_color_t flesh = accent, inner = lv_color_mix(lv_color_white(), accent, 90);
        Shape(p, 160, 96, 176, 176, body);
        Shape(p, 160, 192, 244, 244, body);
        Shape(p, 160, 150, 150, 120, body, 20);
        Shape(p, 160, 98, 146, 146, flesh);
        Shape(p, 160, 192, 214, 214, flesh);
        Shape(p, 160, 150, 126, 110, flesh, 20);
        Shape(p, 160, 100, 118, 118, inner);
        Shape(p, 160, 192, 186, 186, inner);
        Shape(p, 160, 150, 100, 100, inner, 20);
        Shape(p, 160, 256, 76, 76, lv_color_hex(0x96623C));
        Shape(p, 148, 244, 18, 18, lv_color_hex(0xBA865C));
        return {160, 132};
    }
    if (look.shape == "mochi") {
        Shape(p, 160, 178, 304, 236, body);
        Shape(p, 150, 118, 190, 86, lv_color_mix(lv_color_white(), body, 120));
        for (int k = 0; k < 5; ++k) {
            static const int dx[] = {0, 15, 9, -9, -15}, dy[] = {-16, -5, 13, 13, -5};
            Shape(p, 236 + dx[k], 84 + dy[k], 24, 24, accent);
        }
        Shape(p, 236, 84, 14, 14, lv_color_hex(0xFFE278));
        return {160, 186};
    }
    if (look.shape == "robot") {
        lv_obj_t* antenna = Shape(p, 160, 44, 8, 44, lv_color_darken(body, 50), 4);
        lv_obj_t* ball = Shape(p, 160, 20, 28, 28, lv_color_hex(0xFF7878));
        Animate(ball, SetOpa, 255, 90, 900);
        (void)antenna;
        Shape(p, 22, 178, 26, 66, lv_color_darken(body, 40), 10);
        Shape(p, 298, 178, 26, 66, lv_color_darken(body, 40), 10);
        Shape(p, 160, 180, 264, 226, body, 62);
        Shape(p, 160, 180, 218, 164, accent, 46);
        return {160, 176};
    }
    // cloud (default)
    Shape(p, 92, 196, 124, 124, body);
    Shape(p, 228, 196, 124, 124, body);
    Shape(p, 122, 134, 136, 136, body);
    Shape(p, 204, 140, 128, 128, body);
    Shape(p, 160, 200, 160, 150, body);
    Shape(p, 160, 222, 260, 70, body, 35);
    return {160, 178};
}

// ---------- features ----------

struct Expression {
    const char* name;
    const char* eyes;   // open happy closed wide wink sad angry
    const char* mouth;  // smile big open frown o flat tongue
    const char* extra;  // "" zzz tears waves dots hearts sweat question
};

const Expression kExpressions[] = {
    {"neutral", "open", "smile", ""},       {"happy", "happy", "big", ""},
    {"laughing", "happy", "open", ""},      {"funny", "happy", "open", ""},
    {"sad", "sad", "frown", ""},            {"angry", "angry", "frown", ""},
    {"crying", "closed", "frown", "tears"}, {"loving", "happy", "big", "hearts"},
    {"embarrassed", "open", "flat", "sweat"}, {"surprised", "wide", "o", ""},
    {"shocked", "wide", "o", "sweat"},      {"thinking", "open", "flat", "dots"},
    {"winking", "wink", "big", ""},         {"cool", "happy", "smile", ""},
    {"relaxed", "closed", "smile", ""},     {"delicious", "happy", "tongue", ""},
    {"kissy", "closed", "o", "hearts"},     {"confident", "happy", "smile", ""},
    {"sleepy", "closed", "o", "zzz"},       {"silly", "wink", "tongue", ""},
    {"confused", "open", "flat", "question"}, {"listening", "wide", "smile", "waves"},
    {"waking", "wide", "o", ""},
};

const Expression& Find(const char* name) {
    for (auto& e : kExpressions) {
        if (strcmp(e.name, name) == 0) return e;
    }
    return kExpressions[0];
}

void BuildEye(lv_obj_t* p, const char* kind, int x, int y, int side, lv_color_t ink, lv_color_t shine) {
    auto open_eye = [&](int w, int h) {
        lv_obj_t* eye = Shape(p, x, y, w, h, ink);
        lv_obj_set_user_data(eye, (void*)(intptr_t)Px(h));
        lv_obj_t* dot = Shape(eye, 0, 0, w / 3, w / 3, shine);
        lv_obj_set_pos(dot, Px(w * 55 / 100), Px(h * 14 / 100));
        // Blink: quick close/open every few seconds, the two eyes in sync.
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, eye);
        lv_anim_set_exec_cb(&a, SetHeightCentered);
        lv_anim_set_values(&a, Px(h), Px(5));
        lv_anim_set_duration(&a, 90);
        lv_anim_set_reverse_duration(&a, 110);
        lv_anim_set_delay(&a, 1800);
        lv_anim_set_repeat_delay(&a, 3400);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
        return eye;
    };
    std::string k = kind;
    if (k == "wink") k = side < 0 ? "open" : "happy";
    if (k == "open") {
        open_eye(38, 50);
    } else if (k == "wide") {
        Shape(p, x, y, 62, 62, lv_color_white());
        lv_obj_t* pupil = Shape(p, x, y + 2, 34, 34, ink);
        Shape(pupil, 0, 0, 12, 12, shine);
        lv_obj_set_pos(lv_obj_get_child(pupil, 0), Px(18), Px(5));
    } else if (k == "happy") {
        Arc(p, x, y + 12, 50, 200, 340, 10, ink);
    } else if (k == "closed") {
        Arc(p, x, y - 14, 50, 20, 160, 9, ink);
    } else {  // sad / angry: open eye plus a slanted brow
        open_eye(34, 44);
        lv_obj_t* brow = Shape(p, x, y - 40, 44, 10, ink, 5);
        int tilt = (k == "angry" ? 200 : -200) * side;  // 0.1 degree units
        lv_obj_set_style_transform_pivot_x(brow, Px(22), 0);
        lv_obj_set_style_transform_pivot_y(brow, Px(5), 0);
        lv_obj_set_style_transform_rotation(brow, tilt, 0);
    }
}

lv_obj_t* BuildMouth(lv_obj_t* p, const char* kind, int x, int y, lv_color_t ink) {
    std::string k = kind;
    if (k == "talk") {
        lv_obj_t* m = Shape(p, x, y + 4, 46, 30, ink, 15);
        lv_obj_set_user_data(m, (void*)(intptr_t)Px(30));
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, m);
        lv_anim_set_exec_cb(&a, SetHeightCentered);
        lv_anim_set_values(&a, Px(10), Px(34));
        lv_anim_set_duration(&a, 150);
        lv_anim_set_reverse_duration(&a, 170);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
        return m;
    }
    if (k == "big") return Arc(p, x, y - 12, 64, 25, 155, 10, ink);
    if (k == "open" || k == "tongue") {
        lv_obj_t* m = Shape(p, x, y + 4, 60, 40, ink, 20);
        lv_obj_set_style_clip_corner(m, true, 0);
        lv_obj_t* tongue = Shape(m, 0, 0, 36, 28, lv_color_hex(0xFF8096));
        lv_obj_set_pos(tongue, Px(12), Px(k == "tongue" ? 16 : 24));
        return m;
    }
    if (k == "frown") return Arc(p, x, y + 22, 50, 205, 335, 9, ink);
    if (k == "o") return Shape(p, x, y + 4, 24, 28, ink);
    if (k == "flat") return Shape(p, x, y + 2, 34, 9, ink, 4);
    return Arc(p, x, y - 8, 44, 30, 150, 9, ink);  // smile
}

void BuildExtra(lv_obj_t* p, const char* kind, Anchor f, const Look& look) {
    std::string k = kind;
    lv_color_t blue = lv_color_hex(0x68BAFF), pink = lv_color_hex(0xFF5C8A);
    if (k == "zzz") {
        for (int i = 0; i < 3; ++i) {
            lv_obj_t* z = Text(p, f.x + 96 + i * 22, f.y - 80 - i * 30, i == 2 ? "Z" : "z", lv_color_hex(0xB9B0FF));
            Animate(z, SetOpa, 40, 255, 700, i * 350);
            Animate(z, SetY, 0, -Px(8), 1400, i * 350);
        }
    } else if (k == "tears") {
        for (int side = -1; side <= 1; side += 2) {
            lv_obj_t* tear = Shape(p, f.x + side * 58, f.y + 20, 16, 22, blue);
            Animate(tear, SetY, 0, Px(46), 700, side > 0 ? 350 : 0, 0, false);
        }
    } else if (k == "waves") {
        for (int side = -1; side <= 1; side += 2) {
            for (int i = 0; i < 2; ++i) {
                int d = 70 + i * 50;
                lv_obj_t* arc = side > 0 ? Arc(p, 300, f.y, d, 325, 35, 8, blue) : Arc(p, 20, f.y, d, 145, 215, 8, blue);
                Animate(arc, SetOpa, 60, 255, 450, i * 220);
            }
        }
    } else if (k == "dots") {
        for (int i = 0; i < 3; ++i) {
            lv_obj_t* dot = Shape(p, f.x + 70 + i * 26, f.y - 96 - i * 14, 14 + i * 4, 14 + i * 4, lv_color_hex(0xD8D4EA));
            Animate(dot, SetOpa, 50, 255, 450, i * 250);
        }
    } else if (k == "hearts") {
        for (int i = 0; i < 2; ++i) {
            lv_obj_t* h = Text(p, f.x + (i ? 104 : -104), f.y - 86 + i * 16, "\xE2\x99\xA5", pink);
            Animate(h, SetY, 0, -Px(14), 800, i * 300);
        }
    } else if (k == "sweat") {
        lv_obj_t* drop = Shape(p, f.x + 104, f.y - 56, 18, 26, blue);
        Animate(drop, SetY, 0, Px(10), 600);
    } else if (k == "question") {
        lv_obj_t* q = Text(p, f.x + 104, f.y - 84, "?", Hex(look.accent, 0xFFD84D));
        Animate(q, SetY, 0, -Px(8), 700);
    }
}

Anchor g_anchor{160, 170};

void RebuildFeatures() {
    if (g_features != nullptr) lv_obj_delete(g_features);
    g_features = Plain(g_root);
    lv_obj_set_size(g_features, g_size, g_size);
    const Expression& e = Find(g_emotion.c_str());
    lv_color_t ink = Hex(g_look.ink, 0x3A2E4C);
    lv_color_t shine = lv_color_white();
    Anchor f = g_anchor;
    for (int side = -1; side <= 1; side += 2) {
        lv_obj_t* cheek = Shape(g_features, f.x + side * 100, f.y + 30, 38, 26, Hex(g_look.cheek, 0xFF9EB5));
        lv_obj_set_style_bg_opa(cheek, LV_OPA_70, 0);
        BuildEye(g_features, e.eyes, f.x + side * 56, f.y - 10, side, ink, shine);
    }
    g_mouth = BuildMouth(g_features, g_talking ? "talk" : e.mouth, f.x, f.y + 44, ink);
    BuildExtra(g_features, e.extra, f, g_look);
}

}  // namespace

void FaceShow(lv_obj_t* parent, const lv_font_t* font, const char* emotion) {
    auto character = ActiveCharacter();
    g_font = font;
    if (g_root == nullptr || g_character != character.id) {
        if (g_root != nullptr) lv_obj_delete(g_root);
        g_features = nullptr;
        g_look = character.look;
        g_character = character.id;
        g_size = LV_MIN(lv_display_get_horizontal_resolution(nullptr), lv_display_get_vertical_resolution(nullptr)) * 44 / 100;
        g_root = Plain(parent);
        lv_obj_set_size(g_root, g_size, g_size);
        lv_obj_add_flag(g_root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);  // accents may sit beside the body
        lv_obj_align(parent, LV_ALIGN_CENTER, 0, -g_size / 9);  // leave room for subtitles below
        g_body = Plain(g_root);
        lv_obj_set_size(g_body, g_size, g_size);
        g_anchor = BuildBody(g_body, g_look);
        Animate(g_root, SetY, 0, -LV_MAX(2, g_size / 60), 1600);  // gentle breathing
        g_emotion.clear();
    }
    if (g_emotion != emotion) {
        g_emotion = emotion;
        RebuildFeatures();
    }
}

void FaceSetTalking(bool talking) {
    if (g_talking == talking || g_root == nullptr) {
        g_talking = talking;
        return;
    }
    g_talking = talking;
    RebuildFeatures();
}

}  // namespace assistant
