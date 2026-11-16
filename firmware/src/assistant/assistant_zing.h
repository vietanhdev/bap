#pragma once
// Zing MP3 (zingmp3.vn) search and 128 kbps stream URLs, called directly from the device using the
// same signed web API as the zingmp3.vn site. Personal use only; see assistant_zing.cc for the
// signing scheme and how to refresh the keys when the site changes them.
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

namespace assistant {

struct ZingSong {
    std::string id;  // "encodeId", e.g. "d7qtCrRV89zn"
    std::string title;
    std::string artists;  // "artistsNames", comma separated
    int duration_s = 0;
    bool vip = false;  // streamingStatus != 1: VIP/Premium only, ZingStreamUrl() will fail
};

// Searches songs. limit is clamped to 1..20. Blocks (network); call from a worker task.
std::expected<std::vector<ZingSong>, std::string> ZingSearch(const std::string& query, int limit);

// Returns the 128 kbps MP3 URL (https://a128-z3.zmdcdn.me/...). The URL is signed and valid for
// about 48 hours, needs no cookie, and supports HTTP Range. Fails for VIP songs.
std::expected<std::string, std::string> ZingStreamUrl(const std::string& song_id);

// Time-synced lyrics ("[mm:ss.xx]line" LRC) as (start_ms, text) pairs; empty if the song has none.
std::vector<std::pair<uint32_t, std::string>> ZingLyrics(const std::string& song_id);

}  // namespace assistant
