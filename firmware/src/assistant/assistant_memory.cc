// Keep cJSON out of internal RAM. Small heap requests (<= CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL)
// normally land in the ~300 KB of internal SRAM, and cJSON makes thousands of them: parsing a
// 40 KB Gemini audio message or the character data used to starve TLS of internal memory
// ("esp-aes: Failed to allocate memory") and drop the conversation mid-reply.
#include <cJSON.h>
#include <esp_heap_caps.h>

namespace {

void* JsonMalloc(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != nullptr ? p : heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
}

void JsonFree(void* p) { heap_caps_free(p); }

// Runs before app_main, so every cJSON user (MCP, protocols, settings) gets the PSRAM hooks.
__attribute__((constructor)) void InstallJsonHooks() {
    cJSON_Hooks hooks = {.malloc_fn = JsonMalloc, .free_fn = JsonFree};
    cJSON_InitHooks(&hooks);
}

}  // namespace
