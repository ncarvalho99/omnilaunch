#pragma once
#include <nxui/core/Types.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

// Colours the player picks are stored as "#RRGGBBAA" text: readable in
// config.json, stable across builds, and the same notation the hex entry shows.
namespace colorhex {

inline std::optional<nxui::Color> parse(std::string text) {
    std::string h;
    for (char c : text) {
        if (c == '#' || std::isspace(static_cast<unsigned char>(c)))
            continue;
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return std::nullopt;
        h.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (h.size() == 3 || h.size() == 4) {
        std::string wide;
        for (char c : h) { wide.push_back(c); wide.push_back(c); }
        h = wide;
    }
    if (h.size() == 6)
        h += "ff";
    if (h.size() != 8)
        return std::nullopt;
    const unsigned long v = std::stoul(h, nullptr, 16);
    return nxui::Color(static_cast<uint32_t>(v));
}

inline std::string format(const nxui::Color& c, bool withAlpha = true) {
    auto byte = [](float f) {
        return static_cast<int>(std::lround(std::clamp(f, 0.f, 1.f) * 255.f));
    };
    char buf[16];
    if (withAlpha)
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b), byte(c.a));
    else
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b));
    return buf;
}

// Hue, saturation and value (all 0..1) plus alpha: what the sliders edit.
// Kept separately from the RGB colour so the hue survives a trip through grey.
struct Hsva {
    float h = 0.f, s = 0.f, v = 0.f, a = 1.f;
};

inline nxui::Color toColor(const Hsva& in) {
    const float h = std::fmod(std::clamp(in.h, 0.f, 1.f) * 6.f, 6.f);
    const float s = std::clamp(in.s, 0.f, 1.f);
    const float v = std::clamp(in.v, 0.f, 1.f);
    const float c = v * s;
    const float x = c * (1.f - std::fabs(std::fmod(h, 2.f) - 1.f));
    float r = 0.f, g = 0.f, b = 0.f;
    switch (static_cast<int>(h)) {
        case 0: r = c; g = x; break;
        case 1: r = x; g = c; break;
        case 2: g = c; b = x; break;
        case 3: g = x; b = c; break;
        case 4: r = x; b = c; break;
        default: r = c; b = x; break;
    }
    const float m = v - c;
    return nxui::Color(r + m, g + m, b + m, std::clamp(in.a, 0.f, 1.f));
}

inline Hsva fromColor(const nxui::Color& c, float keepHue = 0.f) {
    const float mx = std::max({c.r, c.g, c.b});
    const float mn = std::min({c.r, c.g, c.b});
    const float d = mx - mn;
    Hsva out;
    out.v = mx;
    out.s = mx > 0.f ? d / mx : 0.f;
    out.a = c.a;
    if (d <= 1e-5f) {
        out.h = keepHue;
    } else {
        float h;
        if (mx == c.r)      h = std::fmod((c.g - c.b) / d, 6.f);
        else if (mx == c.g) h = (c.b - c.r) / d + 2.f;
        else                h = (c.r - c.g) / d + 4.f;
        h /= 6.f;
        out.h = h < 0.f ? h + 1.f : h;
    }
    return out;
}

} // namespace colorhex
