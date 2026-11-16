#pragma once
// Music on the SD card (mounted by the board at /sdcard).
#include <string>
#include <vector>

namespace assistant {

constexpr char kSdRoot[] = "/sdcard";
void SetSdMounted(bool mounted);
bool SdMounted();
// Playable files (.mp3, .aac) whose path contains the keyword (accent- and case-insensitive);
// empty keyword = all. At most 500, scanned once and cached.
std::vector<std::string> SdSongs(const std::string& keyword = "");
// "Thư mục/Tên bài.mp3" -> "Tên bài"
std::string SongTitle(const std::string& path);
// Lowercase ASCII with Vietnamese accents removed, for matching spoken requests to file names.
std::string Fold(const std::string& utf8);

}  // namespace assistant
