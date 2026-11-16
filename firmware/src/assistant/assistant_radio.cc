// Internet radio for the notification player: plain MP3/AAC streams and HLS playlists (.m3u8)
// with AAC or MPEG-TS segments. Decoded to 16-bit PCM and handed to the audio service.
#include "assistant_radio.h"

#include <esp_audio_dec_default.h>
#include <esp_audio_simple_dec.h>
#include <esp_audio_simple_dec_default.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "board.h"

#define TAG "Radio"

namespace assistant {
namespace {

constexpr const char* kPrefix = "radio:";

class Decoder {
public:
    explicit Decoder(esp_audio_simple_dec_type_t type) {
        static bool registered = false;
        if (!registered) {
            esp_audio_dec_register_default();  // MP3/AAC/... codecs
            esp_audio_simple_dec_register_default();  // stream parsers (MP3, AAC, TS)
            registered = true;
        }
        esp_audio_simple_dec_cfg_t cfg = {};
        cfg.dec_type = type;
        if (esp_audio_simple_dec_open(&cfg, &handle_) != ESP_AUDIO_ERR_OK)
            handle_ = nullptr;
        out_.resize(4608 * 2);
    }
    ~Decoder() {
        if (handle_ != nullptr)
            esp_audio_simple_dec_close(handle_);
    }
    bool ok() const { return handle_ != nullptr; }

    // Feed encoded bytes; emits mono PCM through sink. Returns false when the sink asks to stop.
    bool Feed(const uint8_t* data, size_t len, const PcmSink& sink) {
        esp_audio_simple_dec_raw_t raw = {};
        raw.buffer = const_cast<uint8_t*>(data);
        raw.len = len;
        while (raw.len > 0) {
            esp_audio_simple_dec_out_t frame = {};
            frame.buffer = out_.data();
            frame.len = out_.size();
            auto ret = esp_audio_simple_dec_process(handle_, &raw, &frame);
            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                out_.resize(frame.needed_size);
                continue;
            }
            if (ret != ESP_AUDIO_ERR_OK) {
                ESP_LOGW(TAG, "decode error %d, skipping %u bytes", ret, (unsigned)raw.len);
                return true;  // corrupt chunk: drop it, keep the stream going
            }
            if (frame.decoded_size > 0) {
                esp_audio_simple_dec_info_t info = {};
                esp_audio_simple_dec_get_info(handle_, &info);
                if (!Emit(frame.decoded_size, info, sink))
                    return false;
            }
            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
            if (raw.consumed == 0 && frame.decoded_size == 0)
                break;  // needs more input
        }
        return true;
    }

private:
    bool Emit(uint32_t bytes, const esp_audio_simple_dec_info_t& info, const PcmSink& sink) {
        if (info.bits_per_sample != 16 || info.sample_rate == 0)
            return true;
        const int16_t* in = reinterpret_cast<const int16_t*>(out_.data());
        size_t frames = bytes / sizeof(int16_t) / (info.channel ? info.channel : 1);
        std::vector<uint8_t> pcm(frames * sizeof(int16_t));
        int16_t* mono = reinterpret_cast<int16_t*>(pcm.data());
        for (size_t i = 0; i < frames; ++i) {
            mono[i] = info.channel == 2 ? (int16_t)(((int)in[2 * i] + in[2 * i + 1]) / 2) : in[i];
        }
        return sink(std::move(pcm), (int)info.sample_rate);
    }

    esp_audio_simple_dec_handle_t handle_ = nullptr;
    std::vector<uint8_t> out_;
};

esp_audio_simple_dec_type_t TypeFor(const std::string& url, const std::string& content_type) {
    auto has = [](const std::string& s, const char* sub) {
        return s.find(sub) != std::string::npos;
    };
    if (has(url, ".ts") || has(content_type, "mp2t"))
        return ESP_AUDIO_SIMPLE_DEC_TYPE_TS;
    if (has(url, ".aac") || has(content_type, "aac") || has(url, "vovmedia"))
        return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
}

std::string Resolve(const std::string& base, const std::string& ref) {
    if (ref.rfind("http://", 0) == 0 || ref.rfind("https://", 0) == 0)
        return ref;
    if (!ref.empty() && ref[0] == '/') {
        size_t scheme = base.find("://");
        size_t host_end = base.find('/', scheme == std::string::npos ? 0 : scheme + 3);
        return base.substr(0, host_end) + ref;
    }
    size_t query = base.find('?');
    std::string path = base.substr(0, query);
    return path.substr(0, path.rfind('/') + 1) + ref;
}

std::vector<std::string> PlaylistEntries(const std::string& body) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t end = body.find('\n', pos);
        if (end == std::string::npos)
            end = body.size();
        std::string line = body.substr(pos, end - pos);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty() && line[0] != '#')
            out.push_back(line);
        pos = end + 1;
    }
    return out;
}

