#include <nxui/Application.hpp>
#include <nxui/Activity.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/focus/FocusManager.hpp>
#include <switch.h>

namespace nxui {

Application::~Application() {
    shutdown();
}

void Application::setActivity(std::unique_ptr<Activity> activity) {
    m_activity = std::move(activity);
    if (m_activity) m_activity->m_app = this;
}

void Application::requestActivity(std::unique_ptr<Activity> activity) {
    m_pendingActivity = std::move(activity);
    if (m_pendingActivity)
        m_pendingActivity->m_app = this;
}

bool Application::applyPendingActivity() {
    if (!m_pendingActivity)
        return true;

    AnimationManager::instance().clear();
    if (m_activity)
        m_activity->onDestroy();

    m_activity = std::move(m_pendingActivity);
    m_navDebounce = 0;

    if (m_activity) {
        m_activity->m_rootBox->setRect({0, 0, (float)m_gpu.width(), (float)m_gpu.height()});
        return m_activity->onCreate();
    }
    return true;
}

bool Application::initialize() {
    m_initializeTrace = {};
    m_initializeTrace.initializeStartTick = armGetSystemTick();
    if (!m_gpu.initialize()) {
        std::fprintf(stderr, "[Application] m_gpu.initialize() failed\n");
        return false;
    }
    m_initializeTrace.gpuReadyTick = armGetSystemTick();

    m_renderer = std::make_unique<Renderer>(m_gpu);
    if (!m_renderer->initialize()) {
        std::fprintf(stderr, "[Application] m_renderer->initialize() failed\n");
        return false;
    }
    m_initializeTrace.rendererReadyTick = armGetSystemTick();

    m_input.initialize();

    // Present one clean frame immediately so that stale framebuffer
    // content from a previous process is never visible on screen.
    // Activities may replace the default black with a leave-frame splash.
    m_gpu.beginFrame();
    m_renderer->beginFrame();
    if (m_activity)
        m_activity->presentInitialFrame(*m_renderer);
    m_renderer->endFrame();
    m_gpu.endFrame();
    m_initializeTrace.blankFrameTick = armGetSystemTick();

    if (m_activity) {
        // Set the root box to cover the entire screen
        m_activity->m_rootBox->setRect({0, 0, (float)m_gpu.width(), (float)m_gpu.height()});
        m_initializeTrace.activityCreateStartTick = armGetSystemTick();
        if (!m_activity->onCreate()) {
            std::fprintf(stderr, "[Application] m_activity->onCreate() failed\n");
            return false;
        }
        m_initializeTrace.activityCreateEndTick = armGetSystemTick();
    }
    return true;
}

// ══════════════════════════════════════════════════════════════════════════════
// Automatic input dispatch
// ══════════════════════════════════════════════════════════════════════════════

void Application::dispatchInput() {
    if (!m_activity) return;

    Widget* root = m_activity->focusRoot();
    if (!root) return;      // nullptr = all input blocked this frame

    auto& fm = m_activity->focusManager();

    // Keep focus constrained to the currently active input root.
    // Without this, stale focus from another UI layer (e.g. home grid while
    // settings/dialog is active) can still receive A/button dispatches.
    auto isUnderRoot = [root](Widget* w) {
        for (Widget* it = w; it != nullptr; it = it->parent()) {
            if (it == root) return true;
        }
        return false;
    };
    Widget* curFocus = fm.current();
    if (!curFocus || !isUnderRoot(curFocus)) {
        if (root->isFocusable()) {
            fm.setFocus(root);
        } else {
            std::vector<Widget*> focusables;
            root->collectFocusable(focusables);
            if (!focusables.empty())
                fm.setFocus(focusables[0]);
        }
    }

    // Debounced D-pad and stick navigation.
    // For each direction: if the focused widget has an action for that
    // D-pad/stick button, fire it (consumed). Otherwise navigate spatially.
    bool anyDpad =
        m_input.isDown(Button::DLeft)   || m_input.isDown(Button::DRight)  ||
        m_input.isDown(Button::DUp)     || m_input.isDown(Button::DDown)   ||
        m_input.isDown(Button::LStickL) || m_input.isDown(Button::LStickR) ||
        m_input.isDown(Button::LStickU) || m_input.isDown(Button::LStickD) ||
        m_input.isDown(Button::RStickL) || m_input.isDown(Button::RStickR) ||
        m_input.isDown(Button::RStickU) || m_input.isDown(Button::RStickD);

    // Holding a direction moved one icon and stopped: this fired only on the
    // frame a button went down. It repeats while held now, after a pause long
    // enough that a deliberate single press is still a single step, and
    // speeding up after that so crossing a wide grid is not a queue of taps.
    // The repeat goes through the same path as a press, so a widget with its
    // own action for that direction -- the grid changing page at its edge --
    // keeps getting it.
    constexpr int kHoldBeforeRepeat = 26;  // ~430 ms
    constexpr int kRepeatSlow       = 8;   // ~130 ms for the first few
    constexpr int kRepeatFast       = 4;   // ~65 ms once it is clearly held
    constexpr int kFramesUntilFast  = 30;  // half a second of repeating
    constexpr int kStickRepeatSlow  = 13;
    constexpr int kStickRepeatFast  = 7;

    const bool anyDirHeld =
        m_input.isHeld(Button::DLeft)   || m_input.isHeld(Button::DRight)  ||
        m_input.isHeld(Button::DUp)     || m_input.isHeld(Button::DDown)   ||
        m_input.isHeld(Button::LStickL) || m_input.isHeld(Button::LStickR) ||
        m_input.isHeld(Button::LStickU) || m_input.isHeld(Button::LStickD) ||
        m_input.isHeld(Button::RStickL) || m_input.isHeld(Button::RStickR) ||
        m_input.isHeld(Button::RStickU) || m_input.isHeld(Button::RStickD);

    if (!anyDirHeld) {
        m_navHoldFrames = 0;
    } else {
        ++m_navHoldFrames;
    }

    // Circling the analog stick changes the held direction every few frames,
    // and a held stick is easier to overshoot with than the D-pad, so the stick
    // alone waits longer between steps and repeats about 1.6x slower
    // (PoloNX #158). The D-pad keeps its timing.
    const bool stickOnly = anyDirHeld &&
        !m_input.isHeld(Button::DLeft) && !m_input.isHeld(Button::DRight) &&
        !m_input.isHeld(Button::DUp)   && !m_input.isHeld(Button::DDown);

    bool repeating = false;
    if (m_navHoldFrames > kHoldBeforeRepeat) {
        const int since = m_navHoldFrames - kHoldBeforeRepeat;
        const int step  = since > kFramesUntilFast
            ? (stickOnly ? kStickRepeatFast : kRepeatFast)
            : (stickOnly ? kStickRepeatSlow : kRepeatSlow);
        repeating = (since % step) == 0;
    }

    if (m_navDebounce > 0) {
        --m_navDebounce;
    } else if (anyDpad || repeating) {
        m_navDebounce = stickOnly ? 10 : 6;  // ~170 / ~100 ms at 60 fps

        Widget* cur = fm.current();
        auto tryDir = [&](Button dpad, Button leftStick, Button rightStick, FocusDirection dir) {
            // While repeating, held counts as pressed -- otherwise nothing
            // would be true on a frame where no button changed.
            bool dpadDown  = m_input.isDown(dpad)  || (repeating && m_input.isHeld(dpad));
            bool leftStickDown = m_input.isDown(leftStick)
                              || (repeating && m_input.isHeld(leftStick));
            bool rightStickDown = m_input.isDown(rightStick)
                              || (repeating && m_input.isHeld(rightStick));
            if (!dpadDown && !leftStickDown && !rightStickDown) return;

            // Focused widget's action takes priority (no bubbling for D-pad)
            if (cur) {
                if (dpadDown && cur->fireAction(static_cast<uint64_t>(dpad))) return;
                if (leftStickDown && cur->fireAction(static_cast<uint64_t>(leftStick))) return;
                if (rightStickDown && cur->fireAction(static_cast<uint64_t>(rightStick))) return;
            }
            fm.navigate(dir, root);
        };

        tryDir(Button::DLeft,  Button::LStickL, Button::RStickL, FocusDirection::LEFT);
        tryDir(Button::DRight, Button::LStickR, Button::RStickR, FocusDirection::RIGHT);
        tryDir(Button::DUp,    Button::LStickU, Button::RStickU, FocusDirection::UP);
        tryDir(Button::DDown,  Button::LStickD, Button::RStickD, FocusDirection::DOWN);
    }

    // Dispatch non-D-pad actions with parent bubbling.
    // Exclude D-pad buttons so they aren't fired a second time.
    constexpr uint64_t kDpadMask =
        static_cast<uint64_t>(Button::DLeft)   | static_cast<uint64_t>(Button::DRight)  |
        static_cast<uint64_t>(Button::DUp)     | static_cast<uint64_t>(Button::DDown)   |
        static_cast<uint64_t>(Button::LStickL) | static_cast<uint64_t>(Button::LStickR) |
        static_cast<uint64_t>(Button::LStickU) | static_cast<uint64_t>(Button::LStickD) |
        static_cast<uint64_t>(Button::RStickL) | static_cast<uint64_t>(Button::RStickR) |
        static_cast<uint64_t>(Button::RStickU) | static_cast<uint64_t>(Button::RStickD);

    constexpr uint64_t kA = static_cast<uint64_t>(Button::A);
    bool pointerConsumesA = m_input.pointerConsumesButton(Button::A);
    uint64_t actionExcludeMask = kDpadMask;
    if (pointerConsumesA)
        actionExcludeMask |= kA;

    uint64_t consumed = fm.dispatchActions(m_input, actionExcludeMask);

    // Auto-activate with A.
    // If the focused widget didn't register an explicit addAction(A, ...),
    // fall through to the legacy activate() / setOnActivate() mechanism.
    if (!pointerConsumesA && !(consumed & kA) && m_input.isDown(Button::A)) {
        if (auto* w = fm.current())
            w->activate();
    }

    // Touch-based focus navigation.
    // Some screens own richer touch handling locally (drag/scroll/menus) and
    // should not also receive the generic focus-manager tap model.
    if (root->frameworkTouchEnabled())
        fm.handleTouch(m_input, root);
}

// ══════════════════════════════════════════════════════════════════════════════
// Main loop
// ══════════════════════════════════════════════════════════════════════════════

void Application::run() {
    uint64_t prevTick = armGetSystemTick();
    float idleSeconds = 0.f;
    bool skipIdleFrame = false;

    while (m_running) {
        // Serve a fila de mensagens do applet.
        //
        // Isto nao existia: o laco era while(m_running) puro, e nada nunca lia
        // as mensagens que o sistema envia -- foco, modo de operacao, pedido de
        // encerramento. Um applet que nao atende sua fila e um applet que o
        // sistema pode fechar a forca, e foi o que apareceu nos registros: uma
        // sessao a 60 fps, sem erro, sem relatorio de falha, terminando no meio
        // de uma linha e sem o "log end" que uma saida limpa escreve.
        //
        // Atendendo a fila, um pedido de encerramento vira saida ordenada: o
        // menu grava o que tem, comita o cartao e sai.
        if (!appletMainLoop()) {
            static const char kMsg[] = "[nxui] applet exit requested by the system";
            svcOutputDebugString(kMsg, sizeof(kMsg) - 1);
            // E tambem no log que alguem le. Sem isso, uma saida pedida pelo
            // sistema e uma queda sao a mesma coisa vista de fora: o registro
            // termina no meio e o daemon relanca o menu.
            if (m_logSink) m_logSink(kMsg);
            m_running = false;
            break;
        }

        uint64_t nowTick = armGetSystemTick();
        float dt = static_cast<float>(nowTick - prevTick)
                 / static_cast<float>(armGetSystemTickFreq());
        prevTick = nowTick;
        // Kept before the clamp. This is the only place that still knows how
        // long the frame really took; everything downstream sees the clamped
        // value, which is right for animation and wrong for measurement.
        m_lastFrameSeconds = dt;
        if (dt > 0.1f) dt = 0.016f;

        m_input.update();
        if (m_firstInputTick == 0)
            m_firstInputTick = armGetSystemTick();
        dispatchInput();

        if (m_activity) {
            m_activity->onUpdate(dt);
            if (!applyPendingActivity()) {
                m_running = false;
                break;
            }
            m_activity->m_rootBox->update(dt);

            if (m_renderEnabled) {
                // Static menus do not need a full GPU submission at 60 Hz.
                // Keep input/update responsive, but present every other frame
                // after the UI has been idle long enough for transitions to
                // settle. Any new input immediately restores 60 Hz.
                if (idleSeconds >= 2.f) {
                    skipIdleFrame = !skipIdleFrame;
                    if (skipIdleFrame) {
                        svcSleepThread(16'000'000ULL);
                        continue;
                    }
                }
                m_gpu.beginFrame();
                m_renderer->beginFrame();
                m_activity->m_rootBox->render(*m_renderer);
                m_activity->onRender(*m_renderer);
                m_renderer->endFrame();
                m_gpu.endFrame();
                if (m_firstFrameTick == 0 && m_firstInputTick != 0 && m_firstFrameCallback) {
                    m_firstFrameTick = armGetSystemTick();
                    auto callback = std::move(m_firstFrameCallback);
                    callback(m_firstInputTick, m_firstFrameTick);
                }
                m_activity->onAfterPresent(*m_renderer);
            } else {
                // Yield CPU while another app owns the foreground.
                svcSleepThread(100000000LL); // 100 ms
            }
        }
    }
}

void Application::shutdown() {
    m_shutdownTrace = {};
    m_shutdownTrace.shutdownStartTick = armGetSystemTick();
    // Clear all pending animations before destroying the activity so that
    // tween callbacks don't fire on already-destroyed widgets.
    AnimationManager::instance().clear();

    m_input.shutdown();

    // Nothing renders after the run loop exits. Drain the queue once before
    // the activity releases its texture graph; individual Texture destructors
    // can then retire resources without repeating a device-wide wait.
    m_shutdownTrace.gpuDrainStartTick = armGetSystemTick();
    m_gpu.beginBulkTeardown();
    m_shutdownTrace.gpuDrainEndTick = armGetSystemTick();

    if (m_activity) {
        m_shutdownTrace.activityDestroyStartTick = armGetSystemTick();
        m_activity->onDestroy();
        m_activity.reset();
    }
    m_pendingActivity.reset();
    m_renderer.reset();
    m_gpu.shutdown();
}

} // namespace nxui
