#include "home/HomeCore.hpp"
#include "core/DebugLog.hpp"
#include "themeshop/ThemeHttp.hpp"
#include "bluetooth/BluetoothManager.hpp"
#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
#include <switchu/file_log.hpp>
#include <switchu/sd_commit.hpp>
#endif

namespace home {

const char* shutdownPhaseName(ShutdownPhase phase) {
    switch (phase) {
    case ShutdownPhase::ActivityReleased: return "activity-released";
    case ShutdownPhase::GpuResources:     return "gpu-resources";
    case ShutdownPhase::Services:         return "services";
    case ShutdownPhase::Audio:            return "audio";
    case ShutdownPhase::ProcessExit:      return "process-exit";
    }
    return "unknown";
}

HomeCore::HomeCore() {
    // Process exit is terminal for HTTP, unlike a frontend switch (which only
    // closes the frontend scope). Runs after the last Activity is gone and
    // before the pool joins, also when no frontend closed its scope.
    addShutdownHook(ShutdownPhase::ActivityReleased, "http terminal cancel", [] {
        themeshop::http::cancelPendingRequests();
    });
    // Hooks of one phase run in reverse registration order. Services must run
    // pool -> HTTP -> Bluetooth: workers can still be inside a request until
    // the pool has joined, and the HTTP runtime owns the shared socket ref.
    addShutdownHook(ShutdownPhase::Services, "bluetooth", [this] {
        bluetooth::Finalize();
#ifdef SWITCHU_MENU
        m_closingTrace.bluetooth_done_tick = armGetSystemTick();
#endif
    });
    addShutdownHook(ShutdownPhase::Services, "http runtime", [this] {
        themeshop::http::shutdown();
#ifdef SWITCHU_MENU
        m_closingTrace.http_shutdown_done_tick = armGetSystemTick();
#endif
    });
    // The scrape runs on its own std::async thread and uses HTTP: stop it
    // before the HTTP runtime goes away (hooks run in reverse order).
    addShutdownHook(ShutdownPhase::Services, "steamgriddb", [this] {
        m_steamGridDb.cancelAndWait();
    });
    addShutdownHook(ShutdownPhase::Services, "thread pool", [this] {
        m_threadPool.shutdown();
    });
    // Registered after the pool so it runs before the pool joins: cancel the
    // pdm batch and collect its future while the worker is still alive.
    addShutdownHook(ShutdownPhase::Services, "catalogue loader", [this] {
        m_catalog.setPendingTransform({});
        m_catalog.waitIdle();
    });
    addShutdownHook(ShutdownPhase::Services, "play-time refresh", [this] {
        m_playtime.shutdown();
    });
    addShutdownHook(ShutdownPhase::Audio, "audio and accessibility", [this] {
        m_audio.shutdown();
    });
#ifdef SWITCHU_MENU
    addShutdownHook(ShutdownPhase::ProcessExit, "daemon closing notification", [this] {
        if (!m_closingArmed)
            return;
        m_closingTrace.core = svcGetCurrentProcessorNumber();
        // The daemon may end this process as soon as MenuClosing arrives,
        // before __appExit closes the log. Write the buffered exit lines
        // (shutdown phases) to the card first.
        DebugLog::log("[home-core] sending closing notification");
        switchu::FileLog::flush();
        switchu::menu::smi_cmd::menuClosing(m_closingTrace);
    });
#endif
}

void HomeCore::frontendStarted() {
    themeshop::http::openFrontendScope();
}

void HomeCore::frontendStopping() {
    themeshop::http::closeFrontendScope();
}

void HomeCore::startBluetooth() {
    bluetooth::Initialize();
    DebugLog::log("[home-core] Bluetooth manager %s",
                  bluetooth::IsAvailable() ? "initialized" : "unavailable");
}

HomeCore::~HomeCore() {
    // A normal exit runs every phase explicitly from main(). This only covers
    // an early return; it never reorders work that already ran.
    runShutdownPhase(ShutdownPhase::ProcessExit);
}

void HomeCore::setRequestExit(std::function<void()> cb) {
    m_requestExit = std::move(cb);
    m_launcher.init({
        .playSfxModalHide = [this]() { m_audio.audio.playSfx(Sfx::ModalHide); },
        .requestExit = [this]() { if (m_requestExit) m_requestExit(); },
        .quiesceWriters = [this]() { quiesceWriters(); },
    });
}

void HomeCore::quiesceWriters() {
    if (m_frontendQuiesce) {
        m_frontendQuiesce();
    }
    m_audio.waitForLoad();
#ifdef SWITCHU_MENU
    switchu::commitSdCard("power action");
#endif
    DebugLog::log("[home-core] writers quiesced and card committed for power action");
}

#ifdef SWITCHU_MENU
void HomeCore::setStartupStatus(const switchu::smi::SystemStatus& status) {
    m_systemStatus = status;
    m_launcher.setStartupStatus(status.suspended_app_id, status.app_running);
    m_transitionReason = status.transition_reason;
    m_safeModeRequested = (status.boot_flags & switchu::smi::kStatusFlagSafeMode) != 0;
    m_safeModeNotice = (status.boot_flags & switchu::smi::kStatusFlagSafeModeNotice) != 0;
}
#endif

void HomeCore::addShutdownHook(ShutdownPhase phase, const char* name, Hook hook) {
    const int index = static_cast<int>(phase);
    if (index <= m_lastPhaseRun) {
        DebugLog::log("[home-core] hook '%s' rejected: phase %s already ran",
                      name ? name : "?", shutdownPhaseName(phase));
        return;
    }
    if (hook)
        m_hooks[index].push_back({name ? name : "?", std::move(hook)});
}

void HomeCore::runShutdownPhase(ShutdownPhase phase) {
    const int target = static_cast<int>(phase);
    while (m_lastPhaseRun < target) {
        const int index = ++m_lastPhaseRun;
        auto& hooks = m_hooks[index];
        DebugLog::log("[home-core] shutdown phase %s (%zu hooks)",
                      shutdownPhaseName(static_cast<ShutdownPhase>(index)),
                      hooks.size());
        for (auto it = hooks.rbegin(); it != hooks.rend(); ++it)
            it->hook();
        hooks.clear();
    }
}

bool HomeCore::hasRun(ShutdownPhase phase) const {
    return static_cast<int>(phase) <= m_lastPhaseRun;
}

} // namespace home
