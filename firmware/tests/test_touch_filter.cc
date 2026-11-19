// Host test for the touch clean-up rules: g++ -std=c++17 -I firmware/src/assistant test_touch_filter.cc
#include <cstdio>

#include "touch_filter.h"

using assistant::TouchFilter;
using Event = TouchFilter::Event;

static int failures = 0;
#define CHECK(cond)                                         \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL line %d: %s\n", __LINE__, #cond);  \
            ++failures;                                     \
        }                                                   \
    } while (0)

int main() {
    {  // A tap gives exactly one press and one release.
        TouchFilter f;
        CHECK(f.Feed(true, 100, 100).event == Event::kDown);
        CHECK(f.Feed(true, 101, 100).event == Event::kMove);
        CHECK(f.Feed(false, 0, 0).event == Event::kNone);
        CHECK(f.Feed(false, 0, 0).event == Event::kNone);
        auto up = f.Feed(false, 0, 0);
        CHECK(up.event == Event::kUp);
        CHECK(up.x == 101 && up.y == 100);  // where the finger was, not the empty reading
        CHECK(!f.down());
        CHECK(f.Feed(false, 0, 0).event == Event::kNone);
    }
    {  // Short dropouts while the finger stays down do not release it.
        TouchFilter f;
        f.Feed(true, 50, 50);
        for (int i = 0; i < 5; ++i) {
            CHECK(f.Feed(false, 0, 0).event == Event::kNone);
            CHECK(f.Feed(false, 0, 0).event == Event::kNone);
            CHECK(f.Feed(true, 50, 50).event == Event::kMove);
        }
        CHECK(f.down());
    }
    {  // One misread point far away is dropped.
        TouchFilter f;
        f.Feed(true, 100, 100);
        CHECK(f.Feed(true, 380, 20).event == Event::kNone);
        auto next = f.Feed(true, 102, 100);
        CHECK(next.event == Event::kMove && next.x == 102);
    }
    {  // A real fast movement arrives one reading later.
        TouchFilter f;
        f.Feed(true, 100, 100);
        CHECK(f.Feed(true, 300, 100).event == Event::kNone);
        auto moved = f.Feed(true, 320, 100);
        CHECK(moved.event == Event::kMove && moved.x == 320);
    }
    {  // Normal drags pass straight through.
        TouchFilter f;
        f.Feed(true, 100, 100);
        for (int x = 120; x < 300; x += 20) {
            auto r = f.Feed(true, x, 100);
            CHECK(r.event == Event::kMove && r.x == x);
        }
    }
    {  // A new touch after a release starts cleanly.
        TouchFilter f;
        f.Feed(true, 10, 10);
        for (int i = 0; i < TouchFilter::kLiftReadings; ++i)
            f.Feed(false, 0, 0);
        auto again = f.Feed(true, 400, 400);
        CHECK(again.event == Event::kDown && again.x == 400);
    }
    printf(failures ? "%d FAILED\n" : "touch filter: all passed\n", failures);
    return failures ? 1 : 0;
}
