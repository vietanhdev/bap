// GET /api/perf: per-task CPU share over one second, plus heap figures. Used to find what starves
// the audio pipeline (choppy voice, mic ring buffer overflows).
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <cJSON.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "assistant_auth.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace assistant {
namespace {

std::map<std::string, uint32_t> g_stack_free;  // task -> lowest free stack seen (bytes)

std::map<std::string, uint32_t> Snapshot(uint32_t* total) {
    std::map<std::string, uint32_t> out;
    UBaseType_t n = uxTaskGetNumberOfTasks() + 4;
    std::vector<TaskStatus_t> tasks(n);
    n = uxTaskGetSystemState(tasks.data(), n, total);
    g_stack_free.clear();
    for (UBaseType_t i = 0; i < n; ++i) {
        out[tasks[i].pcTaskName] += tasks[i].ulRunTimeCounter;
        g_stack_free[tasks[i].pcTaskName] = tasks[i].usStackHighWaterMark * sizeof(StackType_t);
    }
    return out;
}

esp_err_t HandlePerf(httpd_req_t* req) {
    uint32_t t0 = 0, t1 = 0;
    auto a = Snapshot(&t0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    auto b = Snapshot(&t1);
    double window = (double)(t1 - t0) * portNUM_PROCESSORS;  // run time counted per core
    std::vector<std::pair<double, std::string>> rows;
    for (auto& [name, run] : b) {
        auto it = a.find(name);
        uint32_t delta = run - (it == a.end() ? 0 : it->second);
        if (window > 0)
            rows.emplace_back(100.0 * delta / window, name);
    }
    std::sort(rows.rbegin(), rows.rend());
    cJSON* r = cJSON_CreateObject();
    cJSON* cpu = cJSON_AddObjectToObject(r, "cpu_percent");
    for (auto& [pct, name] : rows) {
        if (pct < 0.2)
            break;
        cJSON_AddNumberToObject(cpu, name.c_str(), (int)(pct * 10) / 10.0);
    }
    // Stack headroom per task (lowest ever): anything under ~1 KB is a crash waiting to happen.
    cJSON* stacks = cJSON_AddObjectToObject(r, "stack_free_min");
    for (auto& [name, bytes] : g_stack_free)
        cJSON_AddNumberToObject(stacks, name.c_str(), bytes);
    cJSON_AddNumberToObject(r, "free_internal", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(r, "min_free_internal",
                            heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(r, "largest_internal",
                            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(r, "free_psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(r, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        cJSON_AddNumberToObject(r, "rssi", ap.rssi);
    char* txt = cJSON_PrintUnformatted(r);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, txt);
    cJSON_free(txt);
    cJSON_Delete(r);
    return err;
}

}  // namespace

void RegisterPerfRoutes(void* server) {
    static const httpd_uri_t route = {
        .uri = "/api/perf", .method = HTTP_GET, .handler = HandlePerf, .user_ctx = nullptr};
    RegisterGuarded(server, route);
}

}  // namespace assistant
