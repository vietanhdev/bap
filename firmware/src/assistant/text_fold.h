#pragma once
// Accent folding for matching spoken Vietnamese to file names: "Hà Nội mùa thu" -> "ha noi mua
// thu". Header-only and free of ESP-IDF dependencies so it is unit-tested on the host.
#include <cctype>
#include <cstdint>
#include <string>

namespace assistant {

// Shortens s to at most max bytes without cutting a UTF-8 character in half: a broken character
// makes the whole tool reply invalid JSON.
inline void TruncateUtf8(std::string& s, size_t max) {
    if (s.size() <= max)
        return;
    while (max > 0 && (static_cast<unsigned char>(s[max]) & 0xC0) == 0x80)
        --max;
    s.resize(max);
}

namespace fold_detail {

// Decode one UTF-8 code point.
inline uint32_t Next(const std::string& s, size_t& i) {
    unsigned char c = s[i];
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    uint32_t cp = n == 1 ? c : c & (0x3F >> (n - 1));
    for (int k = 1; k < n && i + k < s.size(); ++k)
        cp = (cp << 6) | (s[i + k] & 0x3F);
    i += n;
    return cp;
}

inline char Base(uint32_t cp) {
    // Vietnamese letters by Unicode block -> base letter.
    static const struct {
        const char* letters;
        char base;
    } kMap[] = {{"àáạảãâầấậẩẫăằắặẳẵÀÁẠẢÃÂẦẤẬẨẪĂẰẮẶẲẴ", 'a'},
                {"èéẹẻẽêềếệểễÈÉẸẺẼÊỀẾỆỂỄ", 'e'},
                {"ìíịỉĩÌÍỊỈĨ", 'i'},
                {"òóọỏõôồốộổỗơờớợởỡÒÓỌỎÕÔỒỐỘỔỖƠỜỚỢỞỠ", 'o'},
                {"ùúụủũưừứựửữÙÚỤỦŨƯỪỨỰỬỮ", 'u'},
                {"ỳýỵỷỹỲÝỴỶỸ", 'y'},
                {"đĐ", 'd'}};
    if (cp < 0x80)
        return (char)tolower((int)cp);
    for (auto& m : kMap) {
        std::string letters = m.letters;
        for (size_t i = 0; i < letters.size();) {
            if (Next(letters, i) == cp)
                return m.base;
        }
    }
    return '?';
}

}  // namespace fold_detail

// Lowercase ASCII with Vietnamese accents removed; '_' and '-' become spaces, other non-ASCII '?'.
inline std::string Fold(const std::string& utf8) {
    std::string out;
    for (size_t i = 0; i < utf8.size();) {
        char c = fold_detail::Base(fold_detail::Next(utf8, i));
        out += (c == '_' || c == '-') ? ' ' : c;
    }
    return out;
}

}  // namespace assistant
