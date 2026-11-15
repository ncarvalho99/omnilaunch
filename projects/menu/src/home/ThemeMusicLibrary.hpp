#pragma once
// Music of every installed theme, for the "Theme music" list of the music
// player. Reads each theme's manifest from the SD card: run it on a worker.
#include "core/AudioManager.hpp"
#include <string>
#include <vector>

namespace home {

// One entry per existing music file of every installed package theme, titled
// with the theme's name (numbered when a theme has several songs), sorted by
// theme name. `assetsRoot` resolves built-in relative paths.
std::vector<TrackInfo> scanThemeMusicLibrary(const std::string& assetsRoot);

} // namespace home
