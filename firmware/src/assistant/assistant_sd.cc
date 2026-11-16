// Music on the SD card: scan, match by name, play through the radio pipeline ("radio:sd:<kw>").
#include "assistant_sd.h"

#include <dirent.h>
#include <esp_log.h>
#include <sys/stat.h>

#include <atomic>
#include <cstring>
#include <mutex>

namespace assistant {
namespace {

constexpr char TAG[] = "SdMusic";
constexpr size_t kMaxSongs = 500;
std::atomic<bool> g_mounted{false};
std::mutex g_mutex;
std::vector<std::string> g_songs;
bool g_scanned = false;

bool Playable(const char* name) {
    const char* dot = strrchr(name, '.');
    if (dot == nullptr || name[0] == '.') return false;
    return strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".aac") == 0;
}

void Scan(const std::string& dir, int depth) {
    if (depth > 3 || g_songs.size() >= kMaxSongs) return;
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) return;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string path = dir + "/" + e->d_name;
        if (e->d_type == DT_DIR) {
            Scan(path, depth + 1);
        } else if (Playable(e->d_name)) {
            g_songs.push_back(path);
            if (g_songs.size() >= kMaxSongs) break;
        }
    }
    closedir(d);
}

// Decode one UTF-8 code point.
uint32_t Next(const std::string& s, size_t& i) {
    unsigned char c = s[i];
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    uint32_t cp = n == 1 ? c : c & (0x3F >> (n - 1));
    for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 0x3F);
    i += n;
    return cp;
}

char Base(uint32_t cp) {
    // Vietnamese letters by Unicode block -> base letter.
    static const struct { const char* letters; char base; } kMap[] = {
        {"àáạảãâầấậẩẫăằắặẳẵÀÁẠẢÃÂẦẤẬẨẪĂẰẮẶẲẴ", 'a'}, {"èéẹẻẽêềếệểễÈÉẸẺẼÊỀẾỆỂỄ", 'e'},
        {"ìíịỉĩÌÍỊỈĨ", 'i'}, {"òóọỏõôồốộổỗơờớợởỡÒÓỌỎÕÔỒỐỘỔỖƠỜỚỢỞỠ", 'o'},
        {"ùúụủũưừứựửữÙÚỤỦŨƯỪỨỰỬỮ", 'u'}, {"ỳýỵỷỹỲÝỴỶỸ", 'y'}, {"đĐ", 'd'}};
    if (cp < 0x80) return (char)tolower((int)cp);
    for (auto& m : kMap) {
        std::string letters = m.letters;
        for (size_t i = 0; i < letters.size();) {
            if (Next(letters, i) == cp) return m.base;
        }
    }
    return '?';
}

}  // namespace

std::string Fold(const std::string& utf8) {
    std::string out;
    for (size_t i = 0; i < utf8.size();) {
        char c = Base(Next(utf8, i));
        out += (c == '_' || c == '-') ? ' ' : c;
    }
    return out;
}

void SetSdMounted(bool mounted) {
    g_mounted = mounted;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_scanned = false;
    g_songs.clear();
}

bool SdMounted() { return g_mounted; }

std::vector<std::string> SdSongs(const std::string& keyword) {
    if (!g_mounted) return {};
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_scanned) {
        Scan(kSdRoot, 0);
        g_scanned = true;
        ESP_LOGI(TAG, "%u songs on the card", (unsigned)g_songs.size());
    }
    if (keyword.empty()) return g_songs;
    std::string want = Fold(keyword);
    std::vector<std::string> out;
    for (auto& p : g_songs) {
        if (Fold(p).find(want) != std::string::npos) out.push_back(p);
    }
    return out;
}

std::string SongTitle(const std::string& path) {
    auto slash = path.rfind('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    auto dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

}  // namespace assistant
