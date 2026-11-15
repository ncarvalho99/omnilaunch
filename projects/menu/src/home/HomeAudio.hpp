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

    // The music player (Classic and Cards share one) has two lists: the HOME
    // playlist (downloaded songs plus the active theme's music) and the music
    // of every installed theme. Playing from the second one makes it the
    // queue until a song of the first is chosen again.
    enum class Queue { Home, Themes };
    void playHomeTrack(int index, const AppConfig& config);
    void playThemeTrack(const std::vector<TrackInfo>& themes, int index);
    // Removes a downloaded song (and its cover) from the SD music folder;
    // refuses anything outside it. The caller reloads the playlist.
    static bool deleteCustomTrack(const std::string& path);
    static bool isCustomTrack(const std::string& path);
    // Display title of a theme song: the theme's name, numbered when it has
    // more than one ("theme" for every theme file read as nothing useful).
    static std::string themeTrackTitle(const std::string& themeName, const std::string& path,
                                       std::size_t index, std::size_t count);

    AudioManager audio;
    AccessibilityManager accessibility;
    std::future<void> loadFuture;
    bool started = false;
    bool presetChangePending = false;
    std::string loadedSoundPreset;
    std::string pendingSoundPreset;
    std::vector<std::string> availablePresets;
    std::vector<std::string> themeMusicTracks;
    std::string themeMusicName;       // name of the theme themeMusicTracks belong to
    std::vector<TrackInfo> customBgmTracks;
    std::vector<TrackInfo> homeTracks; // the HOME playlist as last built
    Queue queue = Queue::Home;

private:
    std::string m_assets;
};

} // namespace home
