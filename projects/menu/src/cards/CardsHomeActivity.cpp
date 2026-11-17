#include "cards/CardsHomeActivity.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include "cards/CardsLayout.hpp"
#include "cards/CardsAchievements.hpp"
#include "cards/CardsPages.hpp"
#include "core/ButtonGlyph.hpp"
#include "core/DebugLog.hpp"
#include "core/FrameProbe.hpp"
#include "core/LeaveFrameCache.hpp"
#include "net/NetService.hpp"
#include "core/PlayTime.hpp"
#include "home/HomeCore.hpp"
#include "home/HomeFrontend.hpp"
#include "core/ThemePreset.hpp"
#include "themeshop/ThemeHttp.hpp"
#include "widgets/OverlayDialog.hpp"
#include "widgets/ProgressDialog.hpp"
#include "launcher/HekateReboot.hpp"
#include "widgets/LockPassword.hpp"
#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
#include <switchu/control_cache.hpp>
#include <switchu/sd_commit.hpp>
#include <switchu/smi_protocol.hpp>
#endif
#include <nxui/Application.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>
#include <switch.h>
#include <switchu/hardware_status.hpp>
#include <sys/statvfs.h>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace cards {

namespace {

#ifdef SWITCHU_HOMEBREW
constexpr const char* kAssets = "romfs:";
#else
constexpr const char* kAssets = "sdmc:/switch/OmniLaunch";
#endif

constexpr float kRepeatDelay = 0.35f;
constexpr float kRepeatRate = 0.09f;
constexpr float kSwitchFadeOut = 0.18f;
constexpr float kSwitchFadeIn = 0.25f;


#ifdef SWITCHU_MENU
// Same 32-hex-digit form Classic stores (WiiUMenuApp.cpp hexToAccountUid).
bool parseAccountUid(const std::string& s, AccountUid& out) {
    if (s.size() != 32)
        return false;
    for (int half = 0; half < 2; ++half) {
        u64 v = 0;
        for (int i = 0; i < 16; ++i) {
            const char ch = s[static_cast<std::size_t>(half * 16 + i)];
            int n = -1;
            if (ch >= '0' && ch <= '9') n = ch - '0';
            else if (ch >= 'a' && ch <= 'f') n = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') n = ch - 'A' + 10;
            if (n < 0)
                return false;
            v = (v << 4) | static_cast<u64>(n);
        }
        out.uid[half] = v;
    }
    return accountUidIsValid(&out);
}
#endif

inline nxui::Rect lerpRect(const nxui::Rect& a, const nxui::Rect& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.width + (b.width - a.width) * t,
        a.height + (b.height - a.height) * t,
    };
}

} // namespace

CardsHomeActivity::CardsHomeActivity(home::HomeCore& core)
    : m_core(core), m_audio(core.audioServices().audio) {
#ifdef SWITCHU_MENU
    const bool isSwitch = core.frontendSwitchActivation();
    const bool isColdBoot = (core.transitionReason() == switchu::smi::MenuTransitionReason::StartupBoot ||
                             core.transitionReason() == switchu::smi::MenuTransitionReason::Unknown ||
                             core.startMode() == switchu::smi::MenuStartMode::StartupBoot);
    m_fastReturn = !isSwitch && (!isColdBoot || core.launcher().suspendedTitleId() != 0);
#endif
}

CardsHomeActivity::~CardsHomeActivity() = default;

bool CardsHomeActivity::presentInitialFrame(nxui::Renderer& ren) {
#ifdef SWITCHU_MENU
    if (!m_fastReturn)
        return false;

    LeaveFrameImage image;
    if (LeaveFrameCache::load(m_leaveSession, image) && image.valid()
        && m_leaveSplashTex.loadFromPixels(app().gpu(), ren,
                                           image.rgba.data(), image.width, image.height)) {
        ren.drawTexture(&m_leaveSplashTex, {0.f, 0.f, 1280.f, 720.f});
        m_leaveSplashDrawn = true;
        m_leaveSplashPhase = LeaveSplashPhase::Hold;
        m_leaveSplashHoldRemaining = kLeaveSplashHoldDur;
        m_leaveSplashFade = 1.f;
        DebugLog::log("[cards] splash %dx%d page=%d focus=%016llX",
                      image.width, image.height, m_leaveSession.page,
                      static_cast<unsigned long long>(m_leaveSession.focusTitleId));
        return true;
    }

    // No cached frame — still avoid pure black while onCreate runs.
    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, nxui::Color(0.07f, 0.12f, 0.18f, 1.f));
    m_leaveSplashDrawn = true;
    m_leaveSplashPhase = LeaveSplashPhase::None;
    m_leaveSplashFade = 0.f;
    DebugLog::log("[cards] splash fallback solid");
    return true;
#else
    (void)ren;
    return false;
#endif
}

void CardsHomeActivity::onAfterPresent(nxui::Renderer& ren) {
#ifdef SWITCHU_MENU
    if (!m_leaveCapturePending)
        return;

    m_leaveCapturePending = false;
    saveLeaveFrame(ren);
    m_leavePreviewSuspendedTitleId = 0;
    if (m_leaveCaptureAfter) {
        auto after = std::move(m_leaveCaptureAfter);
        m_leaveCaptureAfter = {};
        after();
    }
#else
    (void)ren;
#endif
}

void CardsHomeActivity::scheduleLeaveCapture(std::function<void()> afterCapture,
                                             std::uint64_t previewSuspendedTitleId) {
#ifdef SWITCHU_MENU
    if (previewSuspendedTitleId != 0)
        m_leavePreviewSuspendedTitleId = previewSuspendedTitleId;

    if (m_leaveCapturePending) {
        if (!afterCapture)
            return;
        if (m_leaveCaptureAfter) {
            auto prev = std::move(m_leaveCaptureAfter);
            m_leaveCaptureAfter = [prev = std::move(prev),
                                   next = std::move(afterCapture)]() mutable {
                if (prev) prev();
                if (next) next();
            };
        } else {
            m_leaveCaptureAfter = std::move(afterCapture);
        }
        return;
    }
    m_leaveCapturePending = true;
    m_leaveCaptureAfter = std::move(afterCapture);
#else
    (void)previewSuspendedTitleId;
    if (afterCapture)
        afterCapture();
#endif
}

bool CardsHomeActivity::saveLeaveFrame(nxui::Renderer& ren) {
#ifdef SWITCHU_MENU
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
    if (!ren.downloadFramebufferRgba(rgba, width, height, false)) {
        DebugLog::log("[cards] framebuffer download failed");
        return false;
    }

    LeaveFrameSession session;
    session.valid = true;
    // Cards page index; the next fast return opens on it (Nintendo page applets).
    session.page = m_page;
    session.openFolderId = 0;
    session.focusTitleId = (m_selected >= 0 && m_selected < m_catalog.count())
        ? m_catalog.at(m_selected).app.titleId : 0;

    if (!LeaveFrameCache::save(session, rgba.data(), width, height)) {
        DebugLog::log("[cards] leave cache write failed");
        return false;
    }

    DebugLog::log("[cards] leave saved %dx%d focus=%016llX",
                  width, height, static_cast<unsigned long long>(session.focusTitleId));
    return true;
#else
    (void)ren;
    return false;
#endif
}

void CardsHomeActivity::updateLeaveSplashHandoff(float dt) {
    if (m_leaveSplashPhase == LeaveSplashPhase::Hold) {
        m_leaveSplashHoldRemaining -= dt;
        if (m_leaveSplashHoldRemaining <= 0.f) {
            m_leaveSplashHoldRemaining = 0.f;
            m_leaveSplashPhase = LeaveSplashPhase::Fade;
            DebugLog::log("[cards] splash crossfade begin");
        }
        return;
    }

    if (m_leaveSplashPhase == LeaveSplashPhase::Fade) {
        m_leaveSplashFade = std::max(0.f, m_leaveSplashFade - dt / kLeaveSplashFadeDur);
        if (m_leaveSplashFade <= 0.f) {
            m_leaveSplashFade = 0.f;
            m_leaveSplashPhase = LeaveSplashPhase::None;
            m_leaveSplashTex = nxui::Texture();
            DebugLog::log("[cards] splash handoff complete");
        }
    }
}

float CardsHomeActivity::sinceStartMs() const {
#ifdef SWITCHU_MENU
    return static_cast<float>(armTicksToNs(armGetSystemTick() - m_perfCreateTick) / 1000000ULL);
#else
    return m_time * 1000.f;
#endif
}

void CardsHomeActivity::perfLine(const std::string& line) {
    if (!m_core.config().cards.perfLog)
        return; // Settings > About > Performance log (C-PF-6 toggle)
    char head[32];
    std::snprintf(head, sizeof(head), "[%8.1f s] ", sinceStartMs() / 1000.f);
    std::lock_guard<std::mutex> lk(m_perfMutex);
    m_perfOut += head;
    m_perfOut += line;
    m_perfOut += "\n";
}

