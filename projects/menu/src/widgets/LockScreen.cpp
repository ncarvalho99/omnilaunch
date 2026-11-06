#include "LockScreen.hpp"
#include "LockPassword.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Animation.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace {

// Every button the sequence accepts, and the same set that counts as activity.
// The list is deliberately wide: whichever button the owner reaches for first
// is the one they will press three times.
constexpr nxui::Button kButtons[] = {
    nxui::Button::A,      nxui::Button::B,      nxui::Button::X,      nxui::Button::Y,
    nxui::Button::L,      nxui::Button::R,      nxui::Button::ZL,     nxui::Button::ZR,
    nxui::Button::Plus,   nxui::Button::Minus,  nxui::Button::DUp,    nxui::Button::DDown,
    nxui::Button::DLeft,  nxui::Button::DRight, nxui::Button::LStick, nxui::Button::RStick,
};

// A touch counts as its own button, so the sequence still works handheld with
// no controller attached. The value sits outside the HID button mask.
constexpr std::uint64_t kTouchButton = 1ull << 63;

// The stick has to be pushed rather than breathed on. A drifting stick must not
// hold the lock off forever, which is the failure that makes an idle timer
// useless on worn hardware.
constexpr float kStickActivity = 0.35f;

constexpr float kFadeInDur  = 0.45f;
constexpr float kFadeOutDur = 0.26f;
// How long a half-finished sequence is remembered. Long enough to be unhurried,
// short enough that a bag pressing one button twice never adds up to three.
constexpr float kSequenceLife = 2.5f;
constexpr float kBeadPopDur   = 0.2f;
constexpr float kWakeDisplayDuration = 10.f;
// How long input is ignored after the screen locks, so the press that asked for
// the lock cannot also count as the first press asking to leave it.
constexpr float kInputGrace = 0.4f;
// If a suspend request does not actually put the console under, do not spend the
// next frame asking again. One try, then a pause long enough to be noticed.
constexpr float kSleepRetryCooldown = 30.f;
// One full sweep of the slow drift, in seconds, and how far it travels.
constexpr float kDriftPeriod = 47.f;
constexpr float kDriftRange  = 7.f;

} // namespace

void LockScreen::setSleepDelaySeconds(float seconds) {
    if (seconds < 0.f) seconds = -1.f;
    if (m_sleepDelay == seconds) return;
    DebugLog::log("[lock] suspend plan is now %.0fs", (double)seconds);
    m_sleepDelay = seconds;
    m_sleepIdle = 0.f;
}

void LockScreen::resetSleepCountdown() {
    m_sleepIdle = 0.f;
    m_sleepCooldown = 0.f;
}

void LockScreen::lockNow() {
    if (m_state == State::Locked) return;
    DebugLog::log("[lock] locked");
    m_inputGrace = kInputGrace;
    m_state = State::Locked;
    m_presses = 0;
    m_sequenceButton = 0;
    m_sequenceAge = 0.f;
    m_beadPop = 0.f;
    m_wakeDisplaySeconds = 0.f;
    m_clockTimer = 1.f;
    if (m_lockedCb) m_lockedCb();
}

void LockScreen::unlockNow() {
    beginRelease();
}

void LockScreen::setPassword(std::size_t length, std::function<bool(const std::string&)> check,
                             std::function<void()> clear) {
    m_pwLength = length;
    m_pwCheck = std::move(check);
    m_pwClear = std::move(clear);
    m_pwEntry.clear();
}

void LockScreen::beginPasswordSetup(std::function<void(const std::string&)> done) {
    m_setupDone = std::move(done);
    m_setupStage = 1;
    m_setupFirst.clear();
    m_pwEntry.clear();
    lockNow();
    DebugLog::log("[lock] password setup started");
}

void LockScreen::endSetup() {
    m_setupStage = 0;
    m_setupFirst.clear();
    m_pwEntry.clear();
    m_setupDone = nullptr;
    beginRelease();
}

