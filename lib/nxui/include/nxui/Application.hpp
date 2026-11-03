#pragma once
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Input.hpp>
#include <functional>
#include <memory>
#include <cstdint>

namespace nxui {

class Activity;

struct ApplicationInitializeTrace {
    std::uint64_t initializeStartTick = 0;
    std::uint64_t gpuReadyTick = 0;
    std::uint64_t rendererReadyTick = 0;
    std::uint64_t blankFrameTick = 0;
    std::uint64_t activityCreateStartTick = 0;
    std::uint64_t activityCreateEndTick = 0;
};

struct ApplicationShutdownTrace {
    std::uint64_t shutdownStartTick = 0;
    std::uint64_t gpuDrainStartTick = 0;
    std::uint64_t gpuDrainEndTick = 0;
    std::uint64_t activityDestroyStartTick = 0;
};

/// Top-level application object.
/// Owns the GPU device, Renderer, and Input, runs the main loop,
/// and delegates lifecycle events to the attached Activity.
///
/// Usage:
///   nxui::Application app;
///   app.setActivity(std::make_unique<MyActivity>());
///   if (app.initialize()) app.run();
///   app.shutdown();
class Application {
public:
    Application() = default;
    ~Application();

    /// Onde a Application relata o que acontece fora do controle dela. O
    /// pedido de encerramento do sistema so ia para svcOutputDebugString, que
    /// nao esta em lugar nenhum quando se abre o log depois: o menu saia
    /// limpo, o daemon relancava, e o registro terminava sem uma palavra --
    /// indistinguivel de uma queda.
    void setLogSink(std::function<void(const char*)> sink) { m_logSink = std::move(sink); }

    /// Attach the main activity. Must be called before initialize().
    void setActivity(std::unique_ptr<Activity> activity);

    /// Queue an activity change from inside the main loop.
    void requestActivity(std::unique_ptr<Activity> activity);

    /// Initialise GPU, Renderer, Input, then call activity->onCreate().
    bool initialize();

    /// Enter the main loop (blocks until requestExit() is called).
    void run();

    /// Shutdown: activity->onDestroy(), then GPU/Renderer cleanup.
    void shutdown();

    /// One-shot callback run by shutdown() after the last Activity has been
    /// destroyed and before the Renderer and GPU are torn down. Owners of GPU
    /// resources that outlive an Activity release them here.
    void setBeforeGpuShutdown(std::function<void()> callback) {
        m_beforeGpuShutdown = std::move(callback);
    }

    // Accessors used by Activity
    GpuDevice& gpu()       { return m_gpu; }
    Renderer&  renderer()  { return *m_renderer; }
    Input&     input()     { return m_input; }
    int        navHoldFrames() const { return m_navHoldFrames; }

    void requestExit()       { m_running = false; }
    bool isRunning() const   { return m_running; }

    /// Disable/enable GPU rendering.  When disabled the main loop still
    /// runs input + update, but skips beginFrame/endFrame so the GPU is
    /// free for whichever app currently owns the foreground.
    void setRenderEnabled(bool e) { m_renderEnabled = e; }
    bool renderEnabled() const    { return m_renderEnabled; }

    const ApplicationInitializeTrace& initializeTrace() const { return m_initializeTrace; }
    const ApplicationShutdownTrace& shutdownTrace() const { return m_shutdownTrace; }

    // Called once, after the next real activity frame has been submitted.
    // Registering resets first-input/frame capture, which also supports the
    // tutorial handing control to the menu without reporting a tutorial frame.
    void setFirstFrameCallback(std::function<void(std::uint64_t, std::uint64_t)> callback) {
        m_firstFrameCallback = std::move(callback);
        m_firstInputTick = 0;
        m_firstFrameTick = 0;
    }

    // How long the previous loop iteration actually took, before the clamp the
    // activity delta carries. The clamp exists so a stall does not teleport
    // every animation, but it also rewrote any frame over 100 ms as a 16 ms
    // frame -- which meant the worst-frame figure in the menu's perf line could
    // not report a stall at all, and a measured 287 ms Theme Shop opening frame
    // was being logged as 84.8 ms. Measure with this; animate with the delta.
    float lastFrameSeconds() const { return m_lastFrameSeconds; }

private:
    void dispatchInput();
    bool applyPendingActivity();

    GpuDevice  m_gpu;
    std::unique_ptr<Renderer> m_renderer;
    Input      m_input;

    std::unique_ptr<Activity> m_activity;
    std::unique_ptr<Activity> m_pendingActivity;
    bool m_running = true;
    std::function<void(const char*)> m_logSink;
    std::function<void()> m_beforeGpuShutdown;
    bool m_renderEnabled = true;
    ApplicationInitializeTrace m_initializeTrace{};
    ApplicationShutdownTrace m_shutdownTrace{};
    std::function<void(std::uint64_t, std::uint64_t)> m_firstFrameCallback;
    std::uint64_t m_firstInputTick = 0;
    std::uint64_t m_firstFrameTick = 0;
    float m_lastFrameSeconds = 0.f;
    int  m_navDebounce = 0;
    // Frames a direction has been held for. Navigation used to fire only on the
    // frame a button went down, so holding one moved a single icon and stopped.
    int  m_navHoldFrames = 0;
};

} // namespace nxui
