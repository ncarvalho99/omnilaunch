#pragma once
// Adapted from HomeNX (GPL-3.0), projects/menu/src/homebrew/HomebrewStore.hpp.
#include <cstdint>
#include <string>
#include <vector>

// Homebrew on the SD card: every .nro in sdmc:/switch/ and one folder below,
// with the name, author, version and icon embedded in its asset section.
struct HomebrewApp {
    std::string path;
    std::string name;
    std::string author;
    std::string version;
    std::vector<std::uint8_t> icon; // JPEG, may be empty
};

// Sorted by name. `skipPath` (OmniLaunch's own .nro) is left out. Without
// `withIcons` the icons stay on the SD (see readHomebrewIcon) so it's quick.
std::vector<HomebrewApp> scanHomebrew(const std::string& skipPath, bool withIcons = true);
std::vector<std::uint8_t> readHomebrewIcon(const std::string& path);
// The same .nro files by path only (none is opened): changes when one is added,
// removed or renamed. HOME compares it with the last full read's.
std::uint64_t nroPathsHash(const std::string& skipPath);

// Reads one .nro's name/author/version/icon. False if the file can't be opened.
bool readHomebrew(const std::string& path, HomebrewApp& app, bool withIcon = true);
