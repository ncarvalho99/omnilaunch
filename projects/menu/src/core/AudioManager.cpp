#include "AudioManager.hpp"
#include "core/DebugLog.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <random>

namespace {
constexpr int kFft = 1024, kRate = 44100, kSlices = 4;

void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.f * 3.14159265f / static_cast<float>(len);
        const std::complex<float> step(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}
// An MP3's length from its header: the Xing/Info frame count when present,
// otherwise the audio size at the first frame's bit rate. 0 when unknown
// (not an MP3, or no frame in the first bytes). Adapted from HomeNX
// projects/cards/src/Audio.cpp mp3Seconds @ 0448cd4 (GPL-3.0, ad3333m).
double mp3Seconds(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 0.0;
    std::fseek(f, 0, SEEK_END);
    const long fileSize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> b(16 * 1024);
    b.resize(std::fread(b.data(), 1, b.size(), f));
    std::size_t at = 0;
    if (b.size() > 10 && b[0] == 'I' && b[1] == 'D' && b[2] == '3')
        at = 10 + ((static_cast<std::size_t>(b[6] & 0x7F) << 21) | ((b[7] & 0x7F) << 14) | ((b[8] & 0x7F) << 7) | (b[9] & 0x7F));
    const long audioStart = static_cast<long>(at);
    if (at >= b.size()) { // a large tag (cover art): read on from after it
        std::fseek(f, static_cast<long>(at), SEEK_SET);
        b.assign(4096, 0);
        b.resize(std::fread(b.data(), 1, b.size(), f));
        at = 0;
    }
    std::fclose(f);
    for (; at + 4 < b.size(); ++at)
        if (b[at] == 0xFF && (b[at + 1] & 0xE0) == 0xE0) break;
    if (at + 4 >= b.size()) return 0.0;
    const int version = (b[at + 1] >> 3) & 3; // 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5
    const int layer = (b[at + 1] >> 1) & 3;   // 1 = Layer III
    const int rateIndex = (b[at + 2] >> 4) & 15, freqIndex = (b[at + 2] >> 2) & 3;
    const bool mono = ((b[at + 3] >> 6) & 3) == 3;
    if (layer != 1 || version == 1 || rateIndex == 0 || rateIndex == 15 || freqIndex == 3) return 0.0;
    static const int kRates1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const int kRates2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static const int kFreqs[] = {44100, 48000, 32000};
    const bool mpeg1 = version == 3;
    const int kbps = (mpeg1 ? kRates1 : kRates2)[rateIndex];
    const int freq = kFreqs[freqIndex] >> (mpeg1 ? 0 : version == 2 ? 1 : 2);
    const int samples = mpeg1 ? 1152 : 576;
    const std::size_t side = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
    const std::size_t x = at + 4 + side;
    if (x + 12 <= b.size()
        && (std::equal(b.begin() + x, b.begin() + x + 4, "Xing") || std::equal(b.begin() + x, b.begin() + x + 4, "Info"))
        && (b[x + 7] & 1)) {
        const unsigned frames = (static_cast<unsigned>(b[x + 8]) << 24) | (b[x + 9] << 16) | (b[x + 10] << 8) | b[x + 11];
        return static_cast<double>(frames) * samples / freq;
    }
    if (fileSize <= audioStart) return 0.0;
    return static_cast<double>(fileSize - audioStart) * 8.0 / (kbps * 1000.0);
}
} // namespace

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize() {
    if (m_initialized) return true; // Shared across frontend Activities.

    // main() already initializes audio. Do not acquire a second subsystem ref
    // when reusing the process mixer across Activities.
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) &&
        SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        std::fprintf(stderr, "[Audio] SDL_Init(AUDIO) failed: %s\n", SDL_GetError());
        return false;
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        std::fprintf(stderr, "[Audio] Mix_OpenAudio failed: %s\n", Mix_GetError());
        return false;
    }
    Mix_AllocateChannels(8);
    Mix_SetPostMix(postMixCallback, this);
    m_initialized = true;
    setVolume(m_volume);
    return true;
}

