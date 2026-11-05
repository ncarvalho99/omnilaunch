#pragma once
// Cards HOME frontend (HomeNX D1a skeleton). A second nxui::Activity on top of
// HomeCore: it never initializes or finalizes a process service (C-SW-7).
//
// D1a scope: background, card row of installed applications with icons, the
// info panel (title, play time), launch/resume, HOME/daemon notifications and
// the menu-ready / first-frame / closing contract with the daemon. The top bar,
// sheets, toasts, sort, settings and the live switch follow in D1b+/D2.

#include "cards/CardsArt.hpp"
#include "cards/CardsCatalog.hpp"
#include "cards/CardsChrome.hpp"
#include "core/AudioManager.hpp"
#include "core/LeaveFrameCache.hpp"
#include "core/SystemMessages.hpp"
#include "home/HomeStyle.hpp"
#include "widgets/LockScreen.hpp"
#include <nxui/Activity.hpp>
#include <nxui/Theme.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Texture.hpp>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
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
    bool presentInitialFrame(nxui::Renderer& ren) override;
    void onAfterPresent(nxui::Renderer& ren) override;
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
    void updateStatus(float dt);
    void loadAvatar();
    void activateTop();
    void openCardOptions();
    void openSortSheet();
    void applySort(int mode);
    void updateDetails(float dt);
    void updateLock(float dt);
    void ensureClockFont();
    void updateFrontendSwitch(float dt);
    std::string lastPlayedText(std::uint64_t at) const;
    void showToast(const std::string& text);
    void persistLastFocus();
    void finishAudioLoad();
    void pollPlaytime();
    void scheduleLeaveCapture(std::function<void()> afterCapture,
                              std::uint64_t previewSuspendedTitleId = 0);
    bool saveLeaveFrame(nxui::Renderer& ren);
    void updateLeaveSplashHandoff(float dt);
    bool leaveSplashActive() const { return m_leaveSplashPhase != LeaveSplashPhase::None; }

    home::HomeCore& m_core;
    AudioManager& m_audio;
    CardsCatalog m_catalog;
    SystemMessages m_sysMsg;

    nxui::Font m_font;
    nxui::Font m_fontSmall;
    nxui::Font m_fontTitle;
    CardsChrome m_chrome;
    CardsArt m_art;
    StatusSnapshot m_status;
    std::unique_ptr<nxui::Texture> m_avatar;
    float m_statusTimer = 0.f;
    // Focus zone: the card row, the top bar (UP from the row) or the panel
    // (DOWN from the row: 0 = Play/Resume pill, 1 = "..." options circle).
    bool m_topZone = false;
    int m_topIndex = 0;
    bool m_panelZone = false;
    int m_panelIndex = 0;
    bool m_closeConfirmQueued = false;  // card options -> Close software
    // Live frontend switch (D2): queued by the style sheet, then a short fade
    // to black before home::switchFrontend.
    bool m_switchQueued = false;
    home::HomeStyle m_switchTarget = home::HomeStyle::Classic;
    float m_switchFade = -1.f;  // < 0: not fading
    // Arrived by a switch: ignore buttons until everything has been released,
    // so the press that confirmed the switch does nothing here.
    bool m_waitRelease = false;
    float m_fadeIn = 1.f;  // 1 -> 0 over the first frames after a switch
    // Background cross-fade.
    std::uint64_t m_bgId = 0;
    std::uint64_t m_bgPrevId = 0;
    float m_bgChangedAt = 0.f;
    // Short message under the top bar.
    std::string m_toast;
    float m_toastUntil = 0.f;
    float m_detailChangedAt = 0.f;
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
    bool m_playtimeQueued = true;
    int m_refreshDelayFrames = 0;

    // Wake lock screen: Classic's widget (three presses of one button), raised
    // on sleep and wake. Replaces the D1a wake guard.
    LockScreen m_lock;
    nxui::Theme m_lockTheme = nxui::Theme::dark();
    nxui::Font m_fontClock;
    bool m_lockLowPowerPrimed = false;
    float m_sleepPlanTimer = 0.f;

    // Panel details of the selected title, read off the main thread: version
    // from the control cache, last played from pdm.
    struct Details {
        std::uint64_t titleId = 0;
        std::string version;
        std::optional<std::uint64_t> lastPlayed;
    };
    std::shared_ptr<Details> m_detailsPending;
    std::future<void> m_detailsFuture;
    Details m_details;
    std::uint64_t m_detailsWanted = 0;
    float m_detailsDelay = 0.f;

    // Leave-frame splash on fast return (C-SW-6).
    enum class LeaveSplashPhase {
        None,
        Hold,
        Fade,
    };
    static constexpr float kLeaveSplashHoldDur = 0.18f;
    static constexpr float kLeaveSplashFadeDur = 0.16f;

    nxui::Texture m_leaveSplashTex;
    LeaveFrameSession m_leaveSession{};
    LeaveSplashPhase m_leaveSplashPhase = LeaveSplashPhase::None;
    float m_leaveSplashHoldRemaining = 0.f;
    float m_leaveSplashFade = 0.f;
    bool m_leaveSplashDrawn = false;
    bool m_fastReturn = false;
    uint64_t m_fastReturnStartupTick = 0;
    int m_deferredBluetoothInitFrames = 0;

    bool m_leaveCapturePending = false;
    std::function<void()> m_leaveCaptureAfter;
    std::uint64_t m_leavePreviewSuspendedTitleId = 0;
};

} // namespace cards
