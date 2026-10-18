#pragma once
#include <SDL2/SDL_mixer.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>

enum class Sfx {
    Navigate,
    Activate,
    PageChange,
    ModalShow,
    ModalHide,
    LaunchGame,
    ThemeToggle,
    ToggleOff,
    SliderUp,
    SliderDown,
    ConfirmPositive,
    Volume,
};

struct TrackInfo {
    std::string path;
    std::string title;
};

class AudioManager {
public:
    AudioManager() = default;
    ~AudioManager();

    bool initialize();
    void shutdown();

    void loadTrack(const std::string& path, const std::string& title = "");
    void clearTracks();
    void play();
    void playTrack(int index);
    void pause();
    void resume();
    bool isPaused() const { return m_paused.load(); }
    void togglePlayPause();
    void stop();
    /// Silence music and active sound effects without unloading audio assets.
    void stopAll();
    void nextTrack();
    void previousTrack();
    void setVolume(float vol);
    float volume() const { return m_volume; }
    bool  isPlaying() const { return m_playing; }

    void setMusicFade(float fade);
    float musicFade() const { return m_musicFade; }

    void setShuffle(bool shuffle);
    bool isShuffle() const;

    int currentTrackIndex() const;
    size_t trackCount() const;
    std::string currentTrackTitle() const;
    std::string currentTrackPath() const;
    std::vector<TrackInfo> tracks() const;

    void update();

    void loadSfx(Sfx id, const std::string& path);
    void clearSfx();
    void playSfx(Sfx id);
    void loadNamedSfx(const std::string& id, const std::string& path, float volumeScale = 1.f);
    void playNamedSfx(const std::string& id);
    void setSfxVolume(float vol);
    float sfxVolume() const { return m_sfxVolume; }

private:
    static std::atomic<AudioManager*> s_instance;
    static void onTrackFinished();

    void applyMusicVolume();
    void rebuildShuffleOrderLocked();
    void playCurrentTrackLocked();

    mutable std::mutex m_trackMutex;
    std::vector<TrackInfo> m_tracks;
    std::vector<int> m_shuffleOrder;
    int   m_current = -1;
    int   m_shufflePos = 0;
    bool  m_shuffle = false;
    float m_volume  = 0.5f;
    float m_musicFade = 1.f;
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_trackFinished{false};
    bool  m_initialized = false;
    Mix_Music* m_activeMusic = nullptr;

    std::mutex m_sfxMutex;
    std::unordered_map<int, Mix_Chunk*> m_sfx;
    std::unordered_map<std::string, Mix_Chunk*> m_namedSfx;
    std::unordered_map<std::string, float> m_namedSfxVolumeScales;
    // Effects decode on first play instead of all at start-up (issue #2
    // item 8): these keep the registered path until that moment.
    std::unordered_map<int, std::string> m_sfxPaths;
    std::unordered_map<std::string, std::string> m_namedSfxPaths;
    float m_sfxVolume = 0.7f;
};