void AudioManager::shutdown() {
    if (!m_initialized) return;

    Mix_SetPostMix(nullptr, nullptr);
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
    m_activeMusicPath.clear();
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

void AudioManager::postMixCallback(void* udata, Uint8* stream, int len) {
    auto* self = static_cast<AudioManager*>(udata);
    if (!self || len <= 0)
        return;

    static std::vector<std::complex<float>> buf(kFft);
    static std::vector<float> window;
    static std::array<int, kBands + 1> edges{};
    if (window.empty()) {
        window.resize(kFft);
        for (int i = 0; i < kFft; ++i)
            window[i] = 0.5f - 0.5f * std::cos(2.f * 3.14159265f * i / (kFft - 1));
        for (int b = 0; b <= kBands; ++b) {
            const float hz = 50.f * std::pow(16000.f / 50.f, static_cast<float>(b) / kBands);
            edges[b] = std::max(b == 0 ? 1 : edges[b - 1] + 1, static_cast<int>(hz * kFft / kRate));
        }
    }
    const auto* s = reinterpret_cast<const Sint16*>(stream);
    const int frames = len / 4; // 16-bit stereo: 4 bytes per frame
    std::array<std::array<float, kBands>, kSlices> out{};
    int n = 0;
    for (int start = 0; start + kFft <= frames && n < kSlices; start += kFft, ++n) {
        for (int i = 0; i < kFft; ++i)
            buf[i] = {(s[(start + i) * 2] + s[(start + i) * 2 + 1]) * (0.5f / 32768.f) * window[i], 0.f};
        fft(buf);
        for (int b = 0; b < kBands; ++b) {
            float peak = 0.f;
            for (int i = edges[b]; i < std::min(edges[b + 1], kFft / 2); ++i)
                peak = std::max(peak, std::abs(buf[i]));
            const float db = 20.f * std::log10(peak / (kFft / 4.f) + 1e-6f);
            out[n][b] = std::clamp((db + 50.f) / 50.f, 0.f, 1.f);
        }
    }
    if (n > 0) {
        std::lock_guard<std::mutex> lk(self->m_vizMutex);
        self->m_viz = out;
        self->m_vizCount = n;
        self->m_vizTime = std::chrono::steady_clock::now();
    }
}

void AudioManager::spectrum(float* out) const {
    if (!out)
        return;
    std::lock_guard<std::mutex> lk(m_vizMutex);
    if (!m_initialized || m_vizCount == 0) {
        std::fill(out, out + kBands, 0.f);
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const float ms = std::chrono::duration<float, std::milli>(now - m_vizTime).count();
    const int i = std::clamp(static_cast<int>(ms / (1000.f * kFft / kRate)), 0, m_vizCount - 1);
    std::copy(m_viz[static_cast<std::size_t>(i)].begin(), m_viz[static_cast<std::size_t>(i)].end(), out);
}

float AudioManager::bassLevel() const {
    float spec[kBands]{};
    spectrum(spec);
    float bass = 0.f;
    for (int i = 0; i < 8; ++i) bass += spec[i];
    return bass / 8.f;
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
    m_activeMusicPath.clear();
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
    const auto& track = m_tracks[m_current];
    // Replaying the track that is already loaded (repeat, or a one-track list
    // restarted) reuses it. Loading an MP3 reads and indexes the whole file on
    // this thread: a 200 ms frame at every loop of a repeated theme track.
    const bool reuse = m_activeMusic && m_activeMusicPath == track.path;
    if (m_activeMusic && !reuse) {
        Mix_FreeMusic(m_activeMusic);
        m_activeMusic = nullptr;
        m_activeMusicPath.clear();
    }

    if (!reuse) {
        m_activeMusic = Mix_LoadMUS(track.path.c_str());
        if (m_activeMusic)
            m_activeMusicPath = track.path;
    }
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
    // A reused stream may still sit where it ended: start it from the top.
    if (reuse)
        Mix_RewindMusic();
    m_playing.store(true);
    m_paused.store(false);
    m_trackFinished.store(false);
    m_trackStart = std::chrono::steady_clock::now();
    m_pausedTotal = 0.0;
    // One small header read per file, then remembered: track changes do not
    // touch the card again for a song already measured.
    auto known = m_durationCache.find(track.path);
    if (known == m_durationCache.end())
        known = m_durationCache.emplace(track.path, mp3Seconds(track.path)).first;
    m_trackDuration = known->second;
}

void AudioManager::play() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_tracks.empty()) return;
    // Unlock and settings paths call play() defensively; restarting a track
    // that is already playing made an audible skip (PoloNX #133).
    if (m_playing.load() && Mix_PlayingMusic())
        return;
    m_quickEnds = 0; // the player asked for music again
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
    m_quickEnds = 0;
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
        m_pausedAt = std::chrono::steady_clock::now();
        m_paused.store(true);
    }
}

