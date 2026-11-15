#include "home/HomeAudio.hpp"
#include "core/DebugLog.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace {
constexpr const char* kBuiltInSoundPreset = "wiiu";
bool isPackageSoundPreset(const std::string& preset) {
    return preset.rfind("package:", 0) == 0;
}

bool pathExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool directoryExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

std::string resolveAudioOverridePath(const std::string& preferredBase,
                                    const std::string& fallbackBase,
                                    const char* relativePath) {
    if (!preferredBase.empty()) {
        const std::string preferredPath = preferredBase + "/" + relativePath;
        if (pathExists(preferredPath))
            return preferredPath;
    }
    return fallbackBase + "/" + relativePath;
}

std::string installedThemePathFromPackagePreset(const std::string& preset) {
    if (!isPackageSoundPreset(preset))
        return {};

    const std::string slug = preset.substr(std::strlen("package:"));
    if (slug.empty())
        return {};

    const std::string installPath = std::string("sdmc:/config/OmniLaunch/themes/") + slug;
    return directoryExists(installPath) ? installPath : std::string();
}

std::string resolveThemeSoundBase(const std::string& installPath) {
    if (installPath.empty())
        return {};

    const std::string directSfx = installPath + "/sfx";
    const std::string directMusic = installPath + "/music";
    const std::string soundsRoot = installPath + "/sounds";

    const bool hasDirect = directoryExists(directSfx) || directoryExists(directMusic);
    if (hasDirect)
        return installPath;
    if (directoryExists(soundsRoot))
        return soundsRoot;
    return {};
}

} // namespace

