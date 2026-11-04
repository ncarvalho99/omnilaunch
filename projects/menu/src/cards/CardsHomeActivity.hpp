#pragma once
// Cards HOME frontend (HomeNX D1a skeleton). A second nxui::Activity on top of
// HomeCore: it never initializes or finalizes a process service (C-SW-7).
//
// D1a scope: background, card row of installed applications with icons, the
// info panel (title, play time), launch/resume, HOME/daemon notifications and
// the menu-ready / first-frame / closing contract with the daemon. The top bar,
// sheets, toasts, sort, settings and the live switch follow in D1b+/D2.

#include "cards/CardsCatalog.hpp"
#include "core/AudioManager.hpp"
#include "core/SystemMessages.hpp"
#include <nxui/Activity.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Texture.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace home { class HomeCore; }
class OverlayDialog;

namespace cards {

class CardsHomeActivity : public nxui::Activity {
public:
    explicit CardsHomeActivity(home::HomeCore& core);
    ~CardsHomeActivity() override;

    bool onCreate() override;
    void onDestroy() override;
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;
    nxui::Widget* focusRoot() override;

private:
    void handleInput();
    void pollDaemonNotifications();
#ifdef SWITCHU_MENU
    void handleSystemAction(SysAction action);
#endif
    void moveSelection(int delta);
    void activateSelected();
    void launchTitle(const CardEntry& entry);
    void startLaunch(std::uint64_t titleId, AccountUid uid);
    void resumeSuspended();
#ifdef SWITCHU_MENU
    void confirmCloseSuspended();
#endif
    void refreshCatalogue();
    void ensureVisibleIcons();
    void drawCard(nxui::Renderer& ren, int index, const nxui::Rect& r, float chosen);
    void drawPanel(nxui::Renderer& ren);
    void persistLastFocus();
    void finishAudioLoad();
    void pollPlaytime();

    home::HomeCore& m_core;
    AudioManager& m_audio;
    CardsCatalog m_catalog;
    SystemMessages m_sysMsg;

    nxui::Font m_font;
    nxui::Font m_fontSmall;
    nxui::Font m_fontTitle;
    std::shared_ptr<OverlayDialog> m_userSelect;

    int m_selected = 0;
    float m_scroll = 0.f;
    float m_time = 0.f;
    float m_repeatTimer = 0.f;
    int m_repeatDir = 0;
    bool m_launching = false;
    // Seconds since a launch/resume command was sent. The launcher closes the
    // menu on success; if it did not (enqueue failure), input comes back.
    float m_launchElapsed = 0.f;
    bool m_refreshQueued = false;
    bool m_audioStarted = false;
    bool m_wakeGuard = false;
    bool m_playtimeQueued = true;
    int m_refreshDelayFrames = 0;
};

} // namespace cards
