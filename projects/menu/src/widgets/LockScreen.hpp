#pragma once
#include <nxui/core/Types.hpp>
#include <nxui/Theme.hpp>
#include <functional>
#include <string>

namespace nxui {
class Renderer;
class Font;
class Input;
}

/// Wake lock screen for the home menu, in the shape the console's own lock has:
/// the scene goes behind a scrim, the clock is the only thing still worth
/// reading, and getting back in costs three presses of one button so a
/// controller in a bag cannot do it by accident.
///
/// It is not something the owner configures, and it has no idle countdown of
/// its own. The console suspends on its own sleep plan and this is what greets
/// the owner on the other side, exactly as the stock home menu behaves. Locking
/// a console that is still awake would only light the panel and drain the
/// battery in front of a suspend that was already coming.
///
/// It does own the idle count that asks for that suspend, because it already
/// has the activity detector that count needs.
///
/// Presentation and input only. Locking and unlocking are reported through the
/// callbacks so the sound, the voice announcement, and anything else the app
/// wants to do stay with the app.
class LockScreen {
public:
    /// Presses of the same button needed to get back in.
    static constexpr int kUnlockPresses = 3;

    /// Idle delay before the console is asked to genuinely suspend, in
    /// seconds; negative means never. The value comes from the console's own
    /// handheld or docked sleep plan and is pushed in by the app, because
    /// reading system settings is not this widget's job.
    void setSleepDelaySeconds(float seconds);
    /// Asked for once when the sleep delay expires. Only the app knows how to
    /// reach the daemon that owns the real sleep sequence.
    void onSleepRequested(std::function<void()> cb) { m_sleepCb = std::move(cb); }
    /// Restarts the suspend countdown, so waking a console that just slept on
    /// its own does not immediately ask it to sleep again.
    void resetSleepCountdown();

    void setFonts(nxui::Font* clockFont, nxui::Font* captionFont) {
        m_clockFont = clockFont;
        m_captionFont = captionFont;
    }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void setUse12HourClock(bool enabled) { m_use12Hour = enabled; }

    void onLocked(std::function<void()> cb)   { m_lockedCb = std::move(cb); }
    void onUnlocked(std::function<void()> cb) { m_unlockedCb = std::move(cb); }
    /// Every press that is not the third one, so the app can click.
    void onProgress(std::function<void()> cb) { m_progressCb = std::move(cb); }

    bool isLocked() const { return m_state != State::Open; }
    /// True once the scrim is opaque enough that the scene behind it is not
    /// worth drawing. Kept separate from isLocked() so the fade still shows it.
    bool hidesScene() const {
        return m_state == State::Locked && m_fade >= 0.995f && m_wakeDisplaySeconds <= 0.f;
    }

    void lockNow();
    /// Unlock immediately for a system-owned gesture such as HOME. Unlike the
    /// ordinary button sequence, the gesture has already been authenticated by
    /// the console's system layer.
    void unlockNow();

    /// Password mode (E9, widgets/LockPassword.hpp). `length` 0 = no password:
    /// the three-press unlock as before. Otherwise the D-pad entry of that
    /// length is checked through `check`, and HOME (homePressed) no longer
    /// unlocks: it opens the recovery window instead (combo held clears the
    /// password through `clear` and unlocks).
    void setPassword(std::size_t length, std::function<bool(const std::string&)> check,
                     std::function<void()> clear);
    /// HOME arrived while locked. Returns true when it unlocked (no password).
    bool homePressed();
    bool passwordActive() const { return m_pwLength > 0; }
    /// Shows the lock in setup mode: the owner enters a new password (4-8
    /// D-pad presses, + to confirm, B to erase / cancel) twice. `done` gets the
    /// presses on a match; cancel (B on empty entry, or HOME) unlocks with
    /// nothing changed.
    void beginPasswordSetup(std::function<void(const std::string&)> done);
    /// Makes the locked UI visible after Horizon wakes the console. It does not
    /// unlock or count as one of the three required button presses.
    void showAfterSystemWake();
    /// Cancels the lock without an unlock report.
    void reset();

    /// `sleepSuppressed` covers the moments where counting towards a suspend
    /// is wrong: a title is running, or the owner is in the middle of something
    /// the menu started itself. The menu's own black low-power frame is
    /// deliberately not one of them, or a locked console would sit lit forever
    /// waiting for a sleep that never comes.
    void update(float dt, nxui::Input& input, bool sleepSuppressed);
    void render(nxui::Renderer& ren);

private:
    enum class State { Open, Locked, Releasing };

    bool  consumeActivity(nxui::Input& input);
    std::uint64_t pressedUnlockButton(nxui::Input& input) const;
    void  updateClock(float dt);
    void  beginRelease();

    State m_state = State::Open;
    float m_fade = 0.f;

    // Suspend countdown. It keeps running while the lock screen is already up:
    // being locked is not being busy.
    float m_sleepDelay = -1.f;
    float m_sleepIdle = 0.f;
    float m_sleepCooldown = 0.f;

    // Input is ignored for a moment after locking. Without this the very button
    // press that chose Lock Now is still down on the next frame and is counted
    // as the first of the three presses needed to get back out.
    float m_inputGrace = 0.f;

    // Which button is being repeated, as a raw HID mask so any button the pad
    // reports can be the one; 0 while no sequence is in progress.
    std::uint64_t m_sequenceButton = 0;
    int   m_presses = 0;
    float m_sequenceAge = 0.f;
    float m_beadPop = 0.f;
    // First physical button after black low-power screen only reveals UI.
    float m_wakeDisplaySeconds = 0.f;

    // Slow travel of the whole composition. A lock screen is the one place in
    // this menu that can hold a bright, unchanging shape for hours, which is
    // the condition panels burn in under.
    float m_driftPhase = 0.f;

    std::string m_timeStr;
    std::string m_dateStr;
    float m_clockTimer = 1.f;
    bool  m_use12Hour = false;

    nxui::Font* m_clockFont = nullptr;
    nxui::Font* m_captionFont = nullptr;
    const nxui::Theme* m_theme = nullptr;

    std::function<void()> m_lockedCb;
    std::function<void()> m_unlockedCb;
    std::function<void()> m_progressCb;
    std::function<void()> m_sleepCb;

    // Password mode.
    std::size_t m_pwLength = 0;
    std::string m_pwEntry;
    float m_pwShake = 0.f;      // wrong entry feedback
    float m_recoveryWindow = 0.f; // > 0 after HOME on a password lock
    float m_recoveryHeld = 0.f;
    std::function<bool(const std::string&)> m_pwCheck;
    std::function<void()> m_pwClear;
    int m_setupStage = 0; // 0 none, 1 first entry, 2 confirmation
    std::string m_setupFirst;
    std::function<void(const std::string&)> m_setupDone;
    void updatePassword(float dt, nxui::Input& input);
    void updateSetup(nxui::Input& input);
    void endSetup();
};
