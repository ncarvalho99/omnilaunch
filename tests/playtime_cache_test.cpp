#include "core/Config.hpp"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
int main() {
    AppConfig config;
    config.setPlaytime(10, 100);
    config.playtimeCheckedSequence = 8;
    // The old cache had no owner: never relabel console totals as a profile's.
    assert(config.selectPlaytimeProfile("events-v1:profile-a"));
    assert(config.playtime.empty() && config.playtimeCheckedSequence == 0);
    config.setPlaytime(10, 200);
    config.playtimeCheckedSequence = 9;
    assert(!config.selectPlaytimeProfile("events-v1:profile-a"));
    assert(config.playtimeOf(10) == 200 && config.playtimeCheckedSequence == 9);
    assert(config.selectPlaytimeProfile("events-v1:profile-b"));
    assert(!config.hasPlaytime(10) && config.playtimeCheckedSequence == 0);
    config.setPlaytime(20, 0);
    assert(config.hasPlaytime(20)); // a measured zero stays known
    assert(config.selectPlaytimeProfile("events-v1:console"));
    assert(!config.hasPlaytime(20));
    // Real Config serialization in the container's isolated temporary cwd.
    std::filesystem::create_directories("sdmc:/config/OmniLaunch");
    config.setPlaytime(30, 300);
    config.playtimeCheckedSequence = 12;
    assert(config.save());
    AppConfig loaded;
    assert(loaded.load());
    assert(loaded.playtimeProfile == "events-v1:console");
    assert(loaded.playtimeOf(30) == 300 && loaded.playtimeCheckedSequence == 12);
    // Missing owner in a legacy config must not retain an earlier load's owner.
    std::ofstream(AppConfig::kConfigPath) << R"({"playtime":{"000000000000001E":123}})";
    assert(loaded.load() && loaded.playtimeProfile.empty());
    assert(loaded.selectPlaytimeProfile("events-v1:profile-c") && loaded.playtime.empty());
    std::cout << "playtime profile cache persistence tests PASS\n";
}
