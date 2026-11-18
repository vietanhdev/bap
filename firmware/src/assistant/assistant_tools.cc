// Tools the assistant can call: Vietnamese information (âm lịch, thời tiết, tin tức, tỷ giá),
// timers/alarms, notes and long-term memory. Everything runs on the device; data sources are
// public endpoints that need no API key.
#include <esp_log.h>
#include <esp_pm.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

#include <sys/time.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <ctime>
#include <mutex>
#include <vector>

#include "application.h"
#include "assets/lang_config.h"
#include "assistant.h"
#include "assistant_menu.h"
#include "assistant_config.h"
#include "assistant_face.h"
#include "assistant_sd.h"
#include "assistant_zing.h"
#include "board.h"
#include "boards/common/backlight.h"
#include "boards/common/wifi_board.h"
#include "display/display.h"
#include "mcp_server.h"
#include "protocols/gemini_live_protocol.h"
#include "protocols/ticket_ssl.h"
#include "text_fold.h"
#include "vn_lunar.h"

#define TAG "AssistantTools"

namespace assistant {
namespace {

// ---------- helpers ----------

std::expected<std::string, std::string> HttpGet(const std::string& url) {
    auto http = FastNetwork()->CreateHttp(2);
    http->SetTimeout(8000);
    http->SetHeader("User-Agent", "Mozilla/5.0 (Bap assistant)");
    if (!http->Open("GET", url)) {
        vTaskDelay(pdMS_TO_TICKS(500));  // one retry: DNS or Wi-Fi may still be settling
        if (!http->Open("GET", url))
            return std::unexpected("Không kết nối được máy chủ");
    }
    auto status = http->GetStatusCode();
    if (!status || *status != 200) {
        http->Close();
        return std::unexpected("Máy chủ trả về lỗi");
    }
    std::string body = http->ReadAll();
    http->Close();
    return body;
}

std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.')
            out += c;
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string Between(const std::string& s, const char* a, const char* b, size_t from, size_t* end) {
    size_t i = s.find(a, from);
    if (i == std::string::npos)
        return {};
    i += strlen(a);
    size_t j = s.find(b, i);
    if (j == std::string::npos)
        return {};
    if (end)
        *end = j + strlen(b);
    return s.substr(i, j - i);
}

std::string CleanXmlText(std::string t) {
    auto strip = [&t](const char* a) {
        for (size_t p; (p = t.find(a)) != std::string::npos;)
            t.erase(p, strlen(a));
    };
    strip("<![CDATA[");
    strip("]]>");
    const char* ent[][2] = {{"&amp;", "&"},  {"&quot;", "\""}, {"&#39;", "'"},
                            {"&apos;", "'"}, {"&lt;", "<"},    {"&gt;", ">"}};
    for (auto& e : ent) {
        for (size_t p; (p = t.find(e[0])) != std::string::npos;)
            t.replace(p, strlen(e[0]), e[1]);
    }
    size_t a = t.find_first_not_of(" \n\r\t");
    size_t b = t.find_last_not_of(" \n\r\t");
    return a == std::string::npos ? "" : t.substr(a, b - a + 1);
}

bool Now(struct tm* out, time_t* epoch = nullptr) {
    time_t now = time(nullptr);
    if (now < 1700000000)
        return false;
    localtime_r(&now, out);
    if (epoch)
        *epoch = now;
    return true;
}

const char* WeatherText(int code) {
    switch (code) {
        case 0:
            return "trời quang";
        case 1:
            return "ít mây";
        case 2:
            return "có mây";
        case 3:
            return "nhiều mây";
        case 45:
        case 48:
            return "sương mù";
        case 51:
        case 53:
        case 55:
            return "mưa phùn";
        case 61:
            return "mưa nhẹ";
        case 63:
            return "mưa vừa";
        case 65:
            return "mưa to";
        case 80:
        case 81:
            return "mưa rào";
        case 82:
            return "mưa rào rất to";
        case 95:
        case 96:
        case 99:
            return "dông";
        default:
            return "thời tiết không rõ";
    }
}

// ---------- JSON string lists in NVS ----------

std::vector<std::string> ListItems(const char* key) {
    std::vector<std::string> items;
    cJSON* arr = cJSON_Parse(LoadList(key).c_str());
    cJSON* it = nullptr;
    cJSON_ArrayForEach (it, arr) {
        if (cJSON_IsString(it))
            items.push_back(it->valuestring);
    }
    cJSON_Delete(arr);
    return items;
}

void StoreItems(const char* key, const std::vector<std::string>& items) {
    cJSON* arr = cJSON_CreateArray();
    for (auto& s : items)
        cJSON_AddItemToArray(arr, cJSON_CreateString(s.c_str()));
    char* txt = cJSON_PrintUnformatted(arr);
    SaveList(key, txt);
    cJSON_free(txt);
    cJSON_Delete(arr);
}

std::string Numbered(const std::vector<std::string>& items, const char* empty) {
    if (items.empty())
        return empty;
    std::string out;
    for (size_t i = 0; i < items.size(); ++i)
        out += std::to_string(i + 1) + ". " + items[i] + "\n";
    return out;
}

// ---------- timers and alarms ----------

struct Alarm {
    int id;
    time_t at;  // next fire time (epoch)
    bool daily;
    std::string label;
};

std::mutex g_alarm_mutex;
std::vector<Alarm> g_alarms;
int g_next_alarm_id = 1;

void PersistAlarms() {  // only wall-clock alarms survive a reboot
    cJSON* arr = cJSON_CreateArray();
    for (auto& a : g_alarms) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "at", (double)a.at);
        cJSON_AddBoolToObject(o, "daily", a.daily);
        cJSON_AddStringToObject(o, "label", a.label.c_str());
        cJSON_AddItemToArray(arr, o);
    }
    char* txt = cJSON_PrintUnformatted(arr);
    SaveList("alarms", txt);
    cJSON_free(txt);
    cJSON_Delete(arr);
}

void LoadAlarms() {
    cJSON* arr = cJSON_Parse(LoadList("alarms").c_str());
    cJSON* o = nullptr;
    cJSON_ArrayForEach (o, arr) {
        auto at = cJSON_GetObjectItem(o, "at");
        auto label = cJSON_GetObjectItem(o, "label");
        if (!cJSON_IsNumber(at))
            continue;
        g_alarms.push_back({g_next_alarm_id++, (time_t)at->valuedouble,
                            cJSON_IsTrue(cJSON_GetObjectItem(o, "daily")) != 0,
                            cJSON_IsString(label) ? label->valuestring : ""});
    }
    cJSON_Delete(arr);
}

std::string DescribeAlarms() {
    std::lock_guard<std::mutex> lock(g_alarm_mutex);
    if (g_alarms.empty())
        return "Không có hẹn giờ hay báo thức nào.";
    std::string out;
    time_t now = time(nullptr);
    for (auto& a : g_alarms) {
        struct tm t;
        localtime_r(&a.at, &t);
        char buf[160];
        long left = (long)(a.at - now);
        snprintf(buf, sizeof(buf), "#%d: %02d:%02d%s, còn %ld phút %ld giây, nội dung: %s\n", a.id,
                 t.tm_hour, t.tm_min, a.daily ? " hằng ngày" : "", left / 60, left % 60,
                 a.label.empty() ? "(không)" : a.label.c_str());
        out += buf;
    }
    return out;
}

// Backlight and face while resting, checked every second (after 15 quiet minutes the backlight
// switches off; NoteActivity() switches it back on):
//   - night (22:00-06:00, "night mode" on): backlight at most 20 %, still face;
//   - nobody around for kScreenRestS (no touch, wheel, menu, web action or person in view):
//     backlight at most 6 %, still face. The clock and date stay readable.
// Any activity or a conversation restores the user's brightness through the backlight's own fade.
std::atomic<bool> g_night_dim_now{false};
constexpr int kScreenRestS = 5 * 60;  // dim
constexpr int kScreenOffS = 15 * 60;  // backlight off

std::mutex g_busy_mutex;
esp_pm_lock_handle_t g_busy_lock = nullptr;
bool g_busy = false;

