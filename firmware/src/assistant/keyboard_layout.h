#pragma once
// Layout and editing rules of the on-screen keyboard for the 412x412 round display.
// No device headers here, so the rules are tested on the host (firmware/tests/test_keyboard.cc).
#include <cstddef>
#include <string>
#include <vector>

namespace assistant::kb {

enum Code : char { kShift = 1, kBackspace = 2, kMode = 3, kDone = 4 };
enum Page { kLower = 0, kUpper = 1, kDigits = 2, kSymbols = 3 };

struct Key {
    int x, y, w, h;
    char code;
};

constexpr int kScreen = 412;
constexpr int kKeyW = 37, kKeyH = 48;
constexpr int kTop = 122;  // the typed text sits above this line
constexpr int kWideW = 46;
constexpr size_t kMaxLength = 64;
constexpr size_t kMinPassword = 8;  // WPA passwords are 8 to 64 characters

inline const char* const* RowsOf(int page) {
    static const char* const lower[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const char* const upper[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    static const char* const digits[] = {"1234567890", "@#$%&*-+=", "!?/:;'\""};
    static const char* const symbols[] = {"()[]{}<>,_", "^~`|\\", ""};
    switch (page) {
        case kUpper:
            return upper;
        case kDigits:
            return digits;
        case kSymbols:
            return symbols;
        default:
            return lower;
    }
}

inline std::vector<Key> Layout(int page) {
    std::vector<Key> keys;
    auto rows = RowsOf(page);
    for (int r = 0; r < 3; ++r) {
        const std::string row = rows[r];
        const int y = kTop + r * kKeyH;
        const int width = static_cast<int>(row.size()) * kKeyW;
        if (r == 2) {
            const int all = 7 * kKeyW + 2 * kWideW;
            const int left = (kScreen - all) / 2;
            keys.push_back({left, y, kWideW, kKeyH, kShift});
            int x = (kScreen - width) / 2;
            for (char c : row) {
                keys.push_back({x, y, kKeyW, kKeyH, c});
                x += kKeyW;
            }
            keys.push_back({left + all - kWideW, y, kWideW, kKeyH, kBackspace});
        } else {
            int x = (kScreen - width) / 2;
            for (char c : row) {
                keys.push_back({x, y, kKeyW, kKeyH, c});
                x += kKeyW;
            }
        }
    }
    const int y = kTop + 3 * kKeyH;
    int x = (kScreen - 320) / 2;
    keys.push_back({x, y, 70, kKeyH, kMode});
    keys.push_back({x + 70, y, 140, kKeyH, ' '});
    keys.push_back({x + 210, y, 40, kKeyH, '.'});
    keys.push_back({x + 250, y, 70, kKeyH, kDone});
    return keys;
}

// The key closest to a touch, so a finger that lands between two keys still types one of them.
// Returns -1 for a touch well away from the keyboard.
inline int NearestKey(const std::vector<Key>& keys, int x, int y) {
    constexpr int kReach = 30;
    int best = -1, best_distance = kReach * kReach + 1;
    for (size_t i = 0; i < keys.size(); ++i) {
        const Key& k = keys[i];
        const int dx = x < k.x ? k.x - x : (x >= k.x + k.w ? x - (k.x + k.w - 1) : 0);
        const int dy = y < k.y ? k.y - y : (y >= k.y + k.h ? y - (k.y + k.h - 1) : 0);
        const int distance = dx * dx + dy * dy;
        if (distance < best_distance) {
            best_distance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

inline std::string KeyLabel(const Key& key, int page) {
    switch (key.code) {
        case kShift:
            return page == kDigits ? "#+=" : (page == kSymbols ? "123" : "Aa");
        case kBackspace:
            return "<-";
        case kMode:
            return page == kLower || page == kUpper ? "123" : "abc";
        case kDone:
            return "OK";
        case ' ':
            return "";
        default:
            return std::string(1, key.code);
    }
}

inline bool IsValidPassword(const std::string& text) {
    return text.size() >= kMinPassword && text.size() <= kMaxLength;
}

struct Editor {
    std::string text;
    int page = kLower;

    // Returns true when the key finishes the input with a valid text.
    bool Press(char code) {
        switch (code) {
            case kShift:
                if (page == kLower || page == kUpper)
                    page = page == kLower ? kUpper : kLower;
                else
                    page = page == kDigits ? kSymbols : kDigits;
                return false;
            case kMode:
                page = page == kLower || page == kUpper ? kDigits : kLower;
                return false;
            case kBackspace:
                if (!text.empty())
                    text.pop_back();
                return false;
            case kDone:
                return IsValidPassword(text);
            default:
                if (text.size() < kMaxLength)
                    text.push_back(code);
                if (page == kUpper)
                    page = kLower;  // a capital is for one letter, as on a phone
                return false;
        }
    }
};

}  // namespace assistant::kb
