#include "home/HomeCore.hpp"
#include "core/DebugLog.hpp"
#ifdef SWITCHU_MENU
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
    addShutdownHook(ShutdownPhase::Services, "thread pool", [this] {
        m_threadPool.shutdown();
    });
    addShutdownHook(ShutdownPhase::Audio, "audio and accessibility", [this] {
        m_audio.shutdown();
    });
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
