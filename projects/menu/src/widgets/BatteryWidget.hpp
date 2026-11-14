#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Types.hpp>
#include <nxui/core/Texture.hpp>


class BatteryWidget : public nxui::GlassWidget {
public:
    BatteryWidget() = default;
    void setFont(nxui::Font* f) { m_font = f; }
    void setTextColor(const nxui::Color& c) { m_textColor = c; }
    void setBatteryStatus(uint32_t percentage, bool charging);

protected:
    void onContentUpdate(float dt) override;
    void onContentRender(nxui::Renderer& ren) override;
    nxui::Vec2 computeContentSize() const override;
    void renderIcons(nxui::Renderer& ren);

private:
    nxui::Font* m_font = nullptr;
    float m_level   = -1.f;
    bool  m_charging = false;
    float m_timer    = 0.f;
    float m_chargeAnim = 0.f;
    nxui::Color m_textColor {1.f, 1.f, 1.f, 1.f};
    // Icon-pack shell and bolt; when the shell is missing the code-drawn battery stays.
    nxui::Texture m_shellTex;
    nxui::Texture m_boltTex;
    bool m_texLoaded = false;
};
