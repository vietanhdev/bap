// Host test for the on-screen keyboard and the Wi-Fi list:
//   g++ -std=c++17 -I firmware/src/assistant test_keyboard.cc
#include <cstdio>
#include <set>
#include <string>

#include "keyboard_layout.h"
#include "wifi_list.h"

using namespace assistant;
using namespace assistant::kb;

static int failures = 0;
#define CHECK(cond)                                         \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL line %d: %s\n", __LINE__, #cond);  \
            ++failures;                                     \
        }                                                   \
    } while (0)

static bool InsideCircle(int x, int y) {
    const int dx = x - kScreen / 2, dy = y - kScreen / 2;
    return dx * dx + dy * dy <= (kScreen / 2) * (kScreen / 2);
}

int main() {
    std::set<char> typed;
    for (int page : {kLower, kUpper, kDigits, kSymbols}) {
        const auto keys = Layout(page);
        for (size_t i = 0; i < keys.size(); ++i) {
            const Key& k = keys[i];
            // Every corner of every key is on the round glass.
            CHECK(InsideCircle(k.x, k.y));
            CHECK(InsideCircle(k.x + k.w - 1, k.y));
            CHECK(InsideCircle(k.x, k.y + k.h - 1));
            CHECK(InsideCircle(k.x + k.w - 1, k.y + k.h - 1));
            CHECK(k.y >= kTop);
            // The middle of a key always types that key, so keys never overlap there.
            CHECK(NearestKey(keys, k.x + k.w / 2, k.y + k.h / 2) == static_cast<int>(i));
            if (k.code >= ' ')
                typed.insert(k.code);
        }
    }
    // A Wi-Fi password can use any printable ASCII character.
    for (char c = ' '; c <= '~'; ++c) {
        if (!typed.count(c))
            printf("missing key: %c\n", c);
        CHECK(typed.count(c) == 1);
    }

    {  // A touch between keys picks a neighbour; a touch far away picks nothing.
        const auto keys = Layout(kLower);
        CHECK(NearestKey(keys, keys[0].x - 5, keys[0].y + 5) == 0);
        CHECK(NearestKey(keys, 206, 20) == -1);
    }
    {  // Capitals last for one letter.
        Editor e;
        e.Press(kShift);
        CHECK(e.page == kUpper);
        e.Press('A');
        CHECK(e.page == kLower);
        e.Press('b');
        CHECK(e.text == "Ab");
        e.Press(kBackspace);
        CHECK(e.text == "A");
        e.Press(kBackspace);
        e.Press(kBackspace);  // nothing left to delete
        CHECK(e.text.empty());
    }
    {  // Moving between the pages.
        Editor e;
        e.Press(kMode);
        CHECK(e.page == kDigits);
        e.Press(kShift);
        CHECK(e.page == kSymbols);
        e.Press(kShift);
        CHECK(e.page == kDigits);
        e.Press('1');
        CHECK(e.page == kDigits);  // digits stay until the user leaves
        e.Press(kMode);
        CHECK(e.page == kLower);
    }
    {  // OK needs a real WPA password length, and typing stops at the limit.
        Editor e;
        for (int i = 0; i < 7; ++i)
            e.Press('x');
        CHECK(!e.Press(kDone));
        e.Press('x');
        CHECK(e.Press(kDone));
        for (int i = 0; i < 100; ++i)
            e.Press('y');
        CHECK(e.text.size() == kMaxLength);
        CHECK(e.Press(kDone));
    }
    {  // The Wi-Fi list keeps the strongest access point of each name.
        auto list = BestNetworks({{"Home", -80, true},
                                  {"", -30, true},
                                  {"Cafe", -50, false},
                                  {"Home", -55, true},
                                  {"Far", -90, true}},
                                 2);
        CHECK(list.size() == 2);
        CHECK(list[0].ssid == "Cafe" && !list[0].secured);
        CHECK(list[1].ssid == "Home" && list[1].rssi == -55);
        CHECK(std::string(SignalWord(-50)) == "Sóng mạnh");
        CHECK(std::string(SignalWord(-70)) == "Sóng vừa");
        CHECK(std::string(SignalWord(-85)) == "Sóng yếu");
    }
    printf(failures ? "%d FAILED\n" : "keyboard: all passed\n", failures);
    return failures ? 1 : 0;
}