void CardsHomeActivity::flushPerfLog() {
    // A pending write never makes a frame wait. Teardown drains the pool.
    if (m_perfWrite.valid()) {
        if (m_perfWrite.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        try { m_perfWrite.get(); } catch (...) {}
    }
    std::string out;
    {
        std::lock_guard<std::mutex> lk(m_perfMutex);
        if (m_perfOut.empty())
            return;
        out.swap(m_perfOut);
    }
#ifdef SWITCHU_MENU
    m_perfWrite = m_core.threadPool().submit([out = std::move(out)] {
        std::error_code ec;
        std::filesystem::create_directories("sdmc:/config/OmniLaunch", ec);
        std::ofstream f("sdmc:/config/OmniLaunch/perf.log", std::ios::app);
        if (f) f << out;
    });
#else
    (void)out;
#endif
}

void CardsHomeActivity::initPerfLog(bool isSwitch, bool isColdBoot) {
#ifdef SWITCHU_MENU
    m_perfCreateTick = armGetSystemTick();
    m_perfLastFrameTick = 0;
    m_perfFirstFrameLogged = false;

    std::error_code ec;
    if (std::filesystem::file_size("sdmc:/config/OmniLaunch/perf.log", ec) > (512u << 10) && !ec) {
        std::filesystem::remove("sdmc:/config/OmniLaunch/perf.old.log", ec);
        std::filesystem::rename("sdmc:/config/OmniLaunch/perf.log", "sdmc:/config/OmniLaunch/perf.old.log", ec);
    }

    const char* kind = isSwitch ? "(HOME, frontend switch)"
                                : (isColdBoot ? "(HOME, console start)"
                                              : "(HOME, back from game or applet)");
    char head[128];
    std::snprintf(head, sizeof(head), "=== OmniLaunch Cards %s %s ===", SWITCHU_VERSION, kind);
    perfLine(head);

    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    std::snprintf(head, sizeof(head), "memory: %llu MB total, %llu MB used, %llu MB image budget",
                  static_cast<unsigned long long>(total >> 20),
                  static_cast<unsigned long long>(used >> 20),
                  static_cast<unsigned long long>(app().gpu().imageMemoryBudget() >> 20));
    perfLine(head);

    if (m_core.launcher().suspendedTitleId() != 0) {
        std::snprintf(head, sizeof(head), "suspended title: 0x%016llX",
                      static_cast<unsigned long long>(m_core.launcher().suspendedTitleId()));
        perfLine(head);
    }
#else
    (void)isSwitch;
    (void)isColdBoot;
#endif
}

void CardsHomeActivity::samplePerfFrame() {
#ifdef SWITCHU_MENU
    const uint64_t frameTick = armGetSystemTick();
    if (!m_perfFirstFrameLogged) {
        m_perfFirstFrameLogged = true;
        const auto firstFrameMs = static_cast<unsigned long>(
            armTicksToNs(frameTick - m_perfCreateTick) / 1000000ULL);
        char line[64];
        std::snprintf(line, sizeof(line), "first frame: %lu ms", firstFrameMs);
        perfLine(line);
        m_perfLastFrameTick = frameTick;
        return;
    }

    // Lock screen, low-power (render disabled) and sleep are not frames: the
    // first frame after them would otherwise report the whole idle gap as one
    // slow frame (D5 console run: "slow frame: 12810.0 ms" across a lock).
    if (m_lock.isLocked() || !app().renderEnabled()) {
        m_perfLastFrameTick = 0;
        return;
    }

    if (m_perfLastFrameTick != 0) {
        const float interval = static_cast<float>(
            armTicksToNs(frameTick - m_perfLastFrameTick) / 1000000ULL);
        m_perfFrames++;
        m_perfWindow += interval;
        if (interval > m_perfWorst)
            m_perfWorst = interval;
        if (interval > 20.f)
            m_perfSlow++;
        if (interval > 33.3f) {
            m_perfVerySlow++;
            char line[128];
            std::snprintf(line, sizeof(line), "slow frame: %.1f ms (budget 16.6 ms)", interval);
            perfLine(line);
        }

        // 10 second rolling summary window
        if (m_perfWindow >= 10000.f) {
            const float fps = m_perfWindow > 0.f ? (m_perfFrames * 1000.f / m_perfWindow) : 60.f;
            char line[160];
            std::snprintf(line, sizeof(line),
                          "10 s: %.1f fps, worst frame %.1f ms, %d over 20 ms, %d over 33 ms",
                          fps, m_perfWorst, m_perfSlow, m_perfVerySlow);
            perfLine(line);
            m_perfFrames = 0;
            m_perfSlow = 0;
            m_perfVerySlow = 0;
            m_perfWorst = 0.f;
            m_perfWindow = 0.f;
            flushPerfLog();
        }
    }
    m_perfLastFrameTick = frameTick;
#endif
}

bool CardsHomeActivity::onCreate() {
    DebugLog::log("[cards] activity create");
    const bool isSwitch = m_core.frontendSwitchActivation();
    if (isSwitch) {
        // Reached by a live switch (D2): fade in, and wait for the buttons
        // that confirmed the switch to be released before reacting.
        m_core.frontendSwitchConsumed();
        m_waitRelease = true;
        m_fadeIn = 1.f;
        m_fastReturn = false;
        DebugLog::log("[cards] activated by frontend switch");
    } else {
        m_fadeIn = 0.f;
    }
    m_core.setFrontendQuiesce([this] {
        if (m_achievementsCancel) m_achievementsCancel->store(true);
        stopSpeedTest();
        stopHomebrewUpdates();
        waitSelfUpdate();
        stopBrowserDownload();
        m_saveAllStop.store(true);
        if (m_saveAllFuture.valid())
            m_saveAllFuture.wait();
        if (m_singleArtFuture.valid())
            m_singleArtFuture.wait();
        if (m_applyFuture.valid())
            m_applyFuture.wait();
        if (m_browseFuture.valid())
            m_browseFuture.wait();
        if (m_picker)
            m_picker->wait();
        m_catalog.releaseAll();
        m_art.releaseAll();
        waitWallpaper();
        if (m_perfWrite.valid()) { try { m_perfWrite.get(); } catch (...) {} }
    });
    m_core.frontendStarted();
    const uint64_t createStartTick = armGetSystemTick();

#ifdef SWITCHU_MENU
    const bool isColdBoot = (m_core.transitionReason() == switchu::smi::MenuTransitionReason::StartupBoot ||
                             m_core.transitionReason() == switchu::smi::MenuTransitionReason::Unknown ||
                             m_core.startMode() == switchu::smi::MenuStartMode::StartupBoot);
    m_fastReturn = !isSwitch && (!isColdBoot || m_core.launcher().suspendedTitleId() != 0);
    m_hbReturnActivation = m_fastReturn; // kept: m_fastReturn clears after the first frame
    m_tutorialBlocked = m_fastReturn; // first-frame code clears m_fastReturn
    m_fastReturnStartupTick = m_fastReturn ? createStartTick : 0;
    initPerfLog(isSwitch, isColdBoot);
    DebugLog::log("[cards] return mode: fastReturn=%d (startMode=%u reason=%u switch=%d suspended=0x%016llX)",
                  m_fastReturn ? 1 : 0,
                  static_cast<unsigned>(m_core.startMode()),
                  static_cast<unsigned>(m_core.transitionReason()),
                  isSwitch ? 1 : 0,
                  static_cast<unsigned long long>(m_core.launcher().suspendedTitleId()));
#endif

    auto& config = m_core.config();
#ifdef SWITCHU_MENU
    if (m_core.safeModeRequested()) {
        // Same in-memory recovery restrictions as Classic; config.json keeps
        // the player's choices.
        config.enterSafeMode();
        DebugLog::log("[safe-mode] active (cards): built-in sounds, no music");
    }
#endif
    auto& i18n = nxui::I18n::instance();
    i18n.initialize(std::string(kAssets) + "/i18n", "en-US");
    if (config.uiLanguageOverride == "auto" || config.uiLanguageOverride.empty())
        i18n.setLanguageAuto();
    else
        i18n.setLanguage(config.uiLanguageOverride);
    m_core.audioServices().configureAccessibility(config, i18n.activeLanguageTag());
    // Same process-lifetime music/SFX as Classic: started once, kept across
    // frontends (HomeAudio::startClassic is idempotent). Booting straight into
    // Cards resolves the theme's own songs first, as Classic's theme apply
    // does, or Cards played the preset music and Classic then swapped it.
    if (!m_core.audioServices().started)
        resolveThemeMusic();
    m_core.audioServices().startClassic(m_core.threadPool(), config, {});

    auto& gpu = app().gpu();
    auto& ren = app().renderer();
    const std::string fontPath = std::string(kAssets) + "/fonts/DejaVuSans.ttf";
    if (!m_font.load(gpu, ren, fontPath, 24) || !m_fontSmall.load(gpu, ren, fontPath, 18) ||
        !m_fontTitle.load(gpu, ren, fontPath, 30)) {
        // Not fatal: a failed onCreate stops the run loop and the daemon would
        // relaunch into the same failure. Text is simply not drawn.
        DebugLog::log("[cards] font load failed: %s", fontPath.c_str());
    }

    m_chrome.load(gpu, ren, kAssets);
    CardsChrome::setStyle(m_core.config().cards.accent, m_core.config().cards.glass);
    loadAvatar();
    updateStatus(1.f);

    m_userSelect = std::make_shared<OverlayDialog>();
    m_userSelect->setFont(&m_font);
    m_userSelect->setSmallFont(&m_fontSmall);
    m_userSelect->setRect({0, 0, 1280, 720});
    m_userSelect->onNavigateSfx([this] { m_audio.playSfx(Sfx::Navigate); });
    m_userSelect->onActivateSfx([this] { m_audio.playSfx(Sfx::Activate); });
    m_userSelect->onCloseSfx([this] { m_audio.playSfx(Sfx::ModalHide); });
    // Kept out of rootBox: the tree renders before onRender, and the dialog
    // must sit above the card row. Updated and drawn by this activity.

    // Lock screen (Classic's widget). The clock font loads on first lock.
    m_lock.setFonts(&m_fontClock, &m_fontSmall);
    m_lock.setTheme(&m_lockTheme);
    m_lock.setUse12HourClock(m_core.config().clockUse12Hour);
    lockpw::attach(m_lock); // E9 password (shared with Classic)
    m_lock.onSleepRequested([this]() {
        if (m_userSelect)
            m_userSelect->hide();
#ifdef SWITCHU_MENU
        m_core.enterSleep();
#endif
    });
    m_lock.onLocked([this]() {
        // Whatever was open goes with it, as Classic.
        if (m_userSelect)
            m_userSelect->hide();
        m_sheet.close();
        if (m_quick && m_quick->isActive())
            m_quick->hide();
        if (m_text && m_text->isActive())
            m_text->hide(false);
        if (m_picker && m_picker->isActive())
            m_picker->hide();
        if (m_padTest && m_padTest->isActive())
            m_padTest->hide();
        if (m_moving)
            finishMove(false);
        m_closeConfirmQueued = false;
        m_switchQueued = false;
        m_switchFade = -1.f;
        m_topZone = false;
        m_panelZone = false;
        // Settings > Lock screen > Music on the lock screen (HomeNX lockMusic).
        if (!m_core.config().cards.lockMusic)
            m_audio.stopAll();
    });
    m_lock.onUnlocked([this]() {
        // The extra presses of an unlock mash must not reach the Cards view
        // (L opened Discord on its own): wait until every button is released.
        m_waitRelease = true;
        if (m_core.config().musicEnabled && m_audioStarted && !m_audio.isPlaying())
            m_audio.play();
        m_audio.playSfx(Sfx::ModalHide);
    });

    // Sheets (E3+): sfx hooks; QS / keyboard reuse Classic's widgets (E6, E9).
    m_sheet.onNavigate([this] { m_audio.playSfx(Sfx::Navigate); });
    m_sheet.onActivate([this] { m_audio.playSfx(Sfx::Activate); });
    m_sheet.onClosed([this] {
        if (m_achievementsCancel) m_achievementsCancel->store(true);
        m_audio.playSfx(Sfx::ModalHide);
        // B on a sub-sheet opened from Settings goes back to that Settings row.
        if (!m_subSheetTitle.empty() && m_sheet.title() == m_subSheetTitle) {
            if (m_btAudioSheet && bluetooth::IsDiscovering())
                bluetooth::StopDiscovery();
            openSettingsAt(m_settingsReturnSection, m_settingsReturnRow);
            return;
        }
        m_subSheetTitle.clear();
        m_btAudioSheet = false;
    });
    m_fontIcons.load(gpu, ren, std::string(kAssets) + "/fonts/switch_icons.ttf", 24);
    m_text = std::make_shared<TextEntryScreen>();
    m_text->setFont(&m_font);
    m_text->setSmallFont(&m_fontSmall);
    m_text->setTheme(&m_overlayTheme);
    m_text->onKeySfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_text->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_text->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });

    m_picker = std::make_shared<SteamGridDbPickerScreen>(gpu, ren, m_core.threadPool());
    m_picker->setFont(&m_font);
    m_picker->setSmallFont(&m_fontSmall);
    m_picker->setTheme(&m_overlayTheme);
    m_picker->setNetService(&m_core.net());
    m_picker->onClosed([this]() {
        m_overlayLatch = true;
    });
    m_picker->onSearch([this]() {
        if (!m_picker || !m_picker->isActive())
            return;
        const auto kind = m_picker->artworkKind();
        const auto titleId = m_picker->titleId();
        const auto title = m_picker->title();
        requestText(nxui::I18n::instance().tr("settings.steamgriddb.scan", "Search artwork"),
                    "SteamGridDB", m_picker->query(),
                    [this, titleId, title, kind](const std::string& typed) {
                        if (!typed.empty())
                            openTitlePicker(titleId, title, kind, typed);
                    });
    });
    m_picker->onDimensionChange([this](const std::string& /* dim */) {
        if (!m_picker || !m_picker->isActive())
            return;
        const auto kind = m_picker->artworkKind();
        const auto titleId = m_picker->titleId();
        const auto title = m_picker->title();
        const auto query = m_picker->query();
        openTitlePicker(titleId, title, kind, query);
    });
    m_picker->onApply([this](const SteamGridDbManager::BrowseResult& browse,
                             const SteamGridDbManager::Candidate& candidate) {
        applyPickerCandidate(browse, candidate);
    });

    // Pages (E1): open on the first shown page of the user's order.
    m_page = pages::first(m_core.config().cards);
    // Back from a game or applet: reopen the page it was started from
    // (console run 2026-11-09: Nintendo tiles returned to Games).
    if (m_fastReturn && m_leaveSession.valid) {
        const auto shownPages = pages::shown(m_core.config().cards);
        if (std::find(shownPages.begin(), shownPages.end(), m_leaveSession.page) != shownPages.end())
            m_page = m_leaveSession.page;
    }
    m_art.setConfig(&m_core.config());
    startWallpaper(m_core.config().cards.shuffleWall);
    m_catalog.setAssetsRoot(kAssets);
    m_catalog.setPage(m_page, m_core.config());
    requestPageSnapshot(m_page); // queued ahead of the catalogue load
    m_catalog.startLoad(m_core.catalog(), m_core.threadPool());

#ifdef SWITCHU_MENU
    if (m_fastReturn && m_core.launcher().suspendedTitleId() != 0) {
        m_deferredBluetoothInitFrames = 120;
        DebugLog::log("[cards] Bluetooth manager initialization deferred for fast return");
    } else {
        m_core.startBluetooth();
    }
#else
    // Bluetooth is a Core service; starting it is idempotent.
    m_core.startBluetooth();
#endif

#ifdef SWITCHU_MENU
    m_sysMsg.setCallback([this](SysAction a) { handleSystemAction(a); });

    const auto& initTrace = app().initializeTrace();
    switchu::smi::MenuReadyArgs ready{};
    ready.transition_origin_tick = m_core.transitionOriginTick();
    ready.menu_main_tick = m_core.menuMainTick();
    ready.initialize_start_tick = initTrace.initializeStartTick;
    ready.gpu_ready_tick = initTrace.gpuReadyTick;
    ready.renderer_ready_tick = initTrace.rendererReadyTick;
    ready.blank_frame_tick = initTrace.blankFrameTick;
    ready.activity_create_start_tick = createStartTick;
    ready.activity_create_end_tick = armGetSystemTick();
    ready.menu_main_core = m_core.menuMainCore();
    ready.activity_core = svcGetCurrentProcessorNumber();
    ready.catalog_count = 0;
    switchu::menu::smi_cmd::menuReady(ready);

    app().setFirstFrameCallback([this](uint64_t firstInputTick, uint64_t firstFrameTick) {
        switchu::smi::MenuFirstFrameArgs first{};
        first.transition_origin_tick = m_core.transitionOriginTick();
        first.first_input_tick = firstInputTick;
        first.first_frame_tick = firstFrameTick;
        first.image_memory_bytes = app().gpu().imageMemoryUsed();
        first.core = svcGetCurrentProcessorNumber();
        first.catalog_count = static_cast<uint32_t>(m_catalog.count());
        switchu::menu::smi_cmd::menuFirstFrame(first);
    });
#else
    (void)createStartTick;
#endif
    DebugLog::log("[cards] activity ready (suspended=%016llX)",
                  static_cast<unsigned long long>(m_core.launcher().suspendedTitleId()));
    return true;
}

void CardsHomeActivity::onDestroy() {
    DebugLog::log("[cards] activity destroy");
    // Diagnostic: which wait holds the Cards -> Classic switch.
    const std::uint64_t destroyStart = armGetSystemTick();
    auto destroyMs = [destroyStart]() {
        return static_cast<unsigned long>(armTicksToNs(armGetSystemTick() - destroyStart) / 1000000ULL);
    };
    unsigned long tStop = 0, tFutures = 0, tSnap = 0, tWall = 0, tCatalog = 0, tPool = 0;
    m_leaveCapturePending = false;
    m_leaveCaptureAfter = {};
    m_leavePreviewSuspendedTitleId = 0;
    m_leaving = false;
    m_leaveIsResume = false;
    m_leaveT = 0.f;
    m_core.setFrontendQuiesce(nullptr);
    // The mixer is shared: hand Classic its own sound and shuffle settings
    // back, since its audio restore skips an already loaded mixer.
    m_audio.setSfxVolume(m_core.config().sfxVolume);
    m_audio.setShuffle(m_core.config().customBgmShuffle);
#ifdef SWITCHU_MENU
    auto& closingTrace = m_core.closingTrace();
    const auto& shutdownTrace = app().shutdownTrace();
    closingTrace.shutdown_start_tick = shutdownTrace.shutdownStartTick;
    closingTrace.gpu_drain_start_tick = shutdownTrace.gpuDrainStartTick;
    closingTrace.gpu_drain_end_tick = shutdownTrace.gpuDrainEndTick;
    closingTrace.on_destroy_start_tick = shutdownTrace.activityDestroyStartTick;
#endif
    m_core.frontendStopping();
#ifdef SWITCHU_MENU
    m_core.playtime().cancel();
    if (m_achievementsCancel) m_achievementsCancel->store(true);
    closingTrace.http_cancel_done_tick = armGetSystemTick();
#endif
    // Stop our decodes and wait for our pool work before the Core loader is
    // handed to the next frontend.
    if (m_detailsPending)
        m_detailsPending.reset();
    if (m_netCheck.valid())
        m_netCheck.wait(); // the scope closed above cancels the request
    tStop = destroyMs();
    stopSpeedTest();
    stopHomebrewUpdates();
    waitSelfUpdate();
    stopBrowserDownload();
    if (m_detailsFuture.valid())
        m_detailsFuture.wait();
    m_saveAllStop.store(true);
    if (m_saveAllFuture.valid())
        m_saveAllFuture.wait();
    if (m_singleArtFuture.valid())
        m_singleArtFuture.wait();
    if (m_applyFuture.valid())
        m_applyFuture.wait();
    if (m_browseFuture.valid())
        m_browseFuture.wait();
    if (m_picker) {
        m_picker->wait();
        m_picker.reset();
    }
    tFutures = destroyMs();
    m_catalog.releaseAll();
    m_art.releaseAll();
    m_snapCache.wait(); // a page snapshot still being written finishes first
    m_snapItems.clear();
    tSnap = destroyMs();
    waitWallpaper();
    m_wallImage.reset();
    m_wallPending.reset();
    m_wallTex.reset();
    tWall = destroyMs();
    m_core.catalog().waitIdle();
    tCatalog = destroyMs();
    m_core.threadPool().waitForIdle();
    tPool = destroyMs();
#ifdef SWITCHU_MENU
    closingTrace.worker_drain_done_tick = armGetSystemTick();
#endif
    persistLastFocus();
#ifdef SWITCHU_MENU
    closingTrace.state_persist_done_tick = armGetSystemTick();
#endif
    if (m_userSelect)
        m_userSelect->hide();
    // The pool is idle (above), so a delete worker has finished with `this`.
    m_deleteProgress.reset();
    m_selfUpdateProgress.reset();
    m_fetchProgress.reset();
    m_sheet.close();
    m_musicSheet.close();
    m_newsSheet.close();
    m_newsSheet.clearTextures();
    m_moving = false;
    m_quick.reset();
    m_text.reset();
    m_padTest.reset();
    m_profiles.reset();
    // Frontend-owned GPU resources (audit §6.1 rule 3).
    m_chrome.release();
    m_avatar.reset();
    flushPerfLog();
    if (m_perfWrite.valid()) { try { m_perfWrite.get(); } catch (...) {} }
    DebugLog::log("[switch-diag] cards destroy: http/playtime %lu, workers %lu, snapshot %lu, wallpaper %lu, catalog %lu, pool %lu, total %lums",
                  tStop, tFutures, tSnap, tWall, tCatalog, tPool, destroyMs());
}

