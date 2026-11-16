// Zing MP3 search and stream URLs straight from the device (no relay server), for the owner's
// personal use. Uses the signed web API that https://zingmp3.vn itself calls.
//
// Signing scheme (from the site's JS bundle, function that sets ctime/version/sig/apiKey):
//   SIG_KEYS = ctime, id, type, page, count, version   (other params such as q are NOT signed)
//   s   = concatenation, sorted by key, of "<key>=<value>" for SIG_KEYS present with a non-empty
//         value; key and value encodeURIComponent()'d; NO separator between pairs
//   sig = hex(HMAC-SHA512(key = kSecret, msg = path + hex(SHA-256(s))))
//   GET
//   https://zingmp3.vn<path>?<params>&ctime=<unix>&version=<kVersion>&sig=<sig>&apiKey=<kApiKey>
//   with path like "/api/v2/song/get/streaming". The request needs the zmp3_rqid cookie, which the
//   site sets on any first request (GET https://zingmp3.vn/). Responses are always gzip encoded,
//   even with "Accept-Encoding: identity", so they are inflated here with the ROM miniz.
//   API errors seen: err -201 (no/expired cookie), -104 "Incorect signature", -1150 VIP only.
//   ctime is not checked for freshness (+-days accepted), but a synced clock is still required.
//
// Refreshing the constants when requests start failing with -104 (signature) or the site moves on:
//   curl -s --compressed https://zingmp3.vn/ | grep -o 'https://zmdjs.zmdcdn.me/[^"]*main.min.js'
//   curl -s --compressed <that url> -o main.js
//   grep -oE 'var r="[A-Za-z0-9]{32}",i="[A-Za-z0-9]{32}"' main.js    -> r = apiKey, i = secret
//   grep -oE 'L="[0-9]+\.[0-9]+\.[0-9]+"' main.js                     -> version (matches /vX.Y.Z/)
//   grep -oE 'm=\["ctime"[^]]*\]' main.js                               -> signed param names
// Verified 2026-11-16 against zmp3-desktop v1.22.4 (key and secret unchanged since at least 2022;
// only the version string moves). Python reference: research/zing/zing_proto.py.
// Host test of the signing code: firmware/tests/test_zing_sig.cc.
//
// Terms of service: this is an unofficial, undocumented API. Zing MP3's terms forbid automated
// access and redistribution; keep it to personal listening, low request rates, no downloading.

#ifndef ZING_SIG_HOST_TEST
#include "assistant_zing.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <mbedtls/md.h>

#if __has_include("miniz.h")
#include "miniz.h"  // esp_rom (ESP-IDF 5.x)
#elif __has_include("rom/miniz.h")
#include "rom/miniz.h"
#else
#include "esp32s3/rom/miniz.h"
#endif

#include <ctime>
#include <memory>
#include <mutex>

#include "board.h"
#endif  // ZING_SIG_HOST_TEST

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace assistant {
namespace zing_internal {

constexpr char kBase[] = "https://zingmp3.vn";
constexpr char kVersion[] = "1.22.4";
constexpr char kApiKey[] = "X5BM3w8N7MKozC0B85o4KMlzLZKhV00y";
constexpr char kSecret[] = "acOrvUS15XRW2o9JksiK1KgQ6Vbds8ZW";
constexpr const char* kSigKeys[] = {"ctime", "id", "type", "page", "count", "version"};

using Params = std::vector<std::pair<std::string, std::string>>;

// Implemented with mbedtls on the device; the host test provides its own.
std::string Sha256Hex(const std::string& data);
std::string HmacSha512Hex(const std::string& key, const std::string& msg);

// JavaScript encodeURIComponent on UTF-8 bytes.
std::string UriEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            (c != 0 && strchr("-_.!~*'()", c) != nullptr)) {
            out += static_cast<char>(c);
            continue;
        }
        out += '%';
        out += hex[c >> 4];
        out += hex[c & 15];
    }
    return out;
}

std::string SigParamString(const Params& params) {
    Params signed_params;
    for (const auto& kv : params) {
        if (kv.second.empty())
            continue;
        for (const char* k : kSigKeys) {
            if (kv.first == k) {
                signed_params.push_back(kv);
                break;
            }
        }
    }
    std::sort(signed_params.begin(), signed_params.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string s;
    for (const auto& kv : signed_params)
        s += UriEncode(kv.first) + "=" + UriEncode(kv.second);
    return s;
}

std::string Signature(const std::string& path, const Params& params) {
    return HmacSha512Hex(kSecret, path + Sha256Hex(SigParamString(params)));
}

}  // namespace zing_internal

