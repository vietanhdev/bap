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
void RefreshIdleScreen();  // redraws the resting status and date line (e.g. after a language
                           // change)
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
void SetWakeHandler(void (*handler)());                // board: wake the screen
void SetVisionHandler(void (*handler)(bool enabled));  // board: start/stop on-camera detection
void ApplyVision(bool enabled);
// True while a firmware upload writes flash: pollers back off (I2C reads time out meanwhile).
bool OtaInProgress();
void SetOtaInProgress(bool busy);
void InstallFontFallback();  // Korean/Chinese glyphs from the assets partition
// True between 22:00 and 06:00 when "do not disturb" is on: no wake word (touch/wheel still work).
bool QuietHours();
// Screen rest: any touch, wheel, menu, web action or person in view counts as activity. After a
// few quiet minutes the scheduler dims the backlight and stills the face (saves power and SPI
// traffic); the next activity brings it back.
void NoteActivity();
int SecondsSinceActivity();
// Status cards pushed from the LAN (assistant_notify.cc).
std::string LatestCard();                 // text of a recent card, "" if none
void RegisterNotifyRoutes(void* server);  // httpd_handle_t
void RegisterPerfRoutes(void* server);    // GET /api/perf (assistant_perf.cc)
}  // namespace assistant
