#include <esp_heap_caps.h>
#include <esp_log.h>
// Character faces drawn natively with LVGL (no bitmaps). Each character is a layered "sticker":
// outline, soft darker underside, body, sheen and body details, with a feature layer on top
// (glossy eyes, blush, mouth, accents) that is rebuilt on every emotion change.
//
// Cheap by design: the body never changes after a character switch, animations only touch
// translate/opa/size of a few small objects, and the breathing bob only invalidates the face
// when its integer offset actually changes (a handful of redraws per breath).
#include "assistant_face.h"

#include <cJSON.h>

#include <climits>
#include <cstring>
#include <initializer_list>
#include <string>

#include "assistant_config.h"
#include "builtin_data.h"

namespace assistant {
namespace {

constexpr int kDesign = 320;  // all coordinates below are in a 320x320 design space
int g_size = 264;             // actual pixel size of the face area
lv_obj_t* g_root = nullptr;   // body + features container (bobs while breathing)
lv_obj_t* g_body = nullptr;
lv_obj_t* g_features = nullptr;
lv_obj_t* g_mouth = nullptr;   // mouth layer inside g_features (rebuilt when talking toggles)
lv_timer_t* g_life = nullptr;  // random blinks and glances
std::string g_character, g_emotion;
bool g_talking = false;
bool g_calm = false;  // night: no breathing (see FaceSetCalm)

struct Palette {
    lv_color_t body, accent, ink, cheek, shade, outline, sheen, detail, iris;
};
Palette g_pal;
std::string g_shape;

struct Pt {
    int x, y;
};
struct Rig {
    Pt face{160, 190};  // face center
    int eye_dx = 46;    // half distance between the eyes
    int scale = 100;    // feature size in percent
    int cheek_dx = 80, cheek_dy = 26;
    int mouth_dy = 34;
    int left = 40, right = 280;  // body extent at face height (listening waves sit outside)
    Pt slot{266, 54};            // free spot at the top right of the head for accents
    Pt slot2{54, 54};            // free spot at the top left
};
Rig g_rig;

// Eyes that blink / glance. Cleared before the feature layer is deleted.
lv_obj_t* g_eyes[2] = {nullptr, nullptr};
int g_eye_count = 0;
bool g_glance_ok = false;

int Px(int v) { return v * g_size / kDesign; }
int S(int v) { return v * g_rig.scale / 100; }  // feature size scaled for the character

lv_color_t Hex(const std::string& s, lv_color_t fallback) {
    if (s.size() == 7 && s[0] == '#')
        return lv_color_hex(strtoul(s.c_str() + 1, nullptr, 16));
    return fallback;
}

// ---------- primitives ----------

lv_obj_t* Plain(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return o;
}

// Full-size transparent layer (draws nothing itself).
lv_obj_t* Layer(lv_obj_t* parent) {
    lv_obj_t* o = Plain(parent);
    lv_obj_set_size(o, g_size, g_size);
    return o;
}

// Filled rounded shape, top-left (x, y) and size in pixels. radius < 0 = fully round.
lv_obj_t* ShapePx(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t color, int radius = -1) {
    lv_obj_t* o = Plain(parent);
    lv_obj_set_size(o, LV_MAX(1, w), LV_MAX(1, h));
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius < 0 ? LV_RADIUS_CIRCLE : radius, 0);
    return o;
}

// Filled rounded shape centered at (cx, cy) in design units. radius < 0 = fully round.
lv_obj_t* Shape(lv_obj_t* parent, int cx, int cy, int w, int h, lv_color_t color, int radius = -1) {
    int pw = Px(w), ph = Px(h);
    return ShapePx(parent, Px(cx) - pw / 2, Px(cy) - ph / 2, pw, ph, color,
                   radius < 0 ? -1 : Px(radius));
}

void Grad(lv_obj_t* o, lv_color_t bottom, int main_stop = 0, int grad_stop = 255) {
    lv_obj_set_style_bg_grad_color(o, bottom, 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(o, main_stop, 0);
    lv_obj_set_style_bg_grad_stop(o, grad_stop, 0);
}

void Opa(lv_obj_t* o, lv_opa_t opa) { lv_obj_set_style_bg_opa(o, opa, 0); }

// Stroke-only arc centered at (cx, cy): 0 deg = 3 o'clock, clockwise.
lv_obj_t* Arc(lv_obj_t* parent, int cx, int cy, int diameter, int start, int end, int width,
              lv_color_t color) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    int d = Px(diameter);
    lv_obj_set_size(a, d, d);
    lv_obj_set_pos(a, Px(cx) - d / 2, Px(cy) - d / 2);
    lv_arc_set_bg_angles(a, start, end);
    lv_obj_set_style_arc_color(a, color, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, LV_MAX(2, Px(width)), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    return a;
}

// Polyline with rounded caps through design-space points (x0, y0, x1, y1, ...).
lv_obj_t* Line(lv_obj_t* parent, std::initializer_list<int> xy, int width, lv_color_t color) {
    int n = (int)xy.size() / 2;
    auto* pts = (lv_point_precise_t*)lv_malloc(sizeof(lv_point_precise_t) * n);
    int w = LV_MAX(2, Px(width)), pad = w;
    int minx = INT_MAX, miny = INT_MAX, maxx = INT_MIN, maxy = INT_MIN;
    int i = 0;
    for (auto it = xy.begin(); it != xy.end(); it += 2, ++i) {
        int x = Px(*it), y = Px(*(it + 1));
        pts[i].x = x;
        pts[i].y = y;
        minx = LV_MIN(minx, x);
        miny = LV_MIN(miny, y);
        maxx = LV_MAX(maxx, x);
        maxy = LV_MAX(maxy, y);
    }
    for (i = 0; i < n; ++i) {
        pts[i].x -= minx - pad;
        pts[i].y -= miny - pad;
    }
    lv_obj_t* l = lv_line_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(l, minx - pad, miny - pad);
    lv_obj_set_size(l, maxx - minx + 2 * pad, maxy - miny + 2 * pad);
    lv_line_set_points(l, pts, n);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_add_event_cb(
        l, [](lv_event_t* e) { lv_free(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, pts);
    return l;
}

// Small container for a multi-part symbol: centered at (cx, cy), w x h design units.
// Children use design coordinates relative to its top-left corner.
lv_obj_t* Group(lv_obj_t* parent, int cx, int cy, int w, int h) {
    lv_obj_t* g = Plain(parent);
    lv_obj_set_size(g, Px(w), Px(h));
    lv_obj_set_pos(g, Px(cx - w / 2), Px(cy - h / 2));
    return g;
}

// Square rotated by 45 degrees (pixel coordinates, centered at x, y).
lv_obj_t* Diamond(lv_obj_t* parent, int x, int y, int q, lv_color_t color) {
    lv_obj_t* d = ShapePx(parent, x - q / 2, y - q / 2, q, q, color, q / 6);
    lv_obj_set_style_transform_pivot_x(d, q / 2, 0);
    lv_obj_set_style_transform_pivot_y(d, q / 2, 0);
    lv_obj_set_style_transform_rotation(d, 450, 0);
    return d;
}

lv_obj_t* Heart(lv_obj_t* parent, int cx, int cy, int s, lv_color_t color) {
    int P = Px(s);
    lv_obj_t* h = Plain(parent);
    lv_obj_set_size(h, P, P);
    lv_obj_set_pos(h, Px(cx) - P / 2, Px(cy) - P / 2);
    int d = P * 56 / 100;
    ShapePx(h, P * 28 / 100 - d / 2, P * 33 / 100 - d / 2, d, d, color);
    ShapePx(h, P * 72 / 100 - d / 2, P * 33 / 100 - d / 2, d, d, color);
    Diamond(h, P / 2, P * 54 / 100, P * 56 / 100, color);
    lv_obj_t* hl = ShapePx(h, P * 18 / 100, P * 20 / 100, LV_MAX(2, P / 5), LV_MAX(2, P / 7),
                           lv_color_white());
    Opa(hl, LV_OPA_70);
    return h;
}

lv_obj_t* Drop(lv_obj_t* parent, int cx, int cy, int s, lv_color_t color) {
    int P = Px(s);
    lv_obj_t* h = Plain(parent);
    lv_obj_set_size(h, P, P);
    lv_obj_set_pos(h, Px(cx) - P / 2, Px(cy) - P / 2);
    int d = P * 66 / 100;
    ShapePx(h, P / 2 - d / 2, P * 64 / 100 - d / 2, d, d, color);
    Diamond(h, P / 2, P * 44 / 100, P * 42 / 100, color);
    lv_obj_t* hl = ShapePx(h, P * 34 / 100, P * 52 / 100, LV_MAX(2, P / 6), LV_MAX(2, P / 5),
                           lv_color_white());
    Opa(hl, LV_OPA_70);
    return h;
}

// ---------- animation ----------

// Exec callbacks only touch the object when the integer value actually changes, so slow
// animations cause a handful of redraws instead of one per frame.
void SetY(void* obj, int32_t v) {
    auto* o = (lv_obj_t*)obj;
    if (lv_obj_get_style_translate_y(o, LV_PART_MAIN) != v)
        lv_obj_set_style_translate_y(o, v, 0);
}
void SetX(void* obj, int32_t v) {
    auto* o = (lv_obj_t*)obj;
    if (lv_obj_get_style_translate_x(o, LV_PART_MAIN) != v)
        lv_obj_set_style_translate_x(o, v, 0);
}
void SetOpa(void* obj, int32_t v) {
    // ~11 opacity levels: fades still look smooth on this panel, but redraw a few times a second
    // instead of on every frame (glows, zzz and sparkles overlap gradient-filled shapes).
    v = v >= 250 ? 255 : (v / 24) * 24;
    auto* o = (lv_obj_t*)obj;
    if (lv_obj_get_style_opa(o, LV_PART_MAIN) != v)
        lv_obj_set_style_opa(o, (lv_opa_t)v, 0);
}
// Three-level pulse for the listening waves: big anti-aliased arcs over the face are the most
// expensive thing to redraw, so they step instead of fading smoothly.
void SetOpaPulse(void* obj, int32_t v) {
    v = v < 110 ? 60 : (v < 200 ? 150 : 255);
    auto* o = (lv_obj_t*)obj;
    if (lv_obj_get_style_opa(o, LV_PART_MAIN) != v)
        lv_obj_set_style_opa(o, (lv_opa_t)v, 0);
}
// Resize around the center; user data holds the resting size.
void SetSizeCentered(void* obj, int32_t s) {
    auto* o = (lv_obj_t*)obj;
    int32_t base = (int32_t)(intptr_t)lv_obj_get_user_data(o);
    if (lv_obj_get_width(o) == s)
        return;
    lv_obj_set_size(o, s, s);
    lv_obj_set_style_translate_x(o, (base - s) / 2, 0);
    lv_obj_set_style_translate_y(o, (base - s) / 2, 0);
}
// Eye lid: squash the eye around its center and hide its highlights while nearly closed.
void SetBlink(void* obj, int32_t h) {
    auto* o = (lv_obj_t*)obj;
    int32_t base = (int32_t)(intptr_t)lv_obj_get_user_data(o);
    lv_obj_set_height(o, h);
    lv_obj_set_style_translate_y(o, (base - h) / 2, 0);
    lv_opa_t opa = h * 10 < base * 6 ? LV_OPA_TRANSP : LV_OPA_COVER;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i)
        lv_obj_set_style_opa(lv_obj_get_child(o, i), opa, 0);
}

void Animate(lv_obj_t* obj, lv_anim_exec_xcb_t cb, int from, int to, int ms, int delay = 0,
             int repeat_delay = 0, bool reverse = true,
             lv_anim_path_cb_t path = lv_anim_path_ease_in_out) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    if (reverse)
        lv_anim_set_reverse_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_repeat_delay(&a, repeat_delay);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

// Rise and fade out, then start again (hearts, zzz, tears).
void FloatAway(lv_obj_t* obj, int dy, int ms, int delay) {
    Animate(obj, SetY, 0, Px(dy), ms, delay, 0, false, lv_anim_path_ease_out);
    Animate(obj, SetOpa, 255, 0, ms, delay, 0, false, lv_anim_path_ease_in);
}

// ---------- life: blinks and glances ----------

void Blink(int delay) {
    for (int i = 0; i < g_eye_count; ++i) {
        lv_obj_t* eye = g_eyes[i];
        int32_t base = (int32_t)(intptr_t)lv_obj_get_user_data(eye);
        lv_anim_delete(eye, SetBlink);
        SetBlink(eye, base);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, eye);
        lv_anim_set_exec_cb(&a, SetBlink);
        lv_anim_set_values(&a, base, LV_MAX(2, base / 12));
        lv_anim_set_duration(&a, 70);
        lv_anim_set_reverse_duration(&a, 110);
        lv_anim_set_delay(&a, delay);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_start(&a);
    }
}

void Glance() {
    int dir = lv_rand(0, 1) ? 1 : -1;
    int hold = lv_rand(500, 1300);
    for (int i = 0; i < g_eye_count; ++i) {
        lv_anim_delete(g_eyes[i], SetX);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, g_eyes[i]);
        lv_anim_set_exec_cb(&a, SetX);
        lv_anim_set_values(&a, 0, dir * Px(S(7)));
        lv_anim_set_duration(&a, 160);
        lv_anim_set_reverse_delay(&a, hold);
        lv_anim_set_reverse_duration(&a, 200);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }
}

void LifeTick(lv_timer_t* t) {
    uint32_t r = lv_rand(0, 99);
    if (g_eye_count > 0) {
        if (g_glance_ok && r < 22) {
            Glance();
        } else {
            Blink(0);
            if (r > 84)
                Blink(240);  // double blink now and then
        }
    }
    lv_timer_set_period(t, lv_rand(1800, 5200));
}

// ---------- palette ----------

void LoadPalette(const Look& look, const std::string& id) {
    // The five basic colors come with the character; the optional extra ones (shade, outline,
    // sheen, detail, iris) are read straight from the built-in data.
    std::string extra[5];
    static const char* kExtra[5] = {"shade", "outline", "sheen", "detail", "iris"};
    if (cJSON* root = cJSON_Parse(kAssistantJson)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach (item, cJSON_GetObjectItem(root, "characters")) {
            auto cid = cJSON_GetObjectItem(item, "id");
            if (!cJSON_IsString(cid) || id != cid->valuestring)
                continue;
            auto lk = cJSON_GetObjectItem(item, "look");
            for (int i = 0; i < 5; ++i) {
                auto v = cJSON_GetObjectItem(lk, kExtra[i]);
                if (cJSON_IsString(v))
                    extra[i] = v->valuestring;
            }
        }
        cJSON_Delete(root);
    }
    Palette& p = g_pal;
    p.body = Hex(look.body, lv_color_white());
    p.accent = Hex(look.accent, lv_color_hex(0xDDDDDD));
    p.ink = Hex(look.ink, lv_color_hex(0x3A2E4C));
    p.cheek = Hex(look.cheek, lv_color_hex(0xFF9EB5));
    p.shade = Hex(extra[0], lv_color_darken(p.body, 40));
    p.outline = Hex(extra[1], lv_color_darken(p.body, 80));
    p.sheen = Hex(extra[2], lv_color_lighten(p.body, 120));
    p.detail = Hex(extra[3], p.accent);
    p.iris = Hex(extra[4], lv_color_lighten(p.ink, 70));
}

// ---------- bodies ----------

struct Blob {
    int16_t x, y, w, h, r;
};  // r < 0: fully round

// Sticker-style body: outline, darker underside (the body pass is lifted so a crescent of the
// shade color shows at the bottom), then the body itself.
void LayeredBody(lv_obj_t* p, const Blob* b, int n, int grow, int lift, bool gradient) {
    for (int i = 0; i < n; ++i) {
        Shape(p, b[i].x, b[i].y, b[i].w + 2 * grow, b[i].h + 2 * grow, g_pal.outline,
              b[i].r < 0 ? -1 : b[i].r + grow);
    }
    // Only blobs forming the bottom of the silhouette get the underside crescent; on the others
    // it would show as a seam in the middle of the body.
    int bottom = 0;
    for (int i = 0; i < n; ++i)
        bottom = LV_MAX(bottom, b[i].y + b[i].h / 2);
    for (int i = 0; i < n; ++i)
        Shape(p, b[i].x, b[i].y, b[i].w, b[i].h, g_pal.shade, b[i].r);
    for (int i = 0; i < n; ++i) {
        int l = b[i].y + b[i].h / 2 >= bottom - 34 ? lift : 0;
        lv_obj_t* o = Shape(p, b[i].x, b[i].y - l / 2, b[i].w, b[i].h - l, g_pal.body, b[i].r);
        if (gradient) {  // single-shape bodies get a soft top light
            lv_obj_set_style_bg_color(o, lv_color_mix(g_pal.sheen, g_pal.body, 110), 0);
            Grad(o, g_pal.body, 0, 150);
        }
    }
}

void Sheen(lv_obj_t* p, int x, int y, int w, int h, lv_opa_t opa) {
    Opa(Shape(p, x, y, w, h, g_pal.sheen), opa);
}

Rig BuildBody(lv_obj_t* p) {
    Rig rig;
    const Palette& c = g_pal;
    if (g_shape == "corn") {
        lv_color_t leaf = c.accent, leaf_dark = lv_color_darken(c.accent, 50);
        lv_color_t leaf_light = lv_color_lighten(c.accent, 90);
        // back husk leaves curling up beside the cob
        Arc(p, 232, 214, 336, 160, 214, 34, leaf_dark);
        Arc(p, 88, 214, 336, 326, 20, 34, leaf_dark);
        // silk tuft on top
        Arc(p, 150, 26, 26, 160, 320, 6, c.detail);
        Arc(p, 172, 24, 22, 220, 20, 6, c.detail);
        Arc(p, 160, 18, 18, 180, 360, 5, c.detail);
        static const Blob cob[] = {{160, 148, 146, 248, 73}};
        LayeredBody(p, cob, 1, 5, 12, true);
        // kernels: rows on top of the head and a few peeking between the front leaves
        lv_color_t kernel = lv_color_mix(c.sheen, c.body, 120);
        static const Pt kernels[] = {{136, 44}, {160, 42}, {184, 44},  {124, 64},  {148, 63},
                                     {172, 63}, {196, 64}, {112, 86},  {136, 84},  {160, 84},
                                     {184, 84}, {208, 86}, {148, 232}, {172, 232}, {160, 252}};
        for (auto& k : kernels) {
            Shape(p, k.x, k.y + 3, 19, 16, c.shade, 7);
            Shape(p, k.x, k.y, 19, 15, kernel, 7);
        }
        Sheen(p, 116, 112, 16, 44, LV_OPA_50);
        // front husk leaves wrapping the bottom of the cob, with a lighter vein
        Arc(p, 200, 186, 214, 114, 172, 32, leaf);
        Arc(p, 120, 186, 214, 8, 66, 32, leaf);
        Arc(p, 200, 186, 214, 126, 162, 4, leaf_light);
        Arc(p, 120, 186, 214, 18, 54, 4, leaf_light);
        rig.face = {160, 150};
        rig.eye_dx = 33;
        rig.scale = 84;
        rig.cheek_dx = 52;
        rig.cheek_dy = 24;
        rig.mouth_dy = 30;
        rig.left = 82;
        rig.right = 238;
        rig.slot = {262, 56};
        rig.slot2 = {58, 56};
        return rig;
    }
    if (g_shape == "avocado") {
        static const Blob skin[] = {{160, 98, 164, 164, -1}, {160, 192, 212, 212, -1}};
        LayeredBody(p, skin, 2, 4, 10, false);
        // flesh: greener outer ring, pale inner part
        Shape(p, 160, 100, 136, 136, c.accent);
        Shape(p, 160, 188, 180, 176, c.accent);
        Shape(p, 160, 104, 110, 110, c.sheen);
        Shape(p, 160, 190, 152, 146, c.sheen);
        Sheen(p, 124, 70, 30, 18, LV_OPA_80);
        // pit sitting in a slightly darker hollow
        Shape(p, 160, 234, 84, 80, c.accent);
        lv_obj_t* pit = Shape(p, 160, 236, 70, 66, c.detail);
        Grad(pit, lv_color_darken(c.detail, 70), 60, 255);
        Opa(Shape(p, 147, 220, 22, 14, lv_color_lighten(c.detail, 90)), LV_OPA_80);
        rig.face = {160, 128};
        rig.eye_dx = 36;
        rig.scale = 92;
        rig.cheek_dx = 58;
        rig.cheek_dy = 26;
        rig.mouth_dy = 28;
        rig.left = 74;
        rig.right = 246;
        rig.slot = {264, 54};
        rig.slot2 = {56, 54};
        return rig;
    }
    if (g_shape == "mochi") {
        static const Blob mochi[] = {{160, 212, 236, 166, 82}, {160, 178, 200, 176, -1}};
        LayeredBody(p, mochi, 2, 4, 14, false);
        Sheen(p, 118, 126, 62, 24, LV_OPA_80);
        Sheen(p, 90, 148, 12, 12, LV_OPA_70);
        // sakura blossom resting on the head
        int fx = 124, fy = 94;
        static const int dx[] = {0, 13, 8, -8, -13}, dy[] = {-14, -4, 11, 11, -4};
        for (int k = 0; k < 5; ++k)
            Shape(p, fx + dx[k], fy + dy[k], 20, 20, lv_color_darken(c.accent, 30));
        for (int k = 0; k < 5; ++k)
            Shape(p, fx + dx[k], fy + dy[k] - 1, 17, 17, c.accent);
        Shape(p, fx, fy, 11, 11, c.detail);
        rig.face = {160, 204};
        rig.eye_dx = 48;
        rig.scale = 98;
        rig.cheek_dx = 82;
        rig.cheek_dy = 24;
        rig.mouth_dy = 32;
        rig.left = 38;
        rig.right = 282;
        rig.slot = {266, 70};
        rig.slot2 = {54, 70};
        return rig;
    }
    if (g_shape == "robot") {
        // antenna with a softly pulsing light
        Line(p, {160, 74, 160, 40}, 7, c.outline);
        lv_obj_t* glow = Shape(p, 160, 30, 34, 34, c.detail);
        Opa(glow, LV_OPA_30);
        Animate(glow, SetOpa, 255, 60, 1100);
        lv_obj_t* ball = Shape(p, 160, 30, 22, 22, c.detail);
        Grad(ball, lv_color_darken(c.detail, 50), 80, 255);
        Opa(Shape(ball, 0, 0, 7, 6, lv_color_white()), LV_OPA_80);
        lv_obj_set_pos(lv_obj_get_child(ball, 0), Px(5), Px(4));
        // ears
        for (int side = -1; side <= 1; side += 2) {
            Shape(p, 160 + side * 122, 182, 24, 62, c.outline, 9);
            Shape(p, 160 + side * 121, 180, 18, 54, c.shade, 7);
        }
        static const Blob head[] = {{160, 180, 216, 206, 60}};
        LayeredBody(p, head, 1, 5, 12, true);
        Sheen(p, 100, 92, 40, 12, LV_OPA_80);
        // screen with a bezel and a little glare
        Shape(p, 160, 172, 184, 146, c.shade, 46);
        lv_obj_t* screen = Shape(p, 160, 172, 172, 134, c.accent, 40);
        Grad(screen, lv_color_mix(c.ink, c.accent, 40), 120, 255);
        Opa(Shape(p, 106, 118, 30, 6, lv_color_white()), LV_OPA_20);
        // status LEDs
        Shape(p, 142, 262, 9, 9, c.ink);
        Shape(p, 160, 262, 9, 9, c.detail);
        Shape(p, 178, 262, 9, 9, lv_color_hex(0xFFD45E));
        rig.face = {160, 168};
        rig.eye_dx = 40;
        rig.scale = 90;
        rig.cheek_dx = 58;
        rig.cheek_dy = 30;
        rig.mouth_dy = 34;
        rig.left = 32;
        rig.right = 288;
        rig.slot = {270, 52};
        rig.slot2 = {50, 52};
        return rig;
    }
    // cloud (default)
    static const Blob cloud[] = {{160, 240, 232, 96, 48},  {94, 212, 100, 100, -1},
                                 {226, 212, 100, 100, -1}, {112, 152, 116, 116, -1},
                                 {176, 122, 144, 144, -1}, {234, 168, 84, 84, -1}};
    LayeredBody(p, cloud, 6, 4, 14, false);
    Sheen(p, 150, 78, 44, 16, LV_OPA_COVER);
    rig.face = {160, 206};
    rig.eye_dx = 46;
    rig.scale = 98;
    rig.cheek_dx = 80;
    rig.cheek_dy = 26;
    rig.mouth_dy = 32;
    rig.left = 38;
    rig.right = 282;
    rig.slot = {270, 60};
    rig.slot2 = {50, 60};
    return rig;
}

// ---------- features ----------

struct Expression {
    const char* name;
    const char* eyes;   // open large happy closed sleepy squeeze wide heart aside up crossed uneven
                        // half shades wink
    const char* brows;  // "" angry sad raised determined one
    const char*
        mouth;  // smile big open wail frown o tall flat side wavy cat smirk tongue lick kiss yawn
    const char* extra;  // space separated: sparkle joy burst tear tears hearts heart1 anger sweat
                        // question exclaim dots zzz bubble waves note gloom hatch
    uint8_t blush;      // 0 none, 1 soft, 2 strong
};

const Expression kExpressions[] = {
    {"neutral", "open", "", "smile", "", 1},
    {"happy", "happy", "", "big", "sparkle", 1},
    {"laughing", "squeeze", "", "open", "burst", 1},
    {"funny", "happy", "", "cat", "joy", 1},
    {"sad", "open", "sad", "frown", "tear", 0},
    {"angry", "open", "angry", "frown", "anger", 0},
    {"crying", "squeeze", "sad", "wail", "tears", 1},
    {"loving", "heart", "", "big", "hearts", 2},
    {"embarrassed", "aside", "sad", "wavy", "hatch sweat", 2},
    {"surprised", "large", "raised", "o", "exclaim", 1},
    {"shocked", "wide", "raised", "tall", "gloom sweat", 0},
    {"thinking", "up", "one", "side", "dots", 1},
    {"winking", "wink", "", "big", "sparkle", 1},
    {"cool", "shades", "", "smirk", "sparkle", 1},
    {"relaxed", "closed", "", "smile", "note", 1},
    {"delicious", "happy", "", "lick", "", 2},
    {"kissy", "closed", "", "kiss", "heart1", 2},
    {"confident", "open", "determined", "smirk", "sparkle", 1},
    {"sleepy", "sleepy", "", "o", "zzz bubble", 1},
    {"silly", "crossed", "", "tongue", "", 1},
    {"confused", "uneven", "one", "wavy", "question", 0},
    {"listening", "large", "", "smile", "waves", 1},
    {"waking", "half", "", "yawn", "sparkle", 1},
};

const Expression& Find(const char* name) {
    for (auto& e : kExpressions) {
        if (strcmp(e.name, name) == 0)
            return e;
    }
    return kExpressions[0];
}

bool Has(const char* list, const char* token) {
    size_t n = strlen(token);
    for (const char* p = list; (p = strstr(p, token)) != nullptr; p += n) {
        bool start = p == list || p[-1] == ' ';
        bool end = p[n] == '\0' || p[n] == ' ';
        if (start && end)
            return true;
    }
    return false;
}

const lv_color_t kYellow = lv_color_hex(0xFFD45E);
const lv_color_t kBlue = lv_color_hex(0x7CC4FF);
const lv_color_t kPink = lv_color_hex(0xFF5C8A);

// Glossy eye: ink with a lighter iris toward the bottom, a big and a small highlight.
lv_obj_t* GlossyEye(lv_obj_t* p, int x, int y, int w, int h, bool blinks) {
    lv_obj_t* eye = Shape(p, x, y, w, h, g_pal.ink);
    Grad(eye, g_pal.iris, 110, 255);
    int pw = Px(w), ph = Px(h);
    int big = pw * 46 / 100, small = LV_MAX(2, pw * 20 / 100);
    ShapePx(eye, pw * 46 / 100, ph * 10 / 100, big, big, lv_color_white());
    ShapePx(eye, pw * 18 / 100, ph * 62 / 100, small, small, lv_color_white());
    if (blinks && g_eye_count < 2) {
        lv_obj_set_user_data(eye, (void*)(intptr_t)ph);
        g_eyes[g_eye_count++] = eye;
    }
    return eye;
}

// White eye with an ink rim and a pupil offset by (dx, dy).
void WhiteEye(lv_obj_t* p, int x, int y, int d, int pupil, int dx, int dy) {
    lv_obj_t* e = Shape(p, x, y, d, d, lv_color_white());
    lv_obj_set_style_border_color(e, g_pal.ink, 0);
    lv_obj_set_style_border_width(e, LV_MAX(2, Px(4)), 0);
    Shape(p, x + dx, y + dy, pupil, pupil, g_pal.ink);
}

void BuildEye(lv_obj_t* p, const char* kind, int x, int y, int side) {
    std::string k = kind;
    const lv_color_t ink = g_pal.ink;
    int w = S(36), h = S(46);
    if (k == "wink")
        k = side < 0 ? "open" : "happy";
    if (k == "uneven") {
        if (side < 0)
            GlossyEye(p, x, y - S(2), S(40), S(50), true);
        else
            GlossyEye(p, x, y + S(2), S(30), S(36), true);
    } else if (k == "open") {
        GlossyEye(p, x, y, w, h, true);
    } else if (k == "large") {
        GlossyEye(p, x, y, S(42), S(52), true);
    } else if (k == "aside") {
        GlossyEye(p, x - S(7), y + S(4), S(32), S(40), true);
    } else if (k == "up") {
        GlossyEye(p, x + S(6), y - S(5), S(34), S(42), true);
    } else if (k == "happy") {
        Arc(p, x, y + S(10), S(40), 200, 340, S(10), ink);
    } else if (k == "closed") {
        Arc(p, x, y - S(10), S(40), 25, 155, S(9), ink);
    } else if (k == "sleepy") {
        Arc(p, x, y - S(4), S(38), 35, 145, S(8), ink);
    } else if (k == "squeeze") {
        int s = -side;  // ">" on the left, "<" on the right
        Line(p, {x - s * S(12), y - S(13), x + s * S(11), y, x - s * S(12), y + S(13)}, S(9), ink);
    } else if (k == "wide") {
        WhiteEye(p, x, y, S(48), S(12), 0, 0);
    } else if (k == "crossed") {
        WhiteEye(p, x, y, S(46), S(20), side < 0 ? S(9) : -S(9), S(2));
    } else if (k == "heart") {
        lv_obj_t* h1 = Heart(p, x, y + S(2), S(50), kPink);
        Animate(h1, SetY, 0, -Px(4), 420, side > 0 ? 120 : 0);
    } else if (k == "half") {
        // drowsy: the top of the eye hidden under a lid line
        int pw = Px(w), ph = Px(h);
        lv_obj_t* clip = Plain(p);
        lv_obj_set_size(clip, pw, ph * 55 / 100);
        lv_obj_set_pos(clip, Px(x) - pw / 2, Px(y) - ph / 2 + ph * 45 / 100);
        lv_obj_t* eye = GlossyEye(clip, 0, 0, w, h, false);
        lv_obj_set_pos(eye, 0, -ph * 45 / 100);
    } else if (k == "shades") {
        lv_obj_t* lens = Shape(p, x, y, S(60), S(42), lv_color_hex(0x1E1B29), S(16));
        Grad(lens, lv_color_hex(0x3A3450), 60, 255);
        lv_obj_set_style_border_color(lens, ink, 0);
        lv_obj_set_style_border_width(lens, LV_MAX(2, Px(S(4))), 0);
        Line(p, {x - S(16), y - S(4), x - S(6), y - S(13)}, S(5), lv_color_white());
        if (side < 0)
            Line(p, {x + S(30), y - S(10), x + 2 * g_rig.eye_dx - S(30), y - S(10)}, S(6), ink);
    }
}

void BuildBrow(lv_obj_t* p, const char* kind, int x, int y, int side) {
    std::string k = kind;
    if (k.empty())
        return;
    const lv_color_t ink = g_pal.ink;
    int by = y - S(36), half = S(14);
    int in_x = x - side * half, out_x = x + side * half;  // inner end points to the face center
    if (k == "angry") {
        Line(p, {out_x, by - S(6), in_x, by + S(6)}, S(8), ink);
    } else if (k == "determined") {
        Line(p, {out_x, by - S(4), in_x, by + S(3)}, S(7), ink);
    } else if (k == "sad") {
        Line(p, {out_x, by + S(5), in_x, by - S(6)}, S(7), ink);
    } else if (k == "raised" || (k == "one" && side < 0)) {
        Arc(p, x, by + S(4), S(34), 215, 325, S(7), ink);
    } else if (k == "one") {
        Line(p, {out_x, by + S(6), in_x, by + S(8)}, S(7), ink);
    }
}

// D-shaped open mouth with a tongue: a pill clipped to its lower (or upper) half.
lv_obj_t* OpenMouth(lv_obj_t* p, int x, int y, int w, int h, bool upper) {
    int pw = Px(w), ph = Px(h);
    lv_obj_t* clip = Plain(p);
    lv_obj_set_size(clip, pw, ph);
    lv_obj_set_pos(clip, Px(x) - pw / 2, Px(y) - ph / 2);
    lv_obj_t* m = ShapePx(clip, 0, upper ? 0 : -ph, pw, 2 * ph, g_pal.ink);
    lv_obj_set_style_clip_corner(m, true, 0);
    int tw = pw * 62 / 100, th = ph * 80 / 100;
    ShapePx(m, (pw - tw) / 2, upper ? ph - th * 6 / 10 : 2 * ph - th * 7 / 10, tw, th,
            lv_color_hex(0xFF7E96));
    return clip;
}

void SetMouthHeight(void* obj, int32_t h) {
    auto* m = (lv_obj_t*)obj;
    lv_obj_set_height(m, h);
    lv_obj_t* tongue = lv_obj_get_child(m, 0);
    if (tongue != nullptr)
        lv_obj_set_y(tongue, h - lv_obj_get_height(tongue) * 6 / 10);
}

void TalkStep(lv_anim_t* done);
void StartTalk(lv_obj_t* m, int32_t from, int delay) {
    // Alternate between open and nearly closed with random sizes and speeds: reads as syllables.
    bool open = from < Px(S(17));
    int32_t to = open ? Px(S(lv_rand(18, 34))) : Px(S(lv_rand(6, 11)));
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, m);
    lv_anim_set_exec_cb(&a, SetMouthHeight);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, lv_rand(70, 150));
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, TalkStep);
    lv_anim_start(&a);
}
void TalkStep(lv_anim_t* done) {
    auto* m = (lv_obj_t*)done->var;
    StartTalk(m, lv_obj_get_height(m), lv_rand(0, 9) == 0 ? lv_rand(120, 260) : 0);
}

