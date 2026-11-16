#pragma once
// Scroll-wheel menu on the round screen (see assistant_menu.cc).
#include <functional>

namespace assistant {

bool MenuIsOpen();
void MenuOpen();
void MenuBack();                 // one level up; closes the menu at the top level
void MenuRotate(bool forward);
void MenuClick();
void MenuTick();                 // call about once a second; closes an idle menu
// Board hook: cut power. Return false when it cannot (e.g. running on USB power).
void SetPowerOffHandler(std::function<bool()> handler);

}  // namespace assistant
