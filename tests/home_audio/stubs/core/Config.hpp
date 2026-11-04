#pragma once
#include <cstdint>
#include <map>
#include <string>
struct AppConfig {
 bool accessibilityEnabled=false, accessibilitySpeakHints=true,
 accessibilitySpeakContextEveryFocus=true, customBgmShuffle=false,
 themeMusicEnabled=true, musicEnabled=true;
 int accessibilitySpeechRate=175;
 std::string soundPreset="wiiu", audioSourcePreference="theme_only";
 bool safeMode() const { return true; } // No host writes to sdmc:/ paths.
 std::map<std::uint64_t,std::uint64_t> playtime;
 std::uint64_t playtimeCheckedSequence=0;
 bool setPlaytime(std::uint64_t id,std::uint64_t ns){ auto& v=playtime[id]; if(v==ns) return false; v=ns; return true; }
};
