#include "media/MusicPlayerOverlay.hpp"
#include "core/Config.hpp"
#include "home/HomeAudio.hpp"
#include <nxui/core/Renderer.hpp>

namespace media {

MusicPlayerOverlay::MusicPlayerOverlay(nxui::GpuDevice& gpu, nxui::Renderer& ren, home::HomeAudio& service,
                                       AppConfig& config, nxui::ThreadPool& pool, std::string assetsRoot)
    : m_gpu(gpu), m_ren(ren), m_service(service), m_config(config), m_pool(pool), m_assets(std::move(assetsRoot)) {
    setRect({0.f, 0.f, 1280.f, 720.f});
    setFocusable(true);
    setTag("music_player");
    setAccessibilityRole("window");
    setAccessibilityLabel("Music");
    m_sheet.onClosed([this]() {
        if (m_onClose)
            m_onClose();
    });
}

void MusicPlayerOverlay::show() {
    if (!m_chromeLoaded) {
        // Fonts and icons of the sheet, loaded the first time it opens.
        m_chromeLoaded = true;
        m_chrome.load(m_gpu, m_ren, m_assets);
    }
    cards::CardsChrome::setStyle(m_config.cards.accent, m_config.cards.glass);
    m_waitRelease = true;
    m_sheet.open(m_service, m_config, &m_pool);
}

void MusicPlayerOverlay::hide() {
    if (m_sheet.isOpen())
        m_sheet.close();
}

void MusicPlayerOverlay::handleInput(const nxui::Input& input, float /*dt*/) {
    if (!m_sheet.isOpen())
        return;
    if (m_waitRelease) {
        if (input.isHeld(nxui::Button::A) || input.isDown(nxui::Button::A))
            return;
        m_waitRelease = false;
    }
    m_sheet.handleInput(input, m_service, m_config);
}

void MusicPlayerOverlay::onUpdate(float dt) {
    m_sheet.update(dt, m_service);
}

void MusicPlayerOverlay::onRender(nxui::Renderer& ren) {
    if (m_sheet.isOpen() && m_chromeLoaded)
        m_sheet.render(ren, m_chrome, m_service);
}

} // namespace media