#ifndef ZING_SIG_HOST_TEST

namespace zing_internal {

#define TAG "AssistantZing"

std::string ToHex(const unsigned char* d, size_t n) {
    static const char* hex = "0123456789abcdef";
    std::string out(n * 2, '0');
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = hex[d[i] >> 4];
        out[2 * i + 1] = hex[d[i] & 15];
    }
    return out;
}

std::string Sha256Hex(const std::string& data) {
    unsigned char out[32];
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
               reinterpret_cast<const unsigned char*>(data.data()), data.size(), out);
    return ToHex(out, sizeof(out));
}

std::string HmacSha512Hex(const std::string& key, const std::string& msg) {
    // RFC 2104 HMAC over SHA-512 (block size 128). mbedTLS 4 dropped mbedtls_md_hmac().
    const mbedtls_md_info_t* sha512 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA512);
    unsigned char k[128] = {};
    if (key.size() > sizeof(k)) {
        mbedtls_md(sha512, reinterpret_cast<const unsigned char*>(key.data()), key.size(), k);
    } else {
        memcpy(k, key.data(), key.size());
    }
    std::string inner(128, '\0'), outer(128 + 64, '\0');
    for (size_t i = 0; i < 128; ++i) {
        inner[i] = static_cast<char>(k[i] ^ 0x36);
        outer[i] = static_cast<char>(k[i] ^ 0x5c);
    }
    inner += msg;
    mbedtls_md(sha512, reinterpret_cast<const unsigned char*>(inner.data()), inner.size(),
               reinterpret_cast<unsigned char*>(&outer[128]));
    unsigned char out[64];
    mbedtls_md(sha512, reinterpret_cast<const unsigned char*>(outer.data()), outer.size(), out);
    return ToHex(out, sizeof(out));
}

constexpr size_t kMaxInflated = 256 * 1024;  // search with 20 results is ~35 KB
constexpr int64_t kCookieTtlUs = 30LL * 60 * 1000000;
constexpr char kUserAgent[] =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 "
    "Safari/537.36";

std::mutex g_mutex;    // guards the cookie
std::string g_cookie;  // "zmp3_rqid=..."
int64_t g_cookie_at_us = 0;