void ApplyScreenRest() {
    static int dim_level = -1;  // backlight cap in force, -1 = none
    static bool calm = false;
    auto& app = Application::GetInstance();
    bool idle = app.GetDeviceState() == kDeviceStateIdle;
    if (!idle)
        NoteActivity();
    SetBusy(!idle || OtaInProgress() || MenuIsOpen());
    static bool enabled = true;
    static int checks = 0;
    if (checks++ % 15 == 0)  // the setting is read from NVS, so only every 15 s
        enabled = LoadConfig().rest;
    int quiet_s = SecondsSinceActivity();
    bool rest = enabled && idle && quiet_s >= kScreenRestS && !OtaInProgress();
    bool off = rest && quiet_s >= kScreenOffS;
    bool night = idle && g_night_dim_now;
    int want = off ? 0 : rest ? 6 : night ? 20 : -1;
    if (off && !ScreenIsOff()) {
        SetScreenOff(true);  // NoteActivity() brings the backlight back
        if (SecondsSinceActivity() < kScreenOffS)
            NoteActivity();  // a touch landed just now: stay on
    }
    if (!off && dim_level == 0)
        dim_level = -2;  // woken by NoteActivity(): re-apply whatever cap is due now
    auto backlight = Board::GetInstance().GetBacklight();
    if (backlight != nullptr && want != dim_level) {
        if (want < 0)
            backlight->RestoreBrightness();
        else if (dim_level >= 0 || backlight->brightness() > want)
            backlight->SetBrightness(want, false);
        dim_level = want;
    }
    bool want_calm = rest || night;
    if (want_calm != calm) {
        calm = want_calm;
        app.Schedule([want_calm]() {
            DisplayLockGuard lock(Board::GetInstance().GetDisplay());
            FaceSetCalm(want_calm);
        });
    }
}

void SchedulerTask(void*) {
    int ticks = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        time_t now = time(nullptr);
        ++ticks;
        WheelTick();
        // Resting screen: keep the date and lunar date on the bottom line.
        static int idle_seconds = 0;
        idle_seconds =
            Application::GetInstance().GetDeviceState() == kDeviceStateIdle ? idle_seconds + 1 : 0;
        if (idle_seconds == 3 || (idle_seconds > 0 && idle_seconds % 30 == 0)) {
            // Alternate the date line with the latest status card from the computer, if any.
            auto card = LatestCard();
            auto line = (!card.empty() && (idle_seconds / 30) % 2 == 1) ? card : IdleLine();
            if (!line.empty()) {
                Application::GetInstance().Schedule([line]() {
                    if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
                        Board::GetInstance().GetDisplay()->SetChatMessage("system", line.c_str());
                    }
                });
            }
        }
        ApplyScreenRest();
        // Retry every 20 s until the clock is set, then refresh every 6 hours.
        if ((now < 1700000000 && ticks % 20 == 1) || ticks % 21600 == 0)
            SyncClock();
        if (now < 1700000000)
            continue;
        if (ticks % 60 == 5) {  // dim the screen and lower the speaker at night
            struct tm tm_now;
            localtime_r(&now, &tm_now);
            bool night = tm_now.tm_hour >= 22 || tm_now.tm_hour < 6;
            bool idle = Application::GetInstance().GetDeviceState() == kDeviceStateIdle;
            g_night_dim_now = night && LoadConfig().night_dim;
            // Do-not-disturb boundaries (22:00 / 06:00): re-apply the wake word state while
            // resting.
            {
                static int last_allowed = -1;
                int allowed = WakeWordAllowed() ? 1 : 0;
                if (idle && allowed != last_allowed) {
                    last_allowed = allowed;
                    Application::GetInstance().Schedule([allowed]() {
                        auto& app = Application::GetInstance();
                        if (app.GetDeviceState() == kDeviceStateIdle)
                            app.GetAudioService().EnableWakeWordDetection(allowed);
                    });
                }
            }
            // Quiet nights: cap the speaker for the whole night (conversations included) and give
            // the old volume back in the morning.
            // Low battery: one gentle reminder per discharge (not at night).
            static bool battery_warned = false;
            int level = 0;
            bool charging = false, discharging = false;
            if (Board::GetInstance().GetBatteryLevel(level, charging, discharging)) {
                if (charging || level > 25) {
                    battery_warned = false;
                } else if (discharging && level <= 15 && !battery_warned && !night && idle) {
                    battery_warned = true;
                    Application::GetInstance().Schedule([level]() {
                        auto display = Board::GetInstance().GetDisplay();
                        display->SetEmotion("sad");
                        std::string msg = Localize("Pin còn") + " " + std::to_string(level) +
                                          "%. " + Localize("Cắm sạc giúp mình nhé!");
                        display->SetChatMessage("system", msg.c_str());
                    });
                }
            }
            static int saved_volume = -1;
            constexpr int kNightVolume = 35;
            auto codec = Board::GetInstance().GetAudioCodec();
            if (LoadConfig().night_dim && night) {
                if (codec->output_volume() > kNightVolume) {
                    if (saved_volume < 0)
                        saved_volume = codec->output_volume();
                    codec->SetOutputVolume(kNightVolume);
                }
            } else if (saved_volume >= 0) {
                codec->SetOutputVolume(saved_volume);
                saved_volume = -1;
            }
        }
        std::vector<Alarm> due;
        {
            std::lock_guard<std::mutex> lock(g_alarm_mutex);
            bool changed = false;
            for (auto it = g_alarms.begin(); it != g_alarms.end();) {
                if (it->at <= now) {
                    // Skip alarms that expired long ago while the device was off.
                    if (now - it->at < 600)
                        due.push_back(*it);
                    if (it->daily) {
                        while (it->at <= now)
                            it->at += 86400;
                        ++it;
                    } else {
                        it = g_alarms.erase(it);
                    }
                    changed = true;
                } else {
                    ++it;
                }
            }
            if (changed)
                PersistAlarms();
        }
        for (auto& a : due) {
            ESP_LOGI(TAG, "Alarm fired: %s", a.label.c_str());
            auto& app = Application::GetInstance();
            app.PlaySound(Lang::Sounds::OGG_VIBRATION);
            vTaskDelay(pdMS_TO_TICKS(1200));
            Say("<hệ thống>Đã đến giờ hẹn" + (a.label.empty() ? std::string("") : ": " + a.label) +
                ". Hãy báo cho người dùng một cách dễ thương. Nếu nội dung là một yêu cầu (ví dụ "
                "đọc thời "
                "tiết, đọc tin, xem lịch âm) thì thực hiện luôn bằng công cụ.</hệ thống>");
        }
    }
}

}  // namespace

namespace {
std::mutex g_log_mutex;
std::vector<std::pair<std::string, std::string>> g_log;  // (role, text), newest last
}  // namespace

void LogTurn(const char* role, const std::string& text) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    // Streaming transcripts grow in place: replace the last entry while it is a prefix.
    if (!g_log.empty() && g_log.back().first == role && text.rfind(g_log.back().second, 0) == 0) {
        g_log.back().second = text;
        return;
    }
    g_log.emplace_back(role, text);
    if (g_log.size() > 24)
        g_log.erase(g_log.begin());
}

std::string LogJson() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    cJSON* arr = cJSON_CreateArray();
    for (auto& e : g_log) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "role", e.first.c_str());
        cJSON_AddStringToObject(o, "text", e.second.c_str());
        cJSON_AddItemToArray(arr, o);
    }
    char* txt = cJSON_PrintUnformatted(arr);
    std::string out(txt);
    cJSON_free(txt);
    cJSON_Delete(arr);
    return out;
}

// Replaces each "{key}" in a localized template with its value.
static std::string FillTemplate(std::string text,
                                std::initializer_list<std::pair<const char*, std::string>> values) {
    for (auto& [key, value] : values) {
        std::string token = std::string("{") + key + "}";
        for (size_t pos; (pos = text.find(token)) != std::string::npos;)
            text.replace(pos, token.size(), value);
    }
    return text;
}