nxui::Widget* CardsHomeActivity::focusRoot() {
    if (m_lock.isLocked())
        return nullptr;
    if (m_userSelect && m_userSelect->isActive())
        return m_userSelect.get();
    if (m_fetchProgress && m_fetchProgress->isActive())
        return m_fetchProgress.get(); // A or B cancels the download
    if (m_text && m_text->isActive())
        return m_text.get();
    if (m_profiles && m_profiles->isOpen())
        return m_profiles.get();
    if (m_picker && m_picker->isActive())
        return m_picker.get();
    if (m_padTest && m_padTest->isActive())
        return m_padTest.get();
    if (m_quick && m_quick->isActive())
        return m_quick.get();
    if (m_discordPanel && m_discordPanel->isActive())
        return m_discordPanel.get();
    // The row and the sheets handle their own input.
    return nullptr;
}

void CardsHomeActivity::persistLastFocus() {
    if (m_selected < 0 || m_selected >= m_catalog.count())
        return;
    auto& config = m_core.config();
    const std::uint64_t id = m_catalog.at(m_selected).app.titleId;
    if (pages::isSystemId(id) || config.lastPageTitleId == id)
        return;
    config.lastPageTitleId = id;
    config.save();
#ifdef SWITCHU_MENU
    switchu::commitSdCard("cards last focus");
#endif
}

// Classic resolves theme music in applyThemeResources (resolveThemeAssetPath):
// absolute paths as they are, package and built-in themes relative to their
// folder, anything else relative to the OmniLaunch folder.
void CardsHomeActivity::resolveThemeMusic() {
    auto& audio = m_core.audioServices();
    const auto& config = m_core.config();
    if (config.safeMode())
        return;
    std::optional<ThemePreset> theme;
    for (const auto& preset : ThemePreset::builtInPresets())
        if (preset.id == config.themePreset || preset.name == config.themePreset)
            theme = preset;
    if (!theme)
        theme = ThemePreset::loadSinglePackage(config.themePreset);
    if (!theme || theme->music.empty())
        return;
    std::vector<std::string> tracks;
    for (const auto& raw : theme->music) {
        if (raw.empty())
            continue;
        std::string path;
        if (raw.front() == '/' || raw.find(":/") != std::string::npos)
            path = raw;
        else if (!theme->installPath.empty())
            path = theme->installPath + (theme->installPath.back() == '/' ? "" : "/") + raw;
        else
            path = std::string(kAssets) + "/" + raw;
        std::error_code ec;
        if (std::filesystem::exists(path, ec))
            tracks.push_back(std::move(path));
    }
    if (!tracks.empty()) {
        audio.themeMusicTracks = std::move(tracks);
        audio.themeMusicName = theme->name;
        DebugLog::log("[cards] theme music: %zu track(s) from %s", audio.themeMusicTracks.size(),
                      config.themePreset.c_str());
    }
}

