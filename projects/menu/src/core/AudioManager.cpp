#include "AudioManager.hpp"
#include "core/DebugLog.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <random>

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize() {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::fprintf(stderr, "[Audio] SDL_Init(AUDIO) failed: %s\n", SDL_GetError());
        return false;
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        std::fprintf(stderr, "[Audio] Mix_OpenAudio failed: %s\n", Mix_GetError());
        return false;
    }
    Mix_AllocateChannels(8);
    m_initialized = true;
    setVolume(m_volume);
    return true;
}

void AudioManager::shutdown() {
    if (!m_initialized) return;

    Mix_HookMusicFinished(nullptr);
    s_instance.store(nullptr);
    Mix_HaltMusic();
    Mix_HaltChannel(-1);
    Mix_CloseAudio();
    m_initialized = false;
    m_playing.store(false);
    m_paused.store(false);
    m_trackFinished.store(false);

    if (m_activeMusic) {
        Mix_FreeMusic(m_activeMusic);
        m_activeMusic = nullptr;
    }
    m_tracks.clear();
    m_shuffleOrder.clear();
    m_current = -1;
    m_shufflePos = 0;

    for (auto& [id, chunk] : m_sfx) Mix_FreeChunk(chunk);
    m_sfx.clear();
    for (auto& [id, chunk] : m_namedSfx) Mix_FreeChunk(chunk);
    m_namedSfx.clear();
    m_namedSfxVolumeScales.clear();
}

void AudioManager::rebuildShuffleOrderLocked() {
    m_shuffleOrder.resize(m_tracks.size());
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        m_shuffleOrder[i] = static_cast<int>(i);
    }
    if (m_shuffleOrder.size() > 1) {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(m_shuffleOrder.begin(), m_shuffleOrder.end(), g);
    }
    m_shufflePos = 0;
}

void AudioManager::loadTrack(const std::string& path, const std::string& title) {
    std::string effTitle = title;
    if (effTitle.empty()) {
        std::filesystem::path p(path);
        effTitle = p.stem().string();
    }
    std::lock_guard<std::mutex> lk(m_trackMutex);
    m_tracks.push_back({path, effTitle});
    rebuildShuffleOrderLocked();
}

void AudioManager::clearTracks() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    Mix_HookMusicFinished(nullptr);
    Mix_HaltMusic();
    if (m_activeMusic) {
        Mix_FreeMusic(m_activeMusic);
        m_activeMusic = nullptr;
    }
    m_tracks.clear();
    m_shuffleOrder.clear();
    m_current = -1;
    m_shufflePos = 0;
    m_playing.store(false);
    m_paused.store(false);
    m_trackFinished.store(false);
}

std::atomic<AudioManager*> AudioManager::s_instance{nullptr};

void AudioManager::onTrackFinished() {
    AudioManager* inst = s_instance.load();
    if (inst) {
        inst->m_trackFinished.store(true);
    }
}

void AudioManager::playCurrentTrackLocked() {
    if (m_current < 0 || m_current >= static_cast<int>(m_tracks.size())) {
        m_playing.store(false);
        return;
    }

    Mix_HookMusicFinished(nullptr);
    Mix_HaltMusic();
    if (m_activeMusic) {
        Mix_FreeMusic(m_activeMusic);
        m_activeMusic = nullptr;
    }

    const auto& track = m_tracks[m_current];
    m_activeMusic = Mix_LoadMUS(track.path.c_str());
    if (!m_activeMusic) {
        DebugLog::log("[audio] Failed to load %s: %s", track.path.c_str(), Mix_GetError());
        std::fprintf(stderr, "[Audio] Failed to load %s: %s\n", track.path.c_str(), Mix_GetError());
        m_playing.store(false);
        return;
    }

    s_instance.store(this);
    Mix_HookMusicFinished(onTrackFinished);
    applyMusicVolume();

    const int loops = (m_tracks.size() == 1) ? -1 : 1;
    DebugLog::log("[audio] playing track %d/%zu: '%s' (loops=%d vol=%d)",
                  m_current, m_tracks.size(), track.path.c_str(), loops,
                  (int)(m_volume * m_musicFade * MIX_MAX_VOLUME));

    // 250ms fade-in for smooth acoustic transition
    if (Mix_FadeInMusic(m_activeMusic, loops, 250) < 0) {
        Mix_PlayMusic(m_activeMusic, loops);
    }
    m_playing.store(true);
    m_paused.store(false);
    m_trackFinished.store(false);
}

