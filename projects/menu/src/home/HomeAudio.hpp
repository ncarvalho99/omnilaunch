#pragma once
#include "core/AudioManager.hpp"
#include "core/AccessibilityManager.hpp"
#include "core/Config.hpp"
#include "core/ThemePreset.hpp"
#include <nxui/core/ThreadPool.hpp>
#include <future>

namespace home {

// Owned by HomeCore, not by a frontend. Workers capture this service plus value
// snapshots only; no Activity/config/theme references survive a view switch.
// UI consumers read the loaded state after joining loadFuture on the UI thread.
class HomeAudio {
public:
    HomeAudio();
    void shutdown();
    void startTutorial();
    void endTutorial();
    void configureAccessibility(const AppConfig& config, const std::string& language,
                                bool tutorial = false);
    void waitForLoad();
    void startClassic(nxui::ThreadPool& pool, const AppConfig& config,
                      const std::vector<ThemePreset>& themes);
    void queuePreset(nxui::ThreadPool& pool, const std::string& preset,
                     const AppConfig& config, const std::vector<ThemePreset>& themes);
    std::string resolveSoundPresetId(const std::string& preset,
                                    const std::vector<ThemePreset>& themes) const;
    void loadSoundPreset(const std::string& preset, const AppConfig& config,
                         const std::vector<ThemePreset>& themes);
    std::vector<TrackInfo> scanCustomBgmTracks(const AppConfig& config);
    bool reloadMusicTracks(const AppConfig& config, const std::vector<ThemePreset>& themes);
    std::vector<std::string> scanAvailablePresets();

    AudioManager audio;
    AccessibilityManager accessibility;
    std::future<void> loadFuture;
    bool started = false;
    bool presetChangePending = false;
    std::string loadedSoundPreset;
    std::string pendingSoundPreset;
    std::vector<std::string> availablePresets;
    std::vector<std::string> themeMusicTracks;
    std::vector<TrackInfo> customBgmTracks;

private:
    std::string m_assets;
};

} // namespace home
