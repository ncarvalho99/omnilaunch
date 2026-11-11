#include "cards/CardsMusicSheet.hpp"
#include "core/AudioManager.hpp"
#include "core/Config.hpp"
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cards {

namespace {
const nxui::Rect kCard{110.f, 46.f, 1060.f, 628.f};
constexpr float kInfoX = 470.f, kInfoW = 340.f, kListX = 852.f, kListW = 290.f;
constexpr float kRowH = 44.f, kListTop = 132.f;
constexpr int kVisibleRows = 6;

static std::string fmtTime(double sec) {
    if (sec < 0.0) sec = 0.0;
    int s = static_cast<int>(sec + 0.5);
    int m = s / 60;
    s %= 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

nxui::Vec2 buttonPos(int col, float dy) {
    return {kInfoX + 34.f + 68.f * static_cast<float>(col), 330.f + dy};
}
nxui::Rect volumeBar(float dy) {
    return {kInfoX + 44.f, 402.f + dy, kInfoW - 88.f, 10.f};
}
} // namespace

void CardsMusicSheet::open(AudioManager& audio, AppConfig& config) {
    m_open = true;
    m_in = 0.f;
    m_backdropCacheValid = false;
    m_zone = 0;
    m_col = 2;
    m_row = std::max(0, audio.currentTrackIndex());
    m_scroll = 0;
    (void)config;
}

void CardsMusicSheet::close() {
    m_open = false;
    if (m_onClosed)
        m_onClosed();
}

void CardsMusicSheet::controlAction(int col, AudioManager& audio, AppConfig& config) {
    switch (col) {
    case 0: { // Shuffle
        const bool shuf = !audio.isShuffle();
        audio.setShuffle(shuf);
        break;
    }
    case 1: // Previous
        audio.previousTrack();
        break;
    case 2: // Play/Pause
        if (!config.musicEnabled) {
            config.musicEnabled = true;
            audio.play();
        } else {
            audio.togglePlayPause();
        }
        break;
    case 3: // Next
        audio.nextTrack();
        break;
    case 4: // Repeat
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

    if (in.isDown(B::B)) {
        close();
        return true;
    }

    const auto tracks = audio.tracks();
    const int trackCount = static_cast<int>(tracks.size());

    // Navigation
    if (in.isDown(B::DLeft) || in.isDown(B::LStickL)) {
        if (m_zone == 0) {
            if (m_col > 0) --m_col;
        } else if (m_zone == 1) {
            float v = std::clamp(config.musicVolume - 0.05f, 0.f, 1.f);
            config.musicVolume = v;
            audio.setVolume(v);
        } else if (m_zone == 2) {
            m_zone = 0; // jump back to controls
        }
    } else if (in.isDown(B::DRight) || in.isDown(B::LStickR)) {
        if (m_zone == 0) {
            if (m_col < 4) ++m_col;
            else if (trackCount > 0) m_zone = 2; // move into playlist
        } else if (m_zone == 1) {
            float v = std::clamp(config.musicVolume + 0.05f, 0.f, 1.f);
            config.musicVolume = v;
            audio.setVolume(v);
        }
    } else if (in.isDown(B::DUp) || in.isDown(B::LStickU)) {
        if (m_zone == 1) {
            m_zone = 0;
        } else if (m_zone == 2) {
            if (m_row > 0) {
                --m_row;
                if (m_row < m_scroll) m_scroll = m_row;
            }
        }
    } else if (in.isDown(B::DDown) || in.isDown(B::LStickD)) {
        if (m_zone == 0) {
            m_zone = 1;
        } else if (m_zone == 2) {
            if (m_row + 1 < trackCount) {
                ++m_row;
                if (m_row >= m_scroll + kVisibleRows) m_scroll = m_row - kVisibleRows + 1;
            }
        }
    }

    if (in.isDown(B::A)) {
        if (m_zone == 0) {
            controlAction(m_col, audio, config);
        } else if (m_zone == 2 && m_row >= 0 && m_row < trackCount) {
            audio.playTrack(m_row);
        }
    }

    return true;
}

void CardsMusicSheet::update(float dt, const AudioManager& audio) {
    if (!m_open)
        return;
    if (m_in < 1.f)
        m_in = std::min(1.f, m_in + dt / 0.22f);

    if (audio.isPlaying() && !audio.isPaused()) {
        m_spin += dt * 0.8f;
        if (m_spin > 6.2831853f)
            m_spin -= 6.2831853f;
    }
    m_bass = audio.bassLevel();
    audio.spectrum(m_viz);
}

void CardsMusicSheet::render(nxui::Renderer& ren, CardsChrome& chrome, const AudioManager& audio) const {
    if (!m_open)
        return;

    const float a = m_in;
    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, {0.f, 0.f, 0.f, 0.52f * a});

    // Glass panel with no dark flash
    const nxui::Rect card = kCard;
    bool drawnGlass = false;
#ifdef SWITCHU_MENU
    if (ren.gpu().offscreenReady()) {
        if (!m_backdropCacheValid) {
            ren.captureToOffscreenSharp();
            ren.applyBlur(2.5f, 2);
            ren.copyOffscreen(nxui::GpuDevice::OFF_SHARP_A, nxui::GpuDevice::OFF_DIALOG);
            m_backdropCacheValid = true;
        }
        ren.drawLiquidGlass(nxui::GpuDevice::OFF_DIALOG, card, 34.f, CardsChrome::glassTint(1.f), a, 0.35f);
        drawnGlass = true;
    }
#endif
    if (!drawnGlass) {
        ren.drawRoundedRect(card, CardsChrome::glassTint(a), 34.f);
    }
    ren.drawRoundedRectOutline(card, {1.f, 1.f, 1.f, 0.16f * a}, 34.f, 1.5f);

    auto& tf = chrome.titleFont();
    auto& lf = chrome.labelFont();
    auto& sf = chrome.smallFont();
    auto& i18n = nxui::I18n::instance();

    const std::string title = i18n.tr("cards.music_title", "Music");
    chrome.textMid(ren, tf, title, card.x + 44.f, card.y + 46.f, CardsChrome::kWhite.withAlpha(a));

    // ── Vinyl Record ────────────────────────────────────────────────────────
    const nxui::Vec2 center{card.x + 220.f, card.y + 250.f};
    const float R = 126.f;
    // Outer bass glow
    ren.drawCircle(center, R + 8.f + 20.f * m_bass, CardsChrome::accent((0.15f + 0.30f * m_bass) * a), 48);
    // Outer edge & vinyl body
    ren.drawCircle(center, R + 2.f, {0.12f, 0.14f, 0.18f, 0.95f * a}, 48);
    ren.drawCircle(center, R, {0.04f, 0.04f, 0.06f, 0.98f * a}, 48);
    // Grooves
    for (float g = R - 12.f; g > 56.f; g -= 12.f)
        ren.drawRoundedRectOutline({center.x - g, center.y - g, g * 2.f, g * 2.f},
                                   {1.f, 1.f, 1.f, 0.05f * a}, g, 1.f);
    // Center label (accent coloured)
    ren.drawCircle(center, 46.f, CardsChrome::accent(0.85f * a), 36);
    ren.drawCircle(center, 12.f, {0.04f, 0.04f, 0.06f, a}, 24);

    // ── Now Playing Info ────────────────────────────────────────────────────
    const std::string curTitle = audio.currentTrackTitle().empty()
        ? i18n.tr("cards.music_stopped", "No music playing")
        : audio.currentTrackTitle();
    chrome.textMid(ren, tf, chrome.fit(tf, curTitle, kInfoW), kInfoX, card.y + 150.f,
                   CardsChrome::kWhite.withAlpha(a));

    const int curIdx = audio.currentTrackIndex();
    const auto tracks = audio.tracks();
    const int trackCount = static_cast<int>(tracks.size());
    const std::string countStr = tracks.empty() ? ""
        : i18n.tr("cards.music_source", "HOME Music") + " · " + std::to_string(curIdx + 1) + " / " + std::to_string(tracks.size());
    chrome.textMid(ren, sf, countStr, kInfoX, card.y + 192.f, {1.f, 1.f, 1.f, 0.60f * a});

    // ── Seek Bar ────────────────────────────────────────────────────────────
    const double posSec = audio.trackPositionSeconds();
    const double durSec = audio.trackDurationSeconds();
    const float progress = (durSec > 0.0) ? std::clamp(static_cast<float>(posSec / durSec), 0.f, 1.f) : 0.f;
    const nxui::Rect seekR{kInfoX, card.y + 230.f, kInfoW, 6.f};
    ren.drawRoundedRect(seekR, {0.15f, 0.18f, 0.24f, 0.70f * a}, 3.f);
    if (progress > 0.f) {
        ren.drawRoundedRect({seekR.x, seekR.y, seekR.width * progress, seekR.height},
                            CardsChrome::accent(0.9f * a), 3.f);
        ren.drawCircle({seekR.x + seekR.width * progress, seekR.y + 3.f}, 6.f,
                       CardsChrome::kWhite.withAlpha(a), 16);
    }
    if (durSec <= 0.0 && audio.isPlaying()) {
        // Duration unknown: a slow sweep shows the track is running.
        const float sweep = std::fmod(m_spin * 0.35f, 1.f);
        ren.drawRoundedRect({seekR.x + (seekR.width - 60.f) * sweep, seekR.y, 60.f, seekR.height},
                            CardsChrome::accent(0.7f * a), 3.f);
    }
    const std::string posStr = fmtTime(posSec);
    chrome.textMid(ren, sf, posStr, seekR.x, seekR.y + 18.f, {1.f, 1.f, 1.f, 0.65f * a});
    if (durSec > 0.0) {
        const std::string durStr = fmtTime(durSec);
        const float durW = sf.measure(durStr).x;
        chrome.textMid(ren, sf, durStr, seekR.right() - durW, seekR.y + 18.f, {1.f, 1.f, 1.f, 0.65f * a});
    }

    // ── Controls: Shuffle, Prev, Play/Pause, Next, Repeat ───────────────────
    const char* iconNames[5] = {
        "shuffle", "prev",
        (audio.isPlaying() && !audio.isPaused()) ? "pause" : "play",
        "next", "repeat"
    };
    for (int i = 0; i < 5; ++i) {
        const nxui::Vec2 bp = buttonPos(i, card.y - kCard.y);
        const bool focused = (m_zone == 0 && m_col == i);
        const float r = (i == 2) ? 28.f : 22.f;
        const bool activeToggle = (i == 0 && audio.isShuffle()) || (i == 4 && audio.isRepeat());
        const nxui::Color bgCol = focused ? CardsChrome::kWhite.withAlpha(0.95f * a)
                                : activeToggle ? CardsChrome::accent(0.40f * a)
                                : nxui::Color{0.20f, 0.23f, 0.30f, 0.50f * a};
        ren.drawCircle(bp, r, bgCol, 32);
        if (focused)
            ren.drawRoundedRectOutline({bp.x - r - 4.f, bp.y - r - 4.f, (r + 4.f) * 2.f, (r + 4.f) * 2.f},
                                       CardsChrome::accent(a), r + 4.f, 2.5f);
        else if (activeToggle)
            ren.drawRoundedRectOutline({bp.x - r - 2.f, bp.y - r - 2.f, (r + 2.f) * 2.f, (r + 2.f) * 2.f},
                                       CardsChrome::accent(0.8f * a), r + 2.f, 1.5f);

        const nxui::Color iconCol = focused ? nxui::Color{0.08f, 0.09f, 0.12f, a}
                                            : CardsChrome::kWhite.withAlpha(a);
        const float iconSz = (i == 2) ? 26.f : 20.f;
        chrome.icon(ren, iconNames[i], bp.x, bp.y, iconSz, iconCol);
    }

    // ── Volume Slider ───────────────────────────────────────────────────────
    const nxui::Rect volR = volumeBar(card.y - kCard.y);
    chrome.icon(ren, "speaker", volR.x - 22.f, volR.y + 5.f, 20.f, CardsChrome::kWhite.withAlpha(0.8f * a));
    ren.drawRoundedRect(volR, {0.15f, 0.18f, 0.24f, 0.70f * a}, 5.f);
    const float volFill = volR.width * std::clamp(audio.volume(), 0.f, 1.f);
    ren.drawRoundedRect({volR.x, volR.y, volFill, volR.height}, CardsChrome::accent(0.9f * a), 5.f);
    ren.drawCircle({volR.x + volFill, volR.y + 5.f}, 7.f, CardsChrome::kWhite.withAlpha(a), 16);
    if (m_zone == 1) {
        ren.drawRoundedRectOutline({volR.x - 4.f, volR.y - 4.f, volR.width + 8.f, volR.height + 8.f},
                                   CardsChrome::accent(a), 8.f, 2.f);
    }
    const std::string volText = std::to_string(static_cast<int>(audio.volume() * 100.f)) + "%";
    chrome.textMid(ren, sf, volText, volR.right() + 10.f, volR.y + 5.f, {1.f, 1.f, 1.f, 0.75f * a});

    // ── Playlist ────────────────────────────────────────────────────────────
    const float listX = kListX, listY = card.y + kListTop;
    const nxui::Rect listBorder{listX - 10.f, listY - 28.f, kListW + 20.f, kVisibleRows * kRowH + 38.f};
    ren.drawRoundedRect(listBorder, {0.05f, 0.06f, 0.09f, 0.50f * a}, 16.f);
    chrome.icon(ren, "playlist", listX + 8.f, listY - 14.f, 18.f, CardsChrome::kWhite.withAlpha(0.8f * a));
    chrome.textMid(ren, sf, "Playlist", listX + 26.f, listY - 14.f, CardsChrome::kWhite.withAlpha(0.8f * a));

    for (int i = 0; i < kVisibleRows && (m_scroll + i) < trackCount; ++i) {
        const int idx = m_scroll + i;
        const float ry = listY + static_cast<float>(i) * kRowH;
        const bool selected = (m_zone == 2 && m_row == idx);
        const bool isPlayingThis = (idx == curIdx);
        const nxui::Rect rowR{listX, ry, kListW, kRowH - 4.f};

        if (selected) {
            ren.drawRoundedRect(rowR, CardsChrome::accent(0.35f * a), 10.f);
            ren.drawRoundedRectOutline(rowR, CardsChrome::accent(a), 10.f, 2.f);
        } else if (isPlayingThis) {
            ren.drawRoundedRect(rowR, {1.f, 1.f, 1.f, 0.10f * a}, 10.f);
        }
        const nxui::Color rowCol = isPlayingThis ? CardsChrome::accent(a)
                                                : CardsChrome::kWhite.withAlpha((selected ? 1.f : 0.80f) * a);
        const std::string numStr = std::to_string(idx + 1) + " ";
        chrome.textMid(ren, sf, numStr, rowR.x + 8.f, rowR.y + rowR.height * 0.5f, {1.f, 1.f, 1.f, 0.5f * a});
        const float nw = sf.measure(numStr).x;
        chrome.textMid(ren, lf, chrome.fit(lf, tracks[static_cast<std::size_t>(idx)].title, kListW - nw - 42.f),
                       rowR.x + 8.f + nw, rowR.y + rowR.height * 0.5f, rowCol);

        if (isPlayingThis && audio.isPlaying() && !audio.isPaused()) {
            for (int b = 0; b < 3; ++b) {
                const float bh = 3.f + 7.f * (0.5f + 0.5f * std::sin(m_spin * 4.f + b * 1.5f));
                ren.drawRect({rowR.right() - 20.f + b * 5.f, rowR.y + (rowR.height + bh) * 0.5f - bh, 3.f, bh},
                             CardsChrome::accent(a));
            }
        }
    }

    // ── 16-band Spectrum Visualizer at Bottom ───────────────────────────────
    const float specLeft = card.x + 60.f;
    const float specWidth = card.width - 120.f;
    const float specBaseY = card.bottom() - 36.f;
    const float bandStep = specWidth / static_cast<float>(AudioManager::kBands);
    const float bandW = bandStep - 6.f;

    for (int i = 0; i < AudioManager::kBands; ++i) {
        const float lv = m_viz[i];
        const float bh = 3.f + 56.f * lv;
        const nxui::Rect b{specLeft + static_cast<float>(i) * bandStep + 3.f, specBaseY - bh, bandW, bh};
        const nxui::Color col = nxui::Color::lerp(CardsChrome::accent(a), CardsChrome::kWhite.withAlpha(a), lv * 0.4f);
        ren.drawRoundedRect(b, col.withAlpha((0.60f + 0.40f * lv) * a), std::min(bandW * 0.5f, 4.f));
        // Mirrored reflection below
        ren.drawRoundedRect({b.x, specBaseY + 2.f, bandW, bh * 0.35f},
                            col.withAlpha(0.18f * a), std::min(bandW * 0.5f, 4.f));
    }
}

} // namespace cards