void AudioManager::resume() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_paused.load()) {
        Mix_ResumeMusic();
        m_pausedTotal += std::chrono::duration<double>(std::chrono::steady_clock::now() - m_pausedAt).count();
        m_paused.store(false);
        m_playing.store(true);
    } else if (!m_playing.load()) {
        if (!m_tracks.empty()) {
            m_quickEnds = 0;
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

void AudioManager::setRepeat(bool repeat) {
    m_repeat = repeat;
}

bool AudioManager::isRepeat() const {
    return m_repeat;
}

// The bundled SDL_mixer has no position/duration query: elapsed time is
// counted from the track start, minus paused time. Duration is unknown (0).
double AudioManager::trackPositionSeconds() const {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (!m_activeMusic || !m_playing.load()) return 0.0;
    const auto now = m_paused.load() ? m_pausedAt : std::chrono::steady_clock::now();
    const double s = std::chrono::duration<double>(now - m_trackStart).count() - m_pausedTotal;
    return s > 0.0 ? s : 0.0;
}

double AudioManager::trackDurationSeconds() const {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    return m_activeMusic ? m_trackDuration : 0.0;
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
            // A track that ends at once (undecodable or truncated file) made
            // the menu load the next one, on this thread, several times a
            // second. After a few in a row, stop instead of cycling the list.
            constexpr double kQuickEndSeconds = 2.0;
            constexpr int kQuickEndLimit = 3;
            bool isRepeat = false;
            {
                std::lock_guard<std::mutex> lk(m_trackMutex);
                isRepeat = m_repeat;
                const double played =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - m_trackStart).count() -
                    m_pausedTotal;
                // A track that was measured to be legitimately short (< 2 s)
                // and played to completion is not a decoder failure.
                const bool validShortTrack =
                    (m_trackDuration > 0.0 && m_trackDuration <= kQuickEndSeconds && played >= m_trackDuration * 0.8);
                // Also, if the track is in repeat mode and played its duration,
                // normal loop completion is not a failure.
                if (played < kQuickEndSeconds && !validShortTrack && !m_repeat) {
                    const std::string path = (m_current >= 0 && m_current < static_cast<int>(m_tracks.size()))
                        ? m_tracks[m_current].path : std::string();
                    DebugLog::log("[audio] track ended at once: %s (%s)", path.c_str(), Mix_GetError());
                    if (++m_quickEnds >= kQuickEndLimit) {
                        DebugLog::log("[audio] %d tracks in a row ended at once; music stopped", m_quickEnds);
                        Mix_HookMusicFinished(nullptr);
                        Mix_HaltMusic();
                        m_playing.store(false);
                        m_paused.store(false);
                        m_quickEnds = 0;
                        return;
                    }
                } else if (!m_repeat) {
                    m_quickEnds = 0;
                }
            }
            if (isRepeat) {
                std::lock_guard<std::mutex> lk(m_trackMutex);
                playCurrentTrackLocked();
            } else {
                nextTrack();
            }
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
