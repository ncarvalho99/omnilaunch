#pragma once
#include "social/NotificationLog.hpp"

#include <nxui/Theme.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/widgets/Widget.hpp>

#include <functional>
#include <string>
#include <vector>

namespace nxui { class Input; class Renderer; }

// HomeNX's notification centre: a card that drops down from the bell with what
// HOME and the Discord overlay told you lately, newest first (new ones dotted).
// A opens one (a Discord message opens its chat), X clears them, B closes.
class NotificationPanel : public nxui::Widget {
public:
    struct Callbacks {
        std::function<void()> onClosed;
        std::function<void(const notifications::Item&)> onOpen;
        std::function<void()> onNavigate;  // sounds
        std::function<void()> onCleared;
    };

    NotificationPanel();
    void setFonts(nxui::Font* font, nxui::Font* small, nxui::Font* icons = nullptr) {
        m_font = font;
        m_small = small;
        m_icons = icons;
    }
    // The bell it drops down from: the card is centred under this point.
    void setAnchor(float centerX, float bottomY) { m_anchorX = centerX; m_anchorY = bottomY; }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void setCallbacks(Callbacks cb) { m_cb = std::move(cb); }

    void show(); // reads the log and marks it seen
    void hide();
    bool isActive() const { return m_active || m_anim > 0.001f; }
    bool isOpen() const { return m_active; }
    void handleTouch(const nxui::Input& in);

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    nxui::Rect cardRect() const;
    int visibleRows() const;
    void move(int delta);
    void openSelected();
    void clearAll();

    nxui::Font* m_font = nullptr;
    nxui::Font* m_small = nullptr;
    nxui::Font* m_icons = nullptr;    // button glyphs for the hint capsules
    float m_anchorX = -1.f;           // < 0: the old top-left spot
    float m_anchorY = 0.f;
    bool m_backdropValid = false;     // blurred copy of the screen, taken once per open
    const nxui::Theme* m_theme = nullptr;
    Callbacks m_cb;
    std::vector<notifications::Item> m_items;
    std::uint64_t m_seenBefore = 0; // newer than this: new
    int m_sel = 0;
    int m_top = 0;                  // first row shown
    float m_anim = 0.f;
    bool m_active = false;
};
