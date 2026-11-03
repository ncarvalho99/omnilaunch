#pragma once
#include <string>
struct AppConfig {
 bool accessibilityEnabled=false, accessibilitySpeakHints=true,
 accessibilitySpeakContextEveryFocus=true, customBgmShuffle=false,
 themeMusicEnabled=true, musicEnabled=true;
 int accessibilitySpeechRate=175;
 std::string soundPreset="wiiu", audioSourcePreference="theme_only";
 bool safeMode() const { return true; } // No host writes to sdmc:/ paths.
};
