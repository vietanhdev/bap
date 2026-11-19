#pragma once
// Scroll-wheel menu on the round screen (see assistant_menu.cc).
#include <functional>

namespace assistant {

bool MenuIsOpen();
void MenuOpen();
void MenuOpenQuick();   // swipe down on the resting screen: volume, brightness, Wi-Fi
void MenuOpenApps();    // swipe up on the resting screen
void MenuNoteTyping();
void MenuInitWifiScan();  // call at boot, before Wi-Fi starts (see assistant_menu.cc)  // keeps the menu open while the keyboard is in use
void MenuBack();  // one level up; closes the menu at the top level
void MenuRotate(bool forward);
void MenuDrag(int dy);  // finger position relative to where the drag started
void MenuDragEnd();
void MenuClick();
void MenuTick();  // call about once a second; closes an idle menu
// Board hook: cut power. Return false when it cannot (e.g. running on USB power).
void SetPowerOffHandler(std::function<bool()> handler);
void PowerOffNow();  // powers off, or says to unplug the cable first

}  // namespace assistant