void LockScreen::updateSetup(nxui::Input& input) {
    using B = nxui::Button;
    if (m_inputGrace > 0.f)
        return;
    char c = 0;
    if (input.isDown(B::DUp)) c = 'U';
    else if (input.isDown(B::DDown)) c = 'D';
    else if (input.isDown(B::DLeft)) c = 'L';
    else if (input.isDown(B::DRight)) c = 'R';
    if (c) {
        if (m_pwEntry.size() < lockpw::kMax) {
            m_pwEntry.push_back(c);
            m_beadPop = kBeadPopDur;
            if (m_progressCb) m_progressCb();
        }
        return;
    }
    if (input.isDown(B::B)) {
        if (!m_pwEntry.empty()) {
            m_pwEntry.pop_back();
        } else {
            DebugLog::log("[lock] password setup cancelled");
            endSetup();
        }
        return;
    }
    if (!input.isDown(B::Plus) || m_pwEntry.size() < lockpw::kMin)
        return;
    if (m_setupStage == 1) {
        m_setupFirst = m_pwEntry;
        m_pwEntry.clear();
        m_setupStage = 2;
        if (m_progressCb) m_progressCb();
        return;
    }
    if (m_pwEntry != m_setupFirst) {
        DebugLog::log("[lock] password setup mismatch; starting over");
        m_setupFirst.clear();
        m_pwEntry.clear();
        m_setupStage = 1;
        m_pwShake = 0.4f;
        return;
    }
    auto done = m_setupDone;
    const std::string presses = m_pwEntry;
    DebugLog::log("[lock] password setup confirmed (%zu presses)", presses.size());
    endSetup();
    if (done) done(presses);
}

bool LockScreen::homePressed() {
    if (m_state != State::Locked)
        return false;
    if (m_setupStage != 0) {
        DebugLog::log("[lock] password setup cancelled by HOME");
        endSetup();
        return true;
    }
    if (m_pwLength == 0) {
        beginRelease();
        return true;
    }
    // A password lock is not opened by HOME; HOME starts the recovery window.
    m_wakeDisplaySeconds = kWakeDisplayDuration;
    m_recoveryWindow = lockpw::kRecoveryWindow;
    m_recoveryHeld = 0.f;
    DebugLog::log("[lock] HOME on password lock: recovery window open");
    return false;
}

void LockScreen::updatePassword(float dt, nxui::Input& input) {
    using B = nxui::Button;
    if (m_pwShake > 0.f)
        m_pwShake = std::max(0.f, m_pwShake - dt);
    if (m_recoveryWindow > 0.f) {
        m_recoveryWindow = std::max(0.f, m_recoveryWindow - dt);
        // Combo: Minus + Plus (very easy to hold with both thumbs).
        // Also accepts L + R + ZL + ZR + Minus for compatibility.
        const bool combo = (input.isHeld(B::Minus) && input.isHeld(B::Plus)) ||
                           (input.isHeld(B::L) && input.isHeld(B::R) && input.isHeld(B::ZL) &&
                            input.isHeld(B::ZR) && input.isHeld(B::Minus));
        m_recoveryHeld = combo ? m_recoveryHeld + dt : 0.f;
        m_wakeDisplaySeconds = std::max(m_wakeDisplaySeconds, 1.f);
        if (m_recoveryHeld >= lockpw::kRecoveryHold) {
            DebugLog::log("[lock] recovery combo held: password cleared");
            if (m_pwClear) m_pwClear();
            m_pwLength = 0;
            m_recoveryWindow = 0.f;
            m_recoveryHeld = 0.f;
            m_pwEntry.clear();
            beginRelease();
            return;
        }
        if (combo)
            return; // the combo buttons are not password presses
    }
    if (m_inputGrace > 0.f)
        return;
    char c = 0;
    if (input.isDown(B::DUp)) c = 'U';
    else if (input.isDown(B::DDown)) c = 'D';
    else if (input.isDown(B::DLeft)) c = 'L';
    else if (input.isDown(B::DRight)) c = 'R';
    else if (input.isDown(B::B) && !m_pwEntry.empty()) { m_pwEntry.pop_back(); return; }
    if (!c)
        return;
    m_wakeDisplaySeconds = kWakeDisplayDuration; // typing keeps the lock lit
    m_pwEntry.push_back(c);
    m_beadPop = kBeadPopDur;
    if (m_pwEntry.size() < m_pwLength) {
        if (m_progressCb) m_progressCb();
        return;
    }
    const bool ok = m_pwCheck && m_pwCheck(m_pwEntry);
    m_pwEntry.clear();
    if (ok) {
        DebugLog::log("[lock] password accepted");
        beginRelease();
    } else {
        DebugLog::log("[lock] password rejected");
        m_pwShake = 0.4f;
        if (m_progressCb) m_progressCb();
    }
}