// Inflates a gzip member. Non-gzip input is returned as is.
bool Gunzip(std::string* body) {
    const auto* in = reinterpret_cast<const uint8_t*>(body->data());
    size_t n = body->size();
    if (n < 18 || in[0] != 0x1f || in[1] != 0x8b)
        return true;
    if (in[2] != 8)
        return false;
    uint8_t flg = in[3];
    size_t pos = 10;
    if (flg & 4) {  // FEXTRA
        if (pos + 2 > n)
            return false;
        pos += 2 + (in[pos] | (in[pos + 1] << 8));
    }
    if (flg & 8)  // FNAME
        while (pos < n && in[pos++] != 0) {
        }
    if (flg & 16)  // FCOMMENT
        while (pos < n && in[pos++] != 0) {
        }
    if (flg & 2)  // FHCRC
        pos += 2;
    if (pos + 8 > n)
        return false;
    size_t isize = in[n - 4] | (in[n - 3] << 8) | (in[n - 2] << 16) | ((size_t)in[n - 1] << 24);
    if (isize == 0 || isize > kMaxInflated)
        return false;

    // tinfl_decompressor is ~11 KB: keep it off the task stack.
    auto* inf = static_cast<tinfl_decompressor*>(
        heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (inf == nullptr)
        inf = static_cast<tinfl_decompressor*>(malloc(sizeof(tinfl_decompressor)));
    if (inf == nullptr)
        return false;
    tinfl_init(inf);
    std::string out(isize, '\0');
    size_t in_len = n - pos - 8;
    size_t out_len = isize;
    auto* out_buf = reinterpret_cast<mz_uint8*>(out.data());
    tinfl_status st = tinfl_decompress(inf, in + pos, &in_len, out_buf, out_buf, &out_len,
                                       TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(inf);
    if (st != TINFL_STATUS_DONE || out_len != isize) {
        ESP_LOGW(TAG, "inflate failed: status %d, %u/%u bytes", (int)st, (unsigned)out_len,
                 (unsigned)isize);
        return false;
    }
    *body = std::move(out);
    return true;
}

// "name=value; Path=/; ..." -> "name=value"
std::string CookiePair(const std::string& set_cookie) {
    return set_cookie.substr(0, set_cookie.find(';'));
}

struct Response {
    int status = 0;
    std::string body;
    std::string set_cookie;  // last Set-Cookie header only (the HTTP client keeps one per name)
};

std::expected<Response, std::string> Get(const std::string& url, const std::string& cookie) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
    http->SetTimeout(10000);
    http->SetHeader("User-Agent", kUserAgent);
    http->SetHeader("Referer", "https://zingmp3.vn/");
    http->SetHeader("Accept", "*/*");
    http->SetHeader("Accept-Encoding", "gzip");
    if (!cookie.empty())
        http->SetHeader("Cookie", cookie);
    if (auto opened = http->Open("GET", url); !opened)
        return std::unexpected("Không kết nối được Zing MP3");
    Response r;
    auto status = http->GetStatusCode();
    r.status = status ? *status : 0;
    r.set_cookie = http->GetResponseHeader("Set-Cookie");
    r.body = http->ReadAll();
    http->Close();
    if (r.status != 200)
        return std::unexpected("Zing MP3 trả về lỗi HTTP " + std::to_string(r.status));
    if (!Gunzip(&r.body))
        return std::unexpected("Không giải nén được phản hồi Zing MP3");
    return r;
}

// The homepage sets zmp3_app_version.1 and then zmp3_rqid; only zmp3_rqid is needed, and it is the
// last Set-Cookie header, which is the one the HTTP client keeps.
std::expected<std::string, std::string> Cookie(bool force_refresh) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!force_refresh && !g_cookie.empty() &&
            esp_timer_get_time() - g_cookie_at_us < kCookieTtlUs)
            return g_cookie;
    }
    auto r = Get(std::string(kBase) + "/", "");
    if (!r)
        return std::unexpected(r.error());
    std::string pair = CookiePair(r->set_cookie);
    if (pair.empty())
        return std::unexpected("Zing MP3 không cấp cookie");
    if (pair.rfind("zmp3_rqid=", 0) != 0)
        ESP_LOGW(TAG, "unexpected cookie %s", pair.c_str());
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cookie = pair;
    g_cookie_at_us = esp_timer_get_time();
    return g_cookie;
}

struct JsonDeleter {
    void operator()(cJSON* j) const { cJSON_Delete(j); }
};
using Json = std::unique_ptr<cJSON, JsonDeleter>;

// Calls a signed API path such as "/api/v2/search". Returns the parsed root with err == 0.
std::expected<Json, std::string> Api(const std::string& path, Params params) {
    time_t now = time(nullptr);
    if (now < 1700000000)
        return std::unexpected("Chưa đồng bộ giờ, chưa gọi được Zing MP3");
    params.emplace_back("ctime", std::to_string((long long)now));
    params.emplace_back("version", kVersion);
    std::string url = std::string(kBase) + path + "?";
    for (const auto& kv : params)
        url += UriEncode(kv.first) + "=" + UriEncode(kv.second) + "&";
    url += "sig=" + Signature(path, params) + "&apiKey=" + kApiKey;

    for (int attempt = 0; attempt < 2; attempt++) {
        auto cookie = Cookie(attempt > 0);
        if (!cookie)
            return std::unexpected(cookie.error());
        auto r = Get(url, *cookie);
        if (!r)
            return std::unexpected(r.error());
        Json root(cJSON_Parse(r->body.c_str()));
        r->body.clear();
        r->body.shrink_to_fit();
        if (!root)
            return std::unexpected("Phản hồi Zing MP3 không hợp lệ");
        const cJSON* err_item = cJSON_GetObjectItem(root.get(), "err");
        int err = cJSON_IsNumber(err_item) ? err_item->valueint : -1;
        if (err == 0)
            return root;
        const char* msg = cJSON_GetStringValue(cJSON_GetObjectItem(root.get(), "msg"));
        ESP_LOGW(TAG, "%s: err %d %s", path.c_str(), err, msg ? msg : "");
        if (err == -201 && attempt == 0)
            continue;  // cookie missing or expired: fetch a new one and retry once
        if (err == -1150)
            return std::unexpected("Bài hát chỉ dành cho tài khoản VIP");
        if (err == -104)
            return std::unexpected("Sai chữ ký Zing MP3 (cần cập nhật khóa API)");
        return std::unexpected(std::string("Zing MP3 báo lỗi: ") +
                               (msg ? msg : std::to_string(err)));
    }
    return std::unexpected("Zing MP3 từ chối yêu cầu");
}

