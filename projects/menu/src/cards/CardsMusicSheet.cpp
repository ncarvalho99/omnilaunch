#include "cards/CardsMusicSheet.hpp"
#include "core/AudioManager.hpp"
#include "core/ButtonGlyph.hpp"
#include "core/Config.hpp"
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cards {

namespace {
// Layout of the CardsNX Music screen (HomeNX projects/cards/src/CardsActivity.cpp
// drawMusic/updateMusic @ 0448cd4, GPL-3.0, ad3333m), adapted to AudioManager.
const nxui::Rect kCard{110.f, 46.f, 1060.f, 628.f};
constexpr float kInfoX = 470.f, kInfoW = 340.f, kListX = 852.f, kListW = 290.f;
constexpr float kRowH = 46.f, kListTop = 132.f, kListBottom = 452.f;

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
float easeOut(float t) { t = std::clamp(t, 0.f, 1.f); return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }
} // namespace

void CardsMusicSheet::open(AudioManager& audio, AppConfig& config) {
    m_open = true;
    m_in = 0.f;
    m_backdropCacheValid = false;
    m_zone = 0;
    m_col = 2;
    m_row = std::max(0, audio.currentTrackIndex());
    m_listScroll = -1.f; // snap to the playing song on the first frame
    (void)config;
}

void CardsMusicSheet::close() {
    m_open = false;
    if (m_onClosed)
        m_onClosed();
}

void CardsMusicSheet::controlAction(int col, AudioManager& audio, AppConfig& config) {
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
        break;
    default:
        break;
    }
    m_row = std::max(0, audio.currentTrackIndex());
}

bool CardsMusicSheet::handleInput(nxui::Input& in, AudioManager& audio, AppConfig& config) {
    using B = nxui::Button;
    if (!m_open || m_in < 0.8f)
        return true;

    if (in.isDown(B::B) || in.isDown(B::Plus)) {
        close();
        return true;
    }
    if (handleTouch(in, audio, config))
        return true;

    const int songs = static_cast<int>(audio.trackCount());
    const bool left = in.isDown(B::DLeft) || in.isDown(B::LStickL);
    const bool right = in.isDown(B::DRight) || in.isDown(B::LStickR);
    const bool up = in.isDown(B::DUp) || in.isDown(B::LStickU);
    const bool down = in.isDown(B::DDown) || in.isDown(B::LStickD);
    const bool heldLeft = in.isHeld(B::DLeft) || in.isHeld(B::LStickL);
    const bool heldRight = in.isHeld(B::DRight) || in.isHeld(B::LStickR);
    const bool heldUp = in.isHeld(B::DUp) || in.isHeld(B::LStickU);
    const bool heldDown = in.isHeld(B::DDown) || in.isHeld(B::LStickD);

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
        else if (right && songs > 0) m_zone = 2;
        else if (down) m_zone = 1;
        if (in.isDown(B::A)) controlAction(m_col, audio, config);
    } else if (m_zone == 1) {
        if (up) m_zone = 0;
        const int step = (left || (repeatH && dir < 0)) ? -1 : (right || (repeatH && dir > 0)) ? 1 : 0;
        if (step != 0) {
            const float v = std::clamp(config.musicVolume + 0.05f * static_cast<float>(step), 0.f, 1.f);
            config.musicVolume = v;
            audio.setVolume(v);
        }
    } else {
        const int step = (up || (repeatV && vdir < 0)) ? -1 : (down || (repeatV && vdir > 0)) ? 1 : 0;
        if (step < 0 && m_row > 0) --m_row;
        else if (step > 0 && m_row + 1 < songs) ++m_row;
        else if (left) m_zone = 0;
        if (in.isDown(B::A) && m_row >= 0 && m_row < songs) {
            config.musicEnabled = true;
            audio.playTrack(m_row);
        }
    }
    return true;
}

