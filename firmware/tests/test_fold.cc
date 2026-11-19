// Host test for accent folding (SD card song matching).
//   g++ -std=c++17 -I firmware/src/assistant firmware/tests/test_fold.cc && ./a.out
#include <cstdio>
#include <string>

#include "text_fold.h"

int main() {
    struct Case {
        const char* in;
        const char* want;
    } cases[] = {
        {"Hà Nội mùa thu", "ha noi mua thu"},
        {"ĐƯỜNG XƯA LỐI CŨ", "duong xua loi cu"},
        {"Sơn_Tùng-MTP", "son tung mtp"},
        {"Chúng ta của hiện tại.mp3", "chung ta cua hien tai.mp3"},
        {"Nhạc/Trịnh Công Sơn/Diễm xưa.mp3", "nhac/trinh cong son/diem xua.mp3"},
        {"Ỷ Thiên Đồ Long Ký", "y thien do long ky"},
        {"", ""},
    };
    int failed = 0;
    for (auto& c : cases) {
        std::string got = assistant::Fold(c.in);
        bool ok = got == c.want;
        failed += ok ? 0 : 1;
        printf("%s %s -> %s\n", ok ? "ok  " : "FAIL", c.in, got.c_str());
    }
    // Spoken query matches a file name regardless of accents and case.
    bool match =
        assistant::Fold("/sdcard/Nhạc/Diễm Xưa - Khánh Ly.mp3").find(assistant::Fold("diễm xưa")) !=
        std::string::npos;
    printf("%s accent-insensitive match\n", match ? "ok  " : "FAIL");
    failed += match ? 0 : 1;

    // Cutting text never leaves half a UTF-8 character behind.
    struct Cut {
        const char* in;
        size_t max;
        const char* want;
    } cuts[] = {
        {"abc", 10, "abc"},   {"abcdef", 3, "abc"}, {"Hà", 2, "H"},   {"Hà", 3, "Hà"},
        {"Việt", 3, "Vi"},    {"Việt", 4, "Vi"},    {"Việt", 5, "Việ"}, {"ệ", 1, ""},
        {"ệ", 2, ""},         {"", 0, ""},
    };
    for (auto& c : cuts) {
        std::string got = c.in;
        assistant::TruncateUtf8(got, c.max);
        bool ok = got == c.want;
        failed += ok ? 0 : 1;
        printf("%s cut %s at %zu -> %s\n", ok ? "ok  " : "FAIL", c.in, c.max, got.c_str());
    }
    return failed == 0 ? 0 : 1;
}
