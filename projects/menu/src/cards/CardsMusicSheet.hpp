#pragma once
// The music player of both frontends (HomeNX E10, C-UI-7). Cards opens it from
// TopButton::Music; Classic hosts the same sheet in MusicPlayerOverlay, so the
// two never show different players or a different song:
// - Turning vinyl record with bass glow
// - Song title, list and position
// - Controls: Shuffle, Previous, Play/Pause, Next, Repeat
// - Volume slider
// - Two lists: the HOME playlist (downloaded songs and the theme's music) and
//   the music of every installed theme
// - Live 48-band spectrum visualizer (HomeNX onMixed FFT)
// Decode remains off the UI thread via AudioManager / SDL_mixer.

#include "cards/CardsChrome.hpp"
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Types.hpp>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "core/AudioManager.hpp"
struct AppConfig;
namespace home { class HomeAudio; }
namespace nxui { class ThreadPool; }

namespace cards {

class CardsMusicSheet {
public:
    // `pool` scans the installed themes' music when the second list is opened.
    void open(home::HomeAudio& service, AppConfig& config, nxui::ThreadPool* pool);
    void close();
    bool isOpen() const { return m_open; }

    bool handleInput(const nxui::Input& in, home::HomeAudio& service, AppConfig& config);
    void update(float dt, home::HomeAudio& service);
    void render(nxui::Renderer& ren, CardsChrome& chrome, const home::HomeAudio& service) const;

    void onClosed(std::function<void()> cb) { m_onClosed = std::move(cb); }
    // A downloaded song was deleted: the owner rebuilds the HOME playlist.
    void onDeleted(std::function<void()> cb) { m_onDeleted = std::move(cb); }
    // Sound for moves and presses, when the owner wants one.
    void onSfx(std::function<void(Sfx)> cb) { m_onSfx = std::move(cb); }
    // Y changed the playlist order (config.audioSourcePreference): the owner
    // saves the config and rebuilds the HOME playlist.
    void onSourceModeChanged(std::function<void()> cb) { m_onSourceMode = std::move(cb); }

private:
    enum class List { Home, Themes };
    void controlAction(int col, home::HomeAudio& service, AppConfig& config);
    bool handleTouch(const nxui::Input& in, home::HomeAudio& service, AppConfig& config);
    void setList(List list);
    void playRow(int row, home::HomeAudio& service, AppConfig& config);
    void deleteRow(int row, home::HomeAudio& service);
    void pollThemeScan();
    void cycleSourceMode(AppConfig& config);
    std::vector<TrackInfo> listTracks(const home::HomeAudio& service) const;
    int playingRow(const home::HomeAudio& service) const;
    void sfx(Sfx s) const { if (m_onSfx) m_onSfx(s); }

    bool m_open = false;
    float m_in = 0.f;
    float m_spin = 0.f;
    float m_bass = 0.f;
    float m_viz[AudioManager::kBands]{}; // eased bars
    int m_zone = 0;   // 0 controls, 1 volume, 2 list
    int m_col = 2;    // default Play/Pause
    int m_row = 0;    // list row
    float m_listScroll = 0.f; // pixels, eased toward the chosen row
    float m_time = 0.f;
    int m_rowKey = -1;        // chosen row (list + index), for its title scroll
    float m_rowSince = 0.f;   // m_time when that row was chosen
    mutable std::string m_titleShown; // title of the playing song, for its scroll
    mutable float m_titleSince = 0.f;
    int m_repeatDir = 0;      // -1/+1 horizontal, -2/+2 vertical, 0 none
    float m_repeatTimer = 0.f;
    bool m_volDrag = false;
    bool m_confirmDelete = false; // X pressed once on a downloaded song
    float m_toastTime = 0.f;
    std::string m_toast;
    List m_list = List::Home;
    std::vector<TrackInfo> m_themeTracks;
    std::shared_ptr<std::vector<TrackInfo>> m_themeScanResult;
    std::future<void> m_themeScan;
    nxui::ThreadPool* m_pool = nullptr;
    mutable bool m_backdropCacheValid = false;
    std::function<void()> m_onClosed;
    std::function<void()> m_onDeleted;
    std::function<void(Sfx)> m_onSfx;
    std::function<void()> m_onSourceMode;
};

} // namespace cards
