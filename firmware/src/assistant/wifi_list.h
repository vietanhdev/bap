#pragma once
// Turns a raw Wi-Fi scan into the short list shown on the watch.
// No device headers here, so the rules are tested on the host (firmware/tests/test_keyboard.cc).
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace assistant {

struct WifiNetwork {
    std::string ssid;
    int rssi = -100;
    bool secured = true;
};

// One row per network name (the strongest access point), strongest first, hidden networks left out.
inline std::vector<WifiNetwork> BestNetworks(std::vector<WifiNetwork> scan, size_t max) {
    std::stable_sort(scan.begin(), scan.end(),
                     [](const WifiNetwork& a, const WifiNetwork& b) { return a.rssi > b.rssi; });
    std::vector<WifiNetwork> out;
    for (auto& net : scan) {
        if (out.size() >= max)
            break;
        if (net.ssid.empty())
            continue;
        const bool seen = std::any_of(out.begin(), out.end(), [&](const WifiNetwork& n) {
            return n.ssid == net.ssid;
        });
        if (!seen)
            out.push_back(std::move(net));
    }
    return out;
}

inline const char* SignalWord(int rssi) {
    return rssi >= -60 ? "Sóng mạnh" : (rssi >= -75 ? "Sóng vừa" : "Sóng yếu");
}

}  // namespace assistant