void CardsHomeActivity::finishAudioLoad() {
    // HomeAudio::startClassic queues the mixer/preset load on the pool; the
    // frontend applies volumes and starts music when it lands (as Classic).
    auto& audio = m_core.audioServices();
    if (m_audioStarted)
        return;
    if (audio.loadFuture.valid() &&
        audio.loadFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    audio.waitForLoad();
    const auto& config = m_core.config();
    m_audio.setVolume(config.musicVolume);
    m_audio.setSfxVolume(config.cards.sounds ? config.sfxVolume : 0.f);
    m_audio.setShuffle(config.customBgmShuffle); // one setting for both frontends
    m_audio.setRepeat(config.bgmRepeat);
    if (config.musicEnabled && !m_lock.isLocked())
        m_audio.play();
    m_audioStarted = true;
    // Shared with Classic: a later startClassic keeps the mixer and track
    // instead of reloading them after a live switch (D2).
    audio.started = true;
    DebugLog::log("[cards] audio ready (music=%d)", config.musicEnabled ? 1 : 0);
}

void CardsHomeActivity::pollPlaytime() {
#ifdef SWITCHU_MENU
    auto& playtime = m_core.playtime();
    auto& config = m_core.config();
    if (playtime.syncProfile(config)) {
        m_playtimeQueued = true;
        m_details = {};
        m_detailsWanted = 0;
    }
    if (playtime.running()) {
        const int changed = playtime.poll(config);
        if (changed > 0 && config.cardsSort == 2 && m_moving)
            m_playtimeSortDeferred = true;
        if (changed > 0 && config.cardsSort == 2 && m_page == AppConfig::CardsPrefs::kGames && !m_moving &&
            m_catalog.count() > 0) {
            const std::uint64_t keep = m_catalog.at(m_selected).app.titleId;
            m_catalog.sort(config);
            const int index = m_catalog.indexOf(keep);
            m_selected = index >= 0 ? index : 0;
            m_scroll = static_cast<float>(m_selected);
        }
        return;
    }
    if (!m_playtimeQueued || m_catalog.loading() || m_catalog.all().empty())
        return;
    m_playtimeQueued = false;
    // Same rule as Classic (CL 258): titles with no figure, opened since the
    // cache was confirmed, or still suspended.
    const std::uint64_t suspended = m_core.launcher().suspendedTitleId();
    std::vector<std::uint64_t> ids;
    // Every installed title, whatever page is shown (E1).
    for (const auto& entry : m_catalog.all()) {
        const std::uint64_t id = entry.titleId;
        if (id == suspended || !config.hasPlaytime(id) ||
            config.lastOpenedAt(id) > config.playtimeCheckedSequence)
            ids.push_back(id);
    }
    if (ids.empty())
        return;
    std::uint64_t confirms = config.lastOpenedSequence;
    if (suspended != 0) {
        const std::uint64_t openedAt = config.lastOpenedAt(suspended);
        if (openedAt != 0 && openedAt - 1 < confirms)
            confirms = openedAt - 1;
    }
    playtime.start(m_core.threadPool(), std::move(ids), confirms);
#endif
}

// NetService::state() asks nifm for the connection status (55-285 ms on the
// console, 2026-11-16) and starts the network runtime on first use (350 ms).
// Run on the render thread every second for Nextendo, it stalled the frame.
// A worker reads it at most once a second; the frame uses the last answer.
void CardsHomeActivity::pollNetState() {
    if (m_netStateFuture.valid()) {
        if (m_netStateFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try { m_netStateFuture.get(); } catch (...) {}
        m_netOnline = m_netStateResult && m_netStateResult->load(std::memory_order_acquire);
        m_netStateNext = m_time + 1.f;
    }
    if (m_time < m_netStateNext || !m_core.threadPool().acceptingTasks())
        return;
    auto result = std::make_shared<std::atomic<bool>>(false);
    net::NetService* service = &m_core.net();
    try {
        m_netStateFuture = m_core.threadPool().submit([service, result] {
            result->store(service->online(), std::memory_order_release);
        });
        m_netStateResult = std::move(result);
    } catch (...) {
        m_netStateNext = m_time + 1.f;
    }
}

void CardsHomeActivity::refreshCatalogue() {
    if (m_catalog.loading())
        return;
    m_catalog.startLoad(m_core.catalog(), m_core.threadPool());
}

void CardsHomeActivity::onUpdate(float dt) {
    switchu::menu::frame_probe::begin();
    m_core.status().poll(m_core.threadPool(), home::HomeStatus::Clock::now(), m_core.config().cards.showFree);
    m_time += dt;
    m_inputDt = dt;
#ifdef SWITCHU_MENU
    // Startup logs line by line (a hang leaves a trace); once Cards has run
    // a few seconds, buffer and let the background thread write the card.
    // Without this every Cards log line was a synchronous SD write + commit.
    if (m_logImmediateFrames > 0 && --m_logImmediateFrames == 0) {
        DebugLog::log("[cards] run loop healthy, log buffering enabled");
        switchu::FileLog::setImmediate(false);
        switchu::FileLog::startBackgroundFlush();
    }
    // onRender stops while the lock screen has the display off; restart the
    // frame-interval measurement instead of bridging the idle gap (D5 fix).
    if (m_lock.isLocked() || !app().renderEnabled())
        m_perfLastFrameTick = 0;
#endif

    const std::uint64_t previousId =
        (m_selected >= 0 && m_selected < m_catalog.count()) ? m_catalog.at(m_selected).app.titleId : 0;
    if (!m_moving && !m_sheet.isOpen() && !m_musicSheet.isOpen() && !m_newsSheet.isOpen() && !overlayActive() &&
        m_catalog.poll(m_core.catalog(), m_core.config())) {
        // Keep the card in focus; on first load, prefer the suspended title,
        // then the leave-session title (PoloNX #104), then the last-opened one (C-SW-5).
        std::uint64_t want = previousId;
#ifdef SWITCHU_MENU
        if (want == 0)
            want = runningCardId();
#endif
        if (want == 0 && m_leaveSession.valid && m_leaveSession.focusTitleId != 0) {
            want = m_leaveSession.focusTitleId;
            DebugLog::log("[cards] focus restored from leave session: %016llX",
                          static_cast<unsigned long long>(want));
        }
        if (want == 0) {
            // Whatever the sort, land on the last-opened title.
            const int land = landingIndex();
            if (land >= 0)
                want = m_catalog.at(land).app.titleId;
        }
        int index = want != 0 ? m_catalog.indexOf(want) : -1;
        // The title to land on may live on the Media page (E1).
        if (index < 0 && want != 0 && previousId == 0 && m_page != AppConfig::CardsPrefs::kMedia &&
            m_core.config().cards.isMedia(want) &&
            !m_core.config().cards.pageHidden(AppConfig::CardsPrefs::kMedia)) {
            m_page = AppConfig::CardsPrefs::kMedia;
            m_catalog.setPage(m_page, m_core.config());
            index = m_catalog.indexOf(want);
        }
        // A homebrew left open in its host game lives on the Homebrew page.
        if (index < 0 && want != 0 && previousId == 0 && pages::isHomebrewId(want) &&
            m_page != AppConfig::CardsPrefs::kHomebrew &&
            !m_core.config().cards.pageHidden(AppConfig::CardsPrefs::kHomebrew)) {
            m_page = AppConfig::CardsPrefs::kHomebrew;
            m_catalog.setPage(m_page, m_core.config());
            index = m_catalog.indexOf(want);
        }
        // A suspended game stays in focus even when the reopened page is
        // another one (leave session page, below in onCreate).
        if (index < 0 && want != 0 && previousId == 0 && !pages::isHomebrewId(want) &&
            !pages::isSystemId(want) && m_page != AppConfig::CardsPrefs::kGames &&
            !m_core.config().cards.isMedia(want) &&
            !m_core.config().cards.pageHidden(AppConfig::CardsPrefs::kGames)) {
            m_page = AppConfig::CardsPrefs::kGames;
            m_catalog.setPage(m_page, m_core.config());
            index = m_catalog.indexOf(want);
        }
        if (index < 0 && pages::isHomebrewId(want)) m_deferredPageFocus = want;
        if (index < 0)
            index = 0;
        m_selected = std::clamp(index, 0, std::max(0, m_catalog.count() - 1));
        m_scroll = static_cast<float>(m_selected);
        m_leaveSession.valid = false;
        maybeShowTutorial();
    }

    switchu::menu::frame_probe::mark("cards-catalog");
    const auto pageFocus = (m_selected >= 0 && m_selected < m_catalog.count())
        ? m_catalog.at(m_selected).app.titleId : 0;
    if (!m_moving && !overlayActive() && !m_sheet.isOpen() && !m_musicSheet.isOpen() && !m_newsSheet.isOpen() &&
        m_catalog.pollPageFiles(m_core.threadPool(), m_core.config())) {
        const int deferred = m_deferredPageFocus ? m_catalog.indexOf(m_deferredPageFocus) : -1;
        const int index = deferred >= 0 ? deferred : m_catalog.indexOf(pageFocus);
        m_deferredPageFocus = 0;
        m_selected = std::clamp(index, 0, std::max(0, m_catalog.count() - 1));
        m_scroll = static_cast<float>(m_selected);
    }
    if (m_refreshQueued && !m_moving && !m_sheet.isOpen() && !m_musicSheet.isOpen() && !m_newsSheet.isOpen() &&
        --m_refreshDelayFrames <= 0 && !m_catalog.loading()) {
        m_refreshQueued = false;
        refreshCatalogue();
    }

    if (m_deferredBluetoothInitFrames > 0 && --m_deferredBluetoothInitFrames == 0) {
        m_core.startBluetooth();
    }
    switchu::menu::frame_probe::mark("cards-init");
    updateLeaveSplashHandoff(dt);

    nxui::AnimationManager::instance().update(dt);
    finishAudioLoad();
    m_audio.update();
    m_status.musicPlaying = m_audio.isPlaying() && !m_audio.isPaused();
    switchu::menu::frame_probe::mark("cards-audio");
    pollPlaytime();
    switchu::menu::frame_probe::mark("cards-profile-playtime");
    pollNetCheck();
    switchu::menu::frame_probe::mark("svc-netcheck");
    pollArtJobs();
    switchu::menu::frame_probe::mark("svc-artjobs");
    pollHomebrewUpdates();
    switchu::menu::frame_probe::mark("svc-hbupdates");
    pollSelfUpdate();
    switchu::menu::frame_probe::mark("svc-selfupdate");
    pollBrowserDownload();
    switchu::menu::frame_probe::mark("svc-browser");
    pollNetState();
    m_core.nextendo().update(m_core, m_netOnline);
    switchu::menu::frame_probe::mark("svc-nextendo");
    m_core.discord().update(m_core);
    switchu::menu::frame_probe::mark("svc-discord");
    discord::Alert discordAlert;
    if (m_core.discord().takeAlert(discordAlert)) {
        showToast(discordAlert.title + ": " + discordAlert.text);
        m_discordNotices.push_back(discordAlert);
        if (m_discordNotices.size() > 20)
            m_discordNotices.erase(m_discordNotices.begin());
    }
    if (m_discordPanel && m_discordPanel->isActive()) {
        m_discordPanel->update(dt);
    }
    switchu::menu::frame_probe::mark("cards-services");
    updateLock(dt);
    switchu::menu::frame_probe::mark("cards-lock");
    m_sheet.update(dt);
    m_musicSheet.update(dt, m_core.audioServices());
    m_newsSheet.update(dt, app().gpu(), app().renderer(), m_core.threadPool(), m_chrome, m_time);
    if (m_quick)
        m_quick->update(dt);
    if (m_text)
        m_text->update(dt);
    if (m_profiles && m_profiles->isActive()) {
        m_profiles->update(dt);
        if (m_profiles->isOpen() && !m_lock.isLocked())
            m_profiles->handleTouch(app().input());
    }
    if (m_picker)
        m_picker->update(dt);
    if (m_padTest)
        m_padTest->update(dt);
    pumpWallpaper();
    pumpBluetoothAudioSheet();
    switchu::menu::frame_probe::mark("cards-overlays");
    if (m_pageIn < 1.f)
        m_pageIn = std::min(1.f, m_pageIn + dt / 0.22f);
    if (m_tutorialCard > 0)
        m_tutorialIn = std::min(1.f, m_tutorialIn + dt / 0.2f);
    if (!m_lock.isLocked()) {
        if (m_userSelect) {
            m_userSelect->update(dt);
            if (m_userSelect->isActive())
                m_userSelect->handleTouch(app().input());
        }
        if (m_text && m_text->isActive()) {
            m_text->handleTouch(app().input());
        } else if (m_padTest && m_padTest->isActive()) {
            m_padTest->handleTouch(app().input());
        } else if (m_picker && m_picker->isActive()) {
            m_picker->handleTouch(app().input());
        } else if (m_discordPanel && m_discordPanel->isActive()) {
            m_discordPanel->handleTouch(app().input());
        }
        updateSgdbKeyPairing();
#ifdef SWITCHU_MENU
        if (m_closeConfirmQueued && !(m_userSelect && m_userSelect->isActive())) {
            m_closeConfirmQueued = false;
            if (m_selected >= 0 && m_selected < m_catalog.count() &&
                isRunningCard(m_catalog.at(m_selected).app.titleId))
                confirmCloseSuspended();
        }
        if (m_deleteConfirmId != 0 && !(m_userSelect && m_userSelect->isActive()))
            confirmDeleteSoftware();
        if (m_hekateQueued && !(m_userSelect && m_userSelect->isActive())) {
            // Stage the payload (or RTC reason), then the standard reboot.
            m_hekateQueued = false;
            std::string error;
            if (hekate::prepareMenuReboot(error)) {
                m_core.launcher().reboot();
            } else {
                auto& t = nxui::I18n::instance();
                m_audio.playSfx(Sfx::ModalShow);
                m_userSelect->show(t.tr("power.hekate_failed_title", "Cannot restart to Hekate"), error,
                                   {{t.tr("button.ok", "OK"), []() {}, true}}, 0, {});
                focusManager().setFocus(m_userSelect.get());
            }
        }
        if (m_deleteProgress)
            m_deleteProgress->update(dt);
        if (m_selfUpdateProgress)
            m_selfUpdateProgress->update(dt);
        if (m_fetchProgress)
            m_fetchProgress->update(dt);
        pollDeleteSoftware();
#endif
        // D2/D4: input is held off during the switch fade, leaving launch animation,
        // and, after arriving by a switch, until the confirming press is released.
        if (m_switchFade >= 0.f || m_leaving) {
            // no input
        } else if (m_powerOpen) {
            handlePowerInput(app().input());
        } else if (m_dotsOpen) {
            handleDotsInput(app().input());
        } else if (m_cardOptOpen) {
            handleCardOptionsInput(app().input());
        } else if (m_waitRelease) {
            if (!home::anyInputHeld(app().input()))
                m_waitRelease = false;
        } else if (m_overlayLatch || overlayActive()) {
            // An overlay that was open when this frame's input was dispatched
            // (and may have closed on it) owns the press: wait for release.
            if (!home::anyInputHeld(app().input()))
                m_overlayLatch = false;
        } else if (!m_launching) {
            if (m_tutorialCard > 0) {
                auto& in = app().input();
                if (in.isDown(nxui::Button::A)) {
                    m_audio.playSfx(Sfx::Navigate);
                    if (++m_tutorialCard > 4)
                        m_tutorialCard = 0;
                } else if (in.isDown(nxui::Button::B)) {
                    m_tutorialCard = 0;
                }
                if (m_tutorialCard == 0) {
                    m_core.config().cards.tutorialDone = true;
                    saveConfig("cards tutorial");
                }
            } else if (m_sheet.isOpen()) {
                m_sheet.handleInput(app().input(), dt);
            } else if (m_musicSheet.isOpen()) {
                m_musicSheet.handleInput(app().input(), m_core.audioServices(), m_core.config());
            } else if (m_newsSheet.isOpen()) {
                m_newsSheet.handleInput(app().input(), dt, m_chrome, m_audio);
            } else if (m_moving) {
                handleMoveInput();
            } else {
                handleInput();
            }
        }
    } else if (m_userSelect) {
        m_userSelect->update(dt);
    }
    switchu::menu::frame_probe::mark("cards-input");
    if (overlayActive())
        m_overlayLatch = true;
    if (m_fadeIn > 0.f)
        m_fadeIn = std::max(0.f, m_fadeIn - dt / kSwitchFadeIn);
    if (m_leaving) {
        m_leaveT += dt / m_leaveDur;
        const float vol = m_core.config().musicVolume * std::max(0.f, 1.f - m_leaveT);
        m_audio.setVolume(vol);
        if (m_leaveT >= 1.f) {
            m_leaveT = 1.f;
            m_leaving = false;
            m_launching = true;
            m_launchElapsed = 0.f;
#ifdef SWITCHU_MENU
            if (!m_leaveHomebrewPath.empty()) {
                const std::string hbPath = m_leaveHomebrewPath;
                m_leaveHomebrewPath.clear();
                scheduleLeaveCapture([this, hbPath]() {
                    m_core.launcher().launchNro(hbPath, 0, m_leaveUid, true);
                }, m_leaveTitleId);
            } else if (m_leaveIsResume) {
                switchu::smi::LaunchTransitionTrace trace{};
                trace.activation_tick = armGetSystemTick();
                trace.user_selected_tick = trace.activation_tick;
                trace.animation_complete_tick = trace.activation_tick;
                trace.recency_commit_complete_tick = trace.activation_tick;
                scheduleLeaveCapture([this, trace]() mutable {
                    m_core.launcher().resumeApplication(trace);
                }, m_leaveTitleId);
            } else {
                switchu::smi::LaunchTransitionTrace trace{};
                trace.activation_tick = armGetSystemTick();
                trace.user_selected_tick = trace.activation_tick;
                m_core.launcher().prepareApplication(m_leaveTitleId, m_leaveUid, trace);
                themeshop::http::cancelPendingRequests();

                auto& config = m_core.config();
                config.noteOpened(m_leaveTitleId, config.nextLastOpenedAt());
                config.lastPageTitleId = m_leaveTitleId;
                trace.recency_submit_tick = armGetSystemTick();
                if (!config.save())
                    DebugLog::log("[cards] could not save last-opened title=%016llX",
                                  static_cast<unsigned long long>(m_leaveTitleId));
                switchu::commitSdCard("last opened");
                trace.animation_complete_tick = armGetSystemTick();
                trace.recency_commit_complete_tick = trace.animation_complete_tick;

                scheduleLeaveCapture([this, trace]() mutable {
                    m_core.launcher().launchApplication(m_leaveTitleId, m_leaveUid, trace);
                }, m_leaveTitleId);
            }
#endif
        }
    }
    if (m_launching) {
        m_launchElapsed += dt;
        if (m_launchElapsed > 5.f) {
            DebugLog::log("[cards] launch did not close the menu; input restored");
            m_launching = false;
            m_audio.setVolume(m_core.config().musicVolume);
        }
    }
    switchu::menu::frame_probe::mark("cards-launch");
    pollDaemonNotifications();
    switchu::menu::frame_probe::mark("cards-daemon");
    updateFrontendSwitch(dt);
    updateDetails(dt);

    switchu::menu::frame_probe::mark("cards-details");
    // Ease the row toward the selection (snaps when motion is disabled: E8).
    const float target = static_cast<float>(m_selected);
    if (!m_core.config().cards.motion) {
        m_scroll = target;
    } else {
        m_scroll += (target - m_scroll) * std::min(1.f, dt * 12.f);
        if (std::abs(target - m_scroll) < 0.001f)
            m_scroll = target;
    }

    updateStatus(dt);
    switchu::menu::frame_probe::mark("cards-status");
    const std::uint64_t focusedId =
        (m_selected >= 0 && m_selected < m_catalog.count()) ? m_catalog.at(m_selected).app.titleId : 0;
    if (focusedId != previousId)
        m_detailChangedAt = m_time;
    // The background follows the focus once it has settled for a moment, so
    // scrolling past a dozen cards does not decode a dozen backgrounds.
    if (focusedId != m_bgId && std::abs(m_scroll - static_cast<float>(m_selected)) < 0.05f) {
        m_bgPrevId = m_bgId;
        m_bgId = focusedId;
        m_bgChangedAt = m_time;
    }
    ensureVisibleIcons();
    switchu::menu::frame_probe::mark("cards-images");
}

void CardsHomeActivity::requestPageSnapshot(int page) {
    m_snapPage = page;
    m_snapItems.clear();
    m_snapCache.load(m_core.threadPool(), page);
}

// Card of the current page with the latest last-opened mark, or -1: where a
// frontend switch or a cold start lands (onUpdate, first catalogue).
int CardsHomeActivity::landingIndex() const {
    int index = -1;
    std::uint64_t best = 0;
    for (int i = 0; i < m_catalog.count(); ++i) {
        const std::uint64_t at = m_core.config().lastOpenedAt(m_catalog.at(i).app.titleId);
        if (at > best) {
            best = at;
            index = i;
        }
    }
    return index;
}

// Saved images of the page go on their cards as soon as both the read and the
// catalogue are in, and the window shown is offered back for saving. The
// window the page will open on next (landFirst..landLast, empty when < 0) is
// saved with it: a snapshot of only the cards last looked at left the
// landing cards as square icons after a switch back from Classic.
void CardsHomeActivity::pumpPageSnapshot(int first, int last, int landFirst, int landLast) {
    const int page = m_catalog.page();
    if (page != m_snapPage)
        requestPageSnapshot(page); // every way of changing page lands here
    std::vector<SnapItem> got;
    if (m_snapCache.take(page, got)) {
        // Portraits determine the card face. Upload them before square icons
        // so the per-frame budget cannot expose an avoidable icon-only frame.
        std::stable_sort(got.begin(), got.end(), [](const SnapItem& a, const SnapItem& b) {
            return a.kind > b.kind;
        });
        m_snapItems = std::move(got);
    }
    // Kept until the catalogue is in: its first load drops every icon texture
    // (CardsCatalog::poll -> releaseAll), and the saved icons go back on then.
    if (!m_snapItems.empty()) {
        int uploads = 0;
        for (const auto& item : m_snapItems) {
            if (uploads >= 8)
                break; // the rest next frame
            if (item.kind == 0) {
                if (m_catalog.adoptSnapshotIcon(app().gpu(), app().renderer(), item) > 0)
                    ++uploads;
            } else if (const int idx = m_catalog.indexOf(item.titleId); idx >= 0) {
                const auto& card = m_catalog.at(idx);
                if (!card.system && !card.needsDownload &&
                    m_art.adoptSnapshotCover(app().gpu(), app().renderer(), item, m_time))
                    ++uploads;
            }
        }
        if (uploads < 8 && !m_catalog.loading())
            m_snapItems.clear(); // every image placed or not on this page
    }

    // Offer the shown window: complete only when every image in it is a fresh
    // decode (a snapshot image is never saved back as if it were).
    if (m_catalog.loading() || m_catalog.count() == 0)
        return;
    // Not while leaving: onDestroy waits for the write, and one started in the
    // switch fade held the switch to Classic for ~1.1 s (a 23-image page).
    // The page is saved again the next time it settles.
    // activityChangePending: the switch was requested earlier this same frame
    // (updateFrontendSwitch runs first and has already cleared the fade).
    if (m_switchQueued || m_switchFade >= 0.f || m_leaving || app().activityChangePending())
        return;
    std::vector<SnapItem> items;
    bool complete = true;
    // Landing cards first: their covers are uploaded first on the next read.
    std::vector<int> order;
    const auto addRange = [&](int a, int b) {
        for (int i = std::max(0, a); i <= std::min(m_catalog.count() - 1, b); ++i)
            if (std::find(order.begin(), order.end(), i) == order.end())
                order.push_back(i);
    };
    if (landFirst >= 0)
        addRange(landFirst, landLast);
    addRange(first, last);
    for (int i : order) {
        if (!complete)
            break;
        const auto& card = m_catalog.at(i);
        if (card.icon && card.iconFresh && card.iconSnap)
            items.push_back(*card.iconSnap);
        else if (!card.iconFailed)
            complete = false;
        if (card.system || card.needsDownload)
            continue;
        std::shared_ptr<const SnapItem> cover;
        const auto state = m_art.coverSnapshot(card.app.titleId, cover);
        if (state == CardsArt::SnapState::Ready)
            items.push_back(*cover);
        else if (state == CardsArt::SnapState::Pending)
            complete = false;
    }
    m_snapCache.offer(m_core.threadPool(), page, std::move(items), complete);
}

void CardsHomeActivity::ensureVisibleIcons() {
    const int first = static_cast<int>(std::floor(m_scroll)) - 1;
    const int last = static_cast<int>(std::ceil(m_scroll)) + 5;
    // The landing window is decoded and saved too, on pages whose images all
    // stay resident anyway (no extra eviction pressure).
    int landFirst = -1, landLast = -1;
    if (m_catalog.count() <= kKeepAllCoversUpTo && !m_catalog.loading()) {
        const int land = landingIndex();
        if (land >= 0) {
            landFirst = std::max(0, land - 1);
            landLast = land + 5;
        }
    }
    pumpPageSnapshot(first, last, landFirst, landLast); // before the decodes, so saved images show first
    switchu::menu::frame_probe::mark("cards-snapshot");
    m_catalog.pumpIcons(app().gpu(), app().renderer(), m_core.threadPool(), first, last, 4, 2);
    if (landFirst >= 0) // queues decodes only; the visible window keeps upload priority
        m_catalog.pumpIcons(app().gpu(), app().renderer(), m_core.threadPool(), landFirst, landLast, 4, 0);
    switchu::menu::frame_probe::mark("cards-icon-upload");
    std::vector<std::uint64_t> wanted, keep;
    for (int i = std::max(0, first); i <= std::min(m_catalog.count() - 1, last); ++i)
        if (!m_catalog.at(i).system)
            wanted.push_back(m_catalog.at(i).app.titleId);
    if (landFirst >= 0)
        for (int i = landFirst; i <= std::min(m_catalog.count() - 1, landLast); ++i)
            if (!m_catalog.at(i).system &&
                std::find(wanted.begin(), wanted.end(), m_catalog.at(i).app.titleId) == wanted.end())
                wanted.push_back(m_catalog.at(i).app.titleId);
    // A small catalogue keeps every cover it has decoded (about 1 MB each).
    // Dropping those beyond three cards meant that scrolling to the end and
    // back showed each card with its icon until its cover was decoded again.
    const bool keepAll = m_catalog.count() <= kKeepAllCoversUpTo;
    const int keepLo = keepAll ? 0 : std::max(0, first - 3);
    const int keepHi = keepAll ? m_catalog.count() - 1 : std::min(m_catalog.count() - 1, last + 3);
    for (int i = keepLo; i <= keepHi; ++i)
        if (!m_catalog.at(i).system)
            keep.push_back(m_catalog.at(i).app.titleId);
    const std::uint64_t bg = pages::isSystemId(m_bgId) ? 0 : m_bgId;
    m_art.pump(app().gpu(), app().renderer(), m_core.threadPool(), wanted, keep, bg, m_time, 1);
}

void CardsHomeActivity::updateStatus(float dt) {
    m_statusTimer -= dt;
    if (m_statusTimer > 0.f)
        return;
    m_statusTimer = 2.f;
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buf[16];
    if (m_core.config().clockUse12Hour) {
        const int h = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
        std::snprintf(buf, sizeof(buf), "%d:%02d", h, local.tm_min);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", local.tm_hour, local.tm_min);
    }
    m_status.clock = buf;
#ifdef SWITCHU_MENU
    // Battery figures come from the daemon's BatteryStatusChanged notification
    // (pollDaemonNotifications): psm gives nothing inside the menu applet.
    const auto& reading = m_core.status().latest();
    m_status.socCelsius = m_core.config().cards.showTemp ? reading.soc : -1.f;
#endif
    m_status.freeSpace.clear();
    const auto& space = m_core.status().latest();
    if (m_core.config().cards.showFree && space.freeValid) {
        const double gb = static_cast<double>(space.freeBytes) / 1e9;
        char free[24];
        std::snprintf(free, sizeof(free), "%.1f GB", gb);
        m_status.freeSpace = free;
    }
    m_status.avatar = m_avatar.get();
    m_status.musicPlaying = m_audio.isPlaying() && !m_audio.isPaused();
    if (m_core.config().cards.showNextendo) {
        m_status.nextendoPlayers = m_core.nextendo().totalOnline();
    } else {
        m_status.nextendoPlayers = -2;
    }
}

void CardsHomeActivity::loadAvatar() {
#ifdef SWITCHU_MENU
    AccountUid uid{};
    if (R_FAILED(accountGetLastOpenedUser(&uid)) || !accountUidIsValid(&uid)) {
        // Nothing has been opened yet on a cold boot: show the first account.
        AccountUid uids[8]{};
        s32 count = 0;
        if (R_FAILED(accountListAllUsers(uids, 8, &count)) || count <= 0 ||
            !accountUidIsValid(&uids[0]))
            return;
        uid = uids[0];
    }
    AccountProfile profile{};
    if (R_FAILED(accountGetProfile(&profile, uid)))
        return;
    u32 size = 0;
    if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &size)) && size > 0) {
        std::vector<std::uint8_t> data(size);
        u32 real = 0;
        if (R_SUCCEEDED(accountProfileLoadImage(&profile, data.data(), size, &real)) && real > 0) {
            auto tex = std::make_unique<nxui::Texture>();
            if (tex->loadFromMemory(app().gpu(), app().renderer(), data.data(), real, 128))
                m_avatar = std::move(tex);
        }
    }
    accountProfileClose(&profile);
