#pragma once
// LRC lyrics parser ("[mm:ss.xx]text"), header-only for host tests.
// Handles 2- or 3-digit fractions, several timestamps on one line ("[00:12.30][01:20.00]chorus")
// and skips metadata tags ("[ar: ...]"). Result is sorted by time.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace assistant {

inline std::vector<std::pair<uint32_t, std::string>> ParseLrc(const std::string& lrc,
                                                              size_t max_lines = 200) {
    std::vector<std::pair<uint32_t, std::string>> out;
    size_t pos = 0;
    while (pos < lrc.size() && out.size() < max_lines) {
        size_t end = lrc.find('\n', pos);
        if (end == std::string::npos)
            end = lrc.size();
        std::string line = lrc.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        std::vector<uint32_t> stamps;
        size_t i = 0;
        while (i < line.size() && line[i] == '[') {
            size_t close = line.find(']', i);
            if (close == std::string::npos)
                break;
            std::string tag = line.substr(i + 1, close - i - 1);  // "mm:ss.xx"
            size_t colon = tag.find(':');
            if (colon == std::string::npos || colon == 0 || !isdigit((unsigned char)tag[0]))
                break;
            uint32_t mm = 0, ss = 0, frac_ms = 0;
            size_t k = 0;
            for (; k < colon; ++k)
                mm = mm * 10 + (tag[k] - '0');
            for (k = colon + 1; k < tag.size() && isdigit((unsigned char)tag[k]); ++k)
                ss = ss * 10 + (tag[k] - '0');
            if (k < tag.size() && (tag[k] == '.' || tag[k] == ':')) {
                uint32_t scale = 100;  // first fraction digit = tenths = 100 ms
                for (++k; k < tag.size() && isdigit((unsigned char)tag[k]) && scale > 0; ++k) {
                    frac_ms += (tag[k] - '0') * scale;
                    scale /= 10;
                }
            }
            stamps.push_back(mm * 60000 + ss * 1000 + frac_ms);
            i = close + 1;
        }
        if (stamps.empty())
            continue;
        std::string text = line.substr(i);
        size_t start = text.find_first_not_of(' ');
        text = start == std::string::npos ? "" : text.substr(start);
        if (text.empty())
            continue;
        for (uint32_t ms : stamps)
            out.emplace_back(ms, text);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

}  // namespace assistant
