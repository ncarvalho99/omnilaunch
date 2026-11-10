#pragma once
// Cards HOME frontend (HomeNX D1a skeleton). A second nxui::Activity on top of
// HomeCore: it never initializes or finalizes a process service (C-SW-7).
//
// D1a scope: background, card row of installed applications with icons, the
// info panel (title, play time), launch/resume, HOME/daemon notifications and
// the menu-ready / first-frame / closing contract with the daemon. The top bar,
// sheets, toasts, sort, settings and the live switch follow in D1b+/D2.

#include "cards/CardsArt.hpp"
#include "cards/CardsArtSources.hpp"
#include "cards/CardsCatalog.hpp"
#include "cards/CardsChrome.hpp"
#include "cards/CardsMusicSheet.hpp"
#include "cards/CardsNewsSheet.hpp"
#include "cards/CardsSheet.hpp"
#include "discord/DiscordPanel.hpp"
#include "core/AudioManager.hpp"
#include "core/LeaveFrameCache.hpp"
#include "core/SystemMessages.hpp"
#include "homebrew/HomebrewUpdateCatalog.hpp"
#include "home/HomeStyle.hpp"
#include "widgets/LockScreen.hpp"
#include "widgets/QuickSettingsOverlay.hpp"
#include "settings/SteamGridDbPickerScreen.hpp"
#include "settings/TextEntryScreen.hpp"
#include <nxui/Activity.hpp>
#include <nxui/Theme.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Texture.hpp>
#include <atomic>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace home { class HomeCore; }
class OverlayDialog;
class ProgressDialog;

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
    void startLaunchHomebrew(const std::string& path);
    void resumeSuspended();
#ifdef SWITCHU_MENU
    // Card of the suspended software: the homebrew card when a .nro runs in a
    // borrowed host game, else the game itself; 0 when nothing is suspended.
    std::uint64_t runningCardId() const;
    bool isRunningCard(std::uint64_t id) const { return id != 0 && id == runningCardId(); }
    void confirmCloseSuspended();
    // E4: delete with confirmation, on the Core pool behind a progress dialog.
    void confirmDeleteSoftware();
    void startDeleteSoftware(std::uint64_t titleId, const std::string& title);
    void pollDeleteSoftware();
    bool deleteRunning() const;
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
    void openSettingsSheet();
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
    void drawLaunch(nxui::Renderer& ren);
    void initPerfLog(bool isSwitch, bool isColdBoot);
    void flushPerfLog();
    void perfLine(const std::string& line);
    float sinceStartMs() const;
    void samplePerfFrame();

    // HomeNX batch 1 (E1-E11): pages, move mode, sheets.
    void goToPage(int page, int dir);
    void drawPageDots(nxui::Renderer& ren, const nxui::Rect& panel);
    void drawPanelButton(nxui::Renderer& ren, float cy);
    void drawMoveHint(nxui::Renderer& ren);
    void openSystemTile(std::uint64_t id);
    void startMove();
    void finishMove(bool keep);
    void handleMoveInput();
    void saveConfig(const char* why);
    void openDotsMenu();
    void openUsersSheet();
    void openNotificationsSheet();
    void openAchievementsSheet(std::uint64_t titleId);
    void openQuickSettings();
    void closeQuickSettings();
    void openMusicSheet();
    void openNewsSheet();
    void openDiscord();
    void ensureDiscordPanel();
    void requestText(const std::string& title, const std::string& guide, const std::string& initial,
                     std::function<void(const std::string&)> onAccept);
    void renameSelected();
    void openArtSheet(std::uint64_t titleId);
    void openTitlePicker(std::uint64_t titleId, const std::string& title,
                         SteamGridDbManager::ArtworkKind kind, const std::string& query = "");
    void applyPickerCandidate(const SteamGridDbManager::BrowseResult& browse,
                              const SteamGridDbManager::Candidate& candidate);
    void downloadSingleTitleArt(std::uint64_t titleId, const std::string& title);
    void startSaveAll(bool forceAll);
    bool saveAllRunning() const;
    void activatePanelShortcut();
    void reorderSettingsSection(int delta);
    void maybeShowTutorial();
    void drawTutorial(nxui::Renderer& ren);
    bool overlayActive() const;
    std::vector<CardsSheet::Section> settingsSections();

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
    bool m_hekateQueued = false;        // power dialog -> Restart to Hekate (E5)
    // E4 delete. The worker writes only the atomics and the result fields,
    // which are read after the future is ready.
    std::uint64_t m_deleteConfirmId = 0;  // card options -> Delete software
    std::shared_ptr<ProgressDialog> m_deleteProgress;
    std::future<void> m_deleteFuture;
    std::atomic<std::uint64_t> m_deleteDone{0};
    std::atomic<std::uint64_t> m_deleteTotal{0};
    std::uint32_t m_deleteResult = 0;
    int m_deleteSdRemoved = 0;
    std::string m_deleteSdFailure;
    std::uint64_t m_deleteTitleId = 0;
    std::string m_deleteTitle;
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

    // Pages (E1): the shown page and the focus kept per page.
    int m_page = 0;
    std::map<int, std::uint64_t> m_pageFocus;
    float m_pageIn = 1.f;   // 0 -> 1 page slide
    int m_pageDir = 1;
    // Move mode (E2).
    bool m_moving = false;
    bool m_playtimeSortDeferred = false;
    int m_moveFrom = -1;
    std::vector<std::uint64_t> m_moveBackup;
    // Sheets (E3-E7, E11), quick settings (E6), text entry (E9 part), picker (G2).
    CardsSheet m_sheet;
    CardsMusicSheet m_musicSheet;
    CardsNewsSheet m_newsSheet;
    std::shared_ptr<discord::DiscordPanel> m_discordPanel;
    std::shared_ptr<QuickSettingsOverlay> m_quick;
    std::shared_ptr<TextEntryScreen> m_text;
    std::shared_ptr<SteamGridDbPickerScreen> m_picker;
    nxui::Theme m_overlayTheme = nxui::Theme::dark();
    nxui::Font m_fontIcons;
    // Welcome tutorial (E9, C-UI-1): 0 = hidden, else card 1..4.
    int m_tutorialCard = 0;
    float m_tutorialIn = 0.f;
    // Achievements newly earned while away, said once on arrival (E11).
    // An overlay was open this frame: its press must not reach the row.
    bool m_overlayLatch = false;
    // This activation was a fast return: no automatic welcome tour.
    bool m_tutorialBlocked = false;

    int m_selected = 0;
    float m_scroll = 0.f;
    float m_time = 0.f;
    float m_repeatTimer = 0.f;
    int m_repeatDir = 0;
    bool m_launching = false;
    // Seconds since a launch/resume command was sent. The launcher closes the
    // menu on success; if it did not (enqueue failure), input comes back.
    float m_launchElapsed = 0.f;
    // Launch animations (HomeNX D4, C-UI-17 partial).
    bool m_leaving = false;
    bool m_leaveIsResume = false;
    float m_leaveT = 0.f;
    float m_leaveDur = 0.45f;
    int m_leaveIndex = -1;
    std::uint64_t m_leaveTitleId = 0;
    AccountUid m_leaveUid = {};
    std::string m_leaveHomebrewPath;
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
    // G1: Internet settings "Check connection" (NetService), polled in onUpdate.
    std::future<std::string> m_netCheck;
    void startNetCheck();
    void pollNetCheck();

    // G4: connection test (C-NET-7) against the project server: ping,
    // download, upload. The worker writes the atomics; the sheet reads them.
    struct SpeedTest {
        std::atomic<int> stage{0};     ///< 0 idle, 1 ping, 2 down, 3 up, 4 done
        std::atomic<int> pingMs{-1};
        std::atomic<int> downKbps{-1};
        std::atomic<int> upKbps{-1};
        std::atomic<int> signal{-1};   ///< Wi-Fi bars 0-3, 4 wired, -1 unknown
        std::atomic<bool> online{false};
        std::atomic<bool> failed{false};
        std::atomic<bool> stop{false};
    };
    std::shared_ptr<SpeedTest> m_speed;
    std::future<void> m_speedJob;
    void openSpeedTest();
    void startSpeedTest();
    void stopSpeedTest(); ///< cancel and wait (onDestroy, quiesce)

    // G5: homebrew updates (C-NET-6, C-UI-13). One job at a time (check or
    // install), on the Core pool; results land in pollHomebrewUpdates().