#endif
}

void CardsHomeActivity::updateFrontendSwitch(float dt) {
    // D2. Runs after daemon notifications, so a HOME, sleep or launch handled
    // this frame vetoes the switch before it is requested.
    const bool blocked = m_launching || m_lock.isLocked() || !app().isRunning();
    if (m_switchQueued && !(m_userSelect && m_userSelect->isActive())) {
        m_switchQueued = false;
        if (!blocked && m_switchFade < 0.f) {
            m_switchFade = 0.f;
            m_audio.playSfx(Sfx::Activate);
        } else {
            DebugLog::log("[cards] frontend switch refused (busy)");
        }
    }
    if (m_switchFade < 0.f)
        return;
    if (blocked) {
        m_switchFade = -1.f;
        DebugLog::log("[cards] frontend switch cancelled");
        return;
    }
    m_switchFade += dt;
    if (m_switchFade < kSwitchFadeOut)
        return;
    m_switchFade = -1.f;
    if (!home::switchFrontend(app(), m_core, m_switchTarget))
        DebugLog::log("[cards] frontend switch not started");
}

void CardsHomeActivity::ensureClockFont() {
    if (m_fontClock.valid())
        return;
    const std::string fontPath = std::string(kAssets) + "/fonts/DejaVuSans.ttf";
    if (!m_fontClock.load(app().gpu(), app().renderer(), fontPath, 72))
        DebugLog::log("[cards] clock font load failed");
}

#ifdef SWITCHU_MENU
namespace {
// Same table as Classic (WiiUMenuApp.cpp sleepPlanSeconds).
float sleepPlanSeconds(bool docked, std::uint8_t plan) {
    static const float kHandheld[] = {60.f, 180.f, 300.f, 600.f, 1800.f};
    static const float kDocked[] = {3600.f, 7200.f, 10800.f, 21600.f, 43200.f};
    const int index = static_cast<int>(plan);
    if (index < 0 || index > 4)
        return -1.f;
    return docked ? kDocked[index] : kHandheld[index];
}
} // namespace
#endif

void CardsHomeActivity::updateLock(float dt) {
    // As Classic (WiiUMenuApp onUpdate tail): the suspend countdown is held
    // off while a title runs or a launch is under way; the black low-power
    // frame stops presentation after one frame.
#ifdef SWITCHU_MENU
    const bool sleepSuppressed = m_launching || m_core.launcher().isAppRunning() || m_selfUpdateJob ||
                                 m_fetch.running() ||
                                 (!app().renderEnabled() && !m_lockLowPowerPrimed);
    m_sleepPlanTimer += dt;
    if (m_sleepPlanTimer >= 3.f) {
        m_sleepPlanTimer = 0.f;
        const auto& plan = m_core.status().latest();
        if (plan.sleepValid) {
            const bool docked = appletGetOperationMode() != AppletOperationMode_Handheld;
            m_lock.setSleepDelaySeconds(
                sleepPlanSeconds(docked, docked ? plan.consoleSleep : plan.handheldSleep));
        }
    }
#else
    const bool sleepSuppressed = true;
#endif
    m_lock.update(dt, app().input(), sleepSuppressed);
    if (!m_lock.hidesScene()) {
        m_lockLowPowerPrimed = false;
        if (!app().renderEnabled())
            app().setRenderEnabled(true);
    } else if (!m_lockLowPowerPrimed) {
        m_lockLowPowerPrimed = true;
    } else if (app().renderEnabled()) {
        DebugLog::log("[cards] lock low power: presentation stopped");
        app().setRenderEnabled(false);
    }
}

std::string CardsHomeActivity::lastPlayedText(std::uint64_t at) const {
    auto& i18n = nxui::I18n::instance();
    const std::uint64_t now = static_cast<std::uint64_t>(std::time(nullptr));
    if (at == 0 || now < 1000000000ULL)
        return {};
    const std::uint64_t s = now > at ? now - at : 0;
    auto ago = [&i18n](std::uint64_t n, const char* one, const char* oneFb, const char* many,
                       const char* manyFb) {
        std::string t = n == 1 ? i18n.tr(one, oneFb) : i18n.tr(many, manyFb);
        const auto pos = t.find("{n}");
        if (pos != std::string::npos)
            t.replace(pos, 3, std::to_string(n));
        return t;
    };
    if (s < 60)
        return i18n.tr("cards.played_now", "Played just now");
    if (s < 3600)
        return ago(s / 60, "cards.played_min", "Played {n} minute ago", "cards.played_mins",
                   "Played {n} minutes ago");
    if (s < 86400)
        return ago(s / 3600, "cards.played_hour", "Played {n} hour ago", "cards.played_hours",
                   "Played {n} hours ago");
    if (s < 30ULL * 86400ULL)
        return ago(s / 86400, "cards.played_day", "Played {n} day ago", "cards.played_days",
                   "Played {n} days ago");
    const std::time_t t = static_cast<std::time_t>(at);
    std::tm local{};
    localtime_r(&t, &local);
    char date[16];
    std::snprintf(date, sizeof(date), "%02d/%02d/%04d", local.tm_mday, local.tm_mon + 1, local.tm_year + 1900);
    std::string text = i18n.tr("cards.played_on", "Played on {date}");
    const auto pos = text.find("{date}");
    if (pos != std::string::npos)
        text.replace(pos, 6, date);
    return text;
}

void CardsHomeActivity::updateDetails(float dt) {
    // Adopt a finished query.
    if (m_detailsFuture.valid() &&
        m_detailsFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_detailsFuture.get();
        if (m_detailsPending && m_detailsPending->titleId == m_detailsWanted &&
            m_detailsPending->profile == switchu::menu::playtime::currentProfile(m_core.config()))
            m_details = *m_detailsPending;
        m_detailsPending.reset();
    }
    switchu::menu::frame_probe::mark("cards-details-adopt");
    const std::uint64_t id =
        (m_selected >= 0 && m_selected < m_catalog.count()) ? m_catalog.at(m_selected).app.titleId : 0;
    if (id != m_detailsWanted) {
        m_detailsWanted = id;
        m_detailsDelay = 0.2f; // let fast scrolling settle before asking pdm
    }
    if (id == 0 || pages::isSystemId(id) || m_details.titleId == id || m_detailsFuture.valid() || m_launching)
        return;
    m_detailsDelay -= dt;
    if (m_detailsDelay > 0.f)
        return;
    auto pending = std::make_shared<Details>();
    pending->titleId = id;
    pending->profile = switchu::menu::playtime::currentProfile(m_core.config());
    switchu::menu::frame_probe::mark("cards-details-profile");
    m_detailsPending = pending;
    m_detailsFuture = m_core.threadPool().submit([pending]() {
#ifdef SWITCHU_MENU
        switchu::control_cache::Meta meta{};
        if (switchu::control_cache::readMeta(pending->titleId, meta) && meta.display_version[0] != '\0')
            pending->version = meta.display_version;
        pending->lastPlayed = switchu::menu::playtime::queryLastPlayed(pending->titleId, pending->profile);
#endif
    });
}

void CardsHomeActivity::showToast(const std::string& text) {
    m_toast = text;
    m_toastUntil = m_time + 2.5f;
}

void CardsHomeActivity::moveSelection(int delta) {
    if (m_catalog.count() == 0)
        return;
    const int next = std::clamp(m_selected + delta, 0, m_catalog.count() - 1);
    if (next == m_selected)
        return;
    m_selected = next;
    m_audio.playSfx(Sfx::Navigate);
}