// Touch, as CardsNX: drag along the volume, tap a button or a song, tap
// outside the card to close.
bool CardsMusicSheet::handleTouch(nxui::Input& in, AudioManager& audio, AppConfig& config) {
    const float x = in.touchX(), y = in.touchY();
    const nxui::Rect vol = volumeBar(0.f);
    if (in.isTouching() && (m_volDrag || (x >= vol.x - 10.f && x <= vol.x + vol.width + 10.f && std::abs(y - vol.y - 4.f) < 22.f))) {
        m_volDrag = true;
        m_zone = 1;
        const float v = std::clamp((x - vol.x) / vol.width, 0.f, 1.f);
        config.musicVolume = v;
        audio.setVolume(v);
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
            controlAction(i, audio, config);
            return true;
        }
    }
    const int songs = static_cast<int>(audio.trackCount());
    if (x >= kListX && x <= kListX + kListW && y >= kListTop && y <= kListBottom) {
        const int row = static_cast<int>((y - kListTop + m_listScroll) / kRowH);
        if (row >= 0 && row < songs) {
            m_zone = 2;
            m_row = row;
            config.musicEnabled = true;
            audio.playTrack(row);
        }
        return true;
    }
    if (!kCard.contains(x, y)) {
        close();
        return true;
    }
    return true;
}
void CardsMusicSheet::update(float dt, const AudioManager& audio) {
    if (!m_open)
        return;
    if (m_in < 1.f)
        m_in = std::min(1.f, m_in + dt / 0.22f);

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

    const int songs = static_cast<int>(audio.trackCount());
    const float span = kListBottom - kListTop;
    const float want = std::clamp(m_row * kRowH - span * 0.5f + kRowH * 0.5f, 0.f,
                                  std::max(0.f, songs * kRowH - span));
    if (m_listScroll < 0.f) m_listScroll = want;
    m_listScroll += (want - m_listScroll) * std::min(1.f, dt * 15.f);
}

void CardsMusicSheet::render(nxui::Renderer& ren, CardsChrome& chrome, const AudioManager& audio) const {
    if (!m_open)
        return;

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
        const std::vector<ui::glyph::ActionHint> hints = {
            {ui::glyph::button(nxui::Button::A), i18n.tr("hint.select", "Select")},
            {ui::glyph::button(nxui::Button::B), i18n.tr("hint.back", "Back")},
        };
        // Bottom edge 97 px: clear of the Playlist heading below it (y 106).
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

    // The song.
    const auto tracks = audio.tracks();
    const int now = audio.currentTrackIndex();
    const std::string song = audio.currentTrackTitle().empty()
        ? i18n.tr("cards.music_stopped", "No music playing") : audio.currentTrackTitle();
    float ts = 1.f;
    const float sw = tf.measure(song).x;
    if (sw > kInfoW) ts = std::max(0.7f, kInfoW / sw);
    std::string shown = song;
    if (sw * ts > kInfoW) shown = chrome.fit(tf, song, kInfoW / ts);
    ren.drawText(shown, {kInfoX, 150.f + dy - tf.measure(shown).y * ts * 0.5f}, &tf, white.withAlpha(a), ts);

    std::string sub = i18n.tr("cards.music_source", "HOME music");
    if (now >= 0 && !tracks.empty()) {
        std::string of = i18n.tr("cards.music_song_of", "Song {n} of {total}");
        of = replaceAll(replaceAll(of, "{n}", std::to_string(now + 1)), "{total}", std::to_string(tracks.size()));
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

    // Playlist: a divider, its title, and the songs (scrolling, clipped).
    ren.drawRect({kListX - 22.f, 112.f + dy, 1.f, kListBottom - 100.f}, {1.f, 1.f, 1.f, 0.10f * a});
    chrome.icon(ren, "playlist", kListX + 10.f, 106.f + dy, 20.f, muted.withAlpha(a));
    chrome.textMid(ren, pf, i18n.tr("cards.music_playlist", "Playlist"), kListX + 30.f, 106.f + dy, muted.withAlpha(a));
    const int songs = static_cast<int>(tracks.size());
    ren.pushClipRect({kListX - 6.f, kListTop + dy, kListW + 12.f, kListBottom - kListTop});
    for (int i = 0; i < songs; ++i) {
        const nxui::Rect row{kListX, kListTop + dy + i * kRowH - m_listScroll, kListW, kRowH - 6.f};
        if (row.y + row.height < kListTop + dy || row.y > kListBottom + dy) continue;
        const bool current = i == now, chosen = m_zone == 2 && m_row == i;
        if (current || chosen) ren.drawRoundedRect(row, {1.f, 1.f, 1.f, (chosen ? 0.12f : 0.06f) * a}, 14.f);
        if (chosen) ren.drawRoundedRectOutline(row, CardsChrome::accent(ring * a), 14.f, 2.f);
        const float cy = row.y + row.height * 0.5f;
        chrome.textMid(ren, sf, std::to_string(i + 1), row.x + 12.f, cy, muted.withAlpha(0.7f * a));
        chrome.textMid(ren, pf, chrome.fit(pf, tracks[static_cast<std::size_t>(i)].title, row.width - 80.f), row.x + 38.f, cy,
                       current ? CardsChrome::accent(a) : white.withAlpha(a));
        if (current) // little bars that move with the music
            for (int k = 0; k < 3; ++k) {
                const float h = 6.f + 14.f * (playing ? m_viz[4 + k * 9] : 0.15f);
                ren.drawRoundedRect({row.x + row.width - 34.f + k * 8.f, cy + 9.f - h, 5.f, h}, CardsChrome::accent(a), 2.f);
            }
    }
    ren.popClipRect();

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