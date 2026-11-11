#include "cards/CardsChrome.hpp"
#include "cards/CardsLayout.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cards {

using namespace layout;

const nxui::Color CardsChrome::kWhite{1.f, 1.f, 1.f, 1.f};
const nxui::Color CardsChrome::kGlassEdge{1.f, 1.f, 1.f, 0.20f};
const nxui::Color CardsChrome::kGreen{0.204f, 0.78f, 0.349f, 1.f};

const CardsChrome::AccentDef CardsChrome::kAccents[kAccentCount] = {
    {"White",  nxui::Color(1.f, 1.f, 1.f, 1.f)},
    {"Blue",   nxui::Color(0.36f, 0.68f, 1.f, 1.f)},
    {"Green",  nxui::Color(0.30f, 0.86f, 0.47f, 1.f)},
    {"Pink",   nxui::Color(1.f, 0.46f, 0.72f, 1.f)},
    {"Orange", nxui::Color(1.f, 0.63f, 0.26f, 1.f)},
    {"Purple", nxui::Color(0.72f, 0.55f, 1.f, 1.f)},
    {"Gold",   nxui::Color(1.f, 0.84f, 0.36f, 1.f)},
};
const char* const CardsChrome::kGlassNames[3] = {"Clear", "Frosted", "Dark"};

namespace {
// Top bar positions (CardsNX build 185).
constexpr float kMenuX = 134.f, kDiscordX = 219.f, kSortX = 304.f, kPillRight = 872.f;
constexpr float kBellX = 908.f, kMusicX = 976.f, kPowerX = 1061.f, kGearX = 1146.f;
int s_accentIndex = 1;
int s_glassStyle = 1;
} // namespace

void CardsChrome::setStyle(int accentIndex, int glassStyle) {
    s_accentIndex = std::clamp(accentIndex, 0, kAccentCount - 1);
    s_glassStyle = std::clamp(glassStyle, 0, 2);
}

int CardsChrome::currentAccent() { return s_accentIndex; }
int CardsChrome::currentGlass() { return s_glassStyle; }

nxui::Color CardsChrome::glassTint(float a) {
    switch (s_glassStyle) {
    case 0: return {0.20f, 0.23f, 0.30f, 0.16f * a}; // Clear
    case 2: return {0.05f, 0.06f, 0.09f, 0.55f * a}; // Dark
    default: return {0.17f, 0.20f, 0.27f, 0.30f * a}; // Frosted
    }
}

nxui::Color CardsChrome::accent(float a) {
    return kAccents[s_accentIndex].color.withAlpha(a);
}

bool CardsChrome::load(nxui::GpuDevice& gpu, nxui::Renderer& ren, const std::string& assetsRoot) {
    m_gpu = &gpu;
    m_ren = &ren;
    m_iconRoot = assetsRoot + "/icons/cards/";
    const std::string font = assetsRoot + "/fonts/DejaVuSans.ttf";
    const bool ok = m_pill.load(gpu, ren, font, 20) && m_title.load(gpu, ren, font, 30) &&
                    m_label.load(gpu, ren, font, 22) && m_small.load(gpu, ren, font, 17) &&
                    m_glyphs.load(gpu, ren, assetsRoot + "/fonts/switch_icons.ttf", 24);
    if (!ok)
        DebugLog::log("[cards] chrome font load failed: %s", font.c_str());
    return ok;
}

void CardsChrome::release() {
    m_icons.clear();
}

void CardsChrome::drawCover(nxui::Renderer& r, const nxui::Texture* tex, const nxui::Rect& dst, float radius,
                            const nxui::Color& tint, float zoom, float panX, float panY) {
    if (!tex || !tex->valid())
        return;
    const float tw = static_cast<float>(tex->width());
    const float th = static_cast<float>(tex->height());
    const float scale = std::max(dst.width / tw, dst.height / th) * zoom;
    const float sw = dst.width / scale, sh = dst.height / scale;
    const nxui::Rect src{(tw - sw) * (0.5f + panX), (th - sh) * (0.5f + panY), sw, sh};
    if (radius > 0.f)
        r.drawTextureSubRounded(tex, src, dst, radius, tint);
    else
        r.drawTextureSub(tex, src, dst, tint);
}