public:
    struct HbPending {
        hbupdate::Installed app;
        hbupdate::Offer offer;
    };
private:
    struct HbJob;
    std::vector<HbPending> m_hbPending; ///< from the last job (main thread)
    std::shared_ptr<HbJob> m_hbJob;
    std::future<void> m_hbFuture;
    std::shared_ptr<const hbupdate::State> m_hbState; ///< last loaded or checked state (main thread)
    bool m_hbCheckedThisRun = false;
    bool m_hbSheetWanted = false; ///< reopen the updates sheet when the job lands
    bool m_hbReturnActivation = false; ///< this activation is a return from a game/applet
    void checkHomebrewUpdates(bool force, bool openSheet);
    void openHomebrewUpdates();
    void installHomebrewUpdates(std::vector<std::string> paths);
    void pollHomebrewUpdates();
    void stopHomebrewUpdates(); ///< cancel and wait (onDestroy, quiesce)
    int homebrewUpdateCount() const;
    bool homebrewHasUpdate(const std::string& path, const std::string& version) const;

    // G2: Art sources (GameTDB, nlib, SteamGridDB picker and Save All).
    std::future<SteamGridDbManager::BrowseResult> m_browseFuture;
    std::future<bool> m_applyFuture;
    std::future<int> m_saveAllFuture;
    std::future<CardsArtSources::SingleTitleResult> m_singleArtFuture;
    std::atomic<int> m_saveAllDone{0};
    std::atomic<int> m_saveAllTotal{0};
    std::atomic<int> m_saveAllSaved{0};
    std::atomic<bool> m_saveAllStop{false};
    void pollArtJobs();
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

    // Performance log (HomeNX D5, C-PF-6).
    uint64_t m_perfCreateTick = 0;
    uint64_t m_perfLastFrameTick = 0;
    std::string m_perfOut;
    std::mutex m_perfMutex;
    float m_perfWindow = 0.f;
    int m_perfFrames = 0;
    int m_perfSlow = 0;
    int m_perfVerySlow = 0;
    float m_perfWorst = 0.f;
    bool m_perfFirstFrameLogged = false;
};

} // namespace cards