void BuildMouth(lv_obj_t* p, const char* kind, int x, int y) {
    std::string k = kind;
    const lv_color_t ink = g_pal.ink, tongue = lv_color_hex(0xFF7E96);
    if (k == "talk") {
        int pw = Px(S(38)), ph = Px(S(16));
        lv_obj_t* m = ShapePx(p, Px(x) - pw / 2, Px(y - S(8)), pw, ph, ink, Px(S(12)));
        lv_obj_set_style_clip_corner(m, true, 0);
        int tw = pw * 64 / 100, th = Px(S(16));
        ShapePx(m, (pw - tw) / 2, ph - th * 6 / 10, tw, th, tongue);
        StartTalk(m, ph, 0);
    } else if (k == "big") {
        OpenMouth(p, x, y, S(44), S(26), false);
    } else if (k == "open") {
        OpenMouth(p, x, y + S(2), S(52), S(32), false);
    } else if (k == "wail") {
        OpenMouth(p, x, y + S(4), S(42), S(24), true);
    } else if (k == "frown") {
        Arc(p, x, y + S(14), S(32), 215, 325, S(7), ink);
    } else if (k == "o") {
        Shape(p, x, y + S(2), S(16), S(19), ink);
    } else if (k == "yawn") {
        lv_obj_t* m = Shape(p, x, y + S(4), S(24), S(30), ink);
        Grad(m, lv_color_mix(tongue, ink, 120), 120, 255);
    } else if (k == "tall") {
        Shape(p, x, y + S(6), S(24), S(36), ink);
    } else if (k == "flat") {
        Line(p, {x - S(10), y, x + S(10), y}, S(7), ink);
    } else if (k == "side") {
        Line(p, {x + S(2), y + S(3), x + S(20), y - S(2)}, S(7), ink);
    } else if (k == "wavy") {
        Arc(p, x - S(7), y + S(2), S(14), 180, 360, S(6), ink);
        Arc(p, x + S(7), y + S(2), S(14), 0, 180, S(6), ink);
    } else if (k == "cat") {
        Arc(p, x - S(8), y - S(4), S(17), 0, 180, S(6), ink);
        Arc(p, x + S(8), y - S(4), S(17), 0, 180, S(6), ink);
    } else if (k == "smirk") {
        Arc(p, x + S(4), y - S(12), S(32), 10, 120, S(7), ink);
    } else if (k == "tongue") {
        OpenMouth(p, x, y, S(34), S(18), false);
        lv_obj_t* t = Shape(p, x + S(4), y + S(12), S(18), S(22), tongue);
        Line(p, {x + S(4), y + S(6), x + S(4), y + S(14)}, S(3), lv_color_darken(tongue, 50));
        (void)t;
    } else if (k == "lick") {
        Arc(p, x, y - S(10), S(32), 25, 155, S(7), ink);
        Shape(p, x + S(14), y + S(5), S(17), S(19), tongue);
    } else if (k == "kiss") {
        Arc(p, x + S(2), y - S(6), S(16), 270, 90, S(6), ink);
        Arc(p, x + S(2), y + S(8), S(16), 270, 90, S(6), ink);
    } else {  // smile
        Arc(p, x, y - S(10), S(32), 25, 155, S(7), ink);
    }
}

