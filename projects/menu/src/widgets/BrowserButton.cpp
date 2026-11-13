#include "BrowserButton.hpp"

namespace widgets {

namespace {
void loadIcon(nxui::Renderer& ren, nxui::Texture& tex, const char* name) {
    const std::string sd = std::string("sdmc:/switch/OmniLaunch/icons/cards/") + name;
    const std::string romfs = std::string("romfs:/icons/cards/") + name;
    if (!tex.loadFromFile(ren.gpu(), ren, sd, 128))
        tex.loadFromFile(ren.gpu(), ren, romfs, 128);
}
} // namespace

BrowserButton::BrowserButton() {
    setSize(54.0f, 48.0f);
    setCornerRadius(16.0f);
    setLiquidGlassEnabled(true);
    setForceLiquidGlass(true);
    setBlurEnabled(false);
    setFocusable(true);
    setTag("browser_button");
    setAccessibilityRole("button");
    setInstalled(false);
    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (m_onActivateCb) m_onActivateCb();
    });
}

void BrowserButton::setInstalled(bool installed) {
    m_installed = installed;
    setAccessibilityLabel(installed ? "Browser" : "Download Browser");
    setAccessibilityHint(installed ? "A to open the browser" : "A to download the browser");
}

void BrowserButton::onRender(nxui::Renderer& ren) {
    const float alpha = m_opacity * m_panelOpacity;
    if (alpha <= 0.01f) return;
    if (!m_texLoaded) {
        m_texLoaded = true;
        loadIcon(ren, m_browserTex, "browser.png");
        loadIcon(ren, m_downloadTex, "download.png");
    }
    const float cx = m_rect.x + m_rect.width * 0.5f;
    const float cy = m_rect.y + m_rect.height * 0.5f;
    if (m_metroStyle) {
        ren.drawRoundedRect(m_rect, nxui::Color(0.f, 0.47f, 0.84f, 0.90f * alpha), 4.f);
    } else {
        ren.drawRoundedRect(m_rect, nxui::Color(0.06f, 0.10f, 0.18f, 0.38f * alpha), cornerRadius());
        // Focus is drawn by the shared SelectionCursor.
        ren.drawRoundedRectOutline(m_rect, nxui::Color(1.f, 1.f, 1.f, 0.32f * alpha), cornerRadius(), 1.2f);
    }
    const nxui::Texture* icon = m_installed ? &m_browserTex : &m_downloadTex;
    const nxui::Color tint = m_focused ? nxui::Color(1.0f, 0.95f, 0.65f, alpha)
                                       : nxui::Color(1.0f, 1.0f, 1.0f, 0.95f * alpha);
    if (icon->valid()) {
        constexpr float s = 30.0f;
        ren.drawTexture(icon, {cx - s * 0.5f, cy - s * 0.5f, s, s}, tint);
    } else {
        // Globe fallback.
        ren.drawCircle({cx, cy}, 12.f, tint.withAlpha(0.25f * alpha));
        ren.drawRect({cx - 12.f, cy - 1.f, 24.f, 2.f}, tint);
        ren.drawRect({cx - 1.f, cy - 12.f, 2.f, 24.f}, tint);
    }
    if (!m_installed) {
        // Small accent dot: not downloaded yet.
        ren.drawCircle({m_rect.right() - 9.f, m_rect.y + 9.f}, 4.f, nxui::Color(0.30f, 0.75f, 0.95f, alpha));
    }
}

bool BrowserButton::handleTouch(const nxui::Input& input) {
    if (!isVisible() || m_opacity <= 0.05f) return false;
    if (input.touchDown() && m_rect.contains(input.touchX(), input.touchY()) && m_onActivateCb) {
        m_onActivateCb();
        return true;
    }
    return false;
}

} // namespace widgets