void CardsHomeActivity::handleInput() {
    auto& in = app().input();
    using B = nxui::Button;
    // Global shortcuts are available from every focus zone.
    if (in.isDown(B::ZL) || in.isDown(B::ZR)) {
        const int d = in.isDown(B::ZR) ? 1 : -1;
        goToPage(pages::step(m_core.config().cards, m_page, d), d);
        return;
    }
    // Holding Minus switches back to Classic: the same gesture Classic uses
    // to come here, whatever button its layout cycle is on. A short press
    // keeps the button's other role, read on release.
    if (in.isDown(B::Minus)) {
        m_minusArmed = !in.isHeld(B::Plus);
        m_minusFired = false;
        m_minusHeld = 0.f;
        return;
    }
    if (m_minusArmed && in.isHeld(B::Minus)) {
        if (in.isHeld(B::Plus)) {
            m_minusArmed = false;
        } else if (!m_minusFired && (m_minusHeld += m_inputDt) >= kFrontendHoldSeconds) {
            m_minusFired = true;
            m_minusArmed = false;
            if (!m_launching && !m_leaving) {
                m_audio.playSfx(Sfx::Activate);
                m_switchQueued = true;
                m_switchTarget = home::HomeStyle::Classic;
            }
        }
        return;
    }
    const bool minusTap = in.isUp(B::Minus) && m_minusArmed && !m_minusFired;
    if (in.isUp(B::Minus))
        m_minusArmed = false;
    // Same R / Minus contract as Classic (Settings > view switch button): the
    // other button sorts. Cards has no layouts to cycle, so the view button
    // only reminds how to switch frontend. Quick settings: left stick click.
    const std::string& viewBtnName = m_core.config().viewSwitchButton;
    const bool viewOnMinus = viewBtnName == "Minus" || viewBtnName == "-";
    const bool viewPressed = viewOnMinus ? minusTap : in.isDown(B::R);
    const bool sortPressed = viewOnMinus ? in.isDown(B::R) : minusTap;
    if (viewPressed && !in.isHeld(B::Plus)) {
        auto& t = nxui::I18n::instance();
        showToast(t.tr("cards.hold_minus_classic", "Hold - to switch to Classic"));
        return;
    }
    if (sortPressed && !in.isHeld(B::Plus)) { openSortSheet(); return; }
    if (in.isDown(B::LStick)) { openQuickSettings(); return; }
    if (in.isDown(B::L)) { openDiscord(); return; }
    if (in.isDown(B::Y)) { openSortSheet(); return; }
    if (in.isDown(B::Plus)) {
        DebugLog::log("[cards] plus -> options (sel=%d count=%d top=%d panel=%d)", m_selected, m_catalog.count(),
                      m_topZone ? 1 : 0, m_panelZone ? 1 : 0);
        openCardOptions();
        return;
    }
    if (m_topZone) {
        constexpr int kTopCount = static_cast<int>(TopButton::Count);
        const bool l = in.isDown(B::DLeft) || (in.isDown(B::LStickL) || in.isDown(B::RStickL));
        const bool r = in.isDown(B::DRight) || (in.isDown(B::LStickR) || in.isDown(B::RStickR));
        if (l && m_topIndex > 0) {
            --m_topIndex;
            if (static_cast<TopButton>(m_topIndex) == TopButton::Nextendo && !m_core.config().cards.showNextendo) {
                if (m_topIndex > 0) --m_topIndex;
            }
            m_audio.playSfx(Sfx::Navigate);
        }
        if (r && m_topIndex < kTopCount - 1) {
            ++m_topIndex;
            if (static_cast<TopButton>(m_topIndex) == TopButton::Nextendo && !m_core.config().cards.showNextendo) {
                if (m_topIndex < kTopCount - 1) ++m_topIndex;
            }
            m_audio.playSfx(Sfx::Navigate);
        }
        if (in.isDown(B::DDown) || (in.isDown(B::LStickD) || in.isDown(B::RStickD)) || in.isDown(B::B)) {
            m_topZone = false;
            m_audio.playSfx(Sfx::Navigate);
        } else if (in.isDown(B::A)) {
            activateTop();
        }
        return;
    }
    if (m_panelZone) {
        const bool panelBtn = m_core.config().cards.panelButtonEnabled;
        const bool empty = m_catalog.count() == 0;
        if (empty && !panelBtn) {
            m_panelZone = false;
            return;
        }
        if (empty)
            m_panelIndex = 2; // only the panel button exists without a card
        const bool l = in.isDown(B::DLeft) || (in.isDown(B::LStickL) || in.isDown(B::RStickL));
        const bool r = in.isDown(B::DRight) || (in.isDown(B::LStickR) || in.isDown(B::RStickR));
        if (l && !empty && m_panelIndex > 0) { --m_panelIndex; m_audio.playSfx(Sfx::Navigate); }
        if (r && m_panelIndex < (panelBtn ? 2 : 1)) { ++m_panelIndex; m_audio.playSfx(Sfx::Navigate); }
        if (in.isDown(B::DUp) || (in.isDown(B::LStickU) || in.isDown(B::RStickU)) || in.isDown(B::B)) {
            m_panelZone = false;
            m_audio.playSfx(Sfx::Navigate);
        } else if (in.isDown(B::A)) {
            if (m_panelIndex == 0) {
                activateSelected();
                return;
            }
            if (m_panelIndex == 2 && panelBtn) {
                if (m_core.config().cards.panelTitleId != 0)
                    activatePanelShortcut();
                else
                    openNewsSheet();
            } else {
                openCardOptions();
            }
        }
#ifdef SWITCHU_MENU
        else if (!empty && in.isDown(B::X) && isRunningCard(m_catalog.at(m_selected).app.titleId)) {
            confirmCloseSuspended();
        }
#endif
        return;
    }
    if (in.isDown(B::DUp) || (in.isDown(B::LStickU) || in.isDown(B::RStickU))) {
        m_topZone = true;
        m_audio.playSfx(Sfx::Navigate);
        return;
    }
    if ((in.isDown(B::DDown) || (in.isDown(B::LStickD) || in.isDown(B::RStickD))) &&
        (m_catalog.count() > 0 || m_core.config().cards.panelButtonEnabled)) {
        m_panelZone = true;
        m_panelIndex = m_catalog.count() > 0 ? 0 : 2;
        m_audio.playSfx(Sfx::Navigate);
        return;
    }
    const bool left = in.isHeld(B::DLeft) || (in.isHeld(B::LStickL) || in.isHeld(B::RStickL));
    const bool right = in.isHeld(B::DRight) || (in.isHeld(B::LStickR) || in.isHeld(B::RStickR));
    const int dir = left == right ? 0 : (left ? -1 : 1);
    if (dir != 0 && dir != m_repeatDir) {
        m_repeatDir = dir;
        m_repeatTimer = kRepeatDelay;
        moveSelection(dir);
    } else if (dir != 0) {
        m_repeatTimer -= 1.f / 60.f;
        if (m_repeatTimer <= 0.f) {
            m_repeatTimer = kRepeatRate;
            moveSelection(dir);
        }
    } else {
        m_repeatDir = 0;
    }

    if (in.isDown(B::A)) {
        activateSelected();
        return; // nothing else on the frame a launch/resume starts
    }
#ifdef SWITCHU_MENU
    // X on the suspended game's card asks to close it (as Classic does).
    if (in.isDown(B::X) && m_selected >= 0 && m_selected < m_catalog.count() &&
        isRunningCard(m_catalog.at(m_selected).app.titleId))
        confirmCloseSuspended();
#endif
}

void CardsHomeActivity::activateSelected() {
    if (m_selected < 0 || m_selected >= m_catalog.count())
        return;
    if (m_catalog.at(m_selected).system) {
        openSystemTile(m_catalog.at(m_selected).app.titleId);
        return;
    }
    launchTitle(m_catalog.at(m_selected));
}

void CardsHomeActivity::startLaunchHomebrew(const std::string& path) {
    if (m_launching || m_leaving)
        return;
    m_leaveHomebrewPath = path;
    m_leaving = true;
    m_leaveIsResume = false;
    m_leaveT = 0.f;
    m_leaveIndex = m_selected;
    m_leaveTitleId = (m_selected >= 0 && m_selected < m_catalog.count())
                         ? m_catalog.at(m_selected).app.titleId
                         : 0;
    m_leaveUid = {};
    m_leaveDur = m_core.config().cardsLaunchStyle == 1 ? 0.90f : 0.65f;
    m_audio.playSfx(Sfx::LaunchGame);
}

void CardsHomeActivity::launchTitle(const CardEntry& card) {
    if (card.needsDownload) {
        // Media app not on the SD card (TikTok, BrowseNX, as CardsNX).
        startAppFetch(card);
        return;
    }
    if (card.isHomebrew) {
#ifdef SWITCHU_MENU
        if (isRunningCard(card.app.titleId)) {
            // This homebrew is the one left open in its host game: go back to it.
            resumeSuspended();
            return;
        }
        if (m_core.launcher().suspendedTitleId() != 0) {
            m_audio.playSfx(Sfx::ModalShow);
            if (m_userSelect) {
                auto& i18n = nxui::I18n::instance();
                m_userSelect->show(i18n.tr("cards.suspended_title", "Software suspended"),
                                   i18n.tr("cards.suspended_body", "Close the suspended software first (X on its card)."),
                                   {{i18n.tr("button.ok", "OK"), []() {}, true}}, 0, {});
                focusManager().setFocus(m_userSelect.get());
            }
            return;
        }
        startLaunchHomebrew(card.homebrewPath);
#endif
        return;
    }
    const AppEntry& entry = card.app;
#ifdef SWITCHU_MENU
    if (isRunningCard(entry.titleId)) {
        resumeSuspended();
        return;
    }
    if (!entry.isLaunchable()) {
        m_audio.playSfx(Sfx::ModalShow);
        DebugLog::log("[cards] title %016llX not launchable (flags=%08X)",
                      static_cast<unsigned long long>(entry.titleId), entry.viewFlags);
        return;
    }
    if (m_core.launcher().suspendedTitleId() != 0) {
        // Another game is suspended; the stock HOME asks to close it first.
        m_audio.playSfx(Sfx::ModalShow);
        DebugLog::log("[cards] launch refused: another title is suspended");
        if (m_userSelect) {
            auto& i18n = nxui::I18n::instance();
            m_userSelect->show(i18n.tr("cards.suspended_title", "Software suspended"),
                               i18n.tr("cards.suspended_body", "Close the suspended software first (X on its card)."),
                               {{i18n.tr("button.ok", "OK"), []() {}, true}}, 0, {});
            focusManager().setFocus(m_userSelect.get());
        }
        return;
    }

    const std::uint64_t titleId = entry.titleId;
    if (entry.startupUserKnown && entry.startupUserAccount == 0) {
        startLaunch(titleId, AccountUid{});
        return;
    }
    // HomeNX D4: "Perguntar quem vai jogar" (cardsAskUser).
    if (m_core.config().cardsAskUser && m_userSelect) {
        s32 userCount = 0;
        AccountUid uids[8] = {};
        if (R_SUCCEEDED(accountListAllUsers(uids, 8, &userCount)) && userCount > 1) {
            if (m_userSelect->loadUsers(app().gpu(), app().renderer()))
                m_audio.playSfx(Sfx::ModalShow);
            m_userSelect->showUserSelect([this, titleId](AccountUid uid) { startLaunch(titleId, uid); });
            focusManager().setFocus(m_userSelect.get());
            return;
        }
    }
    if (m_core.config().defaultProfileEnabled) {
        AccountUid defaultUid{};
        if (parseAccountUid(m_core.config().defaultProfileUid, defaultUid)) {
            startLaunch(titleId, defaultUid);
            return;
        }
    }
    // Ask who's playing is off: start as the HOME user (the avatar in the
    // top bar: last opened account, else the first), as HomeNX does.
    if (!m_core.config().cardsAskUser) {
        AccountUid homeUid{};
        if (R_FAILED(accountGetLastOpenedUser(&homeUid)) || !accountUidIsValid(&homeUid)) {
            AccountUid uids[8]{};
            s32 count = 0;
            if (R_SUCCEEDED(accountListAllUsers(uids, 8, &count)) && count > 0)
                homeUid = uids[0];
        }
        if (accountUidIsValid(&homeUid)) {
            startLaunch(titleId, homeUid);
            return;
        }
    }
    AccountUid silentUid{};
    const bool networkRequired = entry.startupUserAccount == 2;
    if (R_SUCCEEDED(accountTrySelectUserWithoutInteraction(&silentUid, networkRequired)) &&
        accountUidIsValid(&silentUid)) {
        startLaunch(titleId, silentUid);
        return;
    }
    if (entry.startupUserKnown && !entry.userRequired) {
        startLaunch(titleId, AccountUid{});
        return;
    }
    if (m_userSelect) {
        if (m_userSelect->loadUsers(app().gpu(), app().renderer()))
            m_audio.playSfx(Sfx::ModalShow);
        m_userSelect->showUserSelect([this, titleId](AccountUid uid) { startLaunch(titleId, uid); });
        focusManager().setFocus(m_userSelect.get());
    }
#else
    (void)entry;
#endif
}

void CardsHomeActivity::startLaunch(std::uint64_t titleId, AccountUid uid) {
#ifdef SWITCHU_MENU
    if (m_launching || m_leaving)
        return;
    m_leaving = true;
    m_leaveIsResume = false;
    m_leaveT = 0.f;
    m_leaveIndex = m_selected;
    m_leaveTitleId = titleId;
    m_leaveUid = uid;
    const int style = m_core.config().cardsLaunchStyle;
    m_leaveDur = (style == 1) ? 0.60f : 0.38f;
    m_audio.playSfx(Sfx::LaunchGame);
#else
    (void)titleId;
    (void)uid;
#endif
}

#ifdef SWITCHU_MENU
std::uint64_t CardsHomeActivity::runningCardId() const {
    const std::uint64_t suspended = m_core.launcher().suspendedTitleId();
    if (suspended == 0)
        return 0;
    const std::string nro = m_core.launcher().runningHomebrew();
    if (nro.empty())
        return suspended;
    // BrowseNX is the Media-page card keyed by its download path, wherever
    // the running copy lives, so its glow and X-to-close work.
    if (nro == home::kLegacyBrowserNro)
        return pages::homebrewId(home::kBrowserNro);
    return pages::homebrewId(nro);
}

void CardsHomeActivity::confirmCloseSuspended() {
    if (!m_userSelect)
        return;
    auto& i18n = nxui::I18n::instance();
    const std::string title = m_catalog.at(m_selected).app.title;
    m_audio.playSfx(Sfx::ModalShow);
    m_userSelect->show(
        i18n.tr("game.close_title", "Close game"),
        i18n.tr("game.close_prefix", "Close") + std::string(" ") + title +
            i18n.tr("game.close_suffix", "?\nUnsaved progress will be lost."),
        {
            {i18n.tr("button.cancel", "Cancel"), []() {}, true},
            {i18n.tr("button.close", "Close"), [this]() {
                // The suspended state stays until the daemon sends
                // ApplicationExited.
                m_core.launcher().terminateApplication();
            }, true},
        },
        1, {});
    focusManager().setFocus(m_userSelect.get());
}
#endif

void CardsHomeActivity::resumeSuspended() {
#ifdef SWITCHU_MENU
    if (m_launching || m_leaving)
        return;
    const std::uint64_t suspendedId = m_core.launcher().suspendedTitleId();
    if (suspendedId == 0)
        return;
    m_leaving = true;
    m_leaveIsResume = true;
    m_leaveT = 0.f;
    m_leaveIndex = m_selected;
    m_leaveTitleId = suspendedId;
    m_leaveUid = {};
    m_leaveDur = 0.20f;
    m_audio.playSfx(Sfx::LaunchGame);
#endif
}

