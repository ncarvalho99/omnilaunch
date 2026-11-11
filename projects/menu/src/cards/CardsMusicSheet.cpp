#include "cards/CardsMusicSheet.hpp"
#include "core/AudioManager.hpp"
#include "core/Config.hpp"
#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>

namespace cards {

namespace {
const nxui::Rect kCard{110.f, 46.f, 1060.f, 628.f};
constexpr float kInfoX = 470.f, kInfoW = 340.f, kListX = 852.f, kListW = 290.f;
constexpr float kRowH = 44.f, kListTop = 132.f;
constexpr int kVisibleRows = 6;

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
    case 4: // Stop / mute
        if (audio.isPlaying())
            audio.pause();
        else
            audio.play();
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

    // Glass panel
    const nxui::Rect card = kCard;
    ren.drawRoundedRect(card, {0.08f, 0.09f, 0.13f, 0.92f * a}, 34.f);
    ren.drawRoundedRectOutline(card, {1.f, 1.f, 1.f, 0.16f * a}, 34.f, 1.5f);

    auto& tf = chrome.titleFont();
    auto& lf = chrome.labelFont();
    auto& sf = chrome.smallFont();
    auto& i18n = nxui::I18n::instance();

    const std::string title = i18n.tr("cards.music_title", "Music");
    chrome.textMid(ren, tf, title, card.x + 44.f, card.y + 46.f, CardsChrome::kWhite.withAlpha(a));

    const std::string hint = i18n.tr("cards.music_hint", "A  Select    B  Close");
    chrome.textMid(ren, sf, hint, card.right() - sf.measure(hint).x - 44.f, card.y + 46.f,
                   {1.f, 1.f, 1.f, 0.65f * a});

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
    chrome.textMid(ren, tf, chrome.fit(tf, curTitle, kInfoW), kInfoX, card.y + 160.f,
                   CardsChrome::kWhite.withAlpha(a));

    const int curIdx = audio.currentTrackIndex();
    const auto tracks = audio.tracks();
    const int trackCount = static_cast<int>(tracks.size());
    const std::string countStr = tracks.empty() ? ""
        : std::to_string(curIdx + 1) + " / " + std::to_string(tracks.size());
    chrome.textMid(ren, sf, countStr, kInfoX, card.y + 204.f, {1.f, 1.f, 1.f, 0.60f * a});

    // ── Controls: Shuffle, Prev, Play/Pause, Next, Repeat ───────────────────
    const std::string labels[5] = {
        i18n.tr("cards.music_shuffle", "Shuffle"), i18n.tr("cards.music_prev", "Prev"),
        audio.isPlaying() && !audio.isPaused() ? i18n.tr("cards.music_pause", "Pause")
                                               : i18n.tr("cards.music_play", "Play"),
        i18n.tr("cards.music_next", "Next"), i18n.tr("cards.music_stop", "Stop")};
    for (int i = 0; i < 5; ++i) {
        const nxui::Vec2 bp = buttonPos(i, card.y - kCard.y);
        const bool focused = (m_zone == 0 && m_col == i);
        const float r = (i == 2) ? 26.f : 22.f;
        const nxui::Color bgCol = focused ? CardsChrome::kWhite.withAlpha(0.95f * a)
                                          : nxui::Color{0.20f, 0.23f, 0.30f, 0.50f * a};
        ren.drawCircle(bp, r, bgCol, 32);
        if (focused)
            ren.drawRoundedRectOutline({bp.x - r - 4.f, bp.y - r - 4.f, (r + 4.f) * 2.f, (r + 4.f) * 2.f},
                                       CardsChrome::accent(a), r + 4.f, 2.5f);
        // Short text button label
        const nxui::Color textCol = focused ? nxui::Color{0.08f, 0.09f, 0.12f, a}
                                            : CardsChrome::kWhite.withAlpha(a);
        const float tw = sf.measure(labels[i]).x;
        chrome.textMid(ren, sf, labels[i], bp.x - tw * 0.5f, bp.y, textCol);
    }

    // ── Volume Slider ───────────────────────────────────────────────────────
    const nxui::Rect volR = volumeBar(card.y - kCard.y);
    ren.drawRoundedRect(volR, {0.15f, 0.18f, 0.24f, 0.70f * a}, 5.f);
    const float volFill = volR.width * std::clamp(audio.volume(), 0.f, 1.f);
    ren.drawRoundedRect({volR.x, volR.y, volFill, volR.height}, CardsChrome::accent(0.9f * a), 5.f);
    if (m_zone == 1) {
        ren.drawRoundedRectOutline({volR.x - 4.f, volR.y - 4.f, volR.width + 8.f, volR.height + 8.f},
                                   CardsChrome::accent(a), 8.f, 2.f);
    }
    const std::string volText = i18n.tr("cards.music_volume", "Volume") + ": " +
                                std::to_string(static_cast<int>(audio.volume() * 100.f)) + "%";
    chrome.textMid(ren, sf, volText, volR.x, volR.y - 18.f, {1.f, 1.f, 1.f, 0.75f * a});

    // ── Playlist ────────────────────────────────────────────────────────────
    const float listX = kListX, listY = card.y + kListTop;
    const nxui::Rect listBorder{listX - 10.f, listY - 10.f, kListW + 20.f, kVisibleRows * kRowH + 20.f};
    ren.drawRoundedRect(listBorder, {0.05f, 0.06f, 0.09f, 0.50f * a}, 16.f);

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
        chrome.textMid(ren, lf, chrome.fit(lf, tracks[static_cast<std::size_t>(idx)].title, kListW - 16.f),
                       rowR.x + 8.f, rowR.y + rowR.height * 0.5f, rowCol);
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
    }
}

} // namespace cards