// Streams one HTTP resource through the decoder. Returns false when stopped or on error.
bool StreamUrl(const std::string& url, Decoder* decoder, esp_audio_simple_dec_type_t* type_out,
               const PcmSink& sink, const Cancelled& cancelled, bool endless, int redirects = 0) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    http->SetTimeout(10000);
    http->SetHeader("User-Agent", "Mozilla/5.0 (Bap radio)");
    http->SetHeader("Icy-MetaData", "0");
    if (auto opened = http->Open("GET", url); !opened) {
        ESP_LOGW(TAG, "open failed: %s", url.c_str());
        return false;
    }
    auto status = http->GetStatusCode();
    if (status && *status >= 300 && *status < 400) {  // CDN redirects (Zing MP3 uses 302)
        std::string location = http->GetResponseHeader("Location");
        http->Close();
        if (location.empty() || redirects >= 3)
            return false;
        return StreamUrl(Resolve(url, location), decoder, type_out, sink, cancelled, endless,
                         redirects + 1);
    }
    if (!status || *status >= 400) {
        http->Close();
        return false;
    }
    std::unique_ptr<Decoder> own;
    if (decoder == nullptr) {
        auto type = TypeFor(url, http->GetResponseHeader("Content-Type"));
        if (type_out)
            *type_out = type;
        own = std::make_unique<Decoder>(type);
        decoder = own.get();
        if (!decoder->ok()) {
            http->Close();
            return false;
        }
    }
    std::vector<char> buf(4096);
    bool keep_going = true;
    size_t skip = 0;  // bytes of a leading ID3 tag still to drop (HLS .aac segments carry one)
    bool first_chunk = true;
    size_t total = 0;
    const char* why = "cancelled";
    while (!cancelled()) {
        auto n = http->Read(buf.data(), buf.size());
        if (!n) {
            why = "read error";
            keep_going = false;
            break;
        }
        if (*n <= 0) {
            why = "end of stream";
            keep_going = true;  // a song file or HLS segment finished normally
            break;
        }
        total += *n;
        const uint8_t* data = reinterpret_cast<uint8_t*>(buf.data());
        size_t len = *n;
        if (first_chunk) {
            first_chunk = false;
            if (len >= 10 && memcmp(data, "ID3", 3) == 0) {
                skip = 10 + ((data[6] & 0x7F) << 21 | (data[7] & 0x7F) << 14 |
                             (data[8] & 0x7F) << 7 | (data[9] & 0x7F));
            }
        }
        if (skip > 0) {
            size_t drop = std::min(skip, len);
            skip -= drop;
            data += drop;
            len -= drop;
            if (len == 0)
                continue;
        }
        if (!decoder->Feed(data, len, sink)) {
            why = "player stopped";
            keep_going = false;
            break;
        }
    }
    http->Close();
    ESP_LOGI(TAG, "stream ended (%s) after %u bytes", why, (unsigned)total);
    (void)endless;
    return keep_going && !cancelled();
}

bool FetchText(const std::string& url, std::string* body, int redirects = 0) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    http->SetTimeout(8000);
    if (!http->Open("GET", url))
        return false;
    auto status = http->GetStatusCode();
    if (status && *status >= 300 && *status < 400 && redirects < 3) {
        std::string location = http->GetResponseHeader("Location");
        http->Close();
        return !location.empty() && FetchText(Resolve(url, location), body, redirects + 1);
    }
    if (!status || *status != 200) {
        http->Close();
        return false;
    }
    *body = http->ReadAll();
    http->Close();
    return true;
}

bool PlayHls(std::string playlist_url, const PcmSink& sink, const Cancelled& cancelled) {
    std::string body;
    if (!FetchText(playlist_url, &body))
        return false;
    if (body.find("#EXT-X-STREAM-INF") !=
        std::string::npos) {  // master playlist: take first variant
        auto entries = PlaylistEntries(body);
        if (entries.empty())
            return false;
        playlist_url = Resolve(playlist_url, entries.front());
        if (!FetchText(playlist_url, &body))
            return false;
    }
    std::unique_ptr<Decoder> decoder;
    std::set<std::string> played;
    std::deque<std::string> order;
    bool first = true;
    while (!cancelled()) {
        auto segments = PlaylistEntries(body);
        // Start near the live edge: skip all but the last two segments on the first pass.
        size_t start = first && segments.size() > 2 ? segments.size() - 2 : 0;
        first = false;
        bool any = false;
        for (size_t i = start; i < segments.size() && !cancelled(); ++i) {
            if (played.count(segments[i]))
                continue;
            played.insert(segments[i]);
            order.push_back(segments[i]);
            if (order.size() > 64) {
                played.erase(order.front());
                order.pop_front();
            }
            std::string url = Resolve(playlist_url, segments[i]);
            if (!decoder) {
                decoder = std::make_unique<Decoder>(TypeFor(url, ""));
                if (!decoder->ok())
                    return false;
            }
            any = true;
            if (!StreamUrl(url, decoder.get(), nullptr, sink, cancelled, false) && cancelled())
                return true;
        }
        if (!any)
            vTaskDelay(pdMS_TO_TICKS(1000));
        if (cancelled() || !FetchText(playlist_url, &body))
            break;
    }
    return cancelled();
}

}  // namespace

bool IsRadioUrl(const std::string& url) { return url.rfind(kPrefix, 0) == 0; }

bool PlayRadio(const std::string& radio_url, const PcmSink& sink, const Cancelled& cancelled) {
    std::string url = radio_url.substr(strlen(kPrefix));
    ESP_LOGI(TAG, "Playing %s", url.c_str());
    for (int attempt = 0; attempt < 3 && !cancelled(); ++attempt) {
        bool ok = url.find(".m3u8") != std::string::npos
                      ? PlayHls(url, sink, cancelled)
                      : StreamUrl(url, nullptr, nullptr, sink, cancelled, true);
        if (ok || cancelled())
            return true;
        ESP_LOGW(TAG, "stream dropped, retrying (%d)", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    return cancelled();
}

}  // namespace assistant