// Two-line date under the resting face: weekday and solar date, then the lunar date or festival.
// Templates and weekday names are UI strings, translated through the languages[].ui table.
std::string IdleLine() {
    struct tm t{};
    if (!Now(&t))
        return "";
    static const char* wd[] = {"Chủ nhật", "Thứ hai", "Thứ ba", "Thứ tư",
                               "Thứ năm",  "Thứ sáu", "Thứ bảy"};
    auto l = vn_lunar::FromSolar(t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    std::string solar =
        FillTemplate(Localize("{w} {d}/{m}"), {{"w", Localize(wd[t.tm_wday])},
                                               {"d", std::to_string(t.tm_mday)},
                                               {"m", std::to_string(t.tm_mon + 1)}});
    const char* festival = vn_lunar::Festival(l);
    const char* lunar_template = festival != nullptr ? festival
                                 : l.day == 1        ? "Mùng một tháng {lm} âm lịch"
                                 : l.day == 15       ? "Rằm tháng {lm} âm lịch"
                                                     : "{ld}/{lm} âm lịch";
    std::string lunar = FillTemplate(
        Localize(lunar_template), {{"ld", std::to_string(l.day)}, {"lm", std::to_string(l.month)}});
    return solar + "\n" + lunar;
}

std::string AlarmsText() { return DescribeAlarms(); }

bool HasPendingAlarms() {
    std::lock_guard<std::mutex> lock(g_alarm_mutex);
    return !g_alarms.empty();
}

void RefreshIdleScreen() {
    Application::GetInstance().Schedule([]() {
        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle)
            return;
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        time_t now = time(nullptr);
        struct tm t{};
        localtime_r(&now, &t);
        bool night = now > 1700000000 && (t.tm_hour >= 22 || t.tm_hour < 6);
        display->SetEmotion(night ? "sleepy" : "relaxed");  // redraws the active character
        display->SetChatMessage("system", IdleLine().c_str());
    });
}

void Say(const std::string& text) {
    auto* gemini = GeminiLiveProtocol::instance();
    if (gemini != nullptr && gemini->IsAudioChannelOpened()) {
        gemini->SendUserText(text);
    } else {
        Application::GetInstance().WakeWordInvoke("<say>" + text);
    }
}

// NTP (UDP 123) is blocked on some home networks, so read the clock from an HTTP Date header.
bool SyncClock() {
    auto http = FastNetwork()->CreateHttp(2);
    http->SetTimeout(5000);
    if (auto opened = http->Open("GET", "http://www.gstatic.com/generate_204"); !opened)
        return false;
    (void)http->GetStatusCode();
    std::string date = http->GetResponseHeader("Date");  // e.g. "Mon, 16 Nov 2026 14:30:00 GMT"
    http->Close();
    char mon[4] = {};
    int d, y, h, mi, s;
    if (sscanf(date.c_str(), "%*3s, %d %3s %d %d:%d:%d", &d, mon, &y, &h, &mi, &s) != 6) {
        ESP_LOGW(TAG, "No usable Date header: '%s'", date.c_str());
        return false;
    }
    static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char* p = strstr(months, mon);
    if (p == nullptr)
        return false;
    int m = (int)(p - months) / 3 + 1;
    long days = vn_lunar::JdFromDate(d, m, y) - 2440588;  // days since 1970-01-01
    struct timeval tv = {.tv_sec = (time_t)days * 86400 + h * 3600 + mi * 60 + s, .tv_usec = 0};
    settimeofday(&tv, nullptr);
    ESP_LOGI(TAG, "Clock set from HTTP: %s", date.c_str());
    return true;
}

void InitPowerManagement() {
#if CONFIG_PM_ENABLE
    // No light sleep: the microphone must keep feeding the wake-word engine.
    esp_pm_config_t pm = {};
    pm.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    pm.min_freq_mhz = 80;
    pm.light_sleep_enable = false;
    if (esp_pm_configure(&pm) != ESP_OK ||
        esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "bap_busy", &g_busy_lock) != ESP_OK) {
        ESP_LOGW(TAG, "Power management unavailable");
        g_busy_lock = nullptr;
        return;
    }
    SetBusy(true);  // until the first idle tick of the scheduler
#endif
}

void SetBusy(bool busy) {
    std::lock_guard<std::mutex> lock(g_busy_mutex);
    if (g_busy_lock == nullptr || busy == g_busy)
        return;
    g_busy = busy;
    busy ? esp_pm_lock_acquire(g_busy_lock) : esp_pm_lock_release(g_busy_lock);
}

void StartScheduler() {
    static bool started = false;
    if (started)
        return;
    started = true;
    InitPowerManagement();
    {
        std::lock_guard<std::mutex> lock(g_alarm_mutex);
        LoadAlarms();
    }
    xTaskCreate(SchedulerTask, "assistant_sched", 4096, nullptr, 2, nullptr);
}

