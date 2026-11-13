#pragma once
// A drawn glass sheet for the Cards HOME (HomeNX E3/E4/E5/E7/E11): a title,
// optional section tabs (L/R), and rows with a label, a live value, an
// optional progress bar and actions (A, Left/Right). Drawn without a backdrop
// capture or blur (those belong to E8), so it adds no offscreen GPU work.
// Owned and driven by CardsHomeActivity; no widget-tree or focus coupling.

#include <nxui/core/Types.hpp>
#include <functional>
#include <string>
#include <vector>

namespace nxui { class Input; class Renderer; class Font; }

namespace cards {

class CardsChrome;

class CardsSheet {
public:
    struct Row {
        std::string label;
        std::function<std::string()> value;   // drawn right-aligned, re-read each frame
        std::function<void()> onA;            // A on the row
        std::function<void(int)> onChange;    // Left/Right (-1 / +1)
        std::function<float()> progress;      // 0..1 bar under the label; empty = none
        std::string detail;                   // second line, dim
        bool enabled = true;
        std::function<bool()> toggle;         // set: drawn as an on/off switch (HomeNX), not a value
    };
    struct Section {
        std::string title;
        std::vector<Row> rows;
        std::string icon;                         // CardsChrome icon name (two-column settings)
        nxui::Color iconColor{1.f, 1.f, 1.f, 1.f};
    };

    /// Opens (or replaces) the sheet. `section`/`row` choose the start focus.
    void open(std::string title, std::vector<Section> sections, int section = 0, int row = 0);
    void close();
    bool isOpen() const { return m_open; }
    /// Called when B closes the sheet.
    void onClosed(std::function<void()> cb) { m_onClosed = std::move(cb); }
    /// Sfx hooks.
    void onNavigate(std::function<void()> cb) { m_nav = std::move(cb); }
    void onActivate(std::function<void()> cb) { m_act = std::move(cb); }

    int section() const { return m_section; }
    int row() const { return m_row; }
    const std::string& title() const { return m_title; }

    /// Main thread. Returns true when the sheet consumed the frame's input.
    bool handleInput(nxui::Input& in, float dt);
    void onReorderSection(std::function<void(int)> cb) { m_reorderSection = std::move(cb); }
    void update(float dt);
    void render(nxui::Renderer& ren, CardsChrome& chrome) const;

private:
    int visibleRows() const;
    void clampFocus();
    void moveRow(int dir);

    std::string m_title;
    std::vector<Section> m_sections;
    int m_section = 0;
    int m_row = 0;
    int m_scroll = 0;
    bool m_open = false;
    bool m_focusInContent = false;
    float m_in = 0.f;          // 0 -> 1 open animation
    mutable bool m_backdropCacheValid = false; // C-PF-3 cached backdrop
    float m_repeatTimer = 0.f;
    int m_repeatDir = 0;
    std::function<void()> m_onClosed, m_nav, m_act;
    std::function<void(int)> m_reorderSection;
};

} // namespace cards
