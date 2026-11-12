#pragma once

#include "../SettingsScreen.hpp"

namespace settings::tabs {

class SystemTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
    static void refreshTimezoneRowDescription(SettingsScreen& screen, const std::string& zoneName);
    static void beginTimezoneChange(SettingsScreen& screen,
                                    const std::string& filter = std::string());
    static void refreshConsoleLanguageRow(SettingsScreen& screen, const std::string& name);
    static void beginConsoleLanguageChange(SettingsScreen& screen);
};

class AccessibilityTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class AudioTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class DisplayTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class InternetTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class SteamGridDbTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class ControllersTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class BluetoothTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class SleepTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class StorageTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class AboutTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

}
