#pragma once
// Drawing of the Cards HOME chrome (HomeNX D1b): background art, top bar with
// glass circles and the status pill, card faces and the info panel. Look
// adapted from upstream/homenx @ 0448cd4, projects/cards/src/CardsActivity.cpp
// (drawCover 261-272, glassCircle 7876-7886, drawBackground 7888-7919,
// drawTopBar 7921-8030, drawCards 8070-8144, drawPanel 8421-8528), with
// attribution in integration/provenance/homenx-intake.md. Pure drawing: no
// input, no state beyond loaded icon textures.

#include <nxui/core/Font.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Types.hpp>
#include <map>
#include <memory>
#include <string>

namespace nxui { class GpuDevice; class Renderer; }

namespace cards {

struct StatusSnapshot {
    std::string clock;        // "16:25"
    int batteryPercent = -1;  // -1 unknown
    bool charging = false;
    float socCelsius = -1.f;  // -1 unknown
    std::string freeSpace;    // "13.3 GB", empty = hidden
    const nxui::Texture* avatar = nullptr;
    int nextendoPlayers = -2; // -2 disabled/hidden, -1 counting, >=0 online players
    bool musicPlaying = false;
};

enum class TopButton { Menu, Discord, Sort, User, Nextendo, Bell, Music, Power, Settings, Count };

class CardsChrome {
public:
    bool load(nxui::GpuDevice& gpu, nxui::Renderer& ren, const std::string& assetsRoot);
    void release();

    nxui::Font& pillFont() { return m_pill; }
    nxui::Font& titleFont() { return m_title; }
    nxui::Font& labelFont() { return m_label; }
    nxui::Font& smallFont() { return m_small; }
    nxui::Font& iconFont() { return m_glyphs; }

    /// Art fill (crop to cover), as upstream drawCover.
    static void drawCover(nxui::Renderer& r, const nxui::Texture* tex, const nxui::Rect& dst, float radius,
                          const nxui::Color& tint, float zoom = 1.f, float panX = 0.f, float panY = 0.f);

    void drawBackground(nxui::Renderer& r, const nxui::Texture* current, float currentAlpha,
                        const nxui::Texture* previous, float time, int hazePercent = 45, bool bgMotion = true);
    void drawTopBar(nxui::Renderer& r, const StatusSnapshot& status, TopButton focused, bool topZone, float time);
    void glassCircle(nxui::Renderer& r, float cx, float cy, float d, bool focused, const char* icon,
                     float iconSize, float time);
    void icon(nxui::Renderer& r, const char* name, float cx, float cy, float size, const nxui::Color& tint);
    void textMid(nxui::Renderer& r, nxui::Font& f, const std::string& s, float x, float cy, const nxui::Color& c);
    std::string fit(nxui::Font& f, const std::string& s, float maxWidth);

    struct AccentDef {
        const char* name;
        nxui::Color color;
    };
    static constexpr int kAccentCount = 7;
    static const AccentDef kAccents[kAccentCount];
    static const char* const kGlassNames[3];

    static void setStyle(int accentIndex, int glassStyle);
    static int currentAccent();
    static int currentGlass();
    static const nxui::Color kWhite;
    static const nxui::Color kGlassEdge;
    static const nxui::Color kGreen;
    static nxui::Color glassTint(float a = 1.f);
    static nxui::Color accent(float a = 1.f);

private:
    nxui::Font m_pill;
    nxui::Font m_title;
    nxui::Font m_label;
    nxui::Font m_small;
    nxui::Font m_glyphs;
    std::map<std::string, std::unique_ptr<nxui::Texture>> m_icons;
    nxui::GpuDevice* m_gpu = nullptr;
    nxui::Renderer* m_ren = nullptr;
    std::string m_iconRoot;
};

} // namespace cards