void LockScreen::showAfterSystemWake() {
    if (m_state != State::Locked)
        return;
    DebugLog::log("[lock] system wake reveal for %.0fs", (double)kWakeDisplayDuration);
    m_wakeDisplaySeconds = kWakeDisplayDuration;
}

void LockScreen::reset() {
    m_state = State::Open;
    m_fade = 0.f;
    m_inputGrace = 0.f;
    m_presses = 0;
    m_sequenceButton = 0;
    m_wakeDisplaySeconds = 0.f;
}

void LockScreen::beginRelease() {
    if (m_state != State::Locked) return;
    DebugLog::log("[lock] unlocking after %d press(es)", m_presses);
    m_state = State::Releasing;
    m_presses = 0;
    m_sequenceButton = 0;
    if (m_unlockedCb) m_unlockedCb();
}

bool LockScreen::consumeActivity(nxui::Input& input) {
    for (nxui::Button b : kButtons) {
        if (input.isHeld(b)) return true;
    }
    if (input.isTouching()) return true;
    return std::fabs(input.leftStickX())  > kStickActivity ||
           std::fabs(input.leftStickY())  > kStickActivity ||
           std::fabs(input.rightStickX()) > kStickActivity ||
           std::fabs(input.rightStickY()) > kStickActivity;
}

std::uint64_t LockScreen::pressedUnlockButton(nxui::Input& input) const {
    for (nxui::Button b : kButtons) {
        if (input.isDown(b))
            return static_cast<std::uint64_t>(b);
    }
    return input.touchDown() ? kTouchButton : 0;
}

void LockScreen::updateClock(float dt) {
    m_clockTimer += dt;
    if (m_clockTimer < 1.f && !m_timeStr.empty()) return;
    m_clockTimer = 0.f;

    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    if (!tm) return;
    char buf[64];
    if (m_use12Hour) {
        int hour = tm->tm_hour % 12;
        if (hour == 0) hour = 12;
        std::snprintf(buf, sizeof(buf), "%d:%02d %s", hour, tm->tm_min,
                      tm->tm_hour >= 12 ? "PM" : "AM");
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
    }
    m_timeStr = buf;
    std::snprintf(buf, sizeof(buf), "%02d/%02d/%04d",
                  tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);
    m_dateStr = buf;
}

