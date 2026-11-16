# Host unit tests

Each `*.cc` file here is a small standalone program that runs on the build machine (no ESP-IDF,
no device). It prints one line per case and exits with the number of failures, so `0` means pass.

| Test | Covers |
| --- | --- |
| `test_lunar.cc` | `firmware/src/assistant/vn_lunar.h`: solar to lunar and back (`FromSolar`, `ToSolar`), leap months, Tết dates, can chi names (`YearName`, `DayName`) |

## Run

```bash
cd firmware
g++ -std=c++17 -Wall -Wextra -I src/assistant -I src -o /tmp/test_lunar tests/test_lunar.cc
/tmp/test_lunar && echo PASS
```

Run all of them the way CI does (`.github/workflows/ci.yml`, job `unit-tests`):

```bash
for t in firmware/tests/*.cc; do
  g++ -std=c++17 -Wall -Wextra -I firmware/src/assistant -I firmware/src -o "/tmp/$(basename "$t" .cc)" "$t" &&
  "/tmp/$(basename "$t" .cc)" || echo "FAIL: $t"
done
```

## Writing a test

- Only test code that has no ESP-IDF dependencies (header-only helpers such as `vn_lunar.h`).
  Keep such logic out of files that include `esp_*.h`, `lvgl.h` or `cJSON.h` so it stays testable.
- Include the header by name (`#include "vn_lunar.h"`); the compile line adds `-I src/assistant`.
- Return the failure count from `main()`. No test framework is needed.
- Use known reference values (for the lunar calendar: published Vietnamese calendars, UTC+7).
