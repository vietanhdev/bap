// Korean and Chinese glyphs for subtitles and labels. The Vietnamese Nunito fonts cover Latin
// only; a 24 px Noto Sans KR/SC subset (KS X 1001 Hangul + GB2312 level 1, ~1.7 MB) lives in the
// assets partition and is chained as the LVGL fallback of both text fonts. The font is read in
// place from the memory-mapped partition, so it costs almost no RAM.
#include <esp_log.h>
#include <cbin_font.h>
#include <lvgl.h>

#include "assets.h"
#include "board.h"
#include "display/display.h"

extern "C" lv_font_t font_chat_24;  // not const, see font_chat_24.c

namespace assistant {

void InstallFontFallback() {
    static lv_font_t* cjk = nullptr;
    if (cjk != nullptr)
        return;
    void* data = nullptr;
    size_t size = 0;
    if (!Assets::GetInstance().GetAssetData("font_cjk_24_4.bin", data, size)) {
        ESP_LOGW("Fonts", "No Korean/Chinese font in assets");
        return;
    }
    cjk = cbin_font_create(static_cast<uint8_t*>(data));
    if (cjk == nullptr)
        return;
    auto display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);
    font_chat_24.fallback = cjk;
    // The main text font is created at runtime from the assets, so it is writable.
    auto* text =
        const_cast<lv_font_t*>(lv_obj_get_style_text_font(lv_screen_active(), LV_PART_MAIN));
    if (text != nullptr && text != &font_chat_24 && text->fallback == nullptr)
        text->fallback = cjk;
    ESP_LOGI("Fonts", "Korean/Chinese fallback font ready (%u KB)", (unsigned)(size / 1024));
}

}  // namespace assistant