void CardsChrome::icon(nxui::Renderer& r, const char* name, float cx, float cy, float size, const nxui::Color& tint) {
    if (!name || !*name)
        return; // a circle with no glyph (e.g. a picture drawn over it)
    auto it = m_icons.find(name);
    if (it == m_icons.end()) {
        auto tex = std::make_unique<nxui::Texture>();
        if (!m_gpu || !tex->loadFromFile(*m_gpu, *m_ren, m_iconRoot + name + ".png", 0)) {
            DebugLog::log("[cards] icon missing: %s", name);
            tex.reset();
        }
        it = m_icons.emplace(name, std::move(tex)).first;
    }
    if (it->second && it->second->valid())
        r.drawTexture(it->second.get(), {cx - size * 0.5f, cy - size * 0.5f, size, size}, tint);
}

void CardsChrome::textMid(nxui::Renderer& r, nxui::Font& f, const std::string& s, float x, float cy,
                          const nxui::Color& c) {
    if (s.empty())
        return;
    const nxui::Vec2 m = f.measure(s);
    r.drawText(s, {x, cy - m.y * 0.5f}, &f, c);
}

std::string CardsChrome::fit(nxui::Font& f, const std::string& s, float maxWidth) {
    if (f.measure(s).x <= maxWidth)
        return s;
    std::string out = s;
    while (!out.empty() && f.measure(out + "...").x > maxWidth) {
        out.pop_back();
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
            out.pop_back();
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0xC0)
            out.pop_back();
    }
    return out + "...";
}

void CardsChrome::glassCircle(nxui::Renderer& r, float cx, float cy, float d, bool focused, const char* name,
                              float iconSize, float time) {
    const nxui::Rect box{cx - d * 0.5f, cy - d * 0.5f, d, d};
    r.drawRoundedRect(box, focused ? nxui::Color(1.f, 1.f, 1.f, 0.22f) : glassTint(1.f), d * 0.5f);
    const float pulse = 0.8f + 0.2f * std::sin(time * 5.f);
    r.drawRoundedRectOutline(box, focused ? accent(pulse) : kGlassEdge, d * 0.5f, focused ? 3.f : 1.5f);
    icon(r, name, cx, cy, iconSize, kWhite);
}

void CardsChrome::drawBackground(nxui::Renderer& r, const nxui::Texture* current, float currentAlpha,
                                 const nxui::Texture* previous, float time, int hazePercent, bool bgMotion) {
    const nxui::Rect screen{0.f, 0.f, 1280.f, 720.f};
    r.drawGradientRect(screen, {0.36f, 0.41f, 0.55f, 1.f}, {0.22f, 0.25f, 0.34f, 1.f});
    const float zoom = bgMotion ? (1.06f + 0.03f * std::sin(time * 0.11f)) : 1.04f;
    const float panX = bgMotion ? 0.25f * std::sin(time * 0.07f) : 0.f;
    const float panY = bgMotion ? 0.2f * std::cos(time * 0.05f) : 0.f;
    if (previous && currentAlpha < 1.f)
        drawCover(r, previous, screen, 0.f, kWhite, zoom, panX * 0.1f, panY * 0.1f);
    if (current)
        drawCover(r, current, screen, 0.f, kWhite.withAlpha(currentAlpha), zoom, panX * 0.1f, panY * 0.1f);
    // Haze (0..80 %) and depth at the edges.
    const float haze = std::clamp(hazePercent, 0, 80) * 0.01f;
    if (haze > 0.001f)
        r.drawRect(screen, {0.38f, 0.43f, 0.56f, haze});
    r.drawGradientRect({0.f, 0.f, 1280.f, 140.f}, {0.05f, 0.06f, 0.10f, 0.35f}, {0.05f, 0.06f, 0.10f, 0.f});
    r.drawGradientRect({0.f, 560.f, 1280.f, 160.f}, {0.05f, 0.06f, 0.10f, 0.f}, {0.05f, 0.06f, 0.10f, 0.40f});
}

