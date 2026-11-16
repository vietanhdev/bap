// Host test for the LRC lyrics parser used for Zing MP3 synced lyrics.
//   g++ -std=c++17 -I firmware/src/assistant firmware/tests/test_lrc.cc && ./a.out
#include <cstdio>
#include <string>

#include "lrc.h"

int main() {
    const std::string lrc =
        "[ar: Mỹ Linh]\r\n"
        "[ti: Hà Nội mùa thu]\n"
        "[00:12.34]Hà Nội mùa thu\r\n"
        "[00:15.5]cây cơm nguội vàng\n"    // one fraction digit = 500 ms
        "[01:02.345]ba digits\n"           // milliseconds, not 3450
        "[00:20.00][01:30.00]điệp khúc\n"  // repeated line
        "[00:25.00]   \n"                  // blank text is dropped
        "no tag here\n"
        "[03:00]no fraction\n";
    auto got = assistant::ParseLrc(lrc);
    struct Want {
        uint32_t ms;
        const char* text;
    } want[] = {{12340, "Hà Nội mùa thu"}, {15500, "cây cơm nguội vàng"}, {20000, "điệp khúc"},
                {62345, "ba digits"},      {90000, "điệp khúc"},          {180000, "no fraction"}};
    int failed = 0;
    if (got.size() != sizeof(want) / sizeof(want[0])) {
        printf("FAIL count %zu\n", got.size());
        failed++;
    }
    for (size_t i = 0; i < got.size() && i < sizeof(want) / sizeof(want[0]); ++i) {
        bool ok = got[i].first == want[i].ms && got[i].second == want[i].text;
        failed += ok ? 0 : 1;
        printf("%s %u %s\n", ok ? "ok  " : "FAIL", got[i].first, got[i].second.c_str());
    }
    return failed == 0 ? 0 : 1;
}
