#pragma once
// HomeCore: process-lifetime owner of the HOME services shared by every home
// frontend (Classic today, Cards later). It is created by main() before the
// nxui::Application and outlives it, so a frontend Activity can be replaced at
// runtime without tearing the services down.
//
// Services move in here one at a time with their initialization, callbacks and
// finalization.
// C1: ordered shutdown phases and GPU teardown hook.
// C2: audio, accessibility, preset resolution, playlist management.
// C3: AppletLauncher, ThreadPool, AppConfig, daemon SystemStatus & trace state.
// C4: per-frontend HTTP cancel scope, HTTP runtime and Bluetooth lifetime,
//     final MenuClosing notification.
// C5a: play-time refresh (HomePlaytime).
// C5b: SteamGridDB artwork scrape manager.

#include "home/HomeAudio.hpp"
#include "home/HomePlaytime.hpp"
#include "steamgriddb/SteamGridDbManager.hpp"
#include "launcher/AppletLauncher.hpp"
#include "core/Config.hpp"
#include <nxui/core/ThreadPool.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
#ifdef SWITCHU_MENU
#include <switchu/smi_protocol.hpp>
#endif

namespace home {

/// Process shutdown order. Phases run strictly in this order, each at most
/// once. A phase requested after a later one has already run is skipped.
enum class ShutdownPhase : std::uint8_t {
    ActivityReleased = 0, ///< The last frontend Activity has been destroyed.
    GpuResources     = 1, ///< Core GPU resources; renderer/GPU still alive.
    Services         = 2, ///< Network, Bluetooth, workers, catalog, settings.
    Audio            = 3, ///< Mixer/audio; must precede SDL_Quit().
    ProcessExit      = 4, ///< Final daemon notification (menuClosing).
};

constexpr std::size_t kShutdownPhaseCount = 5;

const char* shutdownPhaseName(ShutdownPhase phase);

class HomeCore {
public:
    using Hook = std::function<void()>;

    HomeCore();
    ~HomeCore();

    HomeCore(const HomeCore&) = delete;
    HomeCore& operator=(const HomeCore&) = delete;

    HomeAudio& audioServices() { return m_audio; }
    HomePlaytime& playtime() { return m_playtime; }
    SteamGridDbManager& steamGridDb() { return m_steamGridDb; }
    AppConfig& config() { return m_config; }
    const AppConfig& config() const { return m_config; }
    nxui::ThreadPool& threadPool() { return m_threadPool; }
    AppletLauncher& launcher() { return m_launcher; }

    void setRequestExit(std::function<void()> cb);
    void setFrontendQuiesce(std::function<void()> quiesce) { m_frontendQuiesce = std::move(quiesce); }
    void quiesceWriters();

    /// Frontend network scope (C4). frontendStarted() lets the new frontend
    /// issue HTTP requests; frontendStopping() aborts the leaving frontend's
    /// requests without the process-wide terminal cancel, so the next frontend
    /// can use the network. Launch handoff and process exit still use
    /// themeshop::http::cancelPendingRequests().
    void frontendStarted();
    void frontendStopping();

    /// Bluetooth is process-lifetime: started once (immediately or deferred by
    /// the frontend on fast return) and finalized in the Services phase.
    void startBluetooth();

#ifdef SWITCHU_MENU
    void setStartupStatus(const switchu::smi::SystemStatus& status);
    void setMenuMainTrace(uint64_t tick, uint32_t core) { m_menuMainTick = tick; m_menuMainCore = core; }
    const switchu::smi::SystemStatus& systemStatus() const { return m_systemStatus; }
    switchu::smi::MenuTransitionReason transitionReason() const { return m_transitionReason; }
    void setTransitionReason(switchu::smi::MenuTransitionReason reason) { m_transitionReason = reason; }
    uint64_t transitionOriginTick() const { return m_systemStatus.transition_origin_tick; }
    bool safeModeRequested() const { return m_safeModeRequested; }
    bool safeModeNotice() const { return m_safeModeNotice; }
    uint64_t menuMainTick() const { return m_menuMainTick; }
    uint32_t menuMainCore() const { return m_menuMainCore; }
    /// Exit trace. The frontend fills its fields in onDestroy; the Services
    /// phase stamps HTTP/Bluetooth; ProcessExit sends it to the daemon.
    /// Taking it arms the notification: MenuClosing tells the boot guard the
    /// exit was orderly, so it is sent only after a frontend's onDestroy ran,
    /// as before C4. (Application::shutdown() also runs onDestroy after a
    /// failed initialize, so that path can still arm it; unchanged by C4.)
    switchu::smi::MenuClosingArgs& closingTrace() {
        m_closingArmed = true;
        return m_closingTrace;
    }
#endif

    /// Register work for a phase. Hooks of one phase run in reverse
    /// registration order, like destructors. Registering into a phase that
    /// has already run is rejected and logged.
    void addShutdownHook(ShutdownPhase phase, const char* name, Hook hook);

    /// Run one phase. Earlier phases that never ran are run first, so the
    /// order holds even if a caller skips one. Idempotent.
    void runShutdownPhase(ShutdownPhase phase);

    bool hasRun(ShutdownPhase phase) const;

private:
    struct NamedHook {
        const char* name;
        Hook hook;
    };

    HomeAudio m_audio;
    HomePlaytime m_playtime;
    SteamGridDbManager m_steamGridDb;
    AppConfig m_config;
    nxui::ThreadPool m_threadPool{2};
    AppletLauncher m_launcher;
    std::function<void()> m_requestExit;
    std::function<void()> m_frontendQuiesce;

#ifdef SWITCHU_MENU
    switchu::smi::SystemStatus m_systemStatus{};
    switchu::smi::MenuTransitionReason m_transitionReason = switchu::smi::MenuTransitionReason::Unknown;
    uint64_t m_menuMainTick = 0;
    uint32_t m_menuMainCore = 0;
    bool m_safeModeRequested = false;
    bool m_safeModeNotice = false;
    switchu::smi::MenuClosingArgs m_closingTrace{};
    bool m_closingArmed = false;
#endif

    std::vector<NamedHook> m_hooks[kShutdownPhaseCount];
    int m_lastPhaseRun = -1;
};

} // namespace home
