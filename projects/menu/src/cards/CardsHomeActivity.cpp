#include "cards/CardsHomeActivity.hpp"
#include "cards/CardsLayout.hpp"
#include "core/DebugLog.hpp"
#include "core/PlayTime.hpp"
#include "home/HomeCore.hpp"
#include "themeshop/ThemeHttp.hpp"
#include "widgets/OverlayDialog.hpp"
#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
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

namespace cards {

namespace {

#ifdef SWITCHU_HOMEBREW
constexpr const char* kAssets = "romfs:";
#else
constexpr const char* kAssets = "sdmc:/switch/OmniLaunch";
#endif

constexpr float kRepeatDelay = 0.35f;
constexpr float kRepeatRate = 0.09f;

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

} // namespace

CardsHomeActivity::CardsHomeActivity(home::HomeCore& core)
    : m_core(core), m_audio(core.audioServices().audio) {}

CardsHomeActivity::~CardsHomeActivity() = default;

bool CardsHomeActivity::onCreate() {
    DebugLog::log("[cards] activity create");
    m_core.setFrontendQuiesce([this] {
        m_catalog.releaseAll();
        m_art.releaseAll();
    });
    m_core.frontendStarted();
    const uint64_t createStartTick = armGetSystemTick();

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
    // frontends (HomeAudio::startClassic is idempotent).
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

    m_catalog.startLoad(m_core.catalog(), m_core.threadPool());

    // Bluetooth is a Core service; starting it is idempotent.
    m_core.startBluetooth();

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
    m_core.setFrontendQuiesce(nullptr);
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
    closingTrace.http_cancel_done_tick = armGetSystemTick();
#endif
    // Stop our decodes and wait for our pool work before the Core loader is
    // handed to the next frontend.
    m_catalog.releaseAll();
    m_art.releaseAll();
    m_core.catalog().waitIdle();
    m_core.threadPool().waitForIdle();
#ifdef SWITCHU_MENU
    closingTrace.worker_drain_done_tick = armGetSystemTick();
#endif
    persistLastFocus();
#ifdef SWITCHU_MENU
    closingTrace.state_persist_done_tick = armGetSystemTick();
#endif
    if (m_userSelect)
        m_userSelect->hide();
    // Frontend-owned GPU resources (audit §6.1 rule 3).
    m_chrome.release();
    m_avatar.reset();
}

nxui::Widget* CardsHomeActivity::focusRoot() {
    if (m_userSelect && m_userSelect->isActive())
        return m_userSelect.get();
    // The row handles its own input; nothing in the widget tree is focusable.
    return nullptr;
}

void CardsHomeActivity::persistLastFocus() {
    if (m_selected < 0 || m_selected >= m_catalog.count())
        return;
    auto& config = m_core.config();
    const std::uint64_t id = m_catalog.at(m_selected).app.titleId;
    if (config.lastPageTitleId == id)
        return;
    config.lastPageTitleId = id;
    config.save();
#ifdef SWITCHU_MENU
    switchu::commitSdCard("cards last focus");
#endif
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
    m_audio.setSfxVolume(config.sfxVolume);
    if (config.musicEnabled)
        m_audio.play();
    m_audioStarted = true;
    DebugLog::log("[cards] audio ready (music=%d)", config.musicEnabled ? 1 : 0);
}

void CardsHomeActivity::pollPlaytime() {
#ifdef SWITCHU_MENU
    auto& playtime = m_core.playtime();
    auto& config = m_core.config();
    if (playtime.running()) {
        const int changed = playtime.poll(config);
        (void)changed;
        return;
    }
    if (!m_playtimeQueued || m_catalog.loading() || m_catalog.count() == 0)
        return;
    m_playtimeQueued = false;
    // Same rule as Classic (CL 258): titles with no figure, opened since the
    // cache was confirmed, or still suspended.
    const std::uint64_t suspended = m_core.launcher().suspendedTitleId();
    std::vector<std::uint64_t> ids;
    for (int i = 0; i < m_catalog.count(); ++i) {
        const std::uint64_t id = m_catalog.at(i).app.titleId;
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

void CardsHomeActivity::refreshCatalogue() {
    if (m_catalog.loading())
        return;
    m_catalog.startLoad(m_core.catalog(), m_core.threadPool());
}

void CardsHomeActivity::onUpdate(float dt) {
    m_time += dt;

    const std::uint64_t previousId =
        (m_selected >= 0 && m_selected < m_catalog.count()) ? m_catalog.at(m_selected).app.titleId : 0;
    if (m_catalog.poll(m_core.catalog(), m_core.config())) {
        // Keep the card in focus; on first load, prefer the suspended title,
        // then the last-opened one (C-SW-5).
        std::uint64_t want = previousId;
        if (want == 0)
            want = m_core.launcher().suspendedTitleId();
        int index = want != 0 ? m_catalog.indexOf(want) : -1;
        if (index < 0)
            index = 0;
        m_selected = std::clamp(index, 0, std::max(0, m_catalog.count() - 1));
        m_scroll = static_cast<float>(m_selected);
    }

    if (m_refreshQueued && --m_refreshDelayFrames <= 0 && !m_catalog.loading()) {
        m_refreshQueued = false;
        refreshCatalogue();
    }

    nxui::AnimationManager::instance().update(dt);
    finishAudioLoad();
    m_audio.update();
    pollPlaytime();
    if (m_userSelect) {
        m_userSelect->update(dt);
        if (m_userSelect->isActive())
            m_userSelect->handleTouch(app().input());
    }
    if (m_launching) {
        m_launchElapsed += dt;
        if (m_launchElapsed > 5.f) {
            DebugLog::log("[cards] launch did not close the menu; input restored");
            m_launching = false;
        }
    }
    pollDaemonNotifications();
#ifdef SWITCHU_MENU
    if (m_closeConfirmQueued && !(m_userSelect && m_userSelect->isActive())) {
        m_closeConfirmQueued = false;
        if (m_selected >= 0 && m_selected < m_catalog.count() &&
            m_core.launcher().isAppSuspended(m_catalog.at(m_selected).app.titleId))
            confirmCloseSuspended();
    }
#endif
    if (m_wakeGuard) {
        auto& in = app().input();
        if (in.isDown(nxui::Button::A) || in.isDown(nxui::Button::B) || in.touchDown()) {
            m_wakeGuard = false;
            DebugLog::log("[cards] wake guard dismissed");
        }
    } else if (!m_launching && !(m_userSelect && m_userSelect->isActive())) {
        handleInput();
    }

    // Ease the row toward the selection.
    const float target = static_cast<float>(m_selected);
    m_scroll += (target - m_scroll) * std::min(1.f, dt * 12.f);
    if (std::abs(target - m_scroll) < 0.001f)
        m_scroll = target;

    updateStatus(dt);
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
}

void CardsHomeActivity::ensureVisibleIcons() {
    const int first = static_cast<int>(std::floor(m_scroll)) - 1;
    const int last = static_cast<int>(std::ceil(m_scroll)) + 5;
    m_catalog.pumpIcons(app().gpu(), app().renderer(), m_core.threadPool(), first, last, 4, 2);
    std::vector<std::uint64_t> wanted, keep;
    for (int i = std::max(0, first); i <= std::min(m_catalog.count() - 1, last); ++i)
        wanted.push_back(m_catalog.at(i).app.titleId);
    for (int i = std::max(0, first - 3); i <= std::min(m_catalog.count() - 1, last + 3); ++i)
        keep.push_back(m_catalog.at(i).app.titleId);
    m_art.pump(app().gpu(), app().renderer(), m_core.threadPool(), wanted, keep, m_bgId, m_time, 1);
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
    const auto thermals = switchu::getThermalReading();
    m_status.socCelsius = thermals.hasSoc ? thermals.soc : -1.f;
#endif
    struct statvfs st{};
    if (statvfs("sdmc:/", &st) == 0) {
        const double gb = static_cast<double>(st.f_bavail) * static_cast<double>(st.f_frsize) / 1e9;
        char free[24];
        std::snprintf(free, sizeof(free), "%.1f GB", gb);
        m_status.freeSpace = free;
    }
    m_status.avatar = m_avatar.get();
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

void CardsHomeActivity::showToast(const std::string& text) {
    m_toast = text;
    m_toastUntil = m_time + 2.5f;
}

void CardsHomeActivity::activateTop() {
    auto& i18n = nxui::I18n::instance();
    const auto button = static_cast<TopButton>(m_topIndex);
    if (m_userSelect && button == TopButton::Power) {
        // Same actions as Classic's power dialog (WiiUMenuApp onOpenPower).
        m_audio.playSfx(Sfx::ModalShow);
        m_userSelect->show(
            i18n.tr("power.title", "Power"),
            i18n.tr("power.choose_action", "Choose a power action."),
            {
                {i18n.tr("button.cancel", "Cancel"), []() {}, true},
                {i18n.tr("power.sleep", "Sleep"), [this]() {
#ifdef SWITCHU_MENU
                    m_core.launcher().enterSleep();
#endif
                }, true},
                {i18n.tr("power.reboot", "Restart"), [this]() {
#ifdef SWITCHU_MENU
                    m_core.launcher().reboot();
#endif
                }, true},
                {i18n.tr("power.shutdown", "Turn Off"), [this]() {
#ifdef SWITCHU_MENU
                    m_core.launcher().shutdown();
#endif
                }, true},
            },
            0, {});
        focusManager().setFocus(m_userSelect.get());
        return;
    }
    if (m_userSelect && button == TopButton::Settings) {
        // HOME style. Saved now, applied by the next menu start until the
        // live switch (D2) exists.
        m_audio.playSfx(Sfx::ModalShow);
        auto choose = [this](home::HomeStyle style) {
            auto& config = m_core.config();
            if (config.homeStyle == style)
                return;
            config.homeStyle = style;
            if (!config.save())
                DebugLog::log("[cards] homeStyle save failed");
            showToast(nxui::I18n::instance().tr("cards.home_style_restart",
                                                "Applies after the console restarts"));
        };
        m_userSelect->show(
            i18n.tr("cards.home_style", "HOME style"),
            i18n.tr("cards.home_style_desc", "Choose how the HOME Menu looks."),
            {
                {i18n.tr("cards.home_style_classic", "Classic"),
                 [choose]() { choose(home::HomeStyle::Classic); }, true},
                {i18n.tr("cards.home_style_cards", "Cards"),
                 [choose]() { choose(home::HomeStyle::Cards); }, true},
            },
            m_core.config().homeStyle == home::HomeStyle::Cards ? 1 : 0, {});
        focusManager().setFocus(m_userSelect.get());
        return;
    }
    m_audio.playSfx(Sfx::Activate);
    // The other sheets (menu, Discord, sort, users, notifications, music)
    // arrive in later steps.
    showToast(i18n.tr("cards.coming_soon", "Coming in a later update"));
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
    if (m_topZone) {
        constexpr int kTopCount = static_cast<int>(TopButton::Count);
        const bool l = in.isDown(B::DLeft) || in.isDown(B::LStickL);
        const bool r = in.isDown(B::DRight) || in.isDown(B::LStickR);
        if (l && m_topIndex > 0) { --m_topIndex; m_audio.playSfx(Sfx::Navigate); }
        if (r && m_topIndex < kTopCount - 1) { ++m_topIndex; m_audio.playSfx(Sfx::Navigate); }
        if (in.isDown(B::DDown) || in.isDown(B::LStickD) || in.isDown(B::B)) {
            m_topZone = false;
            m_audio.playSfx(Sfx::Navigate);
        } else if (in.isDown(B::A)) {
            activateTop();
        }
        return;
    }
    if (m_panelZone) {
        if (m_catalog.count() == 0) {
            m_panelZone = false;
            return;
        }
        const bool l = in.isDown(B::DLeft) || in.isDown(B::LStickL);
        const bool r = in.isDown(B::DRight) || in.isDown(B::LStickR);
        if (l && m_panelIndex > 0) { --m_panelIndex; m_audio.playSfx(Sfx::Navigate); }
        if (r && m_panelIndex < 1) { ++m_panelIndex; m_audio.playSfx(Sfx::Navigate); }
        if (in.isDown(B::DUp) || in.isDown(B::LStickU) || in.isDown(B::B)) {
            m_panelZone = false;
            m_audio.playSfx(Sfx::Navigate);
        } else if (in.isDown(B::A)) {
            if (m_panelIndex == 0) {
                activateSelected();
                return;
            }
            openCardOptions();
        }
#ifdef SWITCHU_MENU
        else if (in.isDown(B::X) && m_core.launcher().isAppSuspended(m_catalog.at(m_selected).app.titleId)) {
            confirmCloseSuspended();
        }
#endif
        return;
    }
    if (in.isDown(B::DUp) || in.isDown(B::LStickU)) {
        m_topZone = true;
        m_audio.playSfx(Sfx::Navigate);
        return;
    }
    if ((in.isDown(B::DDown) || in.isDown(B::LStickD)) && m_catalog.count() > 0) {
        m_panelZone = true;
        m_panelIndex = 0;
        m_audio.playSfx(Sfx::Navigate);
        return;
    }
    const bool left = in.isHeld(B::DLeft) || in.isHeld(B::LStickL);
    const bool right = in.isHeld(B::DRight) || in.isHeld(B::LStickR);
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

    if (in.isDown(B::L))
        moveSelection(-4);
    if (in.isDown(B::R))
        moveSelection(4);
    if (in.isDown(B::A)) {
        activateSelected();
        return; // nothing else on the frame a launch/resume starts
    }
#ifdef SWITCHU_MENU
    // X on the suspended game's card asks to close it (as Classic does).
    if (in.isDown(B::X) && m_selected >= 0 && m_selected < m_catalog.count() &&
        m_core.launcher().isAppSuspended(m_catalog.at(m_selected).app.titleId))
        confirmCloseSuspended();
#endif
}

void CardsHomeActivity::activateSelected() {
    if (m_selected < 0 || m_selected >= m_catalog.count())
        return;
    launchTitle(m_catalog.at(m_selected));
}

void CardsHomeActivity::launchTitle(const CardEntry& card) {
    const AppEntry& entry = card.app;
#ifdef SWITCHU_MENU
    if (m_core.launcher().isAppSuspended(entry.titleId)) {
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
    if (m_core.config().defaultProfileEnabled) {
        AccountUid defaultUid{};
        if (parseAccountUid(m_core.config().defaultProfileUid, defaultUid)) {
            startLaunch(titleId, defaultUid);
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
    if (m_launching)
        return;
    m_launching = true;
    m_launchElapsed = 0.f;
    switchu::smi::LaunchTransitionTrace trace{};
    trace.activation_tick = armGetSystemTick();
    trace.user_selected_tick = trace.activation_tick;
    m_core.launcher().prepareApplication(titleId, uid, trace);
    themeshop::http::cancelPendingRequests();

    auto& config = m_core.config();
    config.noteOpened(titleId, config.nextLastOpenedAt());
    config.lastPageTitleId = titleId;
    trace.recency_submit_tick = armGetSystemTick();
    if (!config.save())
        DebugLog::log("[cards] could not save last-opened title=%016llX",
                      static_cast<unsigned long long>(titleId));
    switchu::commitSdCard("last opened");
    trace.animation_complete_tick = armGetSystemTick();
    trace.recency_commit_complete_tick = trace.animation_complete_tick;
    m_audio.playSfx(Sfx::LaunchGame);
    m_core.launcher().launchApplication(titleId, uid, trace);
#else
    (void)titleId;
    (void)uid;
#endif
}

#ifdef SWITCHU_MENU
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

// The "..." card options: the title's details, and Close software while it is
// suspended. Delete, move and art pickers come with later steps.
void CardsHomeActivity::openCardOptions() {
    if (!m_userSelect || m_selected < 0 || m_selected >= m_catalog.count())
        return;
    auto& i18n = nxui::I18n::instance();
    const AppEntry& app = m_catalog.at(m_selected).app;
    char id[24];
    std::snprintf(id, sizeof(id), "%016llX", static_cast<unsigned long long>(app.titleId));
    const std::uint64_t played = m_core.config().playtimeOf(app.titleId);
    std::string body = i18n.tr("game.title_id", "Title ID") + ": " + id + "\n" +
                       i18n.tr("cards.played", "Played") + ": " +
                       (played > 0 ? switchu::menu::playtime::format(played)
                                   : i18n.tr("cards.not_played", "Not played yet"));
    std::vector<OverlayDialog::ButtonDef> buttons;
#ifdef SWITCHU_MENU
    if (m_core.launcher().isAppSuspended(app.titleId)) {
        buttons.push_back({i18n.tr("cards.close_software", "Close software"),
                           // Deferred: show() inside the dialog's own button
                           // handler would rebuild the actions it runs from.
                           [this]() { m_closeConfirmQueued = true; }, true});
    }
#endif
    buttons.push_back({i18n.tr("button.ok", "OK"), []() {}, true});
    m_audio.playSfx(Sfx::ModalShow);
    m_userSelect->show(app.title, body, std::move(buttons), 0, {});
    focusManager().setFocus(m_userSelect.get());
}

void CardsHomeActivity::resumeSuspended() {
#ifdef SWITCHU_MENU
    if (m_launching)
        return;
    m_launching = true;
    m_launchElapsed = 0.f;
    switchu::smi::LaunchTransitionTrace trace{};
    trace.activation_tick = armGetSystemTick();
    trace.user_selected_tick = trace.activation_tick;
    trace.animation_complete_tick = trace.activation_tick;
    trace.recency_commit_complete_tick = trace.activation_tick;
    m_audio.playSfx(Sfx::LaunchGame);
    m_core.launcher().resumeApplication(trace);
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
            m_wakeGuard = true;
            break;
        case switchu::smi::MenuMessage::WakeUp:
            // D1a has no lock screen yet: the first press after waking only
            // dismisses this guard, so a button pressed to wake the console
            // cannot launch or close software.
            m_wakeGuard = true;
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
        if (m_userSelect && m_userSelect->isActive()) {
            m_userSelect->hide();
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

    const nxui::Texture* cover = m_art.cover(id);
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
        m_chrome.textMid(ren, f, name, card.x + (card.width - w) * 0.5f, card.bottom() - 46.f,
                         CardsChrome::kWhite);
    }
    if (cover)
        CardsChrome::drawCover(ren, cover, card, layout::kCardRadius, CardsChrome::kWhite.withAlpha(coverIn));
    // Neighbours dimmed.
    if (chosen < 1.f)
        ren.drawRoundedRect(card, {0.f, 0.f, 0.f, 0.22f * (1.f - chosen)}, layout::kCardRadius);
#ifdef SWITCHU_MENU
    if (m_core.launcher().isAppSuspended(id)) {
        // Running badge: a green pill at the top left.
        const nxui::Rect badge{card.x + 12.f, card.y + 12.f, 26.f, 26.f};
        ren.drawCircle({badge.x + 13.f, badge.y + 13.f}, 13.f, {0.f, 0.f, 0.f, 0.45f}, 24);
        ren.drawCircle({badge.x + 13.f, badge.y + 13.f}, 8.f, CardsChrome::kGreen, 24);
    }
#endif
    if (chosen > 0.5f && !m_topZone && !m_panelZone) {
        const float pulse = 0.8f + 0.2f * std::sin(m_time * 5.f);
        ren.drawRoundedRectOutline(card, CardsChrome::accent(pulse * chosen), layout::kCardRadius, 3.f);
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
    m_chrome.textMid(ren, m_chrome.titleFont(), m_chrome.fit(m_chrome.titleFont(), entry.app.title, maxW), tx,
                     p.y + 40.f, white);

    // Play time and recency, as "1h 35m   Jogado há 54 minutos".
    std::string line;
    const std::uint64_t played = m_core.config().playtimeOf(id);
    if (played > 0)
        line = switchu::menu::playtime::format(played);
    else
        line = i18n.tr("cards.not_played", "Not played yet");
    m_chrome.textMid(ren, m_chrome.smallFont(), line, tx, p.y + 72.f, dim);

    // Primary pill (Play / Resume), white with dark text, and the "..." circle.
#ifdef SWITCHU_MENU
    const bool suspended = m_core.launcher().isAppSuspended(id);
#else
    const bool suspended = false;
#endif
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
    const std::string label = suspended ? i18n.tr("cards.resume", "Resume") : i18n.tr("cards.play", "Play");
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

    if (suspended) {
        const std::string hint = i18n.tr("cards.hint_close", "X  Close software");
        m_chrome.textMid(ren, m_chrome.smallFont(), hint, button.right() + 100.f, cy, dim);
    }

    // Page dots (one page for now) at the bottom right.
    ren.drawCircle({p.right() - 52.f, p.bottom() - 16.f}, 3.5f, {1.f, 1.f, 1.f, 0.9f}, 12);
    ren.drawCircle({p.right() - 40.f, p.bottom() - 16.f}, 3.5f, {1.f, 1.f, 1.f, 0.35f}, 12);
}

void CardsHomeActivity::onRender(nxui::Renderer& ren) {
    // Background: the focused title's art, cross-faded from the previous one.
    const nxui::Texture* bg = m_bgId ? m_art.background(m_bgId) : nullptr;
    const float bgIn = bg ? std::min(1.f, m_art.backgroundAge(m_bgId, m_time) / 0.6f) : 0.f;
    const nxui::Texture* prev = m_bgPrevId ? m_art.background(m_bgPrevId) : nullptr;
    m_chrome.drawBackground(ren, bg, bgIn, prev, m_time);

    m_chrome.drawTopBar(ren, m_status, static_cast<TopButton>(m_topIndex), m_topZone, m_time);

    // Neighbours first, focused card last so its outline sits on top.
    for (int i = 0; i < m_catalog.count(); ++i) {
        if (i == m_selected)
            continue;
        nxui::Rect r;
        float chosen = 0.f;
        if (layout::cardRect(i, m_scroll, r, chosen))
            drawCard(ren, i, r, chosen);
    }
    if (m_selected >= 0 && m_selected < m_catalog.count()) {
        nxui::Rect r;
        float chosen = 0.f;
        if (layout::cardRect(m_selected, m_scroll, r, chosen))
            drawCard(ren, m_selected, r, chosen);
    }
    drawPanel(ren);

    auto& i18n = nxui::I18n::instance();
    std::string banner = m_wakeGuard ? i18n.tr("cards.wake_hint", "Press A to continue")
                                     : (m_time < m_toastUntil ? m_toast : std::string());
    if (!banner.empty()) {
        auto& f = m_chrome.labelFont();
        const float w = f.measure(banner).x + 48.f;
        const nxui::Rect box{640.f - w * 0.5f, 104.f, w, 44.f};
        ren.drawRoundedRect(box, {0.08f, 0.09f, 0.12f, 0.82f}, 22.f);
        m_chrome.textMid(ren, f, banner, box.x + 24.f, box.y + 22.f, CardsChrome::kWhite);
    }
    if (m_userSelect && m_userSelect->isActive())
        m_userSelect->render(ren);
}

} // namespace cards