void CardsHomeActivity::pollDaemonNotifications() {
#ifdef SWITCHU_MENU
    AppletStorage st;
    while (R_SUCCEEDED(appletPopInteractiveInData(&st))) {
        switchu::smi::DaemonNotification notif{};
        s64 size = 0;
        appletStorageGetSize(&st, &size);
        if (size >= static_cast<s64>(sizeof(notif)))
            appletStorageRead(&st, 0, &notif, sizeof(notif));
        appletStorageClose(&st);
        if (notif.magic != switchu::smi::kNotifyMagic)
            continue;
        DebugLog::log("[cards] notify msg=%u", static_cast<unsigned>(notif.msg));
        auto& launcher = m_core.launcher();
        switch (notif.msg) {
        case switchu::smi::MenuMessage::HomeRequest:
            m_sysMsg.pushAction(SysAction::HomeDismiss);
            break;
        case switchu::smi::MenuMessage::ApplicationExited:
            launcher.setAppRunning(false);
            launcher.setAppHasForeground(false);
            launcher.setSuspendedTitleId(0);
            m_launching = false;
            m_playtimeQueued = true;
            break;
        case switchu::smi::MenuMessage::ApplicationSuspended:
            launcher.setAppRunning(true);
            launcher.setAppHasForeground(false);
            launcher.setSuspendedTitleId(notif.app_id);
            break;
        case switchu::smi::MenuMessage::BatteryStatusChanged:
            m_status.batteryPercent =
                static_cast<int>(switchu::smi::batteryPayloadPercentage(notif.payload));
            m_status.charging = switchu::smi::batteryPayloadCharging(notif.payload);
            break;
        case switchu::smi::MenuMessage::AppRecordsChanged:
        case switchu::smi::MenuMessage::GameCardMountFailure:
            m_refreshQueued = true;
            m_refreshDelayFrames = 3;
            break;
        case switchu::smi::MenuMessage::SleepSequence:
            m_core.discord().sleep();
            // Lock on the way under; WakeUp locks too in case Horizon
            // suspended the applet first (as Classic). Settings > Lock screen
            // can turn this off, unless a password is set (it always locks).
            if (m_core.config().cards.lockScreen || m_lock.passwordActive()) {
                ensureClockFont();
                m_lock.lockNow();
            }
            break;
        case switchu::smi::MenuMessage::WakeUp:
            m_core.discord().wake();
            if (m_core.config().cards.lockScreen || m_lock.passwordActive()) {
                ensureClockFont();
                m_lock.lockNow();
            }
            // A lock taken by hand still shows itself on wake.
            if (m_lock.isLocked())
                m_lock.showAfterSystemWake();
            m_lock.resetSleepCountdown();
            m_lockLowPowerPrimed = false;
            app().setRenderEnabled(true);
            break;
        case switchu::smi::MenuMessage::AppViewFlagsUpdate:
            m_catalog.updateViewFlags(notif.app_id, notif.payload);
            break;
        default:
            break;
        }
    }
    m_sysMsg.pump();
#endif
}

#ifdef SWITCHU_MENU
void CardsHomeActivity::handleSystemAction(SysAction action) {
    switch (action) {
    case SysAction::HomeDismiss:
        // E4: nothing leaves HOME while a delete runs (as Classic's progress
        // dialog); the result has to land in this frontend's preferences.
        if (deleteRunning())
            break;
        // D2: HOME during a frontend switch cancels it.
        if (m_switchQueued || m_switchFade >= 0.f) {
            m_switchQueued = false;
            m_switchFade = -1.f;
            DebugLog::log("[cards] frontend switch cancelled by HOME");
            break;
        }
        // HOME is a system-authenticated unlock, as in Classic.
        if (m_lock.isLocked()) {
            // A password lock only opens its recovery window on HOME (E9).
            m_lock.homePressed();
            m_lockLowPowerPrimed = false;
            app().setRenderEnabled(true);
            break;
        }
        if (m_userSelect && m_userSelect->isActive()) {
            m_userSelect->hide();
            m_overlayLatch = true;
            break;
        }
        if (m_text && m_text->isActive()) {
            m_text->hide(false);
            m_overlayLatch = true;
            break;
        }
        if (m_profiles && m_profiles->isActive()) {
            m_profiles->hide();
            m_overlayLatch = true;
            break;
        }
        if (m_picker && m_picker->isActive()) {
            m_picker->hide();
            m_overlayLatch = true;
            break;
        }
        if (m_padTest && m_padTest->isActive()) {
            m_padTest->hide();
            m_overlayLatch = true;
            break;
        }
        if (m_quick && m_quick->isActive()) {
            closeQuickSettings();
            m_overlayLatch = true;
            break;
        }
        if (m_tutorialCard > 0) {
            m_tutorialCard = 0;
            m_core.config().cards.tutorialDone = true;
            saveConfig("cards tutorial dismissed");
            m_waitRelease = true;
            break;
        }
        if (m_sheet.isOpen()) {
            m_sheet.close();
            m_waitRelease = true;
            break;
        }
        if (m_musicSheet.isOpen()) {
            m_musicSheet.close();
            m_waitRelease = true;
            break;
        }
        if (m_newsSheet.isOpen()) {
            m_newsSheet.close();
            m_waitRelease = true;
            break;
        }
        if (m_moving) {
            finishMove(false);
            m_waitRelease = true;
            break;
        }
        // HOME with nothing to dismiss returns to the suspended game.
        if (m_core.launcher().suspendedTitleId() != 0)
            resumeSuspended();
        break;
    default:
        break;
    }
}
#endif

void CardsHomeActivity::drawCard(nxui::Renderer& ren, int index, const nxui::Rect& card, float chosen) {
    const auto& entry = m_catalog.at(index);
    const std::uint64_t id = entry.app.titleId;
    // Shadow under every card; deeper under the focused one.
    const float shadow = 0.18f + 0.18f * chosen;
    ren.drawRoundedRect({card.x - 3.f, card.y + 6.f + 6.f * chosen, card.width + 6.f, card.height + 6.f},
                        {0.f, 0.f, 0.f, shadow}, layout::kCardRadius + 3.f);

    const nxui::Texture* cover = (entry.system || entry.needsDownload) ? nullptr : m_art.cover(id);
    const float coverIn = cover ? std::min(1.f, m_art.coverAge(id, m_time) / 0.35f) : 0.f;
    if (coverIn < 1.f) {
        // No cover yet (or fading in): the icon on glass with the name.
        ren.drawRoundedRect(card, {0.28f, 0.31f, 0.40f, 0.85f}, layout::kCardRadius);
        if (entry.icon && entry.icon->valid()) {
            const float side = card.width * 0.62f;
            const nxui::Rect art{card.x + (card.width - side) * 0.5f, card.y + card.height * 0.22f, side, side};
            ren.drawTextureRounded(entry.icon.get(), art, 22.f, CardsChrome::kWhite);
        }
        auto& f = m_chrome.labelFont();
        const std::string name = m_chrome.fit(f, entry.app.title, card.width - 28.f);
        const float w = f.measure(name).x;
        const float nameY = entry.needsDownload ? card.bottom() - 52.f : card.bottom() - 46.f;
        m_chrome.textMid(ren, f, name, card.x + (card.width - w) * 0.5f, nameY,
                         CardsChrome::kWhite);
        if (entry.needsDownload) {
            auto& sf = m_chrome.smallFont();
            const std::string sub = m_chrome.fit(sf, entry.author, card.width - 28.f);
            const float sw = sf.measure(sub).x;
            m_chrome.textMid(ren, sf, sub, card.x + (card.width - sw) * 0.5f, card.bottom() - 26.f,
                             CardsChrome::kWhite.withAlpha(0.72f));
        }
    }
    if (cover)
        CardsChrome::drawCover(ren, cover, card, layout::kCardRadius, CardsChrome::kWhite.withAlpha(coverIn));
    // Neighbours dimmed.
    if (chosen < 1.f)
        ren.drawRoundedRect(card, {0.f, 0.f, 0.f, 0.22f * (1.f - chosen)}, layout::kCardRadius);
#ifdef SWITCHU_MENU
    if (isRunningCard(id) ||
        (m_leavePreviewSuspendedTitleId != 0 && m_leavePreviewSuspendedTitleId == id)) {
        // Running badge: a green pill at the top left.
        const nxui::Rect badge{card.x + 12.f, card.y + 12.f, 26.f, 26.f};
        ren.drawCircle({badge.x + 13.f, badge.y + 13.f}, 13.f, {0.f, 0.f, 0.f, 0.45f}, 24);
        ren.drawCircle({badge.x + 13.f, badge.y + 13.f}, 8.f, CardsChrome::kGreen, 24);
    }
#endif
    if (entry.isHomebrew && homebrewHasUpdate(entry.homebrewPath, entry.version)) {
        // Update badge (G5): an accent dot with an arrow at the top right.
        const nxui::Vec2 c{card.right() - 25.f, card.y + 25.f};
        ren.drawCircle(c, 14.f, {0.f, 0.f, 0.f, 0.45f}, 24);
        ren.drawCircle(c, 10.f, CardsChrome::accent(1.f), 24);
        ren.drawRect({c.x - 1.5f, c.y - 5.f, 3.f, 9.f}, CardsChrome::kWhite);
        ren.drawRect({c.x - 5.f, c.y - 5.f, 10.f, 2.5f}, CardsChrome::kWhite);
    }
    if (chosen > 0.5f && !m_topZone && !m_panelZone) {
        const float pulse = 0.8f + 0.2f * std::sin(m_time * 5.f);
        ren.drawRoundedRectOutline(card, CardsChrome::accent(pulse * chosen), layout::kCardRadius, 3.f);
    }
}

// The panel's right button (C-SK-5): News by default, a pinned title's icon, or nothing.
void CardsHomeActivity::drawPanelButton(nxui::Renderer& ren, float cy) {
    if (!m_core.config().cards.panelButtonEnabled)
        return;
    const bool focus = m_panelZone && m_panelIndex == 2;
    const float cx = layout::kPanel.right() - 22.f - 38.5f;
    const auto pin = m_core.config().cards.panelTitleId;
    if (pin == 0) {
        m_chrome.glassCircle(ren, cx, cy, 77.f, focus, "news", 32.f, m_time);
        return;
    }
    const int idx = m_catalog.indexOf(pin);
    const nxui::Texture* pic =
        idx >= 0 && m_catalog.at(idx).icon && m_catalog.at(idx).icon->valid() ? m_catalog.at(idx).icon.get() : nullptr;
    m_chrome.glassCircle(ren, cx, cy, 77.f, focus, pic ? nullptr : "play", 30.f, m_time);
    if (pic) {
        const float s = 44.f;
        ren.drawTextureRounded(pic, {cx - s * 0.5f, cy - s * 0.5f, s, s}, 12.f, CardsChrome::kWhite);
    }
}

void CardsHomeActivity::drawPanel(nxui::Renderer& ren) {
    const auto& p = layout::kPanel;
    ren.drawRoundedRect(p, CardsChrome::glassTint(1.12f), layout::kPanelRadius);
    ren.drawRoundedRectOutline(p, {1.f, 1.f, 1.f, 0.22f}, layout::kPanelRadius, 1.5f);
    auto& i18n = nxui::I18n::instance();
    if (m_catalog.count() == 0) {
        const std::string msg = m_catalog.loading() ? i18n.tr("cards.loading", "Loading...")
                                                    : i18n.tr("cards.empty", "No software installed");
        m_chrome.textMid(ren, m_chrome.labelFont(), msg, p.x + 40.f, p.y + p.height * 0.5f, CardsChrome::kWhite);
        drawPanelButton(ren, p.bottom() - 97.f + 38.5f);
        drawPageDots(ren, p);
        return;
    }
    const auto& entry = m_catalog.at(m_selected);
    const std::uint64_t id = entry.app.titleId;
    const float d = std::min(1.f, (m_time - m_detailChangedAt) / 0.25f);
    const float dx = 14.f * (1.f - d);
    const nxui::Color white = CardsChrome::kWhite.withAlpha(d);
    const nxui::Color dim{0.82f, 0.84f, 0.90f, d};

    const nxui::Rect iconRect{p.x + 25.f + dx, p.y + 20.f, 78.f, 78.f};
    if (entry.icon && entry.icon->valid())
        ren.drawTextureRounded(entry.icon.get(), iconRect, 39.f, white);
    else
        ren.drawCircle({iconRect.x + 39.f, iconRect.y + 39.f}, 39.f, {1.f, 1.f, 1.f, 0.15f * d}, 40);
    const float tx = iconRect.right() + 15.f;
    const float maxW = p.right() - 30.f - tx;
    const bool showLogo = m_core.config().cards.titlePics;
    if (const auto* logo = showLogo ? m_art.logo(id) : nullptr) {
        const float scale = std::min(maxW / static_cast<float>(logo->width()), 48.f / static_cast<float>(logo->height()));
        ren.drawTexture(logo, {tx, p.y + 12.f, logo->width()*scale, logo->height()*scale}, white);
    } else {
        m_chrome.textMid(ren, m_chrome.titleFont(), m_chrome.fit(m_chrome.titleFont(), entry.app.title, maxW), tx,
                         p.y + 40.f, white);
    }

    // Primary pill (Play / Resume), white with dark text, and the "..." circle.
#ifdef SWITCHU_MENU
    const bool suspended = isRunningCard(id) ||
        (m_leavePreviewSuspendedTitleId != 0 && m_leavePreviewSuspendedTitleId == id);
#else
    const bool suspended = false;
#endif

    // Play time and recency, as "1h 35m   Jogado há 54 minutos".
    std::string line;
    const std::uint64_t played = m_core.config().playtimeOf(id);
    if (entry.system) {
        line = i18n.tr("cards.system_tile", "Nintendo");
    } else if (entry.needsDownload) {
        line = entry.author;
    } else if (entry.isHomebrew && !suspended) {
        line = entry.author.empty() ? i18n.tr("cards.hb_kind", "Homebrew")
                                    : i18n.tr("cards.hb_by", "Homebrew by {author}");
        const auto at = line.find("{author}");
        if (at != std::string::npos)
            line.replace(at, 8, entry.author);
        if (m_hbState && !m_hbJob) {
            if (const auto* offer = hbupdate::pendingOffer(
                    *m_hbState, hbupdate::Installed{entry.homebrewPath, std::string(), entry.version})) {
                std::string up = i18n.tr("cards.hb_update_to", "Update to {version} (+)");
                const auto vp = up.find("{version}");
                if (vp != std::string::npos)
                    up.replace(vp, 9, offer->latest);
                line += "    " + up;
            }
        }
    } else if (suspended) {
        const std::string openNow = i18n.tr("cards.open_now_desc", "Open now: Resume carries on");
        if (played > 0)
            line = switchu::menu::playtime::format(played) + "    " + openNow;
        else
            line = openNow;
    } else if (played > 0) {
        line = switchu::menu::playtime::format(played);
    } else {
        line = i18n.tr("cards.not_played", "Not played yet");
    }
    if (!entry.isHomebrew && m_details.titleId == id && m_details.lastPlayed && *m_details.lastPlayed > 0) {
        const std::string ago = lastPlayedText(*m_details.lastPlayed);
        if (!ago.empty())
            line += "    " + ago;
    }
    m_chrome.textMid(ren, m_chrome.smallFont(), m_chrome.fit(m_chrome.smallFont(), line, maxW), tx,
                     p.y + 72.f, dim);
    // Version tag, as CardsNX.
    const std::string versionTag = entry.isHomebrew ? entry.version
        : (m_details.titleId == id ? m_details.version : std::string());
    if (!versionTag.empty()) {
        m_chrome.icon(ren, "tag", tx + 8.f, p.y + 97.f, 16.f, dim);
        m_chrome.textMid(ren, m_chrome.smallFont(), versionTag, tx + 22.f, p.y + 97.f, dim);
    }
    const nxui::Rect button{p.x + 18.f, p.bottom() - 97.f, 262.f, 77.f};
    const bool playFocus = m_panelZone && m_panelIndex == 0;
    const bool dotsFocus = m_panelZone && m_panelIndex == 1;
    const bool rowFocus = !m_topZone;
    ren.drawRoundedRect(button, {1.f, 1.f, 1.f, (rowFocus ? 0.92f : 0.75f) * d}, 38.5f);
    if (playFocus) {
        const float pulse = 0.8f + 0.2f * std::sin(m_time * 5.f);
        ren.drawRoundedRectOutline({button.x - 5.f, button.y - 5.f, button.width + 10.f, button.height + 10.f},
                                   CardsChrome::accent(pulse * d), 43.5f, 3.f);
    }
    const std::string label = suspended ? i18n.tr("cards.resume", "Resume")
        : entry.needsDownload ? i18n.tr("cards.download", "Download")
        : (entry.isHomebrew || entry.system) ? i18n.tr("cards.open", "Open")
                                              : i18n.tr("cards.play", "Play");
    auto& lf = m_chrome.labelFont();
    const float lw = lf.measure(label).x;
    const float cx = button.x + button.width * 0.5f;
    const nxui::Color ink{0.12f, 0.13f, 0.17f, d};
    // Play triangle before the word.
    const float triX = cx - (lw + 26.f) * 0.5f;
    const float cy = button.y + button.height * 0.5f;
    ren.drawTriangle({triX, cy - 8.f}, {triX, cy + 8.f}, {triX + 13.f, cy}, ink);
    m_chrome.textMid(ren, lf, label, triX + 26.f, cy, ink);
    m_chrome.glassCircle(ren, button.right() + 46.f, cy, 72.f, dotsFocus, "dots", 30.f, m_time);

    drawPanelButton(ren, cy);
    if (suspended) {
        const std::string hint = i18n.tr("cards.hint_close", "X  Close software");
        m_chrome.textMid(ren, m_chrome.smallFont(), hint, button.right() + 100.f, cy, dim);
    }

    drawPageDots(ren, p);
}

