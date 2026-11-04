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

namespace cards {

namespace {

#ifdef SWITCHU_HOMEBREW
constexpr const char* kAssets = "romfs:";
#else
constexpr const char* kAssets = "sdmc:/switch/OmniLaunch";
#endif

const nxui::Color kBgTop{0.07f, 0.08f, 0.11f, 1.f};
const nxui::Color kBgBottom{0.02f, 0.02f, 0.04f, 1.f};
const nxui::Color kCardBase{0.16f, 0.17f, 0.21f, 1.f};
const nxui::Color kAccent{0.36f, 0.62f, 1.f, 1.f};
const nxui::Color kPanelBase{1.f, 1.f, 1.f, 0.08f};
const nxui::Color kPanelBorder{1.f, 1.f, 1.f, 0.14f};
const nxui::Color kText{0.95f, 0.96f, 0.98f, 1.f};
const nxui::Color kTextDim{0.70f, 0.72f, 0.78f, 1.f};

constexpr float kRepeatDelay = 0.35f;
constexpr float kRepeatRate = 0.09f;

std::string trimTo(nxui::Font& font, const std::string& text, float maxWidth) {
    if (font.measure(text).x <= maxWidth)
        return text;
    std::string out = text;
    while (!out.empty() && font.measure(out + "...").x > maxWidth) {
        out.pop_back();
        // Do not leave half of a UTF-8 sequence behind.
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
            out.pop_back();
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0xC0)
            out.pop_back();
    }
    return out + "...";
}

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

    ensureVisibleIcons();
}

void CardsHomeActivity::ensureVisibleIcons() {
    const int first = static_cast<int>(std::floor(m_scroll)) - 1;
    const int last = static_cast<int>(std::ceil(m_scroll)) + 5;
    m_catalog.pumpIcons(app().gpu(), app().renderer(), m_core.threadPool(), first, last, 4, 2);
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

void CardsHomeActivity::drawCard(nxui::Renderer& ren, int index, const nxui::Rect& r, float chosen) {
    const auto& card = m_catalog.at(index);
    const float dim = 0.55f + 0.45f * chosen;
    if (chosen > 0.5f) {
        const nxui::Rect shadow{r.x + 4.f, r.y + 12.f, r.width, r.height};
        ren.drawRoundedRect(shadow, {0.f, 0.f, 0.f, 0.35f * chosen}, layout::kCardRadius);
    }
    ren.drawRoundedRect(r, {kCardBase.r * dim, kCardBase.g * dim, kCardBase.b * dim, 1.f},
                        layout::kCardRadius);
    if (card.icon && card.icon->valid()) {
        // D1a: the square icon centred in the upper part; covers come in D1b.
        const float side = r.width - 24.f;
        const nxui::Rect art{r.x + 12.f, r.y + 12.f, side, side};
        ren.drawTextureRounded(card.icon.get(), art, layout::kCardRadius - 10.f,
                               {dim, dim, dim, 1.f});
    }
    const std::string title = trimTo(m_fontSmall, card.app.title, r.width - 24.f);
    ren.drawText(title, {r.x + 12.f, r.bottom() - 70.f}, &m_fontSmall,
                 chosen > 0.5f ? kText : kTextDim);
#ifdef SWITCHU_MENU
    if (m_core.launcher().isAppSuspended(card.app.titleId)) {
        const nxui::Rect badge{r.x + 12.f, r.bottom() - 38.f, 26.f, 26.f};
        ren.drawCircle({badge.x + 13.f, badge.y + 13.f}, 11.f, kAccent);
    }
#endif
    if (chosen > 0.5f)
        ren.drawRoundedRectOutline(r, kAccent.withAlpha(chosen), layout::kCardRadius, 4.f);
}

void CardsHomeActivity::drawPanel(nxui::Renderer& ren) {
    const auto& p = layout::kPanel;
    ren.drawRoundedRect(p, kPanelBase, layout::kPanelRadius);
    ren.drawRoundedRectOutline(p, kPanelBorder, layout::kPanelRadius, 1.5f);
    auto& i18n = nxui::I18n::instance();
    if (m_catalog.count() == 0) {
        const std::string msg = m_catalog.loading()
            ? i18n.tr("cards.loading", "Loading...")
            : i18n.tr("cards.empty", "No software installed");
        ren.drawText(msg, {p.x + 40.f, p.y + 40.f}, &m_font, kTextDim);
        return;
    }
    const auto& card = m_catalog.at(m_selected);
    ren.drawText(trimTo(m_fontTitle, card.app.title, p.width - 80.f),
                 {p.x + 40.f, p.y + 28.f}, &m_fontTitle, kText);
    const std::uint64_t played = m_core.config().playtimeOf(card.app.titleId);
    std::string sub = played > 0
        ? i18n.tr("cards.played", "Played") + ": " + switchu::menu::playtime::format(played)
        : i18n.tr("cards.not_played", "Not played yet");
    ren.drawText(sub, {p.x + 40.f, p.y + 78.f}, &m_fontSmall, kTextDim);

#ifdef SWITCHU_MENU
    const bool suspended = m_core.launcher().isAppSuspended(card.app.titleId);
#else
    const bool suspended = false;
#endif
    const nxui::Rect button{p.x + 40.f, p.bottom() - 97.f, 262.f, 77.f};
    ren.drawRoundedRect(button, kAccent, 38.5f);
    const std::string label = suspended ? i18n.tr("cards.resume", "Resume")
                                        : i18n.tr("cards.play", "Play");
    const nxui::Vec2 size = m_font.measure(label);
    ren.drawText(label, {button.x + (button.width - size.x) * 0.5f,
                         button.y + (button.height - size.y) * 0.5f},
                 &m_font, nxui::Color::white());
    const std::string hint = suspended ? i18n.tr("cards.hint_close", "X  Close software") : std::string();
    if (m_wakeGuard) {
        const std::string w = i18n.tr("cards.wake_hint", "Press A to continue");
        const nxui::Vec2 ws = m_font.measure(w);
        ren.drawText(w, {640.f - ws.x * 0.5f, 40.f}, &m_font, kText);
    }
    if (!hint.empty())
        ren.drawText(hint, {button.right() + 30.f, button.y + 26.f}, &m_fontSmall, kTextDim);

    const std::string count = std::to_string(m_selected + 1) + " / " + std::to_string(m_catalog.count());
    const nxui::Vec2 cs = m_fontSmall.measure(count);
    ren.drawText(count, {p.right() - 40.f - cs.x, p.bottom() - 44.f}, &m_fontSmall, kTextDim);
}

void CardsHomeActivity::onRender(nxui::Renderer& ren) {
    ren.drawGradientRect({0, 0, 1280, 720}, kBgTop, kBgBottom);
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
    if (m_userSelect && m_userSelect->isActive())
        m_userSelect->render(ren);
}

} // namespace cards