void Blush(lv_obj_t* p, int x, int y, int strength) {
    if (strength == 0)
        return;
    int g = strength > 1 ? 8 : 0;
    Opa(Shape(p, x, y, S(42 + g), S(24 + g / 2), g_pal.cheek),
        strength > 1 ? LV_OPA_50 : LV_OPA_30);
    Opa(Shape(p, x, y, S(28 + g), S(15 + g / 2), g_pal.cheek),
        strength > 1 ? LV_OPA_80 : LV_OPA_50);
}

lv_obj_t* Sparkle(lv_obj_t* p, int x, int y, int s, lv_color_t color) {
    lv_obj_t* g = Group(p, x, y, s, s);
    int m = s / 2;
    Line(g, {m, 4, m, s - 4}, 7, color);
    Line(g, {4, m, s - 4, m}, 7, color);
    Shape(g, m, m, s / 4, s / 4, color);
    return g;
}

void BuildExtras(lv_obj_t* p, const Expression& e) {
    const Rig& r = g_rig;
    const char* x = e.extra;
    int fx = r.face.x, fy = r.face.y, ey = fy - S(8), my = fy + r.mouth_dy;
    Pt t = r.slot, t2 = r.slot2;
    if (Has(x, "sparkle")) {
        lv_obj_t* a = Sparkle(p, t.x - 4, t.y, 44, kYellow);
        lv_obj_t* b = Sparkle(p, t.x + 24, t.y + 34, 24, kYellow);
        Animate(a, SetOpa, 255, 60, 700);
        Animate(b, SetOpa, 60, 255, 700);
    }
    if (Has(x, "burst")) {
        for (int side = -1; side <= 1; side += 2) {
            Pt s = side > 0 ? t : t2;
            lv_obj_t* g = Group(p, s.x, s.y + 12, 52, 52);
            int o = side > 0 ? 0 : 52, m = side > 0 ? 1 : -1;  // mirror for the left side
            Line(g, {o + m * 10, 8, o + m * 18, 22}, 7, kYellow);
            Line(g, {o + m * 30, 4, o + m * 30, 20}, 7, kYellow);
            Line(g, {o + m * 46, 16, o + m * 36, 26}, 7, kYellow);
            Animate(g, SetOpa, 255, 70, 320);
        }
    }
    if (Has(x, "joy")) {
        for (int side = -1; side <= 1; side += 2) {
            lv_obj_t* d = Drop(p, fx + side * (r.eye_dx + S(30)), ey - S(8), 20, kBlue);
            Animate(d, SetY, 0, -Px(5), 300, side > 0 ? 150 : 0);
        }
    }
    if (Has(x, "tear")) {
        lv_obj_t* d = Drop(p, fx - r.eye_dx - S(14), ey + S(26), 20, kBlue);
        Animate(d, SetY, 0, Px(3), 900);
    }
    if (Has(x, "tears")) {
        for (int side = -1; side <= 1; side += 2) {
            for (int i = 0; i < 2; ++i) {
                lv_obj_t* d = Drop(p, fx + side * (r.eye_dx + S(12)), ey + S(20), 22, kBlue);
                FloatAway(d, 40, 1000, i * 500 + (side > 0 ? 250 : 0));
            }
        }
    }
    if (Has(x, "hearts")) {
        for (int i = 0; i < 3; ++i) {
            lv_obj_t* h =
                Heart(p, t.x + (i == 1 ? 16 : -10), t.y + 34 - (i == 2 ? 8 : 0), 30 + i * 3, kPink);
            FloatAway(h, -36, 1800, i * 600);
        }
    }
    if (Has(x, "heart1")) {
        lv_obj_t* h = Heart(p, t.x - 10, t.y + 30, 36, kPink);
        FloatAway(h, -30, 1800, 0);
    }
    if (Has(x, "anger")) {
        lv_obj_t* g = Group(p, t.x, t.y + 4, 56, 56);
        lv_color_t red = lv_color_hex(0xFF5A5A);
        Arc(g, 13, 13, 26, 0, 90, 8, red);
        Arc(g, 43, 13, 26, 90, 180, 8, red);
        Arc(g, 43, 43, 26, 180, 270, 8, red);
        Arc(g, 13, 43, 26, 270, 360, 8, red);
        Animate(g, SetOpa, 255, 110, 380);
    }
    if (Has(x, "sweat")) {
        lv_obj_t* d = Drop(p, t.x - 4, t.y + 36, 30, kBlue);
        Animate(d, SetY, 0, Px(8), 700, 0, 200);
    }
    if (Has(x, "question")) {
        lv_obj_t* g = Group(p, t.x, t.y + 6, 44, 66);
        Arc(g, 22, 19, 30, 180, 90, 10, kYellow);
        Line(g, {22, 35, 22, 42}, 10, kYellow);
        Shape(g, 22, 56, 13, 13, kYellow);
        Animate(g, SetY, 0, -Px(6), 600);
    }
    if (Has(x, "exclaim")) {
        lv_obj_t* g = Group(p, t.x, t.y + 6, 28, 66);
        Line(g, {14, 8, 14, 38}, 11, kYellow);
        Shape(g, 14, 55, 14, 14, kYellow);
        Animate(g, SetY, 0, -Px(6), 260);
    }
    if (Has(x, "dots")) {
        static const int dx[] = {-30, -12, 12}, dy[] = {40, 20, -4}, d[] = {12, 17, 25};
        for (int i = 0; i < 3; ++i) {
            lv_obj_t* o = Shape(p, t.x + dx[i], t.y + dy[i], d[i], d[i], lv_color_hex(0xDCD8EE));
            Animate(o, SetOpa, 50, 255, 450, i * 300, 300);
        }
    }
    if (Has(x, "zzz")) {
        static const int sz[] = {15, 21, 28};
        for (int i = 0; i < 3; ++i) {
            int zx = t.x - 26 + i * 20, zy = t.y + 36 - i * 22, s = sz[i];
            lv_obj_t* z = Line(p,
                               {zx - s / 2, zy - s / 2, zx + s / 2, zy - s / 2, zx - s / 2,
                                zy + s / 2, zx + s / 2, zy + s / 2},
                               6, lv_color_hex(0xB9B0FF));
            if (!g_calm) {  // night: static zzz, no redraws
                Animate(z, SetOpa, 50, 255, 800, i * 400);
                Animate(z, SetY, 0, -Px(6), 1600, i * 400);
            }
        }
    }
    if (Has(x, "bubble")) {
        int d = Px(S(20));
        lv_obj_t* b =
            ShapePx(p, Px(fx + S(26)) - d / 2, Px(my - S(2)) - d / 2, d, d, lv_color_hex(0xBFE6FF));
        Opa(b, LV_OPA_50);
        lv_obj_set_style_border_color(b, lv_color_hex(0xE8F7FF), 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_user_data(b, (void*)(intptr_t)d);
        Animate(b, SetSizeCentered, Px(S(10)), Px(S(32)), 1700);
    }
    if (Has(x, "waves")) {
        lv_color_t c = lv_color_hex(0x8ED0FF);
        for (int side = -1; side <= 1; side += 2) {
            int edge = side < 0 ? r.left : r.right;
            int gap = side < 0 ? r.left - 6 : 314 - r.right;
            for (int i = 0; i < 2; ++i) {
                int rad = i == 0 ? gap * 45 / 100 : gap * 88 / 100;
                lv_obj_t* a = side < 0 ? Arc(p, edge, fy, 2 * rad, 140, 220, 8, c)
                                       : Arc(p, edge, fy, 2 * rad, 320, 40, 8, c);
                Animate(a, SetOpaPulse, 50, 255, 600, i * 300);
            }
        }
    }
    if (Has(x, "note")) {
        lv_obj_t* g = Group(p, t.x, t.y + 4, 42, 56);
        lv_color_t c = lv_color_hex(0xB9E6FF);
        Shape(g, 13, 43, 21, 16, c);
        Line(g, {22, 42, 22, 9}, 6, c);
        Line(g, {22, 9, 36, 17}, 6, c);
        Animate(g, SetY, 0, -Px(8), 900);
        Animate(g, SetX, 0, Px(4), 1300);
    }
    if (Has(x, "gloom")) {
        lv_obj_t* g = Group(p, t2.x, t2.y + 8, 52, 52);
        lv_color_t c = lv_color_hex(0xA596FF);
        Line(g, {10, 14, 10, 38}, 7, c);
        Line(g, {26, 6, 26, 44}, 7, c);
        Line(g, {42, 14, 42, 38}, 7, c);
        Animate(g, SetOpa, 255, 70, 500);
    }
    if (Has(x, "hatch")) {
        lv_color_t c = lv_color_darken(g_pal.cheek, 50);
        for (int side = -1; side <= 1; side += 2) {
            int cx = fx + side * r.cheek_dx, cy = fy + r.cheek_dy;
            for (int i = -1; i <= 1; ++i)
                Line(p, {cx + i * S(9) - S(3), cy + S(6), cx + i * S(9) + S(3), cy - S(6)}, 3, c);
        }
    }
}

void RebuildMouth() {
    if (g_features == nullptr)
        return;
    if (g_mouth != nullptr)
        lv_obj_delete(g_mouth);
    g_mouth = Layer(g_features);
    const Expression& e = Find(g_emotion.c_str());
    BuildMouth(g_mouth, g_talking ? "talk" : e.mouth, g_rig.face.x, g_rig.face.y + g_rig.mouth_dy);
}

void RebuildFeatures() {
    g_eye_count = 0;
    g_eyes[0] = g_eyes[1] = nullptr;
    if (g_features != nullptr)
        lv_obj_delete(g_features);
    g_mouth = nullptr;
    g_features = Layer(g_root);
    const Expression& e = Find(g_emotion.c_str());
    const Rig& r = g_rig;
    int ey = r.face.y - S(8);
    for (int side = -1; side <= 1; side += 2) {
        Blush(g_features, r.face.x + side * r.cheek_dx, r.face.y + r.cheek_dy, e.blush);
    }
    for (int side = -1; side <= 1; side += 2) {
        BuildEye(g_features, e.eyes, r.face.x + side * r.eye_dx, ey, side);
        BuildBrow(g_features, e.brows, r.face.x + side * r.eye_dx, ey, side);
    }
    g_glance_ok = strcmp(e.eyes, "open") == 0 || strcmp(e.eyes, "large") == 0;
    RebuildMouth();
    BuildExtras(g_features, e);
    // little hop when the expression changes
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_features);
    lv_anim_set_exec_cb(&a, SetY);
    lv_anim_set_values(&a, Px(7), 0);
    lv_anim_set_duration(&a, 320);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);
}

}  // namespace

