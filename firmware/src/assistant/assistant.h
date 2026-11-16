#pragma once
#include <string>

namespace assistant {
void RegisterTools();   // Vietnamese + utility tools on the device MCP server
void StartWebServer();  // settings UI on http://<device-ip>/
void StartScheduler();  // timers and alarms
bool SyncClock();       // set the clock from an HTTP Date header
// Speak to the assistant as if the user said `text` (opens a session when idle).
void Say(const std::string& text);
void LogTurn(const char* role, const std::string& text);  // conversation log for the web UI
std::string LogJson();
std::string IdleLine();  // "Thứ hai 16/11\n8/10 âm lịch" for the resting screen
std::string AlarmsText();
// Scroll wheel (see assistant_input.cc). Rotate/Click return true when they consumed the event.
bool OnWheelRotate(bool clockwise);
bool OnWheelClick();
void OnWheelDoubleClick();
void WheelTick();
enum class TouchEvent { kDown, kMove, kUp };
void OnTouch(TouchEvent event, int x, int y);
// Camera person detection (Himax, on device). Present: called for every frame with a person.
// Arrived: once per visit; returns true when Gemini should also greet out loud.
void OnPersonPresent();
bool OnPersonArrived(int count);
void SetWakeHandler(void (*handler)());  // board: wake the screen
void SetVisionHandler(void (*handler)(bool enabled));  // board: start/stop on-camera detection
void ApplyVision(bool enabled);  // screen pixels; called from the touch task
void InstallFontFallback();  // Korean/Chinese glyphs from the assets partition
// Status cards pushed from the LAN (assistant_notify.cc).
std::string LatestCard();                 // text of a recent card, "" if none
void RegisterNotifyRoutes(void* server);  // httpd_handle_t
void RegisterPerfRoutes(void* server);    // GET /api/perf (assistant_perf.cc)
}  // namespace assistant