void AudioManager::play() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_tracks.empty()) return;
    if (m_current < 0 || m_current >= static_cast<int>(m_tracks.size())) {
        if (m_shuffle && !m_shuffleOrder.empty()) {
            m_shufflePos = 0;
            m_current = m_shuffleOrder[0];
        } else {
            m_current = 0;
        }
    }
    playCurrentTrackLocked();
}

void AudioManager::playTrack(int index) {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    m_current = index;
    if (m_shuffle) {
        for (size_t i = 0; i < m_shuffleOrder.size(); ++i) {
            if (m_shuffleOrder[i] == m_current) {
                m_shufflePos = static_cast<int>(i);
                break;
            }
        }
    }
    playCurrentTrackLocked();
}

void AudioManager::pause() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_playing.load() && !m_paused.load()) {
        Mix_PauseMusic();
        m_paused.store(true);
    }
}

void AudioManager::resume() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_paused.load()) {
        Mix_ResumeMusic();
        m_paused.store(false);
        m_playing.store(true);
    } else if (!m_playing.load()) {
        if (!m_tracks.empty()) {
            if (m_current < 0 || m_current >= static_cast<int>(m_tracks.size())) {
                m_current = 0;
            }
            playCurrentTrackLocked();
        }
    }
}

void AudioManager::togglePlayPause() {
    if (isPaused()) {
        resume();
    } else if (isPlaying()) {
        pause();
    } else {
        play();
    }
}

void AudioManager::stop() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    Mix_HookMusicFinished(nullptr);
    Mix_HaltMusic();
    m_playing.store(false);
    m_paused.store(false);
    m_trackFinished.store(false);
}

void AudioManager::stopAll() {
    stop();
    Mix_HaltChannel(-1);
}

void AudioManager::nextTrack() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_tracks.empty()) return;
    if (m_tracks.size() == 1) {
        m_current = 0;
        playCurrentTrackLocked();
        return;
    }
    if (m_shuffle) {
        m_shufflePos++;
        if (m_shufflePos >= static_cast<int>(m_shuffleOrder.size())) {
            int last = m_current;
            rebuildShuffleOrderLocked();
            if (m_shuffleOrder.size() > 1 && m_shuffleOrder[0] == last) {
                std::swap(m_shuffleOrder[0], m_shuffleOrder[1]);
            }
            m_shufflePos = 0;
        }
        m_current = m_shuffleOrder[m_shufflePos];
    } else {
        m_current = (m_current + 1) % static_cast<int>(m_tracks.size());
    }
    playCurrentTrackLocked();
}

void AudioManager::previousTrack() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_tracks.empty()) return;
    if (m_tracks.size() == 1) {
        m_current = 0;
        playCurrentTrackLocked();
        return;
    }
    if (m_shuffle) {
        m_shufflePos--;
        if (m_shufflePos < 0) {
            m_shufflePos = static_cast<int>(m_shuffleOrder.size()) - 1;
        }
        m_current = m_shuffleOrder[m_shufflePos];
    } else {
        m_current = (m_current - 1 + static_cast<int>(m_tracks.size())) % static_cast<int>(m_tracks.size());
    }
    playCurrentTrackLocked();
}

void AudioManager::setShuffle(bool shuffle) {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_shuffle != shuffle) {
        m_shuffle = shuffle;
        if (m_shuffle) {
            rebuildShuffleOrderLocked();
            if (m_current >= 0) {
                for (size_t i = 0; i < m_shuffleOrder.size(); ++i) {
                    if (m_shuffleOrder[i] == m_current) {
                        m_shufflePos = static_cast<int>(i);
                        break;
                    }
                }
            }
        }
    }
}

bool AudioManager::isShuffle() const {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    return m_shuffle;
}

int AudioManager::currentTrackIndex() const {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    return m_current;
}

size_t AudioManager::trackCount() const {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    return m_tracks.size();
}

std::string AudioManager::currentTrackTitle() const {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_current >= 0 && m_current < static_cast<int>(m_tracks.size())) {
        return m_tracks[m_current].title;
    }
    return {};
}

std::string AudioManager::currentTrackPath() const {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_current >= 0 && m_current < static_cast<int>(m_tracks.size())) {
        return m_tracks[m_current].path;
    }
    return {};
}

std::vector<TrackInfo> AudioManager::tracks() const {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    return m_tracks;
}

