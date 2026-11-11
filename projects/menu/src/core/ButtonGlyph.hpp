#pragma once

#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Types.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace ui::glyph {

inline std::string utf8Codepoint(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back((char)cp);
    } else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return out;
}

inline std::string button(nxui::Button btn) {
    switch (btn) {
        case nxui::Button::A: return utf8Codepoint(0xE0E0);
        case nxui::Button::B: return utf8Codepoint(0xE0E1);
        case nxui::Button::X: return utf8Codepoint(0xE0E2);
        case nxui::Button::Y: return utf8Codepoint(0xE0E3);
        case nxui::Button::L: return utf8Codepoint(0xE0E4);
        case nxui::Button::R: return utf8Codepoint(0xE0E5);
        case nxui::Button::ZL: return utf8Codepoint(0xE0E6);
        case nxui::Button::ZR: return utf8Codepoint(0xE0E7);
        case nxui::Button::Plus: return utf8Codepoint(0xE0F1);
        case nxui::Button::Minus: return utf8Codepoint(0xE0F2);
        case nxui::Button::LStick: return utf8Codepoint(0xE0C4);
        case nxui::Button::RStick: return utf8Codepoint(0xE0C5);
        default: return {};
    }
}

inline std::string dpad() {
    return utf8Codepoint(0xE0EA);
}

struct ActionHint {
    std::string icon;
    std::string label;
};

inline float capsuleWidth(nxui::Font& iconFont, nxui::Font& textFont,
                          const std::string& icon, const std::string& label,
                          float iconScale = 0.68f, float textScale = 0.54f) {
    constexpr float kPadX = 12.f;
    constexpr float kGap = 6.f;
    return kPadX * 2.f + iconFont.measure(icon).x * iconScale + kGap + textFont.measure(label).x * textScale;
}

inline void drawCapsule(nxui::Renderer& ren, nxui::Font& iconFont, nxui::Font& textFont,
                        const ActionHint& hint, const nxui::Rect& cap, float alpha = 1.f,
                        float iconScale = 0.68f, float textScale = 0.54f) {
    const float radius = cap.height * 0.5f;
    ren.drawRoundedRect({cap.x, cap.y + 2.f, cap.width, cap.height},
                        nxui::Color(0.f, 0.f, 0.f, 0.20f * alpha), radius);
    ren.drawRoundedRect(cap, nxui::Color(0.12f, 0.14f, 0.20f, 0.65f * alpha), radius);
    ren.drawRoundedRectOutline(cap, nxui::Color(1.f, 1.f, 1.f, 0.22f * alpha), radius, 1.2f);

    constexpr float kPadX = 12.f;
    constexpr float kGap = 6.f;
    const nxui::Vec2 iconSz = iconFont.measure(hint.icon);
    const float iconW = iconSz.x * iconScale;
    ren.drawText(hint.icon, {cap.x + kPadX, cap.y + (cap.height - iconSz.y * iconScale) * 0.5f},
                 &iconFont, nxui::Color(1.f, 1.f, 1.f, 0.95f * alpha), iconScale);

    const nxui::Vec2 textSz = textFont.measure(hint.label);
    ren.drawText(hint.label, {cap.x + kPadX + iconW + kGap, cap.y + (cap.height - textSz.y * textScale) * 0.5f},
                 &textFont, nxui::Color(0.88f, 0.90f, 0.95f, 0.90f * alpha), textScale);
}

inline void drawHintBar(nxui::Renderer& ren, nxui::Font& iconFont, nxui::Font& textFont,
                        const std::vector<ActionHint>& hints, float rightX, float bottomY,
                        float alpha = 1.f, float capH = 26.f, float capGap = 10.f) {
    if (hints.empty() || alpha <= 0.01f)
        return;
    float x = rightX;
    for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
        const float w = capsuleWidth(iconFont, textFont, it->icon, it->label);
        x -= w;
        drawCapsule(ren, iconFont, textFont, *it, {x, bottomY - capH, w, capH}, alpha);
        x -= capGap;
    }
}

} // namespace ui::glyph