void CardsChrome::drawTopBar(nxui::Renderer& r, const StatusSnapshot& st, TopButton focused, bool topZone,
                             float time) {
    const float y = kTopY;
    auto on = [&](TopButton b) { return topZone && focused == b; };
    glassCircle(r, kMenuX, y, kCircle, on(TopButton::Menu), "dots", 34.f, time);
    glassCircle(r, kDiscordX, y, kCircle, on(TopButton::Discord), "discord", 40.f, time);
    glassCircle(r, kSortX, y, kCircle, on(TopButton::Sort), "sort", 34.f, time);

    // The pill: avatar, clock, battery, SoC temperature, free space.
    const float face = 34.f, gap = 16.f;
    const std::string battery = st.batteryPercent >= 0 ? std::to_string(st.batteryPercent) + "%" : "--";
    char temp[16] = "";
    if (st.socCelsius > 0.f)
        std::snprintf(temp, sizeof(temp), "%.0f\xC2\xB0", st.socCelsius);
    const float wClock = m_pill.measure(st.clock).x;
    const float wBattery = m_pill.measure(battery).x;
    const float wTemp = temp[0] ? 24.f + m_pill.measure(temp).x + 6.f + 22.f : 0.f;
    const float wFree = st.freeSpace.empty() ? 0.f : 26.f + m_pill.measure(st.freeSpace).x;
    const std::string players = st.nextendoPlayers == -1 ? "?" : (st.nextendoPlayers >= 0 ? std::to_string(st.nextendoPlayers) : "");
    const float wPlayers = (st.nextendoPlayers >= -1) ? (24.f + 6.f + m_pill.measure(players).x) : 0.f;
    const float bolt = st.charging ? 18.f : 0.f;
    const float width = 9.f + face + 14.f + wClock + gap + bolt + wBattery +
                        (wTemp > 0.f ? gap + wTemp : 0.f) + (wFree > 0.f ? gap + wFree : 0.f) +
                        (wPlayers > 0.f ? gap + wPlayers : 0.f) + 18.f;
    const nxui::Rect pill{kPillRight - width, y - 26.f, width, 52.f};
    r.drawRoundedRect(pill, glassTint(1.f), 26.f);
    r.drawRoundedRectOutline(pill, kGlassEdge, 26.f, 1.5f);
    float x = pill.x + 9.f;
    const nxui::Rect faceRect{x, y - face * 0.5f, face, face};
    if (st.avatar && st.avatar->valid())
        r.drawTextureRounded(st.avatar, faceRect, face * 0.5f, kWhite);
    else
        r.drawCircle({x + face * 0.5f, y}, face * 0.5f, {1.f, 1.f, 1.f, 0.3f}, 32);
    r.drawCircle({x + face - 3.f, y + face * 0.5f - 3.f}, 7.f, {0.08f, 0.09f, 0.12f, 1.f}, 16);
    r.drawCircle({x + face - 3.f, y + face * 0.5f - 3.f}, 5.f, kGreen, 16);
    if (on(TopButton::User)) {
        const float pulse = 0.8f + 0.2f * std::sin(time * 5.f);
        r.drawRoundedRectOutline({faceRect.x - 5.f, faceRect.y - 5.f, face + 10.f, face + 10.f}, accent(pulse),
                                 face * 0.65f, 3.f);
    }
    x += face + 14.f;
    textMid(r, m_pill, st.clock, x, y, kWhite);
    x += wClock + gap;
    if (st.charging) {
        // Lightning bolt (two triangles), as the CardsNX charging mark.
        r.drawTriangle({x + 8.f, y - 11.f}, {x - 1.f, y + 2.f}, {x + 7.f, y + 1.f}, kGreen);
        r.drawTriangle({x + 6.f, y - 1.f}, {x + 13.f, y - 2.f}, {x + 4.f, y + 11.f}, kGreen);
        x += bolt;
    }
    textMid(r, m_pill, battery, x, y, kWhite);
    x += wBattery;
    if (wTemp > 0.f) {
        x += gap;
        icon(r, "thermo", x + 10.f, y, 22.f, kWhite);
        x += 24.f;
        textMid(r, m_pill, temp, x, y, kWhite);
        x += m_pill.measure(temp).x + 6.f;
        const float level = std::clamp((st.socCelsius - 30.f) / 40.f, 0.f, 1.f);
        const nxui::Color barColor = st.socCelsius >= 60.f ? nxui::Color(1.f, 0.231f, 0.188f, 1.f)
                                   : st.socCelsius >= 48.f ? nxui::Color(1.f, 0.62f, 0.04f, 1.f)
                                                           : kGreen;
        r.drawRoundedRect({x, y - 3.f, 22.f, 6.f}, {1.f, 1.f, 1.f, 0.25f}, 3.f);
        r.drawRoundedRect({x, y - 3.f, std::max(6.f, 22.f * level), 6.f}, barColor, 3.f);
        x += 22.f;
    }
    if (wFree > 0.f) {
        x += gap;
        // SD card glyph: a small rounded outline with 3 contact lines in cyan.
        const nxui::Color sd{0.39f, 0.82f, 1.f, 1.f};
        const nxui::Rect cardRect{x + 2.f, y - 10.f, 15.f, 20.f};
        r.drawRoundedRectOutline(cardRect, sd, 3.f, 2.f);
        for (int i = 0; i < 3; ++i)
            r.drawRect({cardRect.x + 3.5f + 3.f * static_cast<float>(i), cardRect.y + 3.f, 1.5f, 4.f}, sd);
        x += 26.f;
        textMid(r, m_pill, st.freeSpace, x, y, kWhite);
        x += m_pill.measure(st.freeSpace).x;
    }
    if (wPlayers > 0.f) {
        x += gap;
        const nxui::Rect playersHit{x - 4.f, y - 20.f, wPlayers + 8.f, 40.f};
        icon(r, "friends", x + 12.f, y, 22.f, kWhite);
        textMid(r, m_pill, players, x + 24.f + 6.f, y, kWhite);
        if (on(TopButton::Nextendo)) {
            const float pulse = 0.8f + 0.2f * std::sin(time * 5.f);
            r.drawRoundedRectOutline(playersHit, accent(pulse), 20.f, 2.5f);
        }
        x += wPlayers;
    }

    // Bell (smaller, no circle), music, power, settings.
    if (on(TopButton::Bell)) {
        const float pulse = 0.8f + 0.2f * std::sin(time * 5.f);
        r.drawRoundedRectOutline({kBellX - 26.f, y - 26.f, 52.f, 52.f}, accent(pulse), 26.f, 3.f);
    }
    icon(r, "bell", kBellX, y, 30.f, kWhite);
    glassCircle(r, kMusicX, y, kCircle, on(TopButton::Music), "music", 34.f, time);
    if (st.musicPlaying) {
        for (int i = 0; i < 3; ++i) {
            const float hgt = 3.f + 5.f * (0.5f + 0.5f * std::sin(time * (5.f + i * 1.7f) + i));
            r.drawRect({kMusicX - 7.f + i * 5.5f, y + 25.f - hgt, 3.f, hgt}, kWhite.withAlpha(0.85f));
        }
    }
    glassCircle(r, kPowerX, y, kCircle, on(TopButton::Power), "power", 34.f, time);
    glassCircle(r, kGearX, y, kCircle, on(TopButton::Settings), "gear", 36.f, time);
}

} // namespace cards
