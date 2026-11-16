// Optional password for the settings web page and API (HTTP Basic auth, any user name).
// Only a salted, iterated SHA-256 of the password is stored in NVS. With no password set, the
// page stays open on the LAN as before.
#include "assistant_auth.h"

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>

#include "settings.h"

namespace assistant {
namespace {

constexpr char kTag[] = "Auth";
constexpr int kRounds = 2000;

std::mutex g_mutex;
bool g_loaded = false;
std::string g_salt, g_hash;  // hex; empty hash = no password

std::string Hex(const unsigned char* d, size_t n) {
    static const char* k = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        out += k[d[i] >> 4];
        out += k[d[i] & 15];
    }
    return out;
}

std::string Derive(const std::string& salt, const std::string& password) {
    const auto* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    unsigned char digest[32];
    std::string input = salt + password;
    mbedtls_md(info, reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
    for (int i = 1; i < kRounds; ++i) {
        unsigned char buf[32 + 64];
        memcpy(buf, digest, 32);
        size_t n = std::min(password.size(), sizeof(buf) - 32);
        memcpy(buf + 32, password.data(), n);
        mbedtls_md(info, buf, 32 + n, digest);
    }
    return Hex(digest, sizeof(digest));
}

bool SameString(const std::string& a, const std::string& b) {  // constant time
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= a[i] ^ b[i];
    return diff == 0;
}

void LoadLocked() {
    if (g_loaded)
        return;
    Settings s("assistant", false);
    g_salt = s.GetString("web_salt");
    g_hash = s.GetString("web_hash");
    g_loaded = true;
}

bool Authorized(httpd_req_t* req) {
    std::string salt, hash;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        LoadLocked();
        if (g_hash.empty())
            return true;
        salt = g_salt;
        hash = g_hash;
    }
    char header[160];
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK)
        return false;
    if (strncmp(header, "Basic ", 6) != 0)
        return false;
    unsigned char decoded[120];
    size_t len = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &len,
                              reinterpret_cast<unsigned char*>(header + 6),
                              strlen(header + 6)) != 0)
        return false;
    std::string pair(reinterpret_cast<char*>(decoded), len);
    auto colon = pair.find(':');
    if (colon == std::string::npos)
        return false;
    return SameString(Derive(salt, pair.substr(colon + 1)), hash);
}

esp_err_t Guard(httpd_req_t* req) {
    if (!Authorized(req)) {
        ESP_LOGW(kTag, "unauthorized %s", req->uri);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Bap\", charset=\"UTF-8\"");
        return httpd_resp_sendstr(req, "Cần mật khẩu");
    }
    auto handler = reinterpret_cast<esp_err_t (*)(httpd_req_t*)>(req->user_ctx);
    return handler(req);
}

}  // namespace

bool HasWebPassword() {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked();
    return !g_hash.empty();
}

void SetWebPassword(const std::string& password) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Settings s("assistant", true);
    if (password.empty()) {
        s.EraseKey("web_hash");
        s.EraseKey("web_salt");
        g_salt.clear();
        g_hash.clear();
    } else {
        unsigned char salt[16];
        esp_fill_random(salt, sizeof(salt));
        g_salt = Hex(salt, sizeof(salt));
        g_hash = Derive(g_salt, password);
        s.SetString("web_salt", g_salt);
        s.SetString("web_hash", g_hash);
    }
    g_loaded = true;
}

esp_err_t RegisterGuarded(void* server, const httpd_uri_t& route) {
    httpd_uri_t guarded = route;
    guarded.user_ctx = reinterpret_cast<void*>(route.handler);
    guarded.handler = Guard;
    return httpd_register_uri_handler(server, &guarded);
}

}  // namespace assistant