void AudioManager::update() {
    if (m_trackFinished.exchange(false)) {
        if (m_playing.load()) {
            nextTrack();
        }
    }
}

void AudioManager::applyMusicVolume() {
    Mix_VolumeMusic((int)(m_volume * m_musicFade * MIX_MAX_VOLUME));
}

void AudioManager::setVolume(float vol) {
    m_volume = vol;
    applyMusicVolume();
}

void AudioManager::setMusicFade(float fade) {
    m_musicFade = std::clamp(fade, 0.f, 1.f);
    applyMusicVolume();
}

// Registers the path only. The wav decodes on the first play (issue #2
// item 8): a preset ships dozens of effects and a session touches a few.
void AudioManager::loadSfx(Sfx id, const std::string& path) {
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    auto it = m_sfx.find(static_cast<int>(id));
    if (it != m_sfx.end()) {
        Mix_FreeChunk(it->second);
        m_sfx.erase(it);
    }
    m_sfxPaths[static_cast<int>(id)] = path;
}

void AudioManager::clearSfx() {
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    for (auto& [id, chunk] : m_sfx) Mix_FreeChunk(chunk);
    m_sfx.clear();
    m_sfxPaths.clear();
    for (auto& [id, chunk] : m_namedSfx) Mix_FreeChunk(chunk);
    m_namedSfx.clear();
    m_namedSfxPaths.clear();
    m_namedSfxVolumeScales.clear();
}

void AudioManager::playSfx(Sfx id) {
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    auto it = m_sfx.find(static_cast<int>(id));
    if (it == m_sfx.end()) {
        auto path = m_sfxPaths.find(static_cast<int>(id));
        if (path == m_sfxPaths.end())
            return;
        Mix_Chunk* chunk = Mix_LoadWAV(path->second.c_str());
        if (!chunk) {
            std::fprintf(stderr, "[Audio] Failed to load SFX %s: %s\n",
                         path->second.c_str(), Mix_GetError());
            m_sfxPaths.erase(path);   // do not retry every press
            return;
        }
        Mix_VolumeChunk(chunk, (int)(m_sfxVolume * MIX_MAX_VOLUME));
        it = m_sfx.emplace(static_cast<int>(id), chunk).first;
    }
    if (it->second)
        Mix_PlayChannel(-1, it->second, 0);
}

void AudioManager::loadNamedSfx(const std::string& id, const std::string& path, float volumeScale) {
    const float scale = std::clamp(volumeScale, 0.f, 2.f);
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    auto it = m_namedSfx.find(id);
    if (it != m_namedSfx.end()) {
        Mix_FreeChunk(it->second);
        m_namedSfx.erase(it);
    }
    m_namedSfxPaths[id] = path;
    m_namedSfxVolumeScales[id] = scale;
}

void AudioManager::playNamedSfx(const std::string& id) {
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    auto it = m_namedSfx.find(id);
    if (it == m_namedSfx.end()) {
        auto path = m_namedSfxPaths.find(id);
        if (path == m_namedSfxPaths.end())
            return;
        Mix_Chunk* chunk = Mix_LoadWAV(path->second.c_str());
        if (!chunk) {
            std::fprintf(stderr, "[Audio] Failed to load named SFX %s (%s): %s\n",
                         id.c_str(), path->second.c_str(), Mix_GetError());
            m_namedSfxPaths.erase(path);
            return;
        }
        float scale = 1.f;
        auto sc = m_namedSfxVolumeScales.find(id);
        if (sc != m_namedSfxVolumeScales.end()) scale = sc->second;
        Mix_VolumeChunk(chunk, (int)(m_sfxVolume * scale * MIX_MAX_VOLUME));
        it = m_namedSfx.emplace(id, chunk).first;
    }
    if (it->second)
        Mix_PlayChannel(-1, it->second, 0);
}

void AudioManager::setSfxVolume(float vol) {
    m_sfxVolume = vol;
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    for (auto& [id, chunk] : m_sfx) {
        if (chunk) Mix_VolumeChunk(chunk, (int)(m_sfxVolume * MIX_MAX_VOLUME));
    }
    for (auto& [id, chunk] : m_namedSfx) {
        if (chunk) {
            float scale = 1.f;
            auto it = m_namedSfxVolumeScales.find(id);
            if (it != m_namedSfxVolumeScales.end()) scale = it->second;
            Mix_VolumeChunk(chunk, (int)(m_sfxVolume * scale * MIX_MAX_VOLUME));
        }
    }
}
