#pragma once
// Native LVGL character faces. Call with the display lock held.
#include <lvgl.h>

namespace assistant {

// Create or update the face inside `parent` for the active character and the given emotion.
void FaceShow(lv_obj_t* parent, const lv_font_t* font, const char* emotion);
// Animate the mouth while the assistant is speaking.
void FaceSetTalking(bool talking);

}  // namespace assistant
