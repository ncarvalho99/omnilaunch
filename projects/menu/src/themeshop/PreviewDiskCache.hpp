#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Catalogue previews (theme covers and sprite sheets, icon pack previews) kept
// on the SD card, so opening the Theme Shop or coming back to a tab reads them
// from the card instead of asking the server again.
//
// The key is the preview URL plus a revision token taken from the catalogue
// entry (version, archive size or hash). When a theme or pack is republished
// the token changes, the key changes, and the new preview is downloaded once.
// Files nobody asks for any more are removed by the size cap.
//
// All functions touch the SD card: call them from a worker thread.
namespace themeshop::previewcache {

constexpr const char* kDir = "sdmc:/config/OmniLaunch/cache/previews";
// Upper bound for the folder. A full browse of every animated theme (grid
// sheet + fullscreen sheet) is about 80 MB; past this the oldest files go.
constexpr std::uint64_t kMaxBytes = 48ull * 1024 * 1024;

bool load(const std::string& url, const std::string& token, std::vector<std::uint8_t>& out);
void store(const std::string& url, const std::string& token, const std::vector<std::uint8_t>& bytes);

} // namespace themeshop::previewcache