void CardsHomeActivity::drawLaunch(nxui::Renderer& ren) {
    if (!m_leaving)
        return;

    const nxui::Rect screen{0.f, 0.f, 1280.f, 720.f};
    const float t = std::clamp(m_leaveT, 0.f, 1.f);

    if (m_leaveIsResume) {
        // Fast, smooth blackout for resume
        const float black = std::min(1.f, t / 0.18f);
        ren.drawRect(screen, nxui::Color(0.f, 0.f, 0.f, black));
        return;
    }

    const int style = m_core.config().cardsLaunchStyle;
    if (style == 1) {
        // Style 1: Card Zoom ("Salto do cartão")
        const float lift = nxui::Easing::outQuad(std::min(1.f, t / 0.22f));
        const float open = nxui::Easing::inOutCubic(std::clamp((t - 0.12f) / 0.68f, 0.f, 1.f));

        nxui::Rect from;
        float chosen = 1.f;
        if (!layout::cardRect(m_leaveIndex, m_scroll, from, chosen))
            from = {520.f, 121.f, 240.f, 340.f};

        from.y -= 14.f * lift;
        from.x -= 7.f * lift;
        from.width += 14.f * lift;
        from.height += 14.f * lift;

        const nxui::Rect rect = lerpRect(from, screen, open);
        const float radius = layout::kCardRadius * (1.f - open);

        // Shadow behind the card
        if (open < 0.95f) {
            ren.drawRoundedRect({rect.x - 8.f, rect.y + 12.f, rect.width + 16.f, rect.height + 16.f},
                                nxui::Color(0.f, 0.f, 0.f, 0.35f * (1.f - open)), radius + 8.f);
        }

        // Draw card cover or content
        const nxui::Texture* cover = m_art.cover(m_leaveTitleId);
        if (cover && cover->valid()) {
            CardsChrome::drawCover(ren, cover, rect, radius, CardsChrome::kWhite);
        } else if (m_leaveIndex >= 0 && m_leaveIndex < m_catalog.count()) {
            drawCard(ren, m_leaveIndex, rect, 1.f);
        }

        // Smooth fade to black in the final ~28% of the zoom
        const float black = nxui::Easing::inQuad(std::clamp((t - 0.72f) / 0.28f, 0.f, 1.f));
        if (black > 0.f)
            ren.drawRect(screen, nxui::Color(0.f, 0.f, 0.f, black));
    } else {
        // Style 0: Simple Fade
        const float black = nxui::Easing::inOutCubic(std::clamp((t - 0.05f) / 0.90f, 0.f, 1.f));
        ren.drawRect(screen, nxui::Color(0.f, 0.f, 0.f, black));
    }
}

// Button hints as glyph capsules (Classic hint style) in the strip between
// the top bar (ends y 86) and the focused card (starts y 114), drawn after the
// cards so a lifted or focused card can no longer cover them: the page switch
// (ZL page ZR) and the frontend switch (hold Minus).
void CardsHomeActivity::drawHintChips(nxui::Renderer& ren, const std::string& pageLabel) {
    auto& sf = m_chrome.smallFont();
    auto& tr = nxui::I18n::instance();
    const float gs = 0.62f;
    const float gh = m_fontIcons.measure(ui::glyph::button(nxui::Button::ZL)).y * gs;
    auto chip = [&](float x, const std::string& lead, const std::string& label, const std::string& tail) {
        const float lw = lead.empty() ? 0.f : m_fontIcons.measure(lead).x * gs;
        const float tw = tail.empty() ? 0.f : m_fontIcons.measure(tail).x * gs;
        const float textW = sf.measure(label).x;
        const float w = 16.f + lw + (lw > 0.f ? 6.f : 0.f) + textW + (tw > 0.f ? 6.f + tw : 0.f);
        const nxui::Rect cap{x, 89.f, w, 22.f};
        ren.drawRoundedRect(cap, {0.12f, 0.14f, 0.20f, 0.62f}, 11.f);
        ren.drawRoundedRectOutline(cap, {1.f, 1.f, 1.f, 0.18f}, 11.f, 1.f);
        const float iy = cap.y + (cap.height - gh) * 0.5f;
        float cx = cap.x + 8.f;
        if (lw > 0.f) {
            ren.drawText(lead, {cx, iy}, &m_fontIcons, CardsChrome::kWhite, gs);
            cx += lw + 6.f;
        }
        m_chrome.textMid(ren, sf, label, cx, cap.y + 11.f, {1.f, 1.f, 1.f, 0.9f});
        cx += textW + 6.f;
        if (tw > 0.f)
            ren.drawText(tail, {cx, iy}, &m_fontIcons, CardsChrome::kWhite, gs);
        return cap.right();
    };
    float x = chip(20.f, ui::glyph::button(nxui::Button::ZL), pageLabel, ui::glyph::button(nxui::Button::ZR));
    chip(x + 10.f, ui::glyph::button(nxui::Button::Minus),
         tr.tr("hint.hold", "(hold)") + " " + tr.tr("hint.switch_to_classic", "Classic"), std::string());
}

void CardsHomeActivity::onRender(nxui::Renderer& ren) {
    samplePerfFrame();
    // Background: the focused title's art, cross-faded from the previous one.
    const auto& plainTitles = m_core.config().cards.plainBackgroundTitles;
    const bool plain = std::find(plainTitles.begin(), plainTitles.end(), m_bgId) != plainTitles.end();
    const bool wall = m_wallTex && m_wallTex->valid();
    const nxui::Texture* bg = wall ? m_wallTex.get() : !plain && m_bgId ? m_art.background(m_bgId) : nullptr;
    const float bgIn = wall ? 1.f : bg ? std::min(1.f, m_art.backgroundAge(m_bgId, m_time) / 0.6f) : 0.f;
    const nxui::Texture* prev = !wall && !plain && m_bgPrevId ? m_art.background(m_bgPrevId) : nullptr;
    m_chrome.drawBackground(ren, bg, bgIn, prev, m_time, m_core.config().cards.haze,
                            m_core.config().cards.bgMotion);

    m_chrome.drawTopBar(ren, m_status, static_cast<TopButton>(m_topIndex), m_topZone, m_time);
    auto& tr = nxui::I18n::instance();
    const std::string pageLabel = m_page == AppConfig::CardsPrefs::kGames ? tr.tr("cards.page_games", "Games")
        : m_page == AppConfig::CardsPrefs::kHomebrew ? tr.tr("cards.page_homebrew", "Homebrew")
        : m_page == AppConfig::CardsPrefs::kNintendo ? tr.tr("cards.page_nintendo", "Nintendo")
                                                    : tr.tr("cards.page_media", "Media");

    // Neighbours first, focused card last so its outline sits on top.
    const bool zoomLeaving = m_leaving && m_core.config().cardsLaunchStyle == 1;
    // Page change (E1): the new row slides in from the side it came from.
    const float slide = (1.f - m_pageIn) * (1.f - m_pageIn) * 160.f * static_cast<float>(m_pageDir);
    for (int i = 0; i < m_catalog.count(); ++i) {
        if (i == m_selected)
            continue;
        if (zoomLeaving && i == m_leaveIndex)
            continue;
        nxui::Rect r;
        float chosen = 0.f;
        if (layout::cardRect(i, m_scroll, r, chosen)) {
            r.x += slide;
            drawCard(ren, i, r, chosen);
        }
    }
    if (m_selected >= 0 && m_selected < m_catalog.count()) {
        if (!(zoomLeaving && m_selected == m_leaveIndex)) {
            nxui::Rect r;
            float chosen = 0.f;
            if (layout::cardRect(m_selected, m_scroll, r, chosen)) {
                r.x += slide;
                if (m_moving)
                    r.y -= 18.f; // the carried card is lifted (C-IN-5)
                drawCard(ren, m_selected, r, chosen);
                if (m_moving)
                    ren.drawRoundedRectOutline({r.x - 6.f, r.y - 6.f, r.width + 12.f, r.height + 12.f},
                                               CardsChrome::accent(0.95f), layout::kCardRadius + 6.f, 4.f);
            }
        }
    }
    if (!m_moving)
        drawHintChips(ren, pageLabel);
    drawPanel(ren);
    drawLaunch(ren);

    const std::string banner = m_time < m_toastUntil ? m_toast : std::string();
    if (!banner.empty()) {
        auto& f = m_chrome.labelFont();
        const float w = f.measure(banner).x + 48.f;
        const nxui::Rect box{640.f - w * 0.5f, 104.f, w, 44.f};
        ren.drawRoundedRect(box, {0.08f, 0.09f, 0.12f, 0.82f}, 22.f);
        m_chrome.textMid(ren, f, banner, box.x + 24.f, box.y + 22.f, CardsChrome::kWhite);
    }
    drawMoveHint(ren);
    drawDots(ren);
    drawPower(ren);
    drawCardOptions(ren);
    m_sheet.render(ren, m_chrome);
    m_musicSheet.render(ren, m_chrome, m_core.audioServices());
    m_newsSheet.render(ren, m_chrome, m_time);
    if (m_discordPanel && m_discordPanel->isVisible())
        m_discordPanel->render(ren);
    drawTutorial(ren);
    if (m_quick && m_quick->isActive())
        m_quick->render(ren);
    if (m_picker && m_picker->isActive())
        m_picker->render(ren);
    if (m_padTest && m_padTest->isActive())
        m_padTest->render(ren);
    if (m_text && m_text->isActive())
        m_text->render(ren);
    if (m_profiles && m_profiles->isActive())
        m_profiles->render(ren);
    if (m_userSelect && m_userSelect->isActive())
        m_userSelect->render(ren);
    if (m_deleteProgress && m_deleteProgress->isActive())
        m_deleteProgress->render(ren);
    if (m_selfUpdateProgress && m_selfUpdateProgress->isActive())
        m_selfUpdateProgress->render(ren);
    if (m_fetchProgress && m_fetchProgress->isActive())
        m_fetchProgress->render(ren);
    // D2 switch: fade to black on the way out, from black on the way in.
    float black = m_fadeIn;
    if (m_switchFade >= 0.f)
        black = std::max(black, std::min(1.f, m_switchFade / kSwitchFadeOut));
    if (black > 0.f)
        ren.drawRect({0.f, 0.f, 1280.f, 720.f}, {0.f, 0.f, 0.f, black});
#ifdef SWITCHU_MENU
    if (m_fastReturnStartupTick != 0) {
        const auto elapsedMs = static_cast<unsigned long>(
            armTicksToNs(armGetSystemTick() - m_fastReturnStartupTick) /
            1'000'000ULL);
        DebugLog::log("[first-frame] fast HOME return rendered in %lums (cards)", elapsedMs);
        m_fastReturnStartupTick = 0;
        m_fastReturn = false;
    }
#endif
    if (leaveSplashActive() && m_leaveSplashTex.valid()) {
        const float alpha = (m_leaveSplashPhase == LeaveSplashPhase::Hold)
            ? 1.f
            : m_leaveSplashFade;
        ren.drawTexture(&m_leaveSplashTex, {0.f, 0.f, 1280.f, 720.f},
                        nxui::Color::white().withAlpha(alpha));
    }
    m_lock.render(ren);
}

} // namespace cards