void LockScreen::update(float dt, nxui::Input& input, bool sleepSuppressed) {
    if (m_beadPop > 0.f)
        m_beadPop = std::max(0.f, m_beadPop - dt);
    if (m_inputGrace > 0.f)
        m_inputGrace = std::max(0.f, m_inputGrace - dt);
    if (m_sleepCooldown > 0.f)
        m_sleepCooldown = std::max(0.f, m_sleepCooldown - dt);

    // The suspend countdown runs in every state. Locking the screen is not the
    // end of being idle: on a real console the lock screen is what the owner
    // walks away from, and the console still goes to sleep behind it.
    if (m_sleepDelay < 0.f || sleepSuppressed || consumeActivity(input) ||
        pressedUnlockButton(input) != 0) {
        m_sleepIdle = 0.f;
    } else {
        m_sleepIdle += dt;
        if (m_sleepIdle >= m_sleepDelay && m_sleepCooldown <= 0.f) {
            DebugLog::log("[lock] idle %.0fs reached suspend plan %.0fs, requesting suspend",
                          (double)m_sleepIdle, (double)m_sleepDelay);
            m_sleepIdle = 0.f;
            m_sleepCooldown = kSleepRetryCooldown;
            if (m_sleepCb) m_sleepCb();
        }
    }

    switch (m_state) {
        case State::Open:
            // Nothing counts down here any more. The screen locks when the
            // console goes under and when it comes back, not on a timer of
            // its own.
            m_fade = std::max(0.f, m_fade - dt / kFadeOutDur);
            break;
        case State::Locked: {
            m_fade = std::min(1.f, m_fade + dt / kFadeInDur);
            m_driftPhase = std::fmod(m_driftPhase + dt, kDriftPeriod);
            updateClock(dt);

            // Wake press is deliberately not one of the three unlock presses.
            if (hidesScene()) {
                if (m_inputGrace <= 0.f && pressedUnlockButton(input) != 0)
                    m_wakeDisplaySeconds = kWakeDisplayDuration;
                break;
            }

            if (m_wakeDisplaySeconds > 0.f)
                m_wakeDisplaySeconds = std::max(0.f, m_wakeDisplaySeconds - dt);

            if (m_setupStage != 0) {
                // Setup never goes to the black low-power frame.
                m_wakeDisplaySeconds = kWakeDisplayDuration;
                if (m_pwShake > 0.f)
                    m_pwShake = std::max(0.f, m_pwShake - dt);
                updateSetup(input);
                break;
            }
            if (m_pwLength > 0) {
                updatePassword(dt, input);
                break;
            }

            if (m_sequenceButton != 0) {
                m_sequenceAge += dt;
                if (m_sequenceAge > kSequenceLife) {
                    // Drain one step rather than resetting outright, so a slow
                    // hand is not punished as hard as a bumped pocket.
                    m_sequenceAge = 0.f;
                    if (--m_presses <= 0) {
                        m_presses = 0;
                        m_sequenceButton = 0;
                    }
                }
            }

            const std::uint64_t pressed = m_inputGrace > 0.f ? 0 : pressedUnlockButton(input);
            if (pressed == 0)
                break;

            if (pressed != m_sequenceButton) {
                // A different button starts its own count at one. The console
                // asks for the same button three times, and so does this.
                m_sequenceButton = pressed;
                m_presses = 1;
            } else {
                ++m_presses;
            }
            m_sequenceAge = 0.f;
            m_beadPop = kBeadPopDur;

            if (m_presses >= kUnlockPresses) {
                beginRelease();
            } else {
                DebugLog::log("[lock] unlock press %d/%d", m_presses, kUnlockPresses);
                if (m_progressCb) m_progressCb();
            }
            break;
        }
        case State::Releasing: {
            m_fade = std::max(0.f, m_fade - dt / kFadeOutDur);
            if (m_fade <= 0.f)
                m_state = State::Open;
            break;
        }
    }
}

