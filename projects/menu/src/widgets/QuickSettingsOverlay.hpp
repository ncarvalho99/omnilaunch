#pragma once

#include <nxui/widgets/Widget.hpp>
#include <nxui/core/Types.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/Theme.hpp>
#include "widgets/SelectionCursor.hpp"

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace nxui {
class Renderer;
}

class QuickSettingsOverlay : public nxui::Widget {
public:
    enum class ItemIndex {
        Brightness = 0,
        BgmVolume,
        SfxVolume,
        AirplaneMode,
        Wifi,
        ActivityLog,
        Bluetooth,   // extended mode only (Cards, C-UI-8)
        MiniPlayer,  // extended mode only
        PowerActions,
        Count
    };

    enum class PowerAction {
        Sleep = 0,
        Reboot,
        Shutdown,
        Hekate,      // extended mode only
        Count
    };

    struct Callbacks {
        std::function<void(float)> onBrightnessChanged;
        std::function<void(float)> onBgmVolumeChanged;
        std::function<void(float)> onSfxVolumeChanged;
        std::function<void(bool)>  onAirplaneModeToggled;
        std::function<void(bool)>  onWifiToggled;
        std::function<void()>      onActivityLogRequested;
        std::function<void()>      onSleepRequested;
        std::function<void()>      onRebootRequested;
        std::function<void()>      onShutdownRequested;
        std::function<void()>      onClose;
        std::function<void()>      onNavigateSfx;
        std::function<void()>      onActivateSfx;
        std::function<void()>      onToggleOffSfx;
        // Extended mode (Cards quick settings, HomeNX E6 / C-UI-8).
        std::function<bool()>        bluetoothEnabled;
        std::function<void(bool)>    onBluetoothToggled;
        std::function<std::string()> musicTitle;   ///< empty: nothing playing
        std::function<bool()>        musicPlaying;
        std::function<void()>        onMusicToggle;
        std::function<void(int)>     onMusicSkip;  ///< -1 previous, +1 next
        std::function<void()>        onHekateRequested;
    };

    QuickSettingsOverlay();
    ~QuickSettingsOverlay() override;

    void setFont(nxui::Font* f)      { m_font = f; }
    void setSmallFont(nxui::Font* f) { m_smallFont = f; }
    void setIconFont(nxui::Font* f)  { m_iconFont = f; }
    void setTheme(const nxui::Theme* t);
    void setInput(nxui::Input* input) { m_input = input; }
    void setCallbacks(const Callbacks& cb) { m_callbacks = cb; }
    /// Cards layout: mini player and Bluetooth instead of the activity log,
    /// and a fourth power tile (Hekate). Classic keeps the default layout.
    void setExtended(bool extended) { m_extended = extended; }

    void show();
    void hide();
    bool isActive() const { return m_active || m_animating; }
    bool isFullyVisible() const { return m_active && !m_animating; }

    // External real-time status update from daemon or system messages
    void setBatteryStatus(int percent, bool charging);

    // Initial state setters before showing
    void setInitialValues(float brightness, float bgmVolume, float sfxVolume,
                          bool airplaneMode, bool wifiEnabled);

    void handleTouch(nxui::Input& input);
    void update(float dt) override;
    void render(nxui::Renderer& ren) override;

private:
    void setupNavigationActions();
    void refreshHardwareStatus();
    void adjustSlider(ItemIndex item, float delta);
    void toggleItem(ItemIndex item);
    void triggerPowerAction(PowerAction action);
    void updateCursorTarget();
    bool itemShown(ItemIndex item) const;
    void stepItem(int dir);
    int powerCount() const;
    /// Mini player buttons: 0 previous, 1 play/pause, 2 next (draw and touch).
    nxui::Rect miniButtonRect(int index) const;

    nxui::Rect computePanelRect() const;
    nxui::Rect computeItemRect(ItemIndex item) const;
    nxui::Rect computePowerButtonRect(PowerAction action) const;
    nxui::Rect computeSliderTrackRect(ItemIndex item) const;

    nxui::Font*        m_font = nullptr;
    nxui::Font*        m_smallFont = nullptr;
    nxui::Font*        m_iconFont = nullptr;
    const nxui::Theme* m_theme = nullptr;
    nxui::Input*       m_input = nullptr;
    Callbacks          m_callbacks;

    bool  m_active = false;
    bool  m_animating = false;
    float m_animProgress = 0.f; // 0.f = closed, 1.f = open

    ItemIndex   m_selectedItem = ItemIndex::Brightness;
    PowerAction m_selectedPower = PowerAction::Sleep;

    SelectionCursor m_cursor;

    // Offscreen backdrop blur caching
    bool m_backdropCacheValid = false;

    // Live state values
    float m_brightness = 0.5f;
    float m_bgmVolume = 0.5f;
    float m_sfxVolume = 0.5f;
    bool  m_airplaneMode = false;
    bool  m_wifiEnabled = true;
    bool  m_extended = false;
    bool  m_bluetoothEnabled = false;

    // Real-time battery & thermal status
    int   m_batteryPercent = -1;
    bool  m_batteryCharging = false;
    float m_socTemp = -1.f;
    float m_pcbTemp = -1.f;
    bool  m_hasSocTemp = false;
    bool  m_hasPcbTemp = false;
    float m_statusPollTimer = 0.f;

    // Touch interaction tracking
    bool      m_draggingSlider = false;
    ItemIndex m_draggedSlider = ItemIndex::Brightness;
};