namespace home {

HomeAudio::HomeAudio() {
#ifdef SWITCHU_HOMEBREW
    m_assets = "romfs:";
#else
    m_assets = "sdmc:/switch/OmniLaunch";
#endif
}

void HomeAudio::shutdown() {
    waitForLoad();
    // Speech owns mixer chunks/channels: release them while the mixer lives.
    accessibility.shutdown();
    audio.shutdown();
}

void HomeAudio::startTutorial() {
    waitForLoad();
    audio.initialize();
    audio.setVolume(0.34f);
    audio.loadTrack(m_assets + "/sounds/wiiu/music/bg4.mp3");
    audio.play();
    audio.setSfxVolume(0.52f);
    static constexpr char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const std::string base = m_assets + "/sounds/animalese/";
    for (const char* p = chars; *p; ++p) {
        const std::string id = "animalese_" + std::string(1, *p);
        audio.loadNamedSfx(id, base + std::string(1, *p) + ".wav", 0.45f);
    }
}

void HomeAudio::endTutorial() {
    accessibility.cancelAnnouncement();
    // Only tutorial-specific playback is disposable. Keep mixer/espeak alive.
    audio.stopAll();
    audio.clearTracks();
    audio.clearSfx();
}

void HomeAudio::configureAccessibility(const AppConfig& config,
                                        const std::string& language, bool tutorial) {
    accessibility.initialize(tutorial || config.accessibilityEnabled, language, m_assets);
    accessibility.setSpeakHints(config.accessibilitySpeakHints);
    accessibility.setSpeakContextEveryFocus(config.accessibilitySpeakContextEveryFocus);
    accessibility.setSpeechRate(config.accessibilitySpeechRate);
}

void HomeAudio::waitForLoad() {
    if (loadFuture.valid()) loadFuture.get();
}

void HomeAudio::startClassic(nxui::ThreadPool& pool, const AppConfig& config,
                             const std::vector<ThemePreset>& themes) {
    waitForLoad();
    if (started) return; // Keep the current track/paused state across frontends.
    // Tutorial audio is view-specific; replace its playlist, not its mixer.
    audio.stopAll();
    audio.clearTracks();
    audio.clearSfx();
    loadFuture = pool.submit([this, config, themes] {
        audio.initialize();
        availablePresets = scanAvailablePresets();
        loadSoundPreset(resolveSoundPresetId(config.soundPreset, themes), config, themes);
    });
}

void HomeAudio::queuePreset(nxui::ThreadPool& pool, const std::string& preset,
                            const AppConfig& config,
                            const std::vector<ThemePreset>& themes) {
    waitForLoad();
    loadFuture = pool.submit([this, preset, config, themes] {
        loadSoundPreset(preset, config, themes);
    });
}

std::string HomeAudio::resolveSoundPresetId(const std::string& preset, const std::vector<ThemePreset>& themes) const {
    std::string effectivePreset = preset;
    if (!isPackageSoundPreset(effectivePreset) && effectivePreset != kBuiltInSoundPreset) {
        DebugLog::log("[audio] preset '%s' blocked, using '%s' instead",
                      effectivePreset.c_str(),
                      kBuiltInSoundPreset);
        return kBuiltInSoundPreset;
    }

    if (!isPackageSoundPreset(effectivePreset))
        return effectivePreset;

    if (!resolveThemeSoundBase(installedThemePathFromPackagePreset(effectivePreset)).empty()) {
        DebugLog::log("[audio] package preset '%s' resolved from install directory", effectivePreset.c_str());
        return effectivePreset;
    }

    for (const auto& themePreset : themes) {
        if (themePreset.source != ThemePresetSource::InstalledPackage || themePreset.installPath.empty())
            continue;
        if (themePreset.id != effectivePreset && themePreset.soundPreset != effectivePreset)
            continue;
        return effectivePreset;
    }

    DebugLog::log("[audio] package preset '%s' unavailable, using '%s' instead",
                  effectivePreset.c_str(),
                  kBuiltInSoundPreset);
    return kBuiltInSoundPreset;
}

void HomeAudio::loadSoundPreset(const std::string& preset, const AppConfig& config, const std::vector<ThemePreset>& themes) {
    std::string effectivePreset = preset;
    const bool useBuiltInBase = (effectivePreset == kBuiltInSoundPreset);
    const std::string builtInBase = std::string(m_assets) + "/sounds/" + kBuiltInSoundPreset;

    std::string base;
    if (!useBuiltInBase) {
        base = resolveThemeSoundBase(installedThemePathFromPackagePreset(effectivePreset));

        for (const auto& themePreset : themes) {
            if (!base.empty())
                break;
            if (themePreset.source != ThemePresetSource::InstalledPackage || themePreset.installPath.empty())
                continue;
            if (themePreset.id != effectivePreset && themePreset.soundPreset != effectivePreset)
                continue;

            base = resolveThemeSoundBase(themePreset.installPath);
            break;
        }
    }

    if (base.empty()) {
        if (isPackageSoundPreset(effectivePreset)) {
            base = installedThemePathFromPackagePreset(effectivePreset);
        } else {
            base = std::string(m_assets) + "/sounds/" + effectivePreset;
        }
    }
    DebugLog::log("[audio] Loading preset '%s' from %s", effectivePreset.c_str(), base.c_str());

    const bool hasCustomSfx = directoryExists(base + "/sfx");
    const bool hasCustomMusic = directoryExists(base + "/music");
    const std::string musicBase = hasCustomMusic ? base : builtInBase;
    const std::string preferredSfxBase = (!useBuiltInBase && hasCustomSfx) ? base : std::string();
    auto sfxPath = [&](const char* relativePath) {
        return resolveAudioOverridePath(preferredSfxBase, builtInBase, relativePath);
    };

    if (!useBuiltInBase && !hasCustomSfx) {
        DebugLog::log("[audio] preset '%s' has no custom SFX directory, using '%s' SFX fallback",
                      effectivePreset.c_str(),
                      kBuiltInSoundPreset);
    }
    if (!useBuiltInBase && !hasCustomMusic) {
        DebugLog::log("[audio] preset '%s' has no custom music, using '%s' music fallback",
                      effectivePreset.c_str(),
                      kBuiltInSoundPreset);
    }

    audio.loadSfx(Sfx::Navigate,        sfxPath("sfx/navigation.wav"));
    audio.loadSfx(Sfx::Activate,        sfxPath("sfx/activation.wav"));
    audio.loadSfx(Sfx::PageChange,      sfxPath("sfx/tab_transition.wav"));
    audio.loadSfx(Sfx::ModalShow,       sfxPath("sfx/show_modal.wav"));
    audio.loadSfx(Sfx::ModalHide,       sfxPath("sfx/hide_modal.wav"));
    audio.loadSfx(Sfx::LaunchGame,      sfxPath("sfx/launch_game.wav"));
    audio.loadSfx(Sfx::ThemeToggle,     sfxPath("sfx/toggle_on.wav"));
    audio.loadSfx(Sfx::ToggleOff,       sfxPath("sfx/toggle_off.wav"));
    audio.loadSfx(Sfx::SliderUp,        sfxPath("sfx/slider_up.wav"));
    audio.loadSfx(Sfx::SliderDown,      sfxPath("sfx/slider_down.wav"));
    audio.loadSfx(Sfx::ConfirmPositive, sfxPath("sfx/confirm.wav"));
    audio.loadSfx(Sfx::Volume,          sfxPath("sfx/volume.wav"));

    loadedSoundPreset = effectivePreset;
    reloadMusicTracks(config, themes);
}

constexpr const char* kCustomMusicDir = "sdmc:/config/OmniLaunch/music";

std::vector<TrackInfo> HomeAudio::scanCustomBgmTracks(const AppConfig& config) {
    if (config.safeMode())   // boot-guard safe mode: no third-party audio
        return {};
    std::vector<TrackInfo> tracks;
    std::error_code ec;
    std::filesystem::create_directories(kCustomMusicDir, ec);
    ec.clear();

    if (!std::filesystem::is_directory(kCustomMusicDir, ec)) {
        return tracks;
    }

    for (const auto& entry : std::filesystem::directory_iterator(kCustomMusicDir, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!entry.is_regular_file(ec))
            continue;

        std::error_code szEc;
        auto sz = entry.file_size(szEc);
        if (!szEc && sz < 1024)
            continue;

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (ext == ".mp3" || ext == ".ogg" || ext == ".wav") {
            tracks.push_back({entry.path().string(), entry.path().stem().string()});
        }
    }

    std::sort(tracks.begin(), tracks.end(), [](const TrackInfo& a, const TrackInfo& b) {
        return a.title < b.title;
    });

    return tracks;
}

bool HomeAudio::reloadMusicTracks(const AppConfig& config, const std::vector<ThemePreset>& themes) {
    const bool wasPlaying = audio.isPlaying();
    std::vector<TrackInfo> desired;
    audio.setShuffle(config.customBgmShuffle);
    audio.setRepeat(config.bgmRepeat);

    const std::string& pref = config.audioSourcePreference;
    customBgmTracks = scanCustomBgmTracks(config);
    bool loaded = false;
    // Theme songs show the theme's name, not the file name ("theme").
    auto themeTrack = [this](std::size_t t) -> TrackInfo {
        return {themeMusicTracks[t],
                themeTrackTitle(themeMusicName, themeMusicTracks[t], t, themeMusicTracks.size())};
    };

    const bool themeMusicAllowed = config.themeMusicEnabled;

    if (pref == "custom_only") {
        if (!customBgmTracks.empty()) {
            for (const auto& track : customBgmTracks)
                desired.push_back(track);
            loaded = true;
            DebugLog::log("[audio] Custom Only: Loaded %zu custom track(s)", customBgmTracks.size());
        }
    } else if (pref == "theme_first") {
        // "First" orders the playlist; it no longer hides the other source.
        // Downloading one song used to drop the theme music from both players.
        const size_t themeCount = themeMusicAllowed ? themeMusicTracks.size() : 0;
        if (themeMusicAllowed) {
            for (std::size_t t = 0; t < themeMusicTracks.size(); ++t)
                desired.push_back(themeTrack(t));
        }
        for (const auto& track : customBgmTracks)
            desired.push_back(track);
        loaded = !desired.empty();
        if (loaded)
            DebugLog::log("[audio] Theme First: Loaded %zu theme + %zu custom track(s)",
                          themeCount, customBgmTracks.size());
    } else if (pref == "theme_only") {
        if (themeMusicAllowed && !themeMusicTracks.empty()) {
            for (std::size_t t = 0; t < themeMusicTracks.size(); ++t)
                desired.push_back(themeTrack(t));
            loaded = true;
            DebugLog::log("[audio] Theme Only: Loaded %zu theme track(s)", themeMusicTracks.size());
        }
    } else { // "custom_first" (default)
        // SD songs play first, then the theme music stays in the playlist so
        // the Classic and Cards players can still select it.
        for (const auto& track : customBgmTracks)
            desired.push_back(track);
        const size_t themeCount = themeMusicAllowed ? themeMusicTracks.size() : 0;
        if (themeMusicAllowed) {
            for (std::size_t t = 0; t < themeMusicTracks.size(); ++t)
                desired.push_back(themeTrack(t));
        }
        loaded = !desired.empty();
        if (loaded)
            DebugLog::log("[audio] Custom First: Loaded %zu custom + %zu theme track(s)",
                          customBgmTracks.size(), themeCount);
    }

    // Fallback to Preset Music if nothing loaded and mode allows it
    if (!loaded && pref != "custom_only") {
        const std::string effectivePreset = resolveSoundPresetId(loadedSoundPreset.empty() ? config.soundPreset : loadedSoundPreset, themes);
        std::string musicBase = std::string(m_assets) + "/sounds/" + effectivePreset;
        std::string musicDir = musicBase + "/music";
        std::error_code ec;
        if (std::filesystem::is_directory(musicDir, ec)) {
            std::vector<std::string> tracks;
            ec.clear();
            for (const auto& entry : std::filesystem::directory_iterator(musicDir, ec)) {
                if (ec)
                    break;

                std::string name = entry.path().filename().string();
                if (name.size() > 4 && name.substr(name.size() - 4) == ".mp3")
                    tracks.push_back(name);
            }
            std::sort(tracks.begin(), tracks.end(), [](const std::string& left, const std::string& right) {
                const bool leftIsHome = (left == "home.mp3");
                const bool rightIsHome = (right == "home.mp3");
                if (leftIsHome != rightIsHome)
                    return leftIsHome;
                return left < right;
            });
            for (const auto& t : tracks)
                desired.push_back({musicDir + "/" + t, std::filesystem::path(t).stem().string()});
            DebugLog::log("[audio] Loaded %zu preset music tracks", tracks.size());
        } else {
            DebugLog::log("[audio] No music directory for preset '%s'", effectivePreset.c_str());
        }
    }

    homeTracks = desired;
    // While the theme-music list is the queue, the HOME playlist is only
    // rebuilt for the player to show; the songs playing are left alone --
    // unless the queue was emptied meanwhile (a sound preset change clears it).
    if (queue == Queue::Themes) {
        if (audio.trackCount() > 0)
            return false;
        queue = Queue::Home;
    }

    const auto current = audio.tracks();
    const bool unchanged = current.size() == desired.size() &&
        std::equal(current.begin(), current.end(), desired.begin(),
                   [](const TrackInfo& a, const TrackInfo& b) {
                       return a.path == b.path && a.title == b.title;
                   });
    if (unchanged) return false;
    // A new download, a deleted song or the other frontend rebuilding the same
    // list must not restart the music from another song: keep the one that
    // was playing when it is still in the list.
    const std::string playingPath = audio.currentTrackPath();
    audio.stop();
    audio.clearTracks();
    int keep = -1;
    for (std::size_t i = 0; i < desired.size(); ++i) {
        if (keep < 0 && !playingPath.empty() && desired[i].path == playingPath)
            keep = static_cast<int>(i);
        audio.loadTrack(desired[i].path, desired[i].title);
    }
    if (wasPlaying && config.musicEnabled) {
        if (keep >= 0)
            audio.playTrack(keep);
        else
            audio.play();
    }
    return true;
}

std::string HomeAudio::themeTrackTitle(const std::string& themeName, const std::string& path,
                                       std::size_t index, std::size_t count) {
    std::string base = themeName;
    if (base.empty()) {
        // No theme name known: the folder of a package theme, then the file.
        const std::filesystem::path p(path);
        base = p.stem().string();
        const std::filesystem::path parent = p.parent_path();
        if ((base == "theme" || base == "music" || base == "bgm") && parent.has_parent_path()) {
            std::filesystem::path dir = parent;
            while (dir.has_parent_path() && (dir.filename() == "music" || dir.filename() == "media"))
                dir = dir.parent_path();
            if (!dir.filename().empty())
                base = dir.filename().string();
        }
    }
    if (count > 1)
        base += " (" + std::to_string(index + 1) + ")";
    return base;
}

bool HomeAudio::isCustomTrack(const std::string& path) {
    const std::string prefix = std::string(kCustomMusicDir) + "/";
    return path.size() > prefix.size() && path.rfind(prefix, 0) == 0
        && path.find("..") == std::string::npos && path.find('\\') == std::string::npos
        && path.find('/', prefix.size()) == std::string::npos;
}

bool HomeAudio::deleteCustomTrack(const std::string& path) {
    if (!isCustomTrack(path))
        return false;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return false;
    const bool removed = std::filesystem::remove(path, ec);
    const std::size_t dot = path.find_last_of('.');
    if (dot != std::string::npos)
        std::filesystem::remove(path.substr(0, dot) + ".jpg", ec);
    DebugLog::log("[media] deleted track %s (%d)", path.c_str(), removed ? 1 : 0);
    return removed;
}

void HomeAudio::playHomeTrack(int index, const AppConfig& config) {
    if (queue != Queue::Home) {
        // Back to the HOME playlist: load it as the queue again.
        queue = Queue::Home;
        audio.stop();
        audio.clearTracks();
        for (const auto& t : homeTracks)
            audio.loadTrack(t.path, t.title);
        audio.setShuffle(config.customBgmShuffle);
        audio.setRepeat(config.bgmRepeat);
    }
    audio.playTrack(index);
}

void HomeAudio::playThemeTrack(const std::vector<TrackInfo>& themes, int index) {
    if (index < 0 || index >= static_cast<int>(themes.size()))
        return;
    const auto current = audio.tracks();
    const bool same = queue == Queue::Themes && current.size() == themes.size() &&
        std::equal(current.begin(), current.end(), themes.begin(),
                   [](const TrackInfo& a, const TrackInfo& b) { return a.path == b.path; });
    if (!same) {
        queue = Queue::Themes;
        audio.stop();
        audio.clearTracks();
        for (const auto& t : themes)
            audio.loadTrack(t.path, t.title);
    }
    audio.playTrack(index);
}

std::vector<std::string> HomeAudio::scanAvailablePresets() {
    std::vector<std::string> presets;
    std::string soundsDir = std::string(m_assets) + "/sounds";
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(soundsDir, ec)) {
        if (ec)
            break;

        std::string name = entry.path().filename().string();
        if (name != kBuiltInSoundPreset) continue;

        std::string sub = entry.path().string();
        if (!entry.is_directory(ec)) {
            ec.clear();
            continue;
        }

        std::string sfxSub = sub + "/sfx";
        std::string musicSub = sub + "/music";
        bool hasSfx = directoryExists(sfxSub);
        bool hasMusic = directoryExists(musicSub);
        if (hasSfx || hasMusic)
            presets.push_back(name);
    }
    std::sort(presets.begin(), presets.end());
    return presets;
}


} // namespace home