void LockScreen::render(nxui::Renderer& ren) {
    if (m_fade <= 0.001f || !m_theme)
        return;

    auto& i18n = nxui::I18n::instance();
    const float w = static_cast<float>(ren.width());
    const float h = static_cast<float>(ren.height());
    const float fade = std::clamp(m_fade, 0.f, 1.f);

    // Present one all-black frame before WiiUMenuApp stops rendering. This
    // removes all per-frame GPU work and lets OLED panels draw almost no power.
    // LCD backlight control belongs to Horizon, so physical panel sleep still
    // follows the console's normal auto-sleep setting.
    if (hidesScene()) {
        ren.drawRect({0.f, 0.f, w, h}, nxui::Color(0.f, 0.f, 0.f, 1.f));
        return;
    }

    const float ease = nxui::Easing::outCubic(fade);

    // The scrim is the theme's own background rather than flat black, so a
    // light theme locks to a pale screen instead of a hole.
    ren.drawRect({0.f, 0.f, w, h}, m_theme->background.withAlpha(0.93f * ease));

    const float drift = std::sin(m_driftPhase / kDriftPeriod * 6.2831853f) * kDriftRange;
    // Everything rises into place; on the way out it simply fades.
    const float rise = (1.f - ease) * 14.f;

    if (m_clockFont && !m_timeStr.empty()) {
        // Left-anchored rather than centred. The clock is the one thing worth
        // reading here, and against the left margin it sits where the menu's own
        // headings sit instead of turning the screen into a poster.
        // Drawn at 1:1. The face behind this is loaded at the size it appears
        // at, so nothing here is an upscale of a smaller bitmap.
        const float clockScale = 1.f;
        const nxui::Vec2 timeSz = m_clockFont->measure(m_timeStr);
        const float x = w * 0.075f + drift;
        const float y = h * 0.34f + rise + drift * 0.4f;
        ren.drawText(m_timeStr, {x, y}, m_clockFont,
                     m_theme->textPrimary.withAlpha(ease), clockScale);

        if (m_captionFont && !m_dateStr.empty()) {
            ren.drawText(m_dateStr, {x + 4.f, y + timeSz.y * clockScale + 10.f},
                         m_captionFont,
                         m_theme->textSecondary.withAlpha(0.85f * ease), 1.f);
        }
    }

    // The beads: one per press the sequence still owes. They are the only moving
    // part, and the only thing on screen that answers "what do I do now".
    // Password mode: one bead per password press, shaken after a wrong entry.
    const bool setup = m_setupStage != 0;
    const bool pw = m_pwLength > 0 || setup;
    const int beads = setup ? static_cast<int>(std::max<std::size_t>(lockpw::kMin, m_pwEntry.size()))
                     : pw  ? static_cast<int>(m_pwLength) : kUnlockPresses;
    const int filledCount = pw ? static_cast<int>(m_pwEntry.size()) : m_presses;
    const float beadW = pw ? 40.f : 58.f, beadH = 12.f, beadGap = 16.f;
    const float totalW = beads * beadW + (beads - 1) * beadGap;
    const float beadY = h - 104.f + rise + drift * 0.4f;
    const float shake = m_pwShake > 0.f ? std::sin(m_pwShake * 60.f) * 10.f * (m_pwShake / 0.4f) : 0.f;
    const float beadX0 = (w - totalW) * 0.5f + drift + shake;
    const float pop = m_beadPop > 0.f ? (m_beadPop / kBeadPopDur) : 0.f;

    for (int i = 0; i < beads; ++i) {
        const bool filled = i < filledCount;
        const bool newest = filled && i == filledCount - 1;
        const float grow = newest ? nxui::Easing::outCubic(pop) * 3.f : 0.f;
        const nxui::Rect r{beadX0 + i * (beadW + beadGap) - grow, beadY - grow * 0.5f,
                           beadW + grow * 2.f, beadH + grow};
        if (filled) {
            ren.drawRoundedRect(r, m_theme->textPrimary.withAlpha(ease), r.height * 0.5f);
        } else {
            ren.drawRoundedRect(r, m_theme->panelBase.withAlpha(0.5f * ease), r.height * 0.5f);
            ren.drawRoundedRectOutline(r, m_theme->panelBorder.withAlpha(0.9f * ease),
                                       r.height * 0.5f, 2.f);
        }
    }

    if (m_captionFont) {
        std::string hint;
        if (setup)
            hint = m_setupStage == 1
                ? i18n.tr("lock_screen.setup_hint", "New password: 4 to 8 D-pad presses, then +. B erases or cancels")
                : i18n.tr("lock_screen.confirm_hint", "Enter the same password again, then +");
        else if (!pw)
            hint = i18n.tr("lock_screen.hint", "Press the same button three times to unlock");
        else if (m_recoveryWindow > 0.f)
            hint = i18n.tr("lock_screen.recovery_hint",
                           "Forgot it? Hold Minus + Plus for 3 seconds to remove the password");
        else
            hint = i18n.tr("lock_screen.password_hint", "Enter your password with the D-pad");
        const nxui::Vec2 sz = m_captionFont->measure(hint);
        ren.drawText(hint, {(w - sz.x) * 0.5f + drift, beadY - sz.y - 22.f},
                     m_captionFont, m_theme->textSecondary.withAlpha(0.8f * ease), 1.f);

        // Visual progress bar while holding the recovery combo
        if (m_recoveryHeld > 0.f) {
            const float barW = 200.f, barH = 6.f;
            const float progress = std::clamp(m_recoveryHeld / lockpw::kRecoveryHold, 0.f, 1.f);
            const nxui::Rect bgR{(w - barW) * 0.5f, beadY - sz.y - 34.f, barW, barH};
            ren.drawRoundedRect(bgR, {0.2f, 0.2f, 0.25f, 0.6f * ease}, 3.f);
            ren.drawRoundedRect({bgR.x, bgR.y, barW * progress, barH}, {0.36f, 0.68f, 1.f, 0.95f * ease}, 3.f);
        }
    }
}