void FaceShow(lv_obj_t* parent, const lv_font_t* font, const char* emotion) {
    (void)font;
    auto character = ActiveCharacter();
    if (g_root == nullptr || g_character != character.id) {
        g_eye_count = 0;
        if (g_root != nullptr)
            lv_obj_delete(g_root);
        g_features = nullptr;
        g_mouth = nullptr;
        g_character = character.id;
        g_shape = character.look.shape;
        LoadPalette(character.look, character.id);
        g_size = LV_MIN(lv_display_get_horizontal_resolution(nullptr),
                        lv_display_get_vertical_resolution(nullptr)) *
                 44 / 100;
        g_root = Plain(parent);
        lv_obj_set_size(g_root, g_size, g_size);
        lv_obj_align(parent, LV_ALIGN_CENTER, 0, -g_size / 9);  // leave room for subtitles below
        g_body = Layer(g_root);
        g_rig = BuildBody(g_body);
        if (!g_calm)
            Animate(g_root, SetY, 0, -LV_MAX(2, Px(5)), 1900);  // gentle breathing
        if (g_life == nullptr)
            g_life = lv_timer_create(LifeTick, 2500, nullptr);
        g_emotion.clear();
    }
    if (g_emotion != emotion) {
        g_emotion = emotion;
        RebuildFeatures();
    }
}

void FaceSetCalm(bool calm) {
    if (g_calm == calm)
        return;
    g_calm = calm;
    if (g_root == nullptr)
        return;
    if (calm) {
        // Breathing moves the whole gradient-filled face: the largest idle redraw. At night the
        // screen is dimmed and nobody is watching, so hold still.
        lv_anim_delete(g_root, SetY);
        lv_obj_set_style_translate_y(g_root, 0, 0);
    } else {
        Animate(g_root, SetY, 0, -LV_MAX(2, Px(5)), 1900);
    }
    RebuildFeatures();  // eyes/zzz pick up the calm flag
}

void FaceSetTalking(bool talking) {
    if (g_talking == talking || g_root == nullptr) {
        g_talking = talking;
        return;
    }
    g_talking = talking;
    RebuildMouth();
}

}  // namespace assistant
