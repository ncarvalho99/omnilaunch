#pragma once
// Standalone music player sheet for Cards HOME (HomeNX E10, C-UI-7).
// Opens from TopButton::Music:
// - Turning vinyl record with bass glow
// - Song title, album/track info, seek / play status
// - Controls: Shuffle, Previous, Play/Pause, Next, Repeat
// - Volume slider
// - Full playlist scroll
// - Live 48-band spectrum visualizer (HomeNX onMixed FFT)
// Decode remains off the UI thread via AudioManager / SDL_mixer.

#include "cards/CardsChrome.hpp"
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Types.hpp>
#include <functional>
#include <string>
#include <vector>

#include "core/AudioManager.hpp"
struct AppConfig;

namespace cards {

class CardsMusicSheet {
public:
    void open(AudioManager& audio, AppConfig& config);
    void close();
    bool isOpen() const { return m_open; }

    bool handleInput(nxui::Input& in, AudioManager& audio, AppConfig& config);
    void update(float dt, const AudioManager& audio);
    void render(nxui::Renderer& ren, CardsChrome& chrome, const AudioManager& audio) const;

    void onClosed(std::function<void()> cb) { m_onClosed = std::move(cb); }

private:
    void controlAction(int col, AudioManager& audio, AppConfig& config);
    bool handleTouch(nxui::Input& in, AudioManager& audio, AppConfig& config);

    bool m_open = false;
    float m_in = 0.f;
    float m_spin = 0.f;
    float m_bass = 0.f;
    float m_viz[AudioManager::kBands]{}; // eased bars
    int m_zone = 0;   // 0 controls, 1 volume, 2 playlist
    int m_col = 2;    // default Play/Pause
    int m_row = 0;    // playlist row
    float m_listScroll = 0.f; // pixels, eased toward the chosen row
    float m_time = 0.f;
    int m_repeatDir = 0;      // -1/+1 horizontal, -2/+2 vertical, 0 none
    float m_repeatTimer = 0.f;
    bool m_volDrag = false;
    mutable bool m_backdropCacheValid = false;
    std::function<void()> m_onClosed;
};

} // namespace cards
