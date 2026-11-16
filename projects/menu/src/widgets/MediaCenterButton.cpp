#include "MediaCenterButton.hpp"
#include "core/IconPack.hpp"
#include <nxui/core/Input.hpp>
#include <nxui/Appearance.hpp>
#include <cmath>
#include <algorithm>

namespace widgets {

MediaCenterButton::MediaCenterButton() {
    setSize(54.0f, 48.0f);
    setCornerRadius(16.0f);
    setLiquidGlassEnabled(true);
    setForceLiquidGlass(true);
    setBlurEnabled(false);
    setFocusable(true);
    setTag("media_center_button");
    setAccessibilityRole("button");
    setAccessibilityLabel("Media Center");
    setAccessibilityHint("A to open Multimedia Center");
    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (m_onActivateCb) m_onActivateCb();
    });
}

void MediaCenterButton::setPlaying(bool playing) {
    m_playing = playing;
    setAccessibilityLabel(playing ? "Media Center (Playing)" : "Media Center (Paused)");
}

void MediaCenterButton::onUpdate(float dt) {
    m_animTime += dt;
}

void MediaCenterButton::onRender(nxui::Renderer& ren) {
    const float alpha = m_opacity * m_panelOpacity;
    if (alpha <= 0.01f) return;

    if (m_metroStyle) {
        if (!m_metroMusicLoaded) {
            m_metroMusicLoaded = true;
            m_metroMusicTex.loadFromFile(ren.gpu(), ren, "sdmc:/switch/OmniLaunch/icons/metro/music.png", 128);
        }
        nxui::Color bgCol = nxui::Color(0.f, 0.47f, 0.84f, 0.90f * alpha);
        ren.drawRoundedRect(m_rect, bgCol, 4.f);
        const float cx = m_rect.x + m_rect.width * 0.5f;
        const float cy = m_rect.y + m_rect.height * 0.5f;
        if (m_metroMusicTex.valid()) {
            constexpr float iconSize = 28.0f;
            const nxui::Rect iconRect{cx - iconSize * 0.5f, cy - iconSize * 0.5f, iconSize, iconSize};
            ren.drawTexture(&m_metroMusicTex, iconRect, nxui::Color::white().withAlpha(alpha));
        }
        return;
    }

    const float r = cornerRadius();
    const float pulse = 0.5f + 0.5f * std::sin(m_animTime * 3.0f);

    // Background pill, the same as the browser and WaraWara buttons beside it:
    // the theme's translucent pill, or the player's solid window colour. While
    // music plays the whole button used to turn a pulsing blue, which read as
    // a haze over it; playing is now the small dot below.
    const auto& look = nxui::Appearance::get();
    const nxui::Color bgCol = look.solidPanels
        ? look.panelColor.withAlpha(look.panelColor.a * alpha)
        : nxui::Color(0.06f, 0.10f, 0.18f, 0.38f * alpha);
    ren.drawRoundedRect(m_rect, bgCol, r);

    // Focus is drawn by the shared SelectionCursor; no second (gold) ring here.
    ren.drawRoundedRectOutline(m_rect, nxui::Color(1.0f, 1.0f, 1.0f, 0.32f * alpha), r, 1.2f);

    if (!m_iconLoaded || m_iconGen != iconpack::generation()) {
        m_iconLoaded = true;
        m_iconGen = iconpack::generation();
        iconpack::loadInto(m_iconTex, ren, "media_center.png", 128);
    }

    const float cx = m_rect.x + m_rect.width * 0.5f;
    const float cy = m_rect.y + m_rect.height * 0.5f;

    if (m_iconTex.valid()) {
        constexpr float iconW = 32.0f;
        constexpr float iconH = 32.0f;
        const nxui::Rect iconRect{cx - iconW * 0.5f, cy - iconH * 0.5f, iconW, iconH};
        nxui::Color tint = isFocused() ? nxui::Color(1.0f, 0.95f, 0.65f, alpha)
                                       : nxui::Color(1.0f, 1.0f, 1.0f, 0.95f * alpha);
        ren.drawTexture(&m_iconTex, iconRect, tint);
    } else {
        // Fallback procedural note
        nxui::Color iconCol = isFocused() ? nxui::Color(1.0f, 0.95f, 0.60f, alpha)
                                          : nxui::Color(1.0f, 1.0f, 1.0f, 0.90f * alpha);
        ren.drawCircle({cx - 7.0f, cy + 4.0f}, 4.2f, iconCol);
        ren.drawCircle({cx + 6.5f, cy + 1.5f}, 4.2f, iconCol);
        ren.drawRect({cx - 4.2f, cy - 9.0f, 2.4f, 13.0f}, iconCol);
        ren.drawRect({cx + 9.3f, cy - 11.5f, 2.4f, 13.0f}, iconCol);
        ren.drawRect({cx - 4.2f, cy - 11.5f, 15.9f, 3.8f}, iconCol);
    }
    if (m_playing) {
        // Small accent dot in the corner, where the browser button shows its
        // "not downloaded" dot: music is playing.
        ren.drawCircle({m_rect.right() - 9.f, m_rect.y + 9.f}, 4.f,
                       nxui::Color(0.30f, 0.75f, 0.95f, (0.75f + 0.25f * pulse) * alpha));
    }
}

bool MediaCenterButton::handleTouch(const nxui::Input& input) {
    if (!isVisible() || m_opacity <= 0.05f) return false;
    if (input.touchDown()) {
        if (m_rect.contains(input.touchX(), input.touchY())) {
            if (m_onActivateCb) {
                m_onActivateCb();
                return true;
            }
        }
    }
    return false;
}

} // namespace widgets
