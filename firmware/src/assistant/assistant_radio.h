#pragma once
// Internet radio streamed through the notification player (see notify_player.cc).
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace assistant {

// Receives mono 16-bit PCM; return false to stop playback.
using PcmSink = std::function<bool(std::vector<uint8_t>&& pcm, int sample_rate)>;
using Cancelled = std::function<bool()>;

// Radio URLs are "radio:<http(s) url>" (MP3/AAC stream or HLS .m3u8).
bool IsRadioUrl(const std::string& url);
// Blocks until cancelled or the stream fails permanently. Returns true when stopped normally.
bool PlayRadio(const std::string& radio_url, const PcmSink& sink, const Cancelled& cancelled);

}  // namespace assistant
