#pragma once

#include <nxui/Theme.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/widgets/GlassWidget.hpp>

// HOME's notification centre button, next to the profile tile: a bell, with a
// red count of what came since it was last opened.
class NotificationBell : public nxui::GlassWidget {
public:
    NotificationBell();

    void setTheme(const nxui::Theme* theme);
    void setFont(nxui::Font* font) { m_font = font; }
    void setUnread(int count);
    int unread() const { return m_unread; }
    void setOnActivate(std::function<void()> cb) { m_onActivate = std::move(cb); }
    bool activate() override {
        if (m_onActivate) { m_onActivate(); return true; }
        return false;
    }

protected:
    void onContentRender(nxui::Renderer& ren) override;

private:
    std::function<void()> m_onActivate;
    const nxui::Theme* m_theme = nullptr;
    nxui::Font* m_font = nullptr;
    int m_unread = 0;
    nxui::Texture m_bellTex; // icon pack bell; the drawn bell is the fallback
    bool m_texLoaded = false;
};
