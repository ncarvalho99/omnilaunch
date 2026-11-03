#pragma once
// HomeCore: process-lifetime owner of the HOME services shared by every home
// frontend (Classic today, Cards later). It is created by main() before the
// nxui::Application and outlives it, so a frontend Activity can be replaced at
// runtime without tearing the services down.
//
// Services move in here one at a time with their initialization, callbacks and
// finalization. C2 owns audio/accessibility and their preset/playback state.

#include "home/HomeAudio.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

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

    HomeAudio& audioServices() { return m_audio; }
    ~HomeCore();

    HomeCore(const HomeCore&) = delete;
    HomeCore& operator=(const HomeCore&) = delete;

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
    std::vector<NamedHook> m_hooks[kShutdownPhaseCount];
    int m_lastPhaseRun = -1;
};

} // namespace home
