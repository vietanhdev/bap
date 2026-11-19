#pragma once
// On-screen keyboard for typing a Wi-Fi password on the round display (see assistant_keyboard.cc).
#include <functional>
#include <string>

#include "assistant.h"

namespace assistant {

// Shows the keyboard over the menu. on_done runs on the main task with the typed text.
void KeyboardOpen(const std::string& title, std::function<void(std::string)> on_done);
void KeyboardClose();
bool KeyboardIsOpen();
void KeyboardTouch(TouchEvent event, int x, int y);
void KeyboardRotate(bool forward);  // the wheel moves a highlight across the keys
void KeyboardClick();               // ... and a click presses the highlighted key

}  // namespace assistant
