#pragma once

#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <functional>

namespace widgets {

/// Classic top bar button beside the music button: downloads BrowseNX the
/// first time (download icon) and opens it once installed (browser icon).
class BrowserButton : public nxui::GlassWidget {
public:
    BrowserButton();
    ~BrowserButton() override = default;

    void setInstalled(bool installed);
    bool installed() const { return m_installed; }
    void setMetroStyle(bool metro) { m_metroStyle = metro; }

    void onActivate(std::function<void()> cb) {
        m_onActivateCb = cb;
        setOnActivate(cb);
    }
    bool activate() override {
        if (m_onActivateCb) {
            m_onActivateCb();
            return true;
        }
        return false;
    }

    void onFocusGained() override { m_focused = true; }
    void onFocusLost() override { m_focused = false; }

    void onRender(nxui::Renderer& ren) override;
    bool handleTouch(const nxui::Input& input);

private:
    std::function<void()> m_onActivateCb;
    bool m_installed = false;
    bool m_focused = false;
    bool m_metroStyle = false;
    nxui::Texture m_browserTex;
    nxui::Texture m_downloadTex;
    bool m_texLoaded = false;
};

} // namespace widgets