void RegisterTools() {
    static bool registered = false;
    if (registered)
        return;
    registered = true;
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "vn.lunar_date",
        "Tra lịch âm (âm lịch Việt Nam): đổi ngày dương sang ngày âm, can chi, ngày lễ âm lịch. "
        "Bỏ trống ngày/tháng/năm để xem hôm nay. Dùng khi người dùng hỏi hôm nay mùng mấy, "
        "ngày rằm, mùng một, năm con gì.",
        PropertyList({Property("day", kPropertyTypeInteger, 0, 0, 31),
                      Property("month", kPropertyTypeInteger, 0, 0, 12),
                      Property("year", kPropertyTypeInteger, 0, 0, 2100)}),
        [](const PropertyList& p) -> ReturnValue {
            struct tm t{};
            bool has_now = Now(&t);
            int d = p["day"].value<int>(), m = p["month"].value<int>(), y = p["year"].value<int>();
            if (d == 0 || m == 0) {
                if (!has_now)
                    return std::string("Chưa đồng bộ được giờ.");
                d = t.tm_mday;
                m = t.tm_mon + 1;
                y = t.tm_year + 1900;
            } else if (y == 0) {
                y = has_now ? t.tm_year + 1900 : 2026;
            }
            if (!vn_lunar::IsValidSolarDate(d, m, y))
                return "Ngày " + std::to_string(d) + "/" + std::to_string(m) + "/" +
                       std::to_string(y) + " không có trong lịch.";
            auto l = vn_lunar::FromSolar(d, m, y);
            char buf[320];
            snprintf(
                buf, sizeof(buf),
                "Dương lịch %d/%d/%d là ngày %d tháng %d%s năm %s âm lịch. Ngày %s, tháng %s.%s%s",
                d, m, y, l.day, l.month, l.leap ? " nhuận" : "", vn_lunar::YearName(l.year).c_str(),
                vn_lunar::DayName(l.jd).c_str(), vn_lunar::MonthName(l.month, l.year).c_str(),
                vn_lunar::Festival(l) ? " Hôm đó là " : "",
                vn_lunar::Festival(l) ? vn_lunar::Festival(l) : "");
            return std::string(buf);
        });

    mcp.AddTool(
        "vn.lunar_to_solar",
        "Đổi ngày âm lịch sang dương lịch. Ví dụ: rằm tháng 7 năm nay là ngày nào, Tết năm sau "
        "rơi vào ngày nào (Tết là mùng 1 tháng 1 âm). Bỏ trống năm để dùng năm âm lịch hiện tại.",
        PropertyList({Property("lunar_day", kPropertyTypeInteger, 1, 30),
                      Property("lunar_month", kPropertyTypeInteger, 1, 12),
                      Property("lunar_year", kPropertyTypeInteger, 0, 0, 2100),
                      Property("leap_month", kPropertyTypeBoolean, false)}),
        [](const PropertyList& p) -> ReturnValue {
            int y = p["lunar_year"].value<int>();
            struct tm t{};
            if (y == 0) {
                if (!Now(&t))
                    return std::string("Chưa đồng bộ được giờ.");
                y = vn_lunar::FromSolar(t.tm_mday, t.tm_mon + 1, t.tm_year + 1900).year;
            }
            auto s = vn_lunar::ToSolar(p["lunar_day"].value<int>(), p["lunar_month"].value<int>(),
                                       y, p["leap_month"].value<bool>());
            if (s.day == 0)
                return std::string("Năm đó không có tháng nhuận này.");
            static const char* wd[] = {"Thứ hai", "Thứ ba",  "Thứ tư",  "Thứ năm",
                                       "Thứ sáu", "Thứ bảy", "Chủ nhật"};
            long jd = vn_lunar::JdFromDate(s.day, s.month, s.year);
            char buf[200];
            snprintf(buf, sizeof(buf),
                     "Ngày %d tháng %d năm %s âm lịch là %s, ngày %d/%d/%d dương lịch.",
                     p["lunar_day"].value<int>(), p["lunar_month"].value<int>(),
                     vn_lunar::YearName(y).c_str(), wd[jd % 7], s.day, s.month, s.year);
            return std::string(buf);
        });

    mcp.AddTool(
        "vn.weather",
        "Xem thời tiết hiện tại và dự báo 3 ngày tại Việt Nam hoặc bất kỳ đâu. Bỏ trống địa điểm "
        "để xem nơi người dùng đang ở.",
        PropertyList({Property("place", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ToolResult {
            auto cfg = LoadConfig();
            std::string name = cfg.city;
            double lat = cfg.lat, lon = cfg.lon;
            auto place = p["place"].value<std::string>();
            if (!place.empty() && place != cfg.city) {
                auto geo = HttpGet(
                    "http://geocoding-api.open-meteo.com/v1/search?count=1&language=vi&name=" +
                    UrlEncode(place));
                if (!geo)
                    return std::unexpected(geo.error());
                cJSON* root = cJSON_Parse(geo->c_str());
                cJSON* r = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "results"), 0);
                if (r == nullptr) {
                    cJSON_Delete(root);
                    return std::string("Không tìm thấy địa điểm " + place);
                }
                name = cJSON_GetObjectItem(r, "name")->valuestring;
                lat = cJSON_GetObjectItem(r, "latitude")->valuedouble;
                lon = cJSON_GetObjectItem(r, "longitude")->valuedouble;
                cJSON_Delete(root);
            }
            char url[400];
            snprintf(
                url, sizeof(url),
                // Plain HTTP on purpose: public data, and TLS to these far-away servers cost 2-5 s.
                "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                "&timezone=Asia%%2FHo_Chi_Minh&forecast_days=3"
                "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code"
                "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_"
                "probability_max",
                lat, lon);
            auto body = HttpGet(url);
            if (!body)
                return std::unexpected(body.error());
            cJSON* root = cJSON_Parse(body->c_str());
            cJSON* cur = cJSON_GetObjectItem(root, "current");
            cJSON* daily = cJSON_GetObjectItem(root, "daily");
            if (!cur || !daily) {
                cJSON_Delete(root);
                return std::unexpected("Dữ liệu thời tiết không hợp lệ");
            }
            auto num = [](cJSON* o, const char* k) {
                auto v = cJSON_GetObjectItem(o, k);
                return cJSON_IsNumber(v) ? v->valuedouble : 0.0;
            };
            char buf[200];
            snprintf(buf, sizeof(buf),
                     "%s hiện tại: %s, %.0f độ C (cảm giác %.0f độ), độ ẩm %.0f%%.\n", name.c_str(),
                     WeatherText((int)num(cur, "weather_code")), num(cur, "temperature_2m"),
                     num(cur, "apparent_temperature"), num(cur, "relative_humidity_2m"));
            std::string out = buf;
            static const char* labels[] = {"Hôm nay", "Ngày mai", "Ngày kia"};
            for (int i = 0; i < 3; ++i) {
                auto at = [&](const char* k) {
                    auto v = cJSON_GetArrayItem(cJSON_GetObjectItem(daily, k), i);
                    return cJSON_IsNumber(v) ? v->valuedouble : 0.0;
                };
                snprintf(buf, sizeof(buf), "%s: %s, %.0f đến %.0f độ, khả năng mưa %.0f%%.\n",
                         labels[i], WeatherText((int)at("weather_code")), at("temperature_2m_min"),
                         at("temperature_2m_max"), at("precipitation_probability_max"));
                out += buf;
            }
            cJSON_Delete(root);
            return out;
        });

    mcp.AddTool(
        "vn.news",
        "Đọc tin tức mới nhất từ báo VnExpress. topic là một trong: latest, news, world, business, "
        "entertainment, sports, law, education, health, family, travel, science, tech, cars. Trả "
        "về các tiêu đề và tóm tắt; hãy đọc ngắn gọn 3 đến 5 tin.",
        PropertyList({Property("topic", kPropertyTypeString, std::string("latest"))}),
        [](const PropertyList& p) -> ToolResult {
            std::string topic = p["topic"].value<std::string>();
            static const char* map[][2] = {
                {"latest", "tin-moi-nhat"},     {"news", "thoi-su"},
                {"world", "the-gioi"},          {"business", "kinh-doanh"},
                {"entertainment", "giai-tri"},  {"sports", "the-thao"},
                {"law", "phap-luat"},           {"education", "giao-duc"},
                {"health", "suc-khoe"},         {"family", "gia-dinh"},
                {"travel", "du-lich"},          {"science", "khoa-hoc-cong-nghe"},
                {"tech", "khoa-hoc-cong-nghe"}, {"cars", "oto-xe-may"}};
            const char* slug = "tin-moi-nhat";
            for (auto& m : map)
                if (topic == m[0])
                    slug = m[1];
            auto body = HttpGet(std::string("https://vnexpress.net/rss/") + slug + ".rss");
            if (!body)
                return std::unexpected(body.error());
            std::string out;
            size_t pos = 0;
            for (int n = 0; n < 6; ++n) {
                size_t item = body->find("<item>", pos);
                if (item == std::string::npos)
                    break;
                size_t end = 0;
                std::string title = CleanXmlText(Between(*body, "<title>", "</title>", item, &end));
                std::string desc = Between(*body, "<description>", "</description>", item, nullptr);
                size_t br = desc.rfind("</br>");  // text follows the thumbnail markup
                if (br != std::string::npos)
                    desc = desc.substr(br + 5);
                desc = CleanXmlText(desc);
                if (desc.size() > 220)
                    desc.resize(220);
                if (title.empty())
                    break;
                out += std::to_string(n + 1) + ". " + title + (desc.empty() ? "" : " — " + desc) +
                       "\n";
                pos = end;
            }
            if (out.empty())
                return std::unexpected("Không đọc được tin");
            return "Tin từ VnExpress:\n" + out;
        });

    mcp.AddTool(
        "vn.exchange_rate",
        "Tra tỷ giá ngoại tệ của Vietcombank (đổi sang đồng Việt Nam). code là mã tiền tệ như USD, "
        "EUR, JPY, KRW, CNY, THB, SGD, AUD, GBP.",
        PropertyList({Property("code", kPropertyTypeString, std::string("USD"))}),
        [](const PropertyList& p) -> ToolResult {
            std::string code = p["code"].value<std::string>();
            for (auto& c : code)
                c = toupper(c);
            auto body =
                HttpGet("https://portal.vietcombank.com.vn/Usercontrols/TVPortal.TyGia/pXML.aspx");
            if (!body)
                return std::unexpected(body.error());
            size_t at = body->find("CurrencyCode=\"" + code + "\"");
            if (at == std::string::npos)
                return std::string("Vietcombank không niêm yết " + code);
            std::string buy = Between(*body, "Buy=\"", "\"", at, nullptr);
            std::string transfer = Between(*body, "Transfer=\"", "\"", at, nullptr);
            std::string sell = Between(*body, "Sell=\"", "\"", at, nullptr);
            return "Tỷ giá Vietcombank 1 " + code + ": mua tiền mặt " + buy +
                   " đồng, mua chuyển khoản " + transfer + " đồng, bán ra " + sell +
                   " đồng. (Dấu phẩy là phân cách hàng nghìn.)";
        });

    mcp.AddTool("vn.gold_price",
                "Tra giá vàng trong nước hôm nay (nguồn PNJ): vàng miếng SJC, nhẫn trơn 9999 và "
                "các loại khác. Nếu người dùng nhắc tiệm Hải Hồng thì dùng vn.gold_price_hai_hong. "
                "Người dùng có thể nói \"giá vàng\", \"vàng SJC\", \"vàng nhẫn\", \"vàng 9999\" "
                "(hay bị nghe "
                "thành \"vàng chín chín\", \"vàng đít xi\").",
                PropertyList(), [](const PropertyList&) -> ToolResult {
                    auto body =
                        HttpGet("https://edge-api.pnj.io/ecom-frontend/v1/get-gold-price?zone=00");
                    if (!body)
                        return std::unexpected(body.error());
                    cJSON* root = cJSON_Parse(body->c_str());
                    cJSON* data = cJSON_GetObjectItem(root, "data");
                    auto updated = cJSON_GetObjectItem(root, "updateDate");
                    std::string out = std::string("Giá vàng PNJ cập nhật ") +
                                      (cJSON_IsString(updated) ? updated->valuestring : "hôm nay") +
                                      " (triệu đồng một lượng, mua vào / bán ra):\n";
                    int n = 0;
                    cJSON* it = nullptr;
                    cJSON_ArrayForEach (it, data) {
                        auto name = cJSON_GetObjectItem(it, "tensp");
                        auto buy = cJSON_GetObjectItem(it, "giamua");
                        auto sell = cJSON_GetObjectItem(it, "giaban");
                        if (!cJSON_IsString(name) || !cJSON_IsNumber(buy) || !cJSON_IsNumber(sell))
                            continue;
                        char buf[200];  // source unit: thousand VND per chỉ; 10 chỉ = 1 lượng
                        snprintf(buf, sizeof(buf), "- %s: %.2f / %.2f\n", name->valuestring,
                                 buy->valuedouble / 100.0, sell->valuedouble / 100.0);
                        out += buf;
                        if (++n >= 5)
                            break;
                    }
                    cJSON_Delete(root);
                    if (n == 0)
                        return std::unexpected("Không đọc được giá vàng");
                    return out;
                });

    mcp.AddTool(
        "sports.football",
        "Lịch thi đấu và tỷ số bóng đá gần đây (giờ Việt Nam). league: premier_league (mặc định), "
        "champions_league, la_liga, serie_a, bundesliga, ligue_1. Dùng khi người "
        "dùng "
        "hỏi \"tối nay có trận gì\", \"tỷ số MU\", \"Ngoại hạng Anh\" (hay bị nghe thành \"ngoại "
        "hạng ăng\", \"en pi en\").",
        PropertyList({Property("league", kPropertyTypeString, "premier_league")}),
        [](const PropertyList& props) -> ToolResult {
            static const struct {
                const char* id;
                const char* espn;
                const char* name;
            } kLeagues[] = {{"premier_league", "eng.1", "Ngoại hạng Anh"},
                            {"champions_league", "uefa.champions", "Champions League"},
                            {"la_liga", "esp.1", "La Liga"},
                            {"serie_a", "ita.1", "Serie A"},
                            {"bundesliga", "ger.1", "Bundesliga"},
                            {"ligue_1", "fra.1", "Ligue 1"}};
            std::string want = props["league"].value<std::string>();
            auto league = &kLeagues[0];
            bool known = want.empty();
            for (auto& l : kLeagues)
                if (want == l.id) {
                    league = &l;
                    known = true;
                }
            // ESPN's public scoreboard feed (plain HTTP: public data).
            auto body =
                HttpGet(std::string("http://site.api.espn.com/apis/site/v2/sports/soccer/") +
                        league->espn + "/scoreboard");
            if (!body)
                return std::unexpected("Chưa lấy được lịch " + std::string(league->name));
            cJSON* root = cJSON_Parse(body->c_str());
            body->clear();
            body->shrink_to_fit();
            std::string out =
                known ? std::string()
                      : "Không có giải " + want + ", đây là lịch " + league->name + ". ";
            out += std::string(league->name) + " (giờ Việt Nam):\n";
            int n = 0;
            cJSON* ev = nullptr;
            cJSON_ArrayForEach (ev, cJSON_GetObjectItem(root, "events")) {
                if (n >= 10)
                    break;
                cJSON* comp = cJSON_GetArrayItem(cJSON_GetObjectItem(ev, "competitions"), 0);
                cJSON* teams = cJSON_GetObjectItem(comp, "competitors");
                cJSON* status = cJSON_GetObjectItem(cJSON_GetObjectItem(ev, "status"), "type");
                cJSON* state = cJSON_GetObjectItem(status, "state");
                cJSON* date = cJSON_GetObjectItem(ev, "date");
                std::string home, away, hs, as;
                cJSON* t = nullptr;
                cJSON_ArrayForEach (t, teams) {
                    cJSON* side = cJSON_GetObjectItem(t, "homeAway");
                    cJSON* name =
                        cJSON_GetObjectItem(cJSON_GetObjectItem(t, "team"), "shortDisplayName");
                    cJSON* score = cJSON_GetObjectItem(t, "score");
                    bool is_home = cJSON_IsString(side) && strcmp(side->valuestring, "home") == 0;
                    (is_home ? home : away) = cJSON_IsString(name) ? name->valuestring : "?";
                    (is_home ? hs : as) = cJSON_IsString(score) ? score->valuestring : "";
                }
                // "2026-11-21T12:30Z" (UTC) -> local "dd/mm hh:mm" (UTC+7); timegm-free math.
                char when[32] = "";
                int y, mo, d, h, mi;
                if (cJSON_IsString(date) &&
                    sscanf(date->valuestring, "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) == 5) {
                    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                    h += 7;
                    if (h >= 24) {
                        h -= 24;
                        int dim = kDays[mo - 1] +
                                  (mo == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
                        if (++d > dim) {
                            d = 1;
                            if (++mo > 12)
                                mo = 1;
                        }
                    }
                    snprintf(when, sizeof(when), "%02d/%02d %02d:%02d", d, mo, h, mi);
                }
                std::string st = cJSON_IsString(state) ? state->valuestring : "";
                if (st == "pre")
                    out += std::string("- ") + when + ": " + home + " gặp " + away + "\n";
                else if (st == "in")
                    out += "- Đang đá: " + home + " " + hs + "-" + as + " " + away + "\n";
                else
                    out += std::string("- ") + when + " kết thúc: " + home + " " + hs + "-" + as +
                           " " + away + "\n";
                ++n;
            }
            cJSON_Delete(root);
            if (n == 0)
                return std::string("Hiện chưa có trận nào của ") + league->name + " trong lịch.";
            return out;
        });

    mcp.AddTool(
        "vn.stocks",
        "Xem nhanh thị trường chứng khoán Việt Nam: chỉ số VN-Index, VN30, HNX-Index, UPCoM (điểm, "
        "tăng giảm, giá trị giao dịch). Dùng khi người dùng hỏi \"chứng khoán hôm nay\", "
        "\"VN-Index\", "
        "\"thị trường\" (hay bị nghe thành \"vê en index\", \"chứng khoáng\").",
        PropertyList(), [](const PropertyList&) -> ToolResult {
            // Public CafeF board feed; plain HTTP (public data, answers in ~60 ms).
            auto body = HttpGet("http://banggia.cafef.vn/stockhandler.ashx?index=true");
            if (!body)
                return std::unexpected(body.error());
            cJSON* root = cJSON_Parse(body->c_str());
            if (!cJSON_IsArray(root)) {
                cJSON_Delete(root);
                return std::unexpected("Không đọc được dữ liệu chứng khoán");
            }
            static const struct {
                const char* code;
                const char* name;
            } kIndexes[] = {{"VNINDEX", "VN-Index"},
                            {"VN30", "VN30"},
                            {"HNXINDEX", "HNX-Index"},
                            {"HNXUPCOMINDEX", "UPCoM"}};
            auto str = [](cJSON* o, const char* k) {
                cJSON* v = cJSON_GetObjectItem(o, k);
                return std::string(cJSON_IsString(v) ? v->valuestring : "");
            };
            std::string out = "Chứng khoán (điểm, thay đổi, phần trăm):\n";
            int n = 0;
            for (auto& idx : kIndexes) {
                cJSON* it = nullptr;
                cJSON_ArrayForEach (it, root) {
                    if (str(it, "name") != idx.code)
                        continue;
                    std::string change = str(it, "change");
                    bool up = !change.empty() && change[0] != '-';
                    out += std::string("- ") + idx.name + ": " + str(it, "index") + " điểm, " +
                           (up ? "tăng " : "giảm ") + (up ? change : change.substr(1)) + " (" +
                           str(it, "percent") + "%)";
                    if (strcmp(idx.code, "VNINDEX") == 0)
                        out += ", giá trị giao dịch " + str(it, "value") + " tỷ đồng";
                    out += "\n";
                    ++n;
                }
            }
            cJSON_Delete(root);
            if (n == 0)
                return std::unexpected("Không đọc được dữ liệu chứng khoán");
            return out;
        });

    mcp.AddTool(
        "vn.gold_price_hai_hong",
        "Tra bảng giá vàng, bạc hiện tại của tiệm vàng Hải Hồng (Hải Hồng Jewelry, 32A-48 Trần "
        "Hưng "
        "Đạo, Hải Dương): nhẫn tròn Hải Hồng 9999, trang sức 9999/999/995/980, bạc. Dùng khi người "
        "dùng hỏi \"giá vàng Hải Hồng\", \"vàng Hải Hồng\", \"giá vàng ở Hải Dương\" (có thể bị "
        "nghe "
        "thành \"hải hùng\", \"hai hồng\"). Giá tính theo một chỉ.",
        PropertyList(), [](const PropertyList&) -> ToolResult {
            // Product names come from the public board page, prices from its JSON feed.
            auto page = HttpGet("http://hhj.adp-p.com/tygia/banggia");
            if (!page)
                return std::unexpected(page.error());
            std::vector<std::pair<std::string, std::string>> names;  // id, name
            size_t pos = 0;
            while ((pos = page->find("class=\"l1", pos)) != std::string::npos) {
                size_t a = page->find('>', pos), b = page->find("</div>", pos);
                size_t id_at = page->find("GiaMua_", pos);
                if (a == std::string::npos || b == std::string::npos || id_at == std::string::npos)
                    break;
                size_t id_end = page->find('"', id_at);
                std::string name = page->substr(a + 1, b - a - 1);
                while (!name.empty() && isspace((unsigned char)name.back()))
                    name.pop_back();
                while (!name.empty() && isspace((unsigned char)name.front()))
                    name.erase(0, 1);
                names.emplace_back(page->substr(id_at + 7, id_end - id_at - 7), name);
                pos = b;
            }
            page->clear();
            page->shrink_to_fit();
            auto feed = HttpGet("http://hhj.adp-p.com/Public/get_TyGia?p=");
            if (!feed)
                return std::unexpected(feed.error());
            cJSON* root = cJSON_Parse(feed->c_str());
            if (!cJSON_IsArray(root)) {
                cJSON_Delete(root);
                return std::unexpected("Không đọc được bảng giá Hải Hồng");
            }
            auto money = [](const char* text) {  // "12,550,000" -> "12,55 triệu"
                long v = 0;
                for (const char* c = text; *c; ++c)
                    if (isdigit((unsigned char)*c))
                        v = v * 10 + (*c - '0');
                char buf[32];
                if (v >= 1000000)
                    snprintf(buf, sizeof(buf), "%ld,%02ld triệu", v / 1000000,
                             (v % 1000000) / 10000);
                else
                    snprintf(buf, sizeof(buf), "%ld nghìn", v / 1000);
                return std::string(buf);
            };
            std::string out = "Giá Hải Hồng (một chỉ, mua vào / bán ra):\n";
            int n = 0;
            for (auto& [id, name] : names) {
                cJSON* it = nullptr;
                cJSON_ArrayForEach (it, root) {
                    auto mid = cJSON_GetObjectItem(it, "matygia");
                    auto buy = cJSON_GetObjectItem(it, "giamua");
                    auto sell = cJSON_GetObjectItem(it, "giaban");
                    if (!cJSON_IsString(mid) || id != mid->valuestring || !cJSON_IsString(buy) ||
                        !cJSON_IsString(sell))
                        continue;
                    if (strcmp(buy->valuestring, "0") == 0)
                        break;  // not quoted now
                    out += "- " + name + ": " + money(buy->valuestring) + " / " +
                           money(sell->valuestring) + "\n";
                    ++n;
                    break;
                }
            }
            cJSON_Delete(root);
            if (n == 0)
                return std::unexpected("Hải Hồng chưa niêm yết giá lúc này");
            return out;
        });

    mcp.AddTool(
        "vn.air_quality",
        "Xem chất lượng không khí (AQI, bụi mịn PM2.5) hiện tại và lời khuyên có nên đeo khẩu "
        "trang, "
        "mở cửa sổ. Bỏ trống địa điểm để xem nơi người dùng đang ở.",
        PropertyList({Property("place", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ToolResult {
            auto cfg = LoadConfig();
            std::string name = cfg.city;
            double lat = cfg.lat, lon = cfg.lon;
            auto place = p["place"].value<std::string>();
            if (!place.empty() && place != cfg.city) {
                auto geo = HttpGet(
                    "http://geocoding-api.open-meteo.com/v1/search?count=1&language=vi&name=" +
                    UrlEncode(place));
                if (!geo)
                    return std::unexpected(geo.error());
                cJSON* root = cJSON_Parse(geo->c_str());
                cJSON* r = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "results"), 0);
                if (r == nullptr) {
                    cJSON_Delete(root);
                    return std::string("Không tìm thấy địa điểm " + place);
                }
                name = cJSON_GetObjectItem(r, "name")->valuestring;
                lat = cJSON_GetObjectItem(r, "latitude")->valuedouble;
                lon = cJSON_GetObjectItem(r, "longitude")->valuedouble;
                cJSON_Delete(root);
            }
            char url[220];
            snprintf(
                url, sizeof(url),
                "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
                "&current=us_aqi,pm2_5,pm10&timezone=Asia%%2FHo_Chi_Minh",
                lat, lon);
            auto body = HttpGet(url);
            if (!body)
                return std::unexpected(body.error());
            cJSON* root = cJSON_Parse(body->c_str());
            cJSON* cur = cJSON_GetObjectItem(root, "current");
            auto num = [cur](const char* k) {
                auto v = cJSON_GetObjectItem(cur, k);
                return cJSON_IsNumber(v) ? v->valuedouble : -1.0;
            };
            double aqi = num("us_aqi"), pm25 = num("pm2_5");
            cJSON_Delete(root);
            if (aqi < 0)
                return std::unexpected("Không có dữ liệu không khí");
            const char* level = aqi <= 50    ? "tốt"
                                : aqi <= 100 ? "trung bình"
                                : aqi <= 150 ? "kém, nhạy cảm nên hạn chế ra ngoài"
                                : aqi <= 200 ? "xấu, nên đeo khẩu trang"
                                : aqi <= 300 ? "rất xấu, hạn chế ra ngoài"
                                             : "nguy hại";
            char buf[220];
            snprintf(buf, sizeof(buf),
                     "Không khí ở %s: AQI %.0f, mức %s. Bụi mịn PM2.5 %.0f microgam trên mét khối.",
                     name.c_str(), aqi, level, pm25);
            return std::string(buf);
        });

    mcp.AddTool(
        "vn.lottery",
        "Xem kết quả xổ số kiến thiết mới nhất. region là north, central hoặc south. Người dùng "
        "hay nói "
        "\"xổ số\", \"sổ xố\", \"xsmb\", \"kết quả đề\", \"lô tô\" để hỏi xổ số miền Bắc.",
        PropertyList({Property("region", kPropertyTypeString, std::string("north"))}),
        [](const PropertyList& p) -> ToolResult {
            std::string region = p["region"].value<std::string>();
            const char* slug = region == "south"     ? "mien-nam-xsmn"
                               : region == "central" ? "mien-trung-xsmt"
                                                     : "mien-bac-xsmb";
            auto body =
                HttpGet(std::string("https://xosodaiphat.com/ket-qua-xo-so-") + slug + ".rss");
            if (!body)
                return std::unexpected(body.error());
            size_t item = body->find("<item>");
            if (item == std::string::npos)
                return std::unexpected("Chưa có kết quả");
            std::string title = CleanXmlText(Between(*body, "<title>", "</title>", item, nullptr));
            std::string desc =
                CleanXmlText(Between(*body, "<description>", "</description>", item, nullptr));
            if (desc.size() > 900)
                desc.resize(900);
            return title + "\n" + desc +
                   "\n(DB là giải đặc biệt, G.1 là giải nhất. Chỉ đọc giải đặc biệt và giải nhất "
                   "trừ khi được hỏi thêm.)";
        });

    // User-defined extensions: HTTP endpoints on the local network (Home Assistant, a Next.js
    // server, n8n...) configured in the web UI. The assistant posts {"action","text"} to them.
    {
        cJSON* hooks = cJSON_Parse(LoadList("hooks").c_str());
        std::string names;
        cJSON* h = nullptr;
        cJSON_ArrayForEach (h, hooks) {
            auto name = cJSON_GetObjectItem(h, "name");
            auto desc = cJSON_GetObjectItem(h, "desc");
            if (!cJSON_IsString(name))
                continue;
            names += std::string("\n- ") + name->valuestring + ": " +
                     (cJSON_IsString(desc) ? desc->valuestring : "");
        }
        cJSON_Delete(hooks);
        if (!names.empty()) {
            mcp.AddTool(
                "extension.call",
                "Gọi một tiện ích mở rộng do người dùng cài (nhà thông minh, dịch vụ riêng). "
                "Các tiện ích hiện có:" +
                    names +
                    "\nname phải đúng một trong các tên trên; text là yêu cầu cụ thể bằng lời.",
                PropertyList({Property("name", kPropertyTypeString),
                              Property("text", kPropertyTypeString, std::string(""))}),
                [](const PropertyList& p) -> ToolResult {
                    std::string want = p["name"].value<std::string>(), url;
                    cJSON* hooks = cJSON_Parse(LoadList("hooks").c_str());
                    cJSON* h = nullptr;
                    cJSON_ArrayForEach (h, hooks) {
                        auto name = cJSON_GetObjectItem(h, "name");
                        auto u = cJSON_GetObjectItem(h, "url");
                        if (cJSON_IsString(name) && cJSON_IsString(u) && want == name->valuestring)
                            url = u->valuestring;
                    }
                    cJSON_Delete(hooks);
                    if (url.empty())
                        return std::unexpected("Không có tiện ích tên " + want);
                    cJSON* body = cJSON_CreateObject();
                    cJSON_AddStringToObject(body, "action", want.c_str());
                    cJSON_AddStringToObject(body, "text", p["text"].value<std::string>().c_str());
                    char* txt = cJSON_PrintUnformatted(body);
                    auto http = FastNetwork()->CreateHttp(2);
                    http->SetTimeout(10000);
                    http->SetHeader("Content-Type", "application/json");
                    http->SetContent(std::string(txt));
                    cJSON_free(txt);
                    cJSON_Delete(body);
                    if (auto opened = http->Open("POST", url); !opened)
                        return std::unexpected("Không gọi được tiện ích");
                    auto status = http->GetStatusCode();
                    std::string reply = http->ReadAll();
                    http->Close();
                    if (!status || *status >= 400)
                        return std::unexpected("Tiện ích báo lỗi");
                    if (reply.size() > 1200)
                        reply.resize(1200);
                    return reply.empty() ? std::string("Đã thực hiện.") : reply;
                });
        }
    }

    {
        std::string list;
        for (auto& st : Stations())
            list += (list.empty() ? "" : ", ") + st.id + " (" + st.name + ")";
        mcp.AddTool("music.sd_card",
                    "Phát nhạc có sẵn trong thẻ nhớ SD của máy (không cần mạng). query: tên bài, "
                    "ca sĩ hoặc "
                    "thư mục cần tìm; để trống để phát ngẫu nhiên tất cả. Dùng khi người dùng nói "
                    "\"mở nhạc trong "
                    "thẻ nhớ\", \"nhạc offline\", \"nhạc trong máy\".",
                    PropertyList({Property("query", kPropertyTypeString, "")}),
                    [](const PropertyList& props) -> ReturnValue {
                        if (!SdMounted())
                            return std::string("Máy chưa có thẻ nhớ SD (hoặc thẻ không đọc được).");
                        std::string kw = props["query"].value<std::string>();
                        auto songs = SdSongs(kw);
                        if (songs.empty())
                            return std::string(kw.empty() ? "Thẻ nhớ chưa có bài .mp3 nào."
                                                          : "Không thấy bài nào khớp \"" + kw +
                                                                "\" trong thẻ nhớ.");
                        std::string title =
                            songs.size() == 1 ? SongTitle(songs[0])
                                              : std::to_string(songs.size()) + " bài trong thẻ nhớ";
                        Application::GetInstance().PlayStream("radio:sd:" + kw, title);
                        return "Đang phát " + title + (songs.size() > 1 ? " (ngẫu nhiên)." : ".");
                    });

        mcp.AddTool(
            "music.stop",
            "Tắt nhạc hoặc radio đang phát. Dùng khi người dùng nói \"tắt nhạc\", \"dừng radio\", "
            "\"thôi không nghe nữa\".",
            PropertyList(), [](const PropertyList&) -> ReturnValue {
                Application::GetInstance().StopAll();
                return std::string("Đã tắt.");
            });

        mcp.AddTool(
            "radio.play",
            "Mở radio trực tuyến. Các kênh: " + list +
                ". Người dùng hay nói \"mở đài\", \"nghe radio\", \"VOV một\" (có thể bị nghe "
                "thành "
                "\"vê ô vê\", \"vô vê\", \"vốp\"), \"giao thông\" là VOV Giao thông. Sau khi gọi "
                "công cụ "
                "không cần nói gì thêm; radio sẽ phát ngay. Người dùng gọi từ đánh thức để tắt.",
            PropertyList({Property("station", kPropertyTypeString)}),
            [](const PropertyList& p) -> ReturnValue {
                std::string want = p["station"].value<std::string>();
                for (auto& st : Stations()) {
                    if (st.id == want || st.name == want) {
                        Application::GetInstance().PlayStream("radio:" + st.url, "📻 " + st.name);
                        return std::string("Đang mở " + st.name);
                    }
                }
                return std::string("Không có kênh " + want);
            });
    }

    mcp.AddTool(
        "system.wifi_setup",
        "Đưa máy vào chế độ cài Wi-Fi: máy phát mạng Wi-Fi tên Bap-xxxx, người dùng dùng điện "
        "thoại "
        "kết nối rồi mở 192.168.4.1 để chọn mạng mới. Chỉ dùng khi người dùng yêu cầu rõ ràng như "
        "\"cài lại Wi-Fi\", \"đổi mạng Wi-Fi\". Báo trước cho người dùng rồi mới gọi.",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            xTaskCreate(
                [](void*) {
                    vTaskDelay(pdMS_TO_TICKS(4000));  // let the spoken confirmation finish
                    static_cast<WifiBoard&>(Board::GetInstance()).EnterWifiConfigMode();
                    vTaskDelete(nullptr);
                },
                "wifi_setup", 3072, nullptr, 2, nullptr);
            return std::string("Sắp vào chế độ cài Wi-Fi.");
        });

    mcp.AddTool(
        "music.search",
        "Tìm bài hát trên Zing MP3 và trả về danh sách để người dùng chọn (tên bài, ca sĩ, "
        "độ dài, mã bài). "
        "Dùng khi người dùng muốn tìm bài, hỏi có bài nào của ca sĩ nào, hoặc chưa rõ muốn "
        "nghe bài nào. "
        "Đọc ngắn gọn 3 đến 5 bài đầu kèm số thứ tự; khi người dùng chọn thì gọi music.play "
        "với song_id.",
        PropertyList({Property("query", kPropertyTypeString)}),
        [](const PropertyList& p) -> ToolResult {
            auto songs = ZingSearch(p["query"].value<std::string>(), 8);
            if (!songs)
                return std::unexpected(songs.error());
            if (songs->empty())
                return std::string("Không tìm thấy bài nào.");
            std::string out;
            int n = 0;
            for (auto& s : *songs) {
                char dur[16];
                snprintf(dur, sizeof(dur), "%d:%02d", s.duration_s / 60, s.duration_s % 60);
                out += std::to_string(++n) + ". " + s.title + " - " + s.artists + " (" + dur +
                       ", mã " + s.id + (s.vip ? ", chỉ VIP, không phát được" : "") + ")\n";
            }
            return out;
        });

    mcp.AddTool(
        "music.play",
        "Phát một bài hát từ Zing MP3: theo query (tên bài và/hoặc ca sĩ, ví dụ \"Hà Nội mùa "
        "thu\", "
        "\"nhạc Sơn Tùng\") hoặc theo song_id lấy từ music.search (khi đó query là tên bài để hiển "
        "thị). "
        "Dùng khi người dùng muốn nghe nhạc, mở bài hát. "
        "Sau khi gọi công cụ, nói một câu thật ngắn giới thiệu bài rồi thôi; nhạc phát ngay. Người "
        "dùng "
        "gọi từ đánh thức để dừng nhạc. Bài chỉ dành cho tài khoản VIP sẽ được bỏ qua.",
        PropertyList({Property("query", kPropertyTypeString, std::string("")),
                      Property("song_id", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ToolResult {
            auto query = p["query"].value<std::string>();
            auto picked = p["song_id"].value<std::string>();
            std::expected<std::vector<ZingSong>, std::string> songs;
            if (!picked.empty()) {
                songs = std::vector<ZingSong>{
                    ZingSong{.id = picked, .title = query.empty() ? "bài hát" : query}};
            } else {
                songs = ZingSearch(query, 8);
            }
            if (!songs)
                return std::unexpected(songs.error());
            for (auto& song : *songs) {
                if (song.vip)
                    continue;
                auto url = ZingStreamUrl(song.id);
                if (!url)
                    continue;
                std::string title = song.title + (song.artists.empty() ? "" : " - " + song.artists);
                // Let the short spoken intro finish first, then start the song.
                xTaskCreateWithCaps(
                    [](void* arg) {
                        auto* job = static_cast<std::pair<std::string, std::string>*>(arg);
                        auto& app = Application::GetInstance();
                        // job->first = "<song id>\n<stream url>"
                        std::string id = job->first.substr(0, job->first.find('\n'));
                        std::string url = job->first.substr(job->first.find('\n') + 1);
                        std::vector<NotifySubtitle> lyrics;
                        for (auto& [ms, text] : ZingLyrics(id))
                            lyrics.push_back({.start_ms = ms, .text = text});
                        for (int i = 0; i < 100 && app.GetDeviceState() == kDeviceStateSpeaking;
                             ++i) {
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }
                        vTaskDelay(pdMS_TO_TICKS(1500));
                        app.PlayStream("radio:" + url, "🎵 " + job->second, std::move(lyrics));
                        delete job;
                        vTaskDeleteWithCaps(nullptr);
                    },
                    "zing_play", 8192,
                    new std::pair<std::string, std::string>(song.id + "\n" + *url, title), 2,
                    nullptr,
                    MALLOC_CAP_SPIRAM);  // network only: stack can live in PSRAM
                return "Đang mở bài " + title + " (Zing MP3).";
            }
            return std::string("Không tìm thấy bài miễn phí nào cho \"" + query +
                               "\" trên Zing MP3.");
        });

    mcp.AddTool(
        "app.open",
        "Mở một ứng dụng theo mã (xem danh sách ứng dụng trong hướng dẫn). Trả về kịch bản; hãy "
        "làm theo kịch bản đó ngay.",
        PropertyList({Property("id", kPropertyTypeString)}),
        [](const PropertyList& p) -> ReturnValue {
            std::string id = p["id"].value<std::string>();
            for (auto& a : Apps()) {
                if (a.id == id)
                    return a.prompt;
            }
            return std::string("Không có ứng dụng " + id);
        });

    mcp.AddTool(
        "timer.set",
        "Đặt hẹn giờ đếm ngược. Dùng khi người dùng nói: hẹn giờ 5 phút, nhắc mình sau 10 phút...",
        PropertyList({Property("seconds", kPropertyTypeInteger, 1, 86400),
                      Property("text", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ReturnValue {
            time_t now = time(nullptr);
            if (now < 1700000000)
                return std::string("Chưa đồng bộ được giờ nên chưa đặt được.");
            std::lock_guard<std::mutex> lock(g_alarm_mutex);
            Alarm a{g_next_alarm_id++, now + p["seconds"].value<int>(), false,
                    p["text"].value<std::string>()};
            g_alarms.push_back(a);
            PersistAlarms();
            return "Đã đặt hẹn giờ số " + std::to_string(a.id);
        });

    mcp.AddTool(
        "alarm.set",
        "Đặt báo thức hoặc lời nhắc vào giờ cụ thể trong ngày (giờ Việt Nam, 24 giờ). Nếu giờ đó "
        "hôm nay đã qua thì báo vào ngày mai. daily=true để lặp lại mỗi ngày.",
        PropertyList({Property("hour", kPropertyTypeInteger, 0, 23),
                      Property("minute", kPropertyTypeInteger, 0, 59),
                      Property("text", kPropertyTypeString, std::string("")),
                      Property("daily", kPropertyTypeBoolean, false)}),
        [](const PropertyList& p) -> ReturnValue {
            struct tm t{};
            time_t now;
            if (!Now(&t, &now))
                return std::string("Chưa đồng bộ được giờ nên chưa đặt được.");
            t.tm_hour = p["hour"].value<int>();
            t.tm_min = p["minute"].value<int>();
            t.tm_sec = 0;
            time_t at = mktime(&t);
            if (at <= now)
                at += 86400;
            std::lock_guard<std::mutex> lock(g_alarm_mutex);
            Alarm a{g_next_alarm_id++, at, p["daily"].value<bool>(),
                    p["text"].value<std::string>()};
            g_alarms.push_back(a);
            PersistAlarms();
            return "Đã đặt báo thức số " + std::to_string(a.id);
        });

    mcp.AddTool("timer.list", "Xem danh sách hẹn giờ và báo thức đang chạy.", PropertyList(),
                [](const PropertyList&) -> ReturnValue { return DescribeAlarms(); });

    mcp.AddTool("timer.cancel", "Hủy hẹn giờ hoặc báo thức theo số. number=0 để hủy tất cả.",
                PropertyList({Property("number", kPropertyTypeInteger, 0, 0, 100000)}),
                [](const PropertyList& p) -> ReturnValue {
                    int id = p["number"].value<int>();
                    std::lock_guard<std::mutex> lock(g_alarm_mutex);
                    size_t before = g_alarms.size();
                    g_alarms.erase(
                        std::remove_if(g_alarms.begin(), g_alarms.end(),
                                       [id](const Alarm& a) { return id == 0 || a.id == id; }),
                        g_alarms.end());
                    PersistAlarms();
                    return "Đã hủy " + std::to_string(before - g_alarms.size()) + " mục.";
                });

    mcp.AddTool("notes.add",
                "Thêm một mục vào danh sách ghi chú của người dùng (việc cần làm, đồ cần mua...).",
                PropertyList({Property("text", kPropertyTypeString)}),
                [](const PropertyList& p) -> ReturnValue {
                    auto items = ListItems("notes");
                    if (items.size() >= 30)
                        return std::string("Danh sách đã đầy 30 mục, hãy xóa bớt.");
                    items.push_back(p["text"].value<std::string>());
                    StoreItems("notes", items);
                    return "Đã ghi. Hiện có " + std::to_string(items.size()) + " mục.";
                });
    mcp.AddTool("notes.list", "Đọc danh sách ghi chú của người dùng.", PropertyList(),
                [](const PropertyList&) -> ReturnValue {
                    return Numbered(ListItems("notes"), "Chưa có ghi chú nào.");
                });
    mcp.AddTool("notes.remove", "Xóa ghi chú theo số thứ tự. number=0 để xóa hết.",
                PropertyList({Property("number", kPropertyTypeInteger, 0, 0, 30)}),
                [](const PropertyList& p) -> ReturnValue {
                    auto items = ListItems("notes");
                    int n = p["number"].value<int>();
                    if (n == 0)
                        items.clear();
                    else if (n <= (int)items.size())
                        items.erase(items.begin() + n - 1);
                    else
                        return std::string("Không có mục số đó.");
                    StoreItems("notes", items);
                    return std::string("Đã xóa.");
                });

    mcp.AddTool(
        "memory.save",
        "Ghi nhớ lâu dài một điều về người dùng (tên, sở thích, người thân, thói quen) để các lần "
        "trò chuyện sau vẫn nhớ. Chỉ lưu khi người dùng chia sẻ thông tin đáng nhớ hoặc bảo hãy "
        "nhớ.",
        PropertyList({Property("fact", kPropertyTypeString)}),
        [](const PropertyList& p) -> ReturnValue {
            auto items = ListItems("memory");
            if (items.size() >= 20)
                items.erase(items.begin());
            items.push_back(p["fact"].value<std::string>());
            StoreItems("memory", items);
            return std::string("Đã nhớ.");
        });
    mcp.AddTool("memory.forget",
                "Quên điều đã ghi nhớ về người dùng. fact là điều cần quên (vài từ khóa là đủ); "
                "để trống fact để quên tất cả, chỉ khi người dùng nói rõ là quên hết.",
                PropertyList({Property("fact", kPropertyTypeString, std::string(""))}),
                [](const PropertyList& p) -> ReturnValue {
                    std::string want = Fold(p["fact"].value<std::string>());
                    auto items = ListItems("memory");
                    if (want.find_first_not_of(' ') == std::string::npos) {
                        StoreItems("memory", {});
                        return "Đã quên hết " + std::to_string(items.size()) + " điều.";
                    }
                    size_t before = items.size();
                    items.erase(std::remove_if(items.begin(), items.end(),
                                               [&want](const std::string& item) {
                                                   return Fold(item).find(want) !=
                                                          std::string::npos;
                                               }),
                                items.end());
                    if (items.size() == before)
                        return std::string("Không tìm thấy điều đó trong trí nhớ.");
                    StoreItems("memory", items);
                    return "Đã quên " + std::to_string(before - items.size()) + " điều.";
                });
}

}  // namespace assistant