std::string Str(const cJSON* obj, const char* key) {
    const char* s = cJSON_GetStringValue(cJSON_GetObjectItem(obj, key));
    return s ? s : "";
}

}  // namespace zing_internal

std::expected<std::vector<ZingSong>, std::string> ZingSearch(const std::string& query, int limit) {
    using namespace zing_internal;
    if (query.empty())
        return std::unexpected("Thiếu từ khóa tìm kiếm");
    limit = std::clamp(limit, 1, 20);
    auto root =
        Api("/api/v2/search",
            {{"q", query}, {"type", "song"}, {"page", "1"}, {"count", std::to_string(limit)}});
    if (!root)
        return std::unexpected(root.error());
    const cJSON* items = cJSON_GetObjectItem(cJSON_GetObjectItem(root->get(), "data"), "items");
    std::vector<ZingSong> songs;
    const cJSON* it;
    cJSON_ArrayForEach (it, items) {
        ZingSong s;
        s.id = Str(it, "encodeId");
        if (s.id.empty())
            continue;
        s.title = Str(it, "title");
        s.artists = Str(it, "artistsNames");
        const cJSON* dur = cJSON_GetObjectItem(it, "duration");
        s.duration_s = cJSON_IsNumber(dur) ? dur->valueint : 0;
        const cJSON* st = cJSON_GetObjectItem(it, "streamingStatus");
        s.vip = cJSON_IsNumber(st) && st->valueint != 1;
        songs.push_back(std::move(s));
        if ((int)songs.size() >= limit)
            break;
    }
    return songs;
}

std::expected<std::string, std::string> ZingStreamUrl(const std::string& song_id) {
    using namespace zing_internal;
    if (song_id.empty())
        return std::unexpected("Thiếu mã bài hát");
    auto root = Api("/api/v2/song/get/streaming", {{"id", song_id}});
    if (!root)
        return std::unexpected(root.error());
    std::string url = Str(cJSON_GetObjectItem(root->get(), "data"), "128");
    if (url.rfind("http", 0) != 0)
        return std::unexpected("Bài hát chỉ dành cho tài khoản VIP");
    return url;
}

std::vector<std::pair<uint32_t, std::string>> ZingLyrics(const std::string& song_id) {
    using namespace zing_internal;
    std::vector<std::pair<uint32_t, std::string>> out;
    auto root = Api("/api/v2/lyric/get/lyric", {{"id", song_id}});
    if (!root)
        return out;
    std::string file = Str(cJSON_GetObjectItem(root->get(), "data"), "file");
    root->reset();
    if (file.rfind("https://", 0) != 0)
        return out;
    auto r = Get(file, "");
    if (!r || r->body.size() > 64 * 1024)
        return out;
    const std::string& lrc = r->body;
    size_t pos = 0;
    while (pos < lrc.size() && out.size() < 200) {
        size_t end = lrc.find('\n', pos);
        if (end == std::string::npos)
            end = lrc.size();
        std::string line = lrc.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        // "[mm:ss.xx]text"; metadata tags like "[ar: ...]" have no digits after '['.
        int mm = 0, ss = 0, cs = 0, consumed = 0;
        if (sscanf(line.c_str(), "[%d:%d.%d]%n", &mm, &ss, &cs, &consumed) < 3 || consumed == 0)
            continue;
        std::string text = line.substr(consumed);
        size_t start = text.find_first_not_of(' ');
        text = start == std::string::npos ? "" : text.substr(start);
        uint32_t ms = (uint32_t)(mm * 60000 + ss * 1000 + cs * 10);
        if (!text.empty())
            out.emplace_back(ms, text);
    }
    return out;
}

#undef TAG
#endif  // ZING_SIG_HOST_TEST

}  // namespace assistant
