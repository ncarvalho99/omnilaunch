#include "cards/CardsMusicSheet.hpp"
#include "core/AudioManager.hpp"
#include "core/ButtonGlyph.hpp"
#include "core/Config.hpp"
#include "home/HomeAudio.hpp"
#include "home/ThemeMusicLibrary.hpp"
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cards {

namespace {
// Layout of the CardsNX Music screen (HomeNX projects/cards/src/CardsActivity.cpp
// drawMusic/updateMusic @ 0448cd4, GPL-3.0, ad3333m), adapted to AudioManager.
const nxui::Rect kCard{110.f, 46.f, 1060.f, 628.f};
constexpr float kInfoX = 470.f, kInfoW = 340.f, kListX = 852.f, kListW = 290.f;
constexpr float kRowH = 46.f, kListTop = 150.f, kListBottom = 452.f;
constexpr float kTabY = 104.f, kTabH = 30.f;

#ifdef SWITCHU_HOMEBREW
constexpr const char* kAssets = "romfs:";
#else
constexpr const char* kAssets = "sdmc:/switch/OmniLaunch";
#endif

std::string fmtTime(double sec) {
    if (sec < 0.0) sec = 0.0;
    int s = static_cast<int>(sec);
    int m = s / 60;
    s %= 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

std::string replaceAll(std::string s, const std::string& key, const std::string& value) {
    for (std::size_t at = s.find(key); at != std::string::npos; at = s.find(key, at + value.size()))
        s.replace(at, key.size(), value);
    return s;
}

nxui::Vec2 buttonPos(int col, float dy) { // shuffle, previous, play / pause, next, repeat
    return {kInfoX + 34.f + 68.f * static_cast<float>(col), 330.f + dy};
}
nxui::Rect volumeBar(float dy) {
    return {kInfoX + 44.f, 402.f + dy, kInfoW - 110.f, 8.f};
}
nxui::Rect tabRect(int i, float dy) {
    const float w = (kListW - 8.f) * 0.5f;
    return {kListX + static_cast<float>(i) * (w + 8.f), kTabY + dy, w, kTabH};
}
float easeOut(float t) { t = std::clamp(t, 0.f, 1.f); return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }

// A title wider than its space scrolls left in a loop (pause, scroll, pause)
// instead of being cut with "...", clipped to [x, x + w]. `t` is the time the
// text has been shown, so each title starts at its beginning.
void drawMarquee(nxui::Renderer& ren, nxui::Font& f, const std::string& s, float x, float cy, float w,
                 const nxui::Color& c, float t) {
    if (s.empty())
        return;
    const nxui::Vec2 m = f.measure(s);
    const float y = cy - m.y * 0.5f;
    if (m.x <= w) {
        ren.drawText(s, {x, y}, &f, c);
        return;
    }
    constexpr float kPause = 1.6f, kSpeed = 42.f, kGap = 56.f;
    const float span = m.x + kGap;
    const float period = kPause + span / kSpeed;
    const float phase = std::fmod(std::max(0.f, t), period);
    const float off = phase < kPause ? 0.f : (phase - kPause) * kSpeed;
    ren.pushClipRect({x, y - 4.f, w, m.y + 8.f});
    ren.drawText(s, {x - off, y}, &f, c);
    if (off > 0.f)
        ren.drawText(s, {x - off + span, y}, &f, c);
    ren.popClipRect();
}

const char* const kSourceModes[] = {"custom_first", "theme_first", "custom_only", "theme_only"};
std::string sourceModeLabel(const std::string& mode) {
    auto& i18n = nxui::I18n::instance();
    if (mode == "custom_only") return i18n.tr("media.mode.custom_only", "Custom SD Only");
    if (mode == "theme_first") return i18n.tr("media.mode.theme_first", "Theme Music First");
    if (mode == "theme_only") return i18n.tr("media.mode.theme_only", "Theme & Preset Only");
    return i18n.tr("media.mode.custom_first", "Custom SD First");
}
} // namespace

void CardsMusicSheet::open(home::HomeAudio& service, AppConfig& config, nxui::ThreadPool* pool) {
    m_open = true;
    m_in = 0.f;
    m_backdropCacheValid = false;
    m_zone = 0;
    m_col = 2;
    m_pool = pool;
    m_confirmDelete = false;
    m_toast.clear();
    // Open on the list that is playing.
    m_list = List::Home;
    if (service.queue == home::HomeAudio::Queue::Themes) {
        if (m_themeTracks.empty())
            m_themeTracks = service.audio.tracks(); // until the full scan lands
        setList(List::Themes);
    }
    m_row = std::max(0, playingRow(service));
    m_listScroll = -1.f; // snap to the playing song on the first frame
    (void)config;
}

void CardsMusicSheet::close() {
    m_open = false;
    m_confirmDelete = false;
    if (m_onClosed)
        m_onClosed();
}

std::vector<TrackInfo> CardsMusicSheet::listTracks(const home::HomeAudio& service) const {
    if (m_list == List::Themes)
        return m_themeTracks;
    // The HOME playlist as the player knows it: the queue itself while it
    // plays, otherwise the list it was last built as.
    if (service.queue == home::HomeAudio::Queue::Home)
        return service.audio.tracks();
    return service.homeTracks;
}

int CardsMusicSheet::playingRow(const home::HomeAudio& service) const {
    const bool themesPlaying = service.queue == home::HomeAudio::Queue::Themes;
    if ((m_list == List::Themes) != themesPlaying)
        return -1;
    if (m_list == List::Home)
        return service.audio.currentTrackIndex();
    const std::string path = service.audio.currentTrackPath();
    for (std::size_t i = 0; i < m_themeTracks.size(); ++i)
        if (m_themeTracks[i].path == path)
            return static_cast<int>(i);
    return -1;
}

void CardsMusicSheet::pollThemeScan() {
    if (!m_themeScan.valid() || m_themeScan.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try {
        m_themeScan.get();
    } catch (...) {
    }
    if (m_themeScanResult)
        m_themeTracks = std::move(*m_themeScanResult);
    m_themeScanResult.reset();
}

void CardsMusicSheet::setList(List list) {
    if (m_list == list)
        return;
    m_list = list;
    m_row = 0;
    m_listScroll = -1.f;
    m_confirmDelete = false;
    if (list == List::Themes && !m_themeScan.valid() && m_pool) {
        // Read every theme's manifest on a worker; the list fills when done.
        auto result = std::make_shared<std::vector<TrackInfo>>();
        m_themeScanResult = result;
        try {
            m_themeScan = m_pool->submit([result]() { *result = home::scanThemeMusicLibrary(kAssets); });
        } catch (...) {
            m_themeScanResult.reset();
        }
    }
}

void CardsMusicSheet::playRow(int row, home::HomeAudio& service, AppConfig& config) {
    config.musicEnabled = true;
    if (m_list == List::Themes) {
        service.playThemeTrack(m_themeTracks, row);
    } else {
        service.playHomeTrack(row, config);
    }
}

void CardsMusicSheet::deleteRow(int row, home::HomeAudio& service) {
    const auto tracks = listTracks(service);
    if (m_list != List::Home || row < 0 || row >= static_cast<int>(tracks.size()))
        return;
    auto& i18n = nxui::I18n::instance();
    const std::string path = tracks[static_cast<std::size_t>(row)].path;
    if (!home::HomeAudio::isCustomTrack(path)) {
        m_toast = i18n.tr("media.delete_theme_song", "Theme songs are removed with their theme.");
        m_toastTime = 2.5f;
        return;
    }
    if (!m_confirmDelete) {
        m_confirmDelete = true;
        m_toast = i18n.tr("media.delete_confirm", "Press X again to delete this song.");
        m_toastTime = 3.f;
        return;
    }
    m_confirmDelete = false;
    if (service.audio.currentTrackPath() == path)
        service.audio.stop(); // the file is about to go
    if (home::HomeAudio::deleteCustomTrack(path)) {
        m_toast = i18n.tr("media.deleted", "Song deleted.");
        sfx(Sfx::ToggleOff);
    } else {
        m_toast = i18n.tr("media.delete_failed", "The song could not be deleted.");
    }
    m_toastTime = 2.f;
    if (m_onDeleted)
        m_onDeleted();
    const int songs = static_cast<int>(listTracks(service).size());
    m_row = std::clamp(m_row, 0, std::max(0, songs - 1));
}

void CardsMusicSheet::cycleSourceMode(AppConfig& config) {
    int idx = 0;
    for (int i = 0; i < 4; ++i)
        if (config.audioSourcePreference == kSourceModes[i]) idx = i;
    config.audioSourcePreference = kSourceModes[(idx + 1) % 4];
    m_toast = nxui::I18n::instance().tr("media.order", "Order") + ": " + sourceModeLabel(config.audioSourcePreference);
    m_toastTime = 2.5f;
    if (m_onSourceMode)
        m_onSourceMode();
}

void CardsMusicSheet::controlAction(int col, home::HomeAudio& service, AppConfig& config) {
    AudioManager& audio = service.audio;
    switch (col) {
    case 0:
        audio.setShuffle(!audio.isShuffle());
        config.customBgmShuffle = audio.isShuffle(); // shared with Classic, saved with the config
        break;
    case 1:
        audio.previousTrack();
        break;
    case 2:
        if (!config.musicEnabled) {
            config.musicEnabled = true;
            audio.play();
        } else {
            audio.togglePlayPause();
        }
        break;
    case 3:
        audio.nextTrack();
        break;
    case 4:
        audio.setRepeat(!audio.isRepeat());
        config.bgmRepeat = audio.isRepeat(); // saved with the config, like shuffle
        break;
    default:
        break;
    }
    const int now = playingRow(service);
    if (now >= 0)
        m_row = now;
}

bool CardsMusicSheet::handleInput(const nxui::Input& in, home::HomeAudio& service, AppConfig& config) {
    using B = nxui::Button;
    if (!m_open || m_in < 0.8f)
        return true;

    if (in.isDown(B::B) || in.isDown(B::Plus)) {
        close();
        return true;
    }
    if (handleTouch(in, service, config))
        return true;

    // L / R: the two lists, from anywhere in the sheet.
    if (in.isDown(B::L) || in.isDown(B::R)) {
        setList(in.isDown(B::L) ? List::Home : List::Themes);
        sfx(Sfx::PageChange);
        return true;
    }
    if (in.isDown(B::Y)) {
        cycleSourceMode(config);
        return true;
    }

    const auto tracks = listTracks(service);
    const int songs = static_cast<int>(tracks.size());
    const bool left = in.isDown(B::DLeft) || in.isDown(B::LStickL);
    const bool right = in.isDown(B::DRight) || in.isDown(B::LStickR);
    const bool up = in.isDown(B::DUp) || in.isDown(B::LStickU);
    const bool down = in.isDown(B::DDown) || in.isDown(B::LStickD);
    const bool heldLeft = in.isHeld(B::DLeft) || in.isHeld(B::LStickL);
    const bool heldRight = in.isHeld(B::DRight) || in.isHeld(B::LStickR);
    const bool heldUp = in.isHeld(B::DUp) || in.isHeld(B::LStickU);
    const bool heldDown = in.isHeld(B::DDown) || in.isHeld(B::LStickD);
    if (left || right || up || down)
        m_confirmDelete = false;

    // Held directions repeat (volume sweep, long playlists), as in CardsNX.
    constexpr float kDelay = 0.38f, kRate = 0.07f;
    const int dir = heldLeft == heldRight ? 0 : (heldLeft ? -1 : 1);
    const int vdir = heldUp == heldDown ? 0 : (heldUp ? -1 : 1);
    bool repeatH = false, repeatV = false;
    if (dir != 0 && dir == m_repeatDir && !left && !right) {
        m_repeatTimer -= 1.f / 60.f;
        if (m_repeatTimer <= 0.f) { m_repeatTimer = kRate; repeatH = true; }
    } else if (vdir != 0 && vdir * 2 == m_repeatDir && !up && !down) {
        m_repeatTimer -= 1.f / 60.f;
        if (m_repeatTimer <= 0.f) { m_repeatTimer = kRate; repeatV = true; }
    }
    if (left || right) { m_repeatDir = left ? -1 : 1; m_repeatTimer = kDelay; }
    else if (up || down) { m_repeatDir = up ? -2 : 2; m_repeatTimer = kDelay; }
    else if (dir == 0 && vdir == 0) m_repeatDir = 0;

    if (m_zone == 0) {
        if (left && m_col > 0) --m_col;
        else if (right && m_col < 4) ++m_col;
        else if (right) m_zone = 2;
        else if (down) m_zone = 1;
        if (in.isDown(B::A)) controlAction(m_col, service, config);
        if (in.isDown(B::X)) controlAction(2, service, config);
    } else if (m_zone == 1) {
        if (up) m_zone = 0;
        const int step = (left || (repeatH && dir < 0)) ? -1 : (right || (repeatH && dir > 0)) ? 1 : 0;
        if (step != 0) {
            const float v = std::clamp(config.musicVolume + 0.05f * static_cast<float>(step), 0.f, 1.f);
            config.musicVolume = v;
            service.audio.setVolume(v);
        }
        if (in.isDown(B::X)) controlAction(2, service, config);
    } else {
        const int step = (up || (repeatV && vdir < 0)) ? -1 : (down || (repeatV && vdir > 0)) ? 1 : 0;
        if (step < 0 && m_row > 0) --m_row;
        else if (step > 0 && m_row + 1 < songs) ++m_row;
        else if (left) m_zone = 0;
        if (in.isDown(B::A) && m_row >= 0 && m_row < songs)
            playRow(m_row, service, config);
        if (in.isDown(B::X))
            deleteRow(m_row, service);
    }
    return true;
}

// Touch, as CardsNX: drag along the volume, tap a button, a tab or a song,
// tap outside the card to close.
bool CardsMusicSheet::handleTouch(const nxui::Input& in, home::HomeAudio& service, AppConfig& config) {
    const float x = in.touchX(), y = in.touchY();
    const nxui::Rect vol = volumeBar(0.f);
    if (in.isTouching() && (m_volDrag || (x >= vol.x - 10.f && x <= vol.x + vol.width + 10.f && std::abs(y - vol.y - 4.f) < 22.f))) {
        m_volDrag = true;
        m_zone = 1;
        const float v = std::clamp((x - vol.x) / vol.width, 0.f, 1.f);
        config.musicVolume = v;
        service.audio.setVolume(v);
        return true;
    }
    if (!in.touchUp())
        return in.isTouching();
    if (m_volDrag) {
        m_volDrag = false;
        return true;
    }
    for (int i = 0; i < 5; ++i) {
        const nxui::Vec2 c = buttonPos(i, 0.f);
        if (std::hypot(x - c.x, y - c.y) < (i == 2 ? 36.f : 28.f)) {
            m_zone = 0;
            m_col = i;
            controlAction(i, service, config);
            return true;
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (tabRect(i, 0.f).contains(x, y)) {
            setList(i == 0 ? List::Home : List::Themes);
            return true;
        }
    }
    const int songs = static_cast<int>(listTracks(service).size());
    if (x >= kListX && x <= kListX + kListW && y >= kListTop && y <= kListBottom) {
        const int row = static_cast<int>((y - kListTop + m_listScroll) / kRowH);
        if (row >= 0 && row < songs) {
            m_zone = 2;
            m_row = row;
            playRow(row, service, config);
        }
        return true;
    }
    if (!kCard.contains(x, y)) {
        close();
        return true;
    }
    return true;
}

void CardsMusicSheet::update(float dt, home::HomeAudio& service) {
    if (!m_open)
        return;
    pollThemeScan();
    if (m_in < 1.f)
        m_in = std::min(1.f, m_in + dt / 0.22f);
    if (m_toastTime > 0.f) {
        m_toastTime -= dt;
        if (m_toastTime <= 0.f) {
            m_toast.clear();
            m_confirmDelete = false;
        }
    }

    const AudioManager& audio = service.audio;
    const bool playing = audio.isPlaying() && !audio.isPaused();
    float target[AudioManager::kBands]{};
    audio.spectrum(target);
    // Up quickly, down slowly, as the CardsNX bars.
    for (int i = 0; i < AudioManager::kBands; ++i) {
        const float t = playing ? target[i] : 0.f;
        float& v = m_viz[i];
        v = t > v ? v + (t - v) * 0.6f : std::max(t, v - dt * 1.4f);
    }
    float bass = 0.f;
    for (int i = 0; i < 8; ++i) bass += m_viz[i] / 8.f;
    m_bass += (bass - m_bass) * 0.25f;
    if (playing) {
        m_spin += dt * 1.1f;
        if (m_spin > 6.2831853f) m_spin -= 6.2831853f;
    }
    m_time += dt;

    const int songs = static_cast<int>(listTracks(service).size());
    if (m_row >= songs) m_row = std::max(0, songs - 1);
    // A newly chosen row scrolls its title from the beginning.
    const int rowKey = m_zone == 2 ? (m_list == List::Themes ? 100000 : 0) + m_row : -1;
    if (rowKey != m_rowKey) {
        m_rowKey = rowKey;
        m_rowSince = m_time;
    }
    const float span = kListBottom - kListTop;
    const float want = std::clamp(m_row * kRowH - span * 0.5f + kRowH * 0.5f, 0.f,
                                  std::max(0.f, songs * kRowH - span));
    if (m_listScroll < 0.f) m_listScroll = want;
    m_listScroll += (want - m_listScroll) * std::min(1.f, dt * 15.f);
}

void CardsMusicSheet::render(nxui::Renderer& ren, CardsChrome& chrome, const home::HomeAudio& service) const {
    if (!m_open)
        return;

    const AudioManager& audio = service.audio;
    const float a = easeOut(m_in);
    const float ring = 0.8f + 0.2f * std::sin(m_time * 5.f);
    const bool playing = audio.isPlaying() && !audio.isPaused();
    const nxui::Color white = CardsChrome::kWhite;
    const nxui::Color muted{1.f, 1.f, 1.f, 0.62f};

    // The card grows in slightly, as in CardsNX.
    const float grow = 0.95f + 0.05f * a;
    const nxui::Rect card{kCard.x + kCard.width * (1.f - grow) * 0.5f, kCard.y + kCard.height * (1.f - grow) * 0.5f,
                          kCard.width * grow, kCard.height * grow};
    const float dy = card.y - kCard.y;

    bool drawnGlass = false;
#ifdef SWITCHU_MENU
    if (ren.gpu().offscreenReady()) {
        if (!m_backdropCacheValid) {
            ren.captureToOffscreenSharp();
            ren.applyBlur(2.5f, 2);
            ren.copyOffscreen(nxui::GpuDevice::OFF_SHARP_A, nxui::GpuDevice::OFF_DIALOG);
            m_backdropCacheValid = true;
        }
        ren.drawRect({0.f, 0.f, 1280.f, 720.f}, {0.f, 0.f, 0.f, 0.5f * a});
        ren.drawLiquidGlass(nxui::GpuDevice::OFF_DIALOG, card, 34.f, CardsChrome::glassTint(1.f), a, 0.35f);
        drawnGlass = true;
    }
#endif
    if (!drawnGlass)
        ren.drawRect({0.f, 0.f, 1280.f, 720.f}, {0.f, 0.f, 0.f, 0.5f * a});
    ren.drawRoundedRect(card, {0.08f, 0.09f, 0.13f, (drawnGlass ? 0.72f : 0.94f) * a}, 34.f);
    ren.drawRoundedRectOutline(card, {1.f, 1.f, 1.f, 0.16f * a}, 34.f, 1.5f);
    ren.drawGradientRect({card.x + 2.f, card.y + 2.f, card.width - 4.f, 90.f}, {1.f, 1.f, 1.f, 0.05f * a},
                         {1.f, 1.f, 1.f, 0.f});

    auto& tf = chrome.titleFont();
    auto& pf = chrome.pillFont();
    auto& sf = chrome.smallFont();
    auto& i18n = nxui::I18n::instance();

    // Header.
    chrome.icon(ren, "music", kCard.x + 46.f, kCard.y + 46.f + dy, 28.f, white.withAlpha(a));
    chrome.textMid(ren, tf, i18n.tr("cards.music_title", "Music"), kCard.x + 72.f, kCard.y + 46.f + dy, white.withAlpha(a));
    {
        // Glyph capsules, as in the other Cards sheets (not plain "A ... B ..." text).
        std::vector<ui::glyph::ActionHint> hints = {
            {ui::glyph::button(nxui::Button::A), i18n.tr("hint.select", "Select")},
        };
        if (m_zone == 2 && m_list == List::Home)
            hints.push_back({ui::glyph::button(nxui::Button::X), i18n.tr("media.hint_delete", "Delete Track")});
        else
            hints.push_back({ui::glyph::button(nxui::Button::X), i18n.tr("media.hint_play_pause", "Play/Pause")});
        hints.push_back({ui::glyph::button(nxui::Button::Y), i18n.tr("media.order", "Order")});
        hints.push_back({ui::glyph::button(nxui::Button::B), i18n.tr("hint.back", "Back")});
        ui::glyph::drawHintBar(ren, chrome.iconFont(), sf, hints, kCard.x + kCard.width - 36.f,
                               kCard.y + 51.f + dy, a);
    }

    // The record: it turns while music plays; its glow beats with the bass.
    const nxui::Vec2 c{kCard.x + 190.f, 262.f + dy};
    const float R = 122.f;
    ren.drawCircle(c, R + 10.f + 26.f * m_bass, CardsChrome::accent(0.10f * a + 0.22f * m_bass * a), 64);
    ren.drawCircle(c, R + 4.f, {1.f, 1.f, 1.f, 0.10f * a}, 64);
    ren.drawCircle(c, R, {0.03f, 0.03f, 0.05f, 0.96f * a}, 64);
    for (float g = R - 8.f; g > 56.f; g -= 9.f)
        ren.drawRoundedRectOutline({c.x - g, c.y - g, g * 2.f, g * 2.f}, {1.f, 1.f, 1.f, 0.05f * a}, g, 1.f);
    for (int k = 0; k < 2; ++k) { // light on it, turning
        const float th = m_spin + 3.14159265f * static_cast<float>(k);
        const nxui::Vec2 p1{c.x + std::cos(th - 0.28f) * (R - 6.f), c.y + std::sin(th - 0.28f) * (R - 6.f)};
        const nxui::Vec2 p2{c.x + std::cos(th + 0.28f) * (R - 6.f), c.y + std::sin(th + 0.28f) * (R - 6.f)};
        ren.drawTriangle(c, p1, p2, {1.f, 1.f, 1.f, 0.07f * a});
    }
    ren.drawCircle(c, 50.f, CardsChrome::accent(a), 48); // its label
    ren.drawCircle(c, 50.f, {1.f, 1.f, 1.f, 0.12f * a}, 48);
    ren.drawCircle(c, 42.f, CardsChrome::accent(a), 48);
    const nxui::Vec2 dot{c.x + std::cos(m_spin) * 30.f, c.y + std::sin(m_spin) * 30.f};
    ren.drawCircle(dot, 4.f, {1.f, 1.f, 1.f, 0.6f * a}, 16);
    chrome.icon(ren, "music", c.x, c.y, 34.f, nxui::Color{0.f, 0.f, 0.f, 0.55f * a});
    ren.drawCircle(c, 4.f, {0.03f, 0.03f, 0.05f, a}, 16);

    // The song: the title of what plays, whichever list it came from.
    const auto queueTracks = audio.tracks();
    const int now = audio.currentTrackIndex();
    const std::string song = audio.currentTrackTitle().empty()
        ? i18n.tr("cards.music_stopped", "No music playing") : audio.currentTrackTitle();
    if (song != m_titleShown) { // a new song scrolls from its beginning
        m_titleShown = song;
        m_titleSince = m_time;
    }
    drawMarquee(ren, tf, song, kInfoX, 150.f + dy, kInfoW, white.withAlpha(a), m_time - m_titleSince);

    std::string sub = service.queue == home::HomeAudio::Queue::Themes
        ? i18n.tr("cards.music_theme_source", "Theme music")
        : i18n.tr("cards.music_source", "HOME music");
    if (now >= 0 && !queueTracks.empty()) {
        std::string of = i18n.tr("cards.music_song_of", "Song {n} of {total}");
        of = replaceAll(replaceAll(of, "{n}", std::to_string(now + 1)), "{total}", std::to_string(queueTracks.size()));
        sub += "    " + of;
    }
    chrome.textMid(ren, sf, chrome.fit(sf, sub, kInfoW), kInfoX, 194.f + dy, white.withAlpha(0.85f * a));

    // Progress.
    const double at = audio.trackPositionSeconds();
    const double length = audio.trackDurationSeconds();
    const float done = length > 0.0 ? std::clamp(static_cast<float>(at / length), 0.f, 1.f) : 0.f;
    const nxui::Rect bar{kInfoX, 252.f + dy, kInfoW, 6.f};
    ren.drawRoundedRect(bar, {1.f, 1.f, 1.f, 0.16f * a}, 3.f);
    if (length > 0.0) {
        ren.drawRoundedRect({bar.x, bar.y, bar.width * done, bar.height}, CardsChrome::accent(a), 3.f);
        ren.drawCircle({bar.x + bar.width * done, bar.y + 3.f}, 7.f, white.withAlpha(a), 20);
    } else if (playing) { // length unknown (not an MP3): a sweep shows it runs
        const float sweep = std::fmod(m_time * 0.25f, 1.f);
        ren.drawRoundedRect({bar.x + (bar.width - 60.f) * sweep, bar.y, 60.f, bar.height}, CardsChrome::accent(0.7f * a), 3.f);
    }
    chrome.textMid(ren, sf, fmtTime(at), kInfoX, 278.f + dy, muted.withAlpha(a));
    const std::string total = length > 0.0 ? fmtTime(length) : std::string("-:--");
    chrome.textMid(ren, sf, total, kInfoX + kInfoW - sf.measure(total).x, 278.f + dy, muted.withAlpha(a));

    // Buttons.
    const char* glyphs[5] = {"shuffle", "prev", playing ? "pause" : "play", "next", "repeat"};
    for (int i = 0; i < 5; ++i) {
        const nxui::Vec2 b = buttonPos(i, dy);
        const bool on = (i == 0 && audio.isShuffle()) || (i == 4 && audio.isRepeat());
        const float d = i == 2 ? 66.f : 48.f;
        ren.drawCircle(b, d * 0.5f, i == 2 ? white.withAlpha(0.94f * a)
                                   : on ? CardsChrome::accent(0.4f * a) : nxui::Color{1.f, 1.f, 1.f, 0.08f * a}, 40);
        if (m_zone == 0 && m_col == i)
            ren.drawRoundedRectOutline({b.x - d * 0.5f - 5.f, b.y - d * 0.5f - 5.f, d + 10.f, d + 10.f},
                                       CardsChrome::accent(ring * a), d * 0.5f + 5.f, 2.5f);
        chrome.icon(ren, glyphs[i], b.x, b.y, i == 2 ? 30.f : 22.f,
                    i == 2 ? nxui::Color{0.f, 0.f, 0.f, a} : white.withAlpha(a));
    }

    // Volume.
    const nxui::Rect vol = volumeBar(dy);
    chrome.icon(ren, "speaker", kInfoX + 14.f, vol.y + 4.f, 22.f, white.withAlpha(0.85f * a));
    ren.drawRoundedRect(vol, {1.f, 1.f, 1.f, 0.16f * a}, 4.f);
    const float v = std::clamp(audio.volume(), 0.f, 1.f);
    ren.drawRoundedRect({vol.x, vol.y, vol.width * v, vol.height}, white.withAlpha(0.9f * a), 4.f);
    ren.drawCircle({vol.x + vol.width * v, vol.y + 4.f}, m_zone == 1 ? 11.f : 9.f, white.withAlpha(a), 24);
    if (m_zone == 1)
        ren.drawRoundedRectOutline({vol.x - 10.f, vol.y - 14.f, vol.width + 20.f, 36.f}, CardsChrome::accent(ring * a), 18.f, 2.f);
    chrome.textMid(ren, sf, std::to_string(static_cast<int>(std::lround(v * 100.f))) + "%", vol.x + vol.width + 18.f,
                   vol.y + 4.f, muted.withAlpha(a));

    // The two lists: tabs (L / R), then the songs (scrolling, clipped).
    ren.drawRect({kListX - 22.f, 100.f + dy, 1.f, kListBottom - 88.f}, {1.f, 1.f, 1.f, 0.10f * a});
    const std::string tabNames[2] = {
        i18n.tr("cards.music_playlist", "Playlist"),
        i18n.tr("cards.music_theme_tab", "Themes"),
    };
    for (int i = 0; i < 2; ++i) {
        const nxui::Rect t = tabRect(i, dy);
        const bool active = (i == 0) == (m_list == List::Home);
        ren.drawRoundedRect(t, active ? CardsChrome::accent(0.32f * a) : nxui::Color{1.f, 1.f, 1.f, 0.06f * a}, 15.f);
        if (active)
            ren.drawRoundedRectOutline(t, CardsChrome::accent(0.8f * a), 15.f, 1.5f);
        const std::string label = chrome.fit(sf, tabNames[i], t.width - 34.f);
        const float lw = sf.measure(label).x;
        chrome.textMid(ren, sf, label, t.x + (t.width - lw) * 0.5f + 8.f, t.y + t.height * 0.5f,
                       active ? white.withAlpha(a) : muted.withAlpha(a));
    }
    // L / R glyphs on the tab edges.
    {
        auto& gf = chrome.iconFont();
        const std::string lg = ui::glyph::button(nxui::Button::L), rg = ui::glyph::button(nxui::Button::R);
        const nxui::Rect t0 = tabRect(0, dy), t1 = tabRect(1, dy);
        const float gs = 0.62f;
        ren.drawText(lg, {t0.x + 6.f, t0.y + t0.height * 0.5f - gf.measure(lg).y * gs * 0.5f}, &gf, muted.withAlpha(a), gs);
        ren.drawText(rg, {t1.x + 6.f, t1.y + t1.height * 0.5f - gf.measure(rg).y * gs * 0.5f}, &gf, muted.withAlpha(a), gs);
    }

    const auto tracks = listTracks(service);
    const int songs = static_cast<int>(tracks.size());
    const int current = playingRow(service);
    if (songs == 0) {
        const bool scanning = m_list == List::Themes && m_themeScan.valid();
        const std::string msg = scanning
            ? i18n.tr("cards.music_theme_loading", "Looking for theme music...")
            : (m_list == List::Themes
                ? i18n.tr("cards.music_theme_empty", "No installed theme has music.")
                : i18n.tr("cards.music_empty", "No songs yet. Download some in the Theme Shop."));
        const auto lines = chrome.wrap(sf, msg, kListW - 20.f, 3);
        float ly = kListTop + 30.f + dy;
        for (const auto& line : lines) {
            chrome.textMid(ren, sf, line, kListX + 10.f, ly, muted.withAlpha(a));
            ly += 24.f;
        }
    }
    ren.pushClipRect({kListX - 6.f, kListTop + dy, kListW + 12.f, kListBottom - kListTop});
    for (int i = 0; i < songs; ++i) {
        const nxui::Rect row{kListX, kListTop + dy + i * kRowH - m_listScroll, kListW, kRowH - 6.f};
        if (row.y + row.height < kListTop + dy || row.y > kListBottom + dy) continue;
        const bool isCurrent = i == current, chosen = m_zone == 2 && m_row == i;
        if (isCurrent || chosen) ren.drawRoundedRect(row, {1.f, 1.f, 1.f, (chosen ? 0.12f : 0.06f) * a}, 14.f);
        if (chosen) ren.drawRoundedRectOutline(row, (m_confirmDelete ? nxui::Color{1.f, 0.4f, 0.35f, ring * a}
                                                                      : CardsChrome::accent(ring * a)), 14.f, 2.f);
        const float cy = row.y + row.height * 0.5f;
        chrome.textMid(ren, sf, std::to_string(i + 1), row.x + 12.f, cy, muted.withAlpha(0.7f * a));
        // The chosen and the playing row show their whole title (scrolling);
        // the others are cut to fit.
        const std::string& title = tracks[static_cast<std::size_t>(i)].title;
        const nxui::Color titleColor = isCurrent ? CardsChrome::accent(a) : white.withAlpha(a);
        if (chosen || isCurrent)
            drawMarquee(ren, pf, title, row.x + 38.f, cy, row.width - 80.f, titleColor,
                        m_time - (chosen ? m_rowSince : m_titleSince));
        else
            chrome.textMid(ren, pf, chrome.fit(pf, title, row.width - 80.f), row.x + 38.f, cy, titleColor);
        if (isCurrent) // little bars that move with the music
            for (int k = 0; k < 3; ++k) {
                const float h = 6.f + 14.f * (playing ? m_viz[4 + k * 9] : 0.15f);
                ren.drawRoundedRect({row.x + row.width - 34.f + k * 8.f, cy + 9.f - h, 5.f, h}, CardsChrome::accent(a), 2.f);
            }
    }
    ren.popClipRect();

    // A short message (delete confirmation, order changed).
    if (!m_toast.empty()) {
        const float ta = std::clamp(m_toastTime / 0.3f, 0.f, 1.f) * a;
        const float tw = std::min(kCard.width - 120.f, sf.measure(m_toast).x + 40.f);
        const nxui::Rect box{kCard.x + (kCard.width - tw) * 0.5f, kListBottom + 8.f + dy, tw, 32.f};
        ren.drawRoundedRect(box, {0.f, 0.f, 0.f, 0.6f * ta}, 16.f);
        const std::string text = chrome.fit(sf, m_toast, tw - 30.f);
        chrome.textMid(ren, sf, text, box.x + (tw - sf.measure(text).x) * 0.5f, box.y + 16.f, white.withAlpha(ta));
    }

    // The music's bars along the bottom, with their reflection.
    const float baseY = kCard.y + kCard.height - 44.f + dy, left = kCard.x + 40.f;
    const float step = (kCard.width - 80.f) / AudioManager::kBands, bw = step - 6.f;
    for (int i = 0; i < AudioManager::kBands; ++i) {
        const float lv = m_viz[i];
        const float h = 4.f + 118.f * lv;
        const nxui::Rect b{left + i * step + 3.f, baseY - h, bw, h};
        const nxui::Color col = nxui::Color::lerp(CardsChrome::accent(1.f), white, lv * 0.45f);
        ren.drawRoundedRect(b, col.withAlpha((0.55f + 0.45f * lv) * a), std::min(bw * 0.5f, 5.f));
        ren.drawRoundedRect({b.x, baseY + 6.f, bw, h * 0.28f}, col.withAlpha(0.14f * a), std::min(bw * 0.5f, 4.f));
    }
}

} // namespace cards
