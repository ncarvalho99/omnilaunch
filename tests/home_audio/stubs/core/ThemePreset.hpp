#pragma once
#include <string>
enum class ThemePresetSource { BuiltIn, InstalledPackage };
struct ThemePreset { ThemePresetSource source=ThemePresetSource::BuiltIn;
 std::string id, soundPreset, installPath; };
