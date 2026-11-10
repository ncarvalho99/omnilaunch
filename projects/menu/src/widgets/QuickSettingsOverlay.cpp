#include "QuickSettingsOverlay.hpp"
#include "settings/SettingsGlassTuning.hpp"
#include "core/DebugLog.hpp"
#include <switchu/hardware_status.hpp>

#include <nxui/core/Renderer.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Animation.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef __SWITCH__
#include <switch.h>
#include <switch/services/ts.h>
#include <switch/services/tc.h>
#endif

namespace {

#ifdef __SWITCH__
class SettingsNifmSystemSession {
public:
    ~SettingsNifmSystemSession() {
        serviceClose(&m_general);
        serviceClose(&m_static);
    }

    Result open() {
        Result rc = smGetService(&m_static, "nifm:s");
        if (R_SUCCEEDED(rc))
            rc = serviceConvertToDomain(&m_static);
        if (R_SUCCEEDED(rc)) {
            const u64 reserved = 0;
            serviceAssumeDomain(&m_static);
            rc = serviceDispatchIn(&m_static, 5, reserved,
                .in_send_pid = true,
                .out_num_objects = 1,
                .out_objects = &m_general,
            );
        }
        return rc;
    }

    Result getWirelessCommunicationEnabled(bool* out) {
        u8 value = 0;
        serviceAssumeDomain(&m_general);
        Result rc = serviceDispatchOut(&m_general, 17, value);
        if (R_SUCCEEDED(rc) && out)
            *out = (value & 1) != 0;
        return rc;
    }

    Result setWirelessCommunicationEnabled(bool enabled) {
        const u8 value = enabled ? 1 : 0;
        serviceAssumeDomain(&m_general);
        return serviceDispatchIn(&m_general, 16, value);
    }

private:
    Service m_static{};
    Service m_general{};
};

static Result queryAirplaneMode(bool* outAirplaneMode) {
    SettingsNifmSystemSession session;
    Result rc = session.open();
    if (R_SUCCEEDED(rc)) {
        bool wirelessEnabled = true;
        rc = session.getWirelessCommunicationEnabled(&wirelessEnabled);
        if (R_SUCCEEDED(rc) && outAirplaneMode) {
            *outAirplaneMode = !wirelessEnabled;
        }
    }
    return rc;
}

static Result setAirplaneModeSetting(bool enableAirplaneMode) {
    SettingsNifmSystemSession session;
    Result rc = session.open();
    if (R_SUCCEEDED(rc)) {
        rc = session.setWirelessCommunicationEnabled(!enableAirplaneMode);
    }
    return rc;
}
#endif

static constexpr float kPanelWidth  = 440.f;
static constexpr float kPanelHeight = 636.f;
static constexpr float kPanelTop    = 16.f;
static constexpr float kPanelMarginRight = 20.f;
static constexpr float kAnimDuration = 0.25f;

static std::string utf8Codepoint(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

static void drawWifiIcon(nxui::Renderer& ren, nxui::Vec2 center, const nxui::Color& color, float scale = 1.0f) {
    ren.drawCircle(center, 1.8f * scale, color);

    const float radii[] = { 5.5f * scale, 10.0f * scale, 14.5f * scale };
    for (float r : radii) {
        constexpr int steps = 8;
        constexpr float degStart = 225.0f;
        constexpr float degEnd = 315.0f;
        constexpr float degStep = (degEnd - degStart) / steps;
        nxui::Vec2 prev;
        for (int i = 0; i <= steps; ++i) {
            float deg = degStart + i * degStep;
            float rad = deg * (3.14159265358979323846f / 180.0f);
            nxui::Vec2 pt = { center.x + r * std::cos(rad), center.y + r * std::sin(rad) };
            if (i > 0) {
                ren.drawLine(prev, pt, color, 1.8f * scale);
            }
            prev = pt;
        }
    }
}

static void drawMoonIcon(nxui::Renderer& ren, nxui::Vec2 center, const nxui::Color& color, float scale = 1.0f) {
    const int R = static_cast<int>(std::round(7.0f * scale));
    const float r = 6.0f * scale;
    const float dx = 2.5f * scale;

    for (int y_off = -R; y_off <= R; ++y_off) {
        float val_out = static_cast<float>(R * R - y_off * y_off);
        if (val_out < 0.0f) continue;
        float x_out_left = -std::sqrt(val_out);
        float x_out_right = std::sqrt(val_out);

        float val_in = r * r - static_cast<float>(y_off * y_off);
        float x_in_left = (val_in >= 0.0f) ? (dx - std::sqrt(val_in)) : x_out_right;

        float x1 = center.x + x_out_left;
        float x2 = center.x + std::min(x_out_right, x_in_left);
        if (x2 > x1) {
            ren.drawLine({x1, center.y + static_cast<float>(y_off)},
                         {x2, center.y + static_cast<float>(y_off)}, color, 1.0f);
        }
    }
}

} // namespace

QuickSettingsOverlay::QuickSettingsOverlay() {
    setRect({0.f, 0.f, 1280.f, 720.f});
    setVisible(false);
    setFocusable(true);
    setFrameworkTouchEnabled(false);

    m_cursor.setBorderWidth(2.6f);
    m_cursor.setColor(nxui::Color(0.12f, 0.76f, 0.98f, 1.f));

    setupNavigationActions();
}

QuickSettingsOverlay::~QuickSettingsOverlay() = default;

void QuickSettingsOverlay::setTheme(const nxui::Theme* t) {
    m_theme = t;
    if (m_theme) {
        m_cursor.setColor(m_theme->cursorNormal);
    }
}

void QuickSettingsOverlay::setupNavigationActions() {
    // Up / Down navigation across items
    addDirectionAction(nxui::FocusDirection::UP, [this]() {
        if (!m_active) return;
        stepItem(-1);
    });

    addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
        if (!m_active) return;
        stepItem(+1);
    });

    // Left / Right navigation / slider adjustment
    addDirectionAction(nxui::FocusDirection::LEFT, [this]() {
        if (!m_active) return;
        switch (m_selectedItem) {
            case ItemIndex::Brightness:
                adjustSlider(ItemIndex::Brightness, -0.05f);
                break;
            case ItemIndex::BgmVolume:
                adjustSlider(ItemIndex::BgmVolume, -0.05f);
                break;
            case ItemIndex::SfxVolume:
                adjustSlider(ItemIndex::SfxVolume, -0.05f);
                break;
            case ItemIndex::AirplaneMode:
                toggleItem(ItemIndex::AirplaneMode);
                break;
            case ItemIndex::Wifi:
                toggleItem(ItemIndex::Wifi);
                break;
            case ItemIndex::Bluetooth:
                toggleItem(ItemIndex::Bluetooth);
                break;
            case ItemIndex::MiniPlayer:
                if (m_callbacks.onMusicSkip) m_callbacks.onMusicSkip(-1);
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                break;
            case ItemIndex::PowerActions: {
                int p = static_cast<int>(m_selectedPower) - 1;
                if (p < 0) p = powerCount() - 1;
                m_selectedPower = static_cast<PowerAction>(p);
                updateCursorTarget();
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                break;
            }
            default:
                break;
        }
    });

    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() {
        if (!m_active) return;
        switch (m_selectedItem) {
            case ItemIndex::Brightness:
                adjustSlider(ItemIndex::Brightness, 0.05f);
                break;
            case ItemIndex::BgmVolume:
                adjustSlider(ItemIndex::BgmVolume, 0.05f);
                break;
            case ItemIndex::SfxVolume:
                adjustSlider(ItemIndex::SfxVolume, 0.05f);
                break;
            case ItemIndex::AirplaneMode:
                toggleItem(ItemIndex::AirplaneMode);
                break;
            case ItemIndex::Wifi:
                toggleItem(ItemIndex::Wifi);
                break;
            case ItemIndex::Bluetooth:
                toggleItem(ItemIndex::Bluetooth);
                break;
            case ItemIndex::MiniPlayer:
                if (m_callbacks.onMusicSkip) m_callbacks.onMusicSkip(+1);
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                break;
            case ItemIndex::PowerActions: {
                int p = static_cast<int>(m_selectedPower) + 1;
                if (p >= powerCount()) p = 0;
                m_selectedPower = static_cast<PowerAction>(p);
                updateCursorTarget();
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                break;
            }
            default:
                break;
        }
    });

    // Activation (Button A)
    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (!m_active) return;
        switch (m_selectedItem) {
            case ItemIndex::AirplaneMode:
                toggleItem(ItemIndex::AirplaneMode);
                break;
            case ItemIndex::Wifi:
                toggleItem(ItemIndex::Wifi);
                break;
            case ItemIndex::ActivityLog:
                if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
                if (m_callbacks.onActivityLogRequested) m_callbacks.onActivityLogRequested();
                break;
            case ItemIndex::Bluetooth:
                toggleItem(ItemIndex::Bluetooth);
                break;
            case ItemIndex::MiniPlayer:
                if (m_callbacks.onMusicToggle) m_callbacks.onMusicToggle();
                if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
                break;
            case ItemIndex::PowerActions:
                triggerPowerAction(m_selectedPower);
                break;
            default:
                break;
        }
    });

    // Dismissal (Button B or Click Left Stick)
    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (!m_active) return;
        if (m_callbacks.onClose) {
            m_callbacks.onClose();
        } else {
            hide();
        }
    });

    addAction(static_cast<uint64_t>(nxui::Button::LStick), [this]() {
        if (!m_active) return;
        if (m_callbacks.onClose) {
            m_callbacks.onClose();
        } else {
            hide();
        }
    });
}

bool QuickSettingsOverlay::itemShown(ItemIndex item) const {
    switch (item) {
        case ItemIndex::ActivityLog: return !m_extended;
        case ItemIndex::Bluetooth:
        case ItemIndex::MiniPlayer: return m_extended;
        case ItemIndex::Count: return false;
        default: return true;
    }
}

// Visual order differs from the enum in extended mode: the mini player sits
// above the sliders, Bluetooth below Wi-Fi.
void QuickSettingsOverlay::stepItem(int dir) {
    static const ItemIndex kClassic[] = {ItemIndex::Brightness, ItemIndex::BgmVolume, ItemIndex::SfxVolume,
                                         ItemIndex::AirplaneMode, ItemIndex::Wifi, ItemIndex::ActivityLog,
                                         ItemIndex::PowerActions};
    static const ItemIndex kExtended[] = {ItemIndex::MiniPlayer, ItemIndex::Brightness, ItemIndex::BgmVolume,
                                          ItemIndex::SfxVolume, ItemIndex::AirplaneMode, ItemIndex::Wifi,
                                          ItemIndex::Bluetooth, ItemIndex::PowerActions};
    const ItemIndex* order = m_extended ? kExtended : kClassic;
    const int n = m_extended ? 8 : 7;
    int at = 0;
    for (int i = 0; i < n; ++i)
        if (order[i] == m_selectedItem) at = i;
    m_selectedItem = order[(at + (dir > 0 ? 1 : n - 1)) % n];
    updateCursorTarget();
    if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
}

int QuickSettingsOverlay::powerCount() const {
    return m_extended ? static_cast<int>(PowerAction::Count) : static_cast<int>(PowerAction::Hekate);
}

void QuickSettingsOverlay::setInitialValues(float brightness, float bgmVolume, float sfxVolume,
                                            bool airplaneMode, bool wifiEnabled) {
    m_brightness = std::clamp(brightness, 0.05f, 1.0f);
    m_bgmVolume = std::clamp(bgmVolume, 0.0f, 1.0f);
    m_sfxVolume = std::clamp(sfxVolume, 0.0f, 1.0f);
    m_airplaneMode = airplaneMode;
    m_wifiEnabled = wifiEnabled;
}

void QuickSettingsOverlay::setBatteryStatus(int percent, bool charging) {
    m_batteryPercent = percent;
    m_batteryCharging = charging;
}

void QuickSettingsOverlay::show() {
    m_active = true;
    m_animating = true;
    m_animProgress = 0.f;
    m_backdropCacheValid = false;
    setVisible(true);
    setFocusable(true);
    m_selectedItem = m_extended ? ItemIndex::MiniPlayer : ItemIndex::Brightness;
    m_selectedPower = PowerAction::Sleep;
    m_statusPollTimer = 0.f;

    // Refresh live hardware state
    refreshHardwareStatus();
    updateCursorTarget();
    DebugLog::log("[quicksettings] shown");
}

void QuickSettingsOverlay::hide() {
    if (!m_active) return;
    m_active = false;
    m_animating = true;
    m_backdropCacheValid = false;
    setFocusable(false);
    DebugLog::log("[quicksettings] hiding");
}

void QuickSettingsOverlay::refreshHardwareStatus() {
#ifdef __SWITCH__
    // Query brightness
    float b = 0.5f;
    if (R_SUCCEEDED(lblGetCurrentBrightnessSetting(&b))) {
        m_brightness = std::clamp(b, 0.05f, 1.0f);
    }

    // Query airplane mode
    bool ap = false;
    if (R_SUCCEEDED(queryAirplaneMode(&ap))) {
        m_airplaneMode = ap;
    }

    // Query Wi-Fi
    bool wf = true;
    if (R_SUCCEEDED(setsysGetWirelessLanEnableFlag(&wf))) {
        m_wifiEnabled = wf;
    }

    if (m_extended && m_callbacks.bluetoothEnabled)
        m_bluetoothEnabled = m_callbacks.bluetoothEnabled();

    // Query battery if needed
    if (m_batteryPercent < 0) {
        u32 pct = 0;
        if (R_SUCCEEDED(psmGetBatteryChargePercentage(&pct))) {
            m_batteryPercent = static_cast<int>(pct);
            PsmChargerType ct = PsmChargerType_Unconnected;
            if (R_SUCCEEDED(psmGetChargerType(&ct))) {
                m_batteryCharging = (ct != PsmChargerType_Unconnected);
            }
        }
    }

    // Query thermals
    const auto thermals = switchu::getThermalReading();
    m_socTemp = thermals.soc;
    m_pcbTemp = thermals.pcb;
    m_hasSocTemp = thermals.hasSoc;
    m_hasPcbTemp = thermals.hasPcb;
#else
    // Mock values for test environments
    if (m_batteryPercent < 0) {
        m_batteryPercent = 85;
        m_batteryCharging = true;
    }
    m_socTemp = 42.0f;
    m_pcbTemp = 36.5f;
    m_hasSocTemp = true;
    m_hasPcbTemp = true;
#endif
}

void QuickSettingsOverlay::adjustSlider(ItemIndex item, float delta) {
    float* target = nullptr;
    std::function<void(float)> cb;

    switch (item) {
        case ItemIndex::Brightness:
            target = &m_brightness;
            cb = m_callbacks.onBrightnessChanged;
            break;
        case ItemIndex::BgmVolume:
            target = &m_bgmVolume;
            cb = m_callbacks.onBgmVolumeChanged;
            break;
        case ItemIndex::SfxVolume:
            target = &m_sfxVolume;
            cb = m_callbacks.onSfxVolumeChanged;
            break;
        default:
            return;
    }

    if (!target) return;
    float minVal = (item == ItemIndex::Brightness) ? 0.05f : 0.0f;
    *target = std::clamp(*target + delta, minVal, 1.0f);

#ifdef __SWITCH__
    if (item == ItemIndex::Brightness) {
        lblSetCurrentBrightnessSetting(*target);
    }
#endif

    if (cb) cb(*target);
    if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
}

void QuickSettingsOverlay::toggleItem(ItemIndex item) {
    if (item == ItemIndex::AirplaneMode) {
        m_airplaneMode = !m_airplaneMode;
#ifdef __SWITCH__
        setAirplaneModeSetting(m_airplaneMode);
        if (m_airplaneMode) {
            m_wifiEnabled = false;
        }
#endif
        if (m_callbacks.onAirplaneModeToggled)
            m_callbacks.onAirplaneModeToggled(m_airplaneMode);

        if (m_airplaneMode) {
            if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
        } else {
            if (m_callbacks.onToggleOffSfx) m_callbacks.onToggleOffSfx();
        }
    } else if (item == ItemIndex::Bluetooth) {
        m_bluetoothEnabled = !m_bluetoothEnabled;
        if (m_callbacks.onBluetoothToggled)
            m_callbacks.onBluetoothToggled(m_bluetoothEnabled);
        if (m_bluetoothEnabled) {
            if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
        } else {
            if (m_callbacks.onToggleOffSfx) m_callbacks.onToggleOffSfx();
        }
    } else if (item == ItemIndex::Wifi) {
        m_wifiEnabled = !m_wifiEnabled;
#ifdef __SWITCH__
        if (m_wifiEnabled && m_airplaneMode) {
            // Turning Wi-Fi on disables Airplane mode
            m_airplaneMode = false;
            setAirplaneModeSetting(false);
            if (m_callbacks.onAirplaneModeToggled)
                m_callbacks.onAirplaneModeToggled(false);
        }
        setsysSetWirelessLanEnableFlag(m_wifiEnabled);
#endif
        if (m_callbacks.onWifiToggled)
            m_callbacks.onWifiToggled(m_wifiEnabled);

        if (m_wifiEnabled) {
            if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
        } else {
            if (m_callbacks.onToggleOffSfx) m_callbacks.onToggleOffSfx();
        }
    }
}

void QuickSettingsOverlay::triggerPowerAction(PowerAction action) {
    switch (action) {
        case PowerAction::Sleep:
            if (m_callbacks.onSleepRequested) m_callbacks.onSleepRequested();
            break;
        case PowerAction::Reboot:
            if (m_callbacks.onRebootRequested) m_callbacks.onRebootRequested();
            break;
        case PowerAction::Shutdown:
            if (m_callbacks.onShutdownRequested) m_callbacks.onShutdownRequested();
            break;
        case PowerAction::Hekate:
            if (m_callbacks.onHekateRequested) m_callbacks.onHekateRequested();
            break;
        default:
            break;
    }
}

nxui::Rect QuickSettingsOverlay::computePanelRect() const {
    float openX = 1280.f - kPanelWidth - kPanelMarginRight;
    float closedX = 1280.f;
    float eased = nxui::Easing::outCubic(m_animProgress);
    float curX = closedX + (openX - closedX) * eased;
    return {curX, kPanelTop, kPanelWidth, kPanelHeight};
}

nxui::Rect QuickSettingsOverlay::computeItemRect(ItemIndex item) const {
    nxui::Rect panel = computePanelRect();
    float cx = panel.x + 22.f;
    float cw = panel.width - 44.f;
    // Extended mode puts the mini player first and shifts the rest down.
    const float o = m_extended ? 62.f : 0.f;

    switch (item) {
        case ItemIndex::MiniPlayer:
            return {cx - 4.f, panel.y + 142.f, cw + 8.f, 54.f};
        case ItemIndex::Brightness:
            return {cx - 4.f, panel.y + 142.f + o, cw + 8.f, 54.f};
        case ItemIndex::BgmVolume:
            return {cx - 4.f, panel.y + 204.f + o, cw + 8.f, 54.f};
        case ItemIndex::SfxVolume:
            return {cx - 4.f, panel.y + 266.f + o, cw + 8.f, 54.f};
        case ItemIndex::AirplaneMode:
            return {cx - 4.f, panel.y + 328.f + o, cw + 8.f, 42.f};
        case ItemIndex::Wifi:
            return {cx - 4.f, panel.y + 378.f + o, cw + 8.f, 42.f};
        case ItemIndex::ActivityLog:
        case ItemIndex::Bluetooth:
            return {cx - 4.f, panel.y + 428.f + o, cw + 8.f, 42.f};
        case ItemIndex::PowerActions:
            return computePowerButtonRect(m_selectedPower);
        default:
            return {cx, panel.y, cw, 40.f};
    }
}

nxui::Rect QuickSettingsOverlay::computePowerButtonRect(PowerAction action) const {
    nxui::Rect panel = computePanelRect();
    float cx = panel.x + 22.f;
    const int n = powerCount();
    float btnW = (panel.width - 44.f - 10.f * static_cast<float>(n - 1)) / static_cast<float>(n);
    float by = panel.y + 514.f + (m_extended ? 52.f : 0.f);

    int idx = static_cast<int>(action);
    float bx = cx + idx * (btnW + 10.f);
    return {bx, by, btnW, 46.f};
}

nxui::Rect QuickSettingsOverlay::computeSliderTrackRect(ItemIndex item) const {
    nxui::Rect panel = computePanelRect();
    float cx = panel.x + 26.f;
    float cw = panel.width - 52.f;

    const float o = m_extended ? 62.f : 0.f;
    switch (item) {
        case ItemIndex::Brightness:
            return {cx, panel.y + 172.f + o, cw, 12.f};
        case ItemIndex::BgmVolume:
            return {cx, panel.y + 234.f + o, cw, 12.f};
        case ItemIndex::SfxVolume:
            return {cx, panel.y + 296.f + o, cw, 12.f};
        default:
            return {};
    }
}

void QuickSettingsOverlay::updateCursorTarget() {
    nxui::Rect r = computeItemRect(m_selectedItem);
    float radius = (m_selectedItem == ItemIndex::PowerActions) ? 12.f : 14.f;
    m_cursor.moveTo(r, radius, 0.15f);
}

void QuickSettingsOverlay::update(float dt) {
    if (!isVisible()) return;

    setRect(computePanelRect());

    // Animation progress
    if (m_animating) {
        if (m_active) {
            m_animProgress += dt / kAnimDuration;
            if (m_animProgress >= 1.f) {
                m_animProgress = 1.f;
                m_animating = false;
            }
        } else {
            m_animProgress -= dt / kAnimDuration;
            if (m_animProgress <= 0.f) {
                m_animProgress = 0.f;
                m_animating = false;
                setVisible(false);
            }
        }
        updateCursorTarget();
    }

    if (isFullyVisible()) {
        m_statusPollTimer += dt;
        if (m_statusPollTimer >= 1.0f) {
            m_statusPollTimer = 0.f;
            refreshHardwareStatus();
        }

        if (m_input) {
            handleTouch(*m_input);
        }
    }

    m_cursor.update(dt);
}

void QuickSettingsOverlay::handleTouch(nxui::Input& input) {
    nxui::Rect panel = computePanelRect();

    if (input.touchDown()) {
        float tx = input.touchX();
        float ty = input.touchY();

        // Tap outside panel to dismiss
        if (tx < panel.x) {
            if (m_callbacks.onClose) {
                m_callbacks.onClose();
            } else {
                hide();
            }
            return;
        }

        // Check sliders
        for (auto item : {ItemIndex::Brightness, ItemIndex::BgmVolume, ItemIndex::SfxVolume}) {
            nxui::Rect track = computeSliderTrackRect(item);
            nxui::Rect hitArea = {track.x - 10.f, track.y - 14.f, track.width + 20.f, track.height + 28.f};
            if (hitArea.contains(tx, ty)) {
                m_selectedItem = item;
                m_draggingSlider = true;
                m_draggedSlider = item;
                float val = std::clamp((tx - track.x) / track.width, 0.0f, 1.0f);
                if (item == ItemIndex::Brightness) val = std::max(0.05f, val);
                adjustSlider(item, val - ((item == ItemIndex::Brightness) ? m_brightness : (item == ItemIndex::BgmVolume ? m_bgmVolume : m_sfxVolume)));
                updateCursorTarget();
                return;
            }
        }

        // Check Airplane Mode toggle
        nxui::Rect apRect = computeItemRect(ItemIndex::AirplaneMode);
        if (apRect.contains(tx, ty)) {
            m_selectedItem = ItemIndex::AirplaneMode;
            toggleItem(ItemIndex::AirplaneMode);
            updateCursorTarget();
            return;
        }

        // Check Wi-Fi toggle
        nxui::Rect wfRect = computeItemRect(ItemIndex::Wifi);
        if (wfRect.contains(tx, ty)) {
            m_selectedItem = ItemIndex::Wifi;
            toggleItem(ItemIndex::Wifi);
            updateCursorTarget();
            return;
        }

        if (m_extended) {
            nxui::Rect btRect = computeItemRect(ItemIndex::Bluetooth);
            if (btRect.contains(tx, ty)) {
                m_selectedItem = ItemIndex::Bluetooth;
                toggleItem(ItemIndex::Bluetooth);
                updateCursorTarget();
                return;
            }
            nxui::Rect mpRect = computeItemRect(ItemIndex::MiniPlayer);
            if (mpRect.contains(tx, ty)) {
                m_selectedItem = ItemIndex::MiniPlayer;
                updateCursorTarget();
                const float third = mpRect.width / 3.f;
                if (tx < mpRect.x + third) {
                    if (m_callbacks.onMusicSkip) m_callbacks.onMusicSkip(-1);
                } else if (tx > mpRect.right() - third) {
                    if (m_callbacks.onMusicSkip) m_callbacks.onMusicSkip(+1);
                } else if (m_callbacks.onMusicToggle) {
                    m_callbacks.onMusicToggle();
                }
                if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
                return;
            }
        }

        // Check Activity Log button
        nxui::Rect actRect = computeItemRect(ItemIndex::ActivityLog);
        if (!m_extended && actRect.contains(tx, ty)) {
            m_selectedItem = ItemIndex::ActivityLog;
            updateCursorTarget();
            if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
            if (m_callbacks.onActivityLogRequested) m_callbacks.onActivityLogRequested();
            return;
        }

        // Check Power Buttons
        for (int pi = 0; pi < powerCount(); ++pi) {
            const auto pa = static_cast<PowerAction>(pi);
            nxui::Rect btnRect = computePowerButtonRect(pa);
            if (btnRect.contains(tx, ty)) {
                m_selectedItem = ItemIndex::PowerActions;
                m_selectedPower = pa;
                updateCursorTarget();
                triggerPowerAction(pa);
                return;
            }
        }
    }

    if (input.isTouching() && m_draggingSlider) {
        nxui::Rect track = computeSliderTrackRect(m_draggedSlider);
        float tx = input.touchX();
        float val = std::clamp((tx - track.x) / track.width, 0.0f, 1.0f);
        if (m_draggedSlider == ItemIndex::Brightness) val = std::max(0.05f, val);
        adjustSlider(m_draggedSlider, val - ((m_draggedSlider == ItemIndex::Brightness) ? m_brightness : (m_draggedSlider == ItemIndex::BgmVolume ? m_bgmVolume : m_sfxVolume)));
    }

    if (input.touchUp()) {
        m_draggingSlider = false;
    }
}

void QuickSettingsOverlay::render(nxui::Renderer& ren) {
    if (!isVisible() || m_animProgress <= 0.001f) return;

    auto& i18n = nxui::I18n::instance();
    float alpha = m_animProgress;

    // 1. Capture and blur scene backdrop for authentic liquid frosted glass
    if (!m_backdropCacheValid) {
        ren.captureToOffscreenSharp();
        const auto& tuning = settings::debug::settingsGlassTuning();
        if (tuning.blurIterations > 0 && tuning.preBlurRadius > 0.001f) {
            ren.applyBlur(tuning.preBlurRadius, tuning.blurIterations);
        }
        ren.copyOffscreen(nxui::GpuDevice::OFF_SHARP_A, nxui::GpuDevice::OFF_DIALOG);
        m_backdropCacheValid = true;
    }

    // 2. Soft translucent backdrop scrim
    nxui::Rect screen = {0.f, 0.f, (float)ren.width(), (float)ren.height()};
    nxui::Color scrim = m_theme
        ? nxui::Color::lerp(m_theme->background, nxui::Color::black(),
                            m_theme->mode == nxui::ThemeMode::Dark ? 0.65f : 0.20f)
              .withAlpha((m_theme->mode == nxui::ThemeMode::Dark ? 0.25f : 0.14f) * alpha)
        : nxui::Color(0.f, 0.f, 0.f, 0.20f * alpha);
    ren.drawRect(screen, scrim);

    // 3. Liquid Glass blurry slide-out panel
    nxui::Rect panel = computePanelRect();
    const auto& tuning = settings::debug::settingsGlassTuning();
    nxui::LiquidGlassSettings savedGlass = ren.liquidGlassSettings();
    auto& glass = ren.liquidGlassSettings();
    glass.refractionIntensity = std::clamp(tuning.refractionIntensity, 0.0f, 1.5f);
    glass.blurIntensity = std::max(0.0f, tuning.shaderBlurIntensity);
    glass.noiseIntensity = 0.0f;
    glass.glowIntensity = std::max(0.0f, tuning.glowIntensity);
    glass.saturation = std::max(0.0f, tuning.saturation);
    glass.opacityMultiplier = 1.0f;
    glass.roughness = std::max(0.0f, tuning.roughness);
    glass.powerFactor = std::max(1.001f, tuning.powerFactor);

    nxui::Color glassTint = m_theme
        ? m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark
            ? std::clamp(tuning.tintAlphaDark, 0.0f, 1.0f)
            : std::clamp(tuning.tintAlphaLight, 0.0f, 1.0f))
        : nxui::Color(0.12f, 0.16f, 0.24f, 0.20f);

    ren.drawLiquidGlass(nxui::GpuDevice::OFF_DIALOG, panel, 24.f, glassTint, alpha,
                        std::clamp(tuning.shade, 0.0f, 1.0f));
    ren.liquidGlassSettings() = savedGlass;

    nxui::Color borderColor = m_theme
        ? m_theme->panelBorder.withAlpha(0.28f * alpha)
        : nxui::Color(1.f, 1.f, 1.f, 0.22f * alpha);
    ren.drawRoundedRectOutline(panel, borderColor, 24.f, 1.2f);

    nxui::Color highlightColor = m_theme
        ? m_theme->panelHighlight.withAlpha(0.08f * alpha)
        : nxui::Color(1.f, 1.f, 1.f, 0.08f * alpha);
    ren.drawRoundedRectOutline(panel.shrunk(1.f), highlightColor, 23.f, 1.0f);

    float cx = panel.x + 22.f;
    float cw = panel.width - 44.f;

    // 3. Header
    if (m_font) {
        std::string title = i18n.tr("quicksettings.title", "Quick Settings");
        ren.drawText(title, {cx, panel.y + 20.f}, m_font,
                     nxui::Color(1.f, 1.f, 1.f, alpha), 0.95f);
    }

    // 4. Hardware Status Card (Battery & Thermals)
    nxui::Rect statusCard = {cx, panel.y + 54.f, cw, 78.f};
    ren.drawRoundedRect(statusCard, nxui::Color(0.12f, 0.18f, 0.28f, 0.65f * alpha), 14.f);
    ren.drawRoundedRectOutline(statusCard, nxui::Color(1.f, 1.f, 1.f, 0.14f * alpha), 14.f, 1.f);

    // Battery Column (Left)
    if (m_smallFont) {
        ren.drawText(i18n.tr("quicksettings.battery", "BATTERY"), {statusCard.x + 16.f, statusCard.y + 10.f},
                     m_smallFont, nxui::Color(0.65f, 0.72f, 0.82f, alpha), 0.72f);

        char bBuf[32];
        if (m_batteryPercent >= 0)
            std::snprintf(bBuf, sizeof(bBuf), "%d%%", m_batteryPercent);
        else
            std::snprintf(bBuf, sizeof(bBuf), "--%%");

        ren.drawText(bBuf, {statusCard.x + 16.f, statusCard.y + 28.f},
                     m_font ? m_font : m_smallFont, nxui::Color(1.f, 1.f, 1.f, alpha), 0.95f);

        std::string chgText = m_batteryCharging
            ? i18n.tr("quicksettings.charging", "⚡ Charging")
            : i18n.tr("quicksettings.discharging", "Discharging");
        nxui::Color chgCol = m_batteryCharging
            ? nxui::Color(0.25f, 0.90f, 0.45f, alpha)
            : nxui::Color(0.60f, 0.68f, 0.78f, alpha);
        ren.drawText(chgText, {statusCard.x + 16.f, statusCard.y + 54.f},
                     m_smallFont, chgCol, 0.72f);
    }

    // Divider
    ren.drawLine({statusCard.x + cw * 0.5f, statusCard.y + 8.f},
                 {statusCard.x + cw * 0.5f, statusCard.y + 70.f},
                 nxui::Color(1.f, 1.f, 1.f, 0.12f * alpha), 1.f);

    // Thermal Column (Right)
    float rx = statusCard.x + cw * 0.5f + 16.f;
    if (m_smallFont) {
        ren.drawText(i18n.tr("quicksettings.hardware_temp", "THERMALS"), {rx, statusCard.y + 10.f},
                     m_smallFont, nxui::Color(0.65f, 0.72f, 0.82f, alpha), 0.72f);

        char tBuf[64];
        if (m_hasSocTemp || m_hasPcbTemp) {
            if (m_hasSocTemp && m_hasPcbTemp)
                std::snprintf(tBuf, sizeof(tBuf), "SoC: %.0f°C  PCB: %.0f°C", m_socTemp, m_pcbTemp);
            else if (m_hasSocTemp)
                std::snprintf(tBuf, sizeof(tBuf), "SoC: %.0f°C", m_socTemp);
            else
                std::snprintf(tBuf, sizeof(tBuf), "PCB: %.0f°C", m_pcbTemp);
        } else {
            std::snprintf(tBuf, sizeof(tBuf), "-- °C");
        }

        ren.drawText(tBuf, {rx, statusCard.y + 30.f},
                     m_smallFont, nxui::Color(1.f, 1.f, 1.f, alpha), 0.85f);

        float maxT = std::max(m_socTemp, m_pcbTemp);
        nxui::Color badgeCol = (maxT > 70.f)
            ? nxui::Color(0.95f, 0.35f, 0.25f, alpha) // Hot / Amber-Red
            : (maxT > 55.f)
                ? nxui::Color(0.95f, 0.75f, 0.20f, alpha) // Warm / Amber
                : nxui::Color(0.20f, 0.85f, 0.50f, alpha); // Optimal / Green

        ren.drawCircle({rx + 6.f, statusCard.y + 58.f}, 4.5f, badgeCol);
        std::string badgeText = (maxT > 65.f)
            ? i18n.tr("quicksettings.temp_warm", "Warm")
            : i18n.tr("quicksettings.temp_optimal", "Optimal");
        ren.drawText(badgeText, {rx + 16.f, statusCard.y + 52.f},
                     m_smallFont, badgeCol, 0.72f);
    }

    // 5. Selection Cursor
    m_cursor.render(ren);

    // Helper lambda for rendering a slider
    auto drawSlider = [&](ItemIndex idx, const std::string& label, float value,
                          const nxui::Color& fillColor) {
        nxui::Rect card = computeItemRect(idx);
        ren.drawRoundedRect(card, nxui::Color(0.12f, 0.18f, 0.26f, 0.45f * alpha), 12.f);
        ren.drawRoundedRectOutline(card, nxui::Color(1.f, 1.f, 1.f, 0.08f * alpha), 12.f, 1.f);

        if (m_smallFont) {
            ren.drawText(label, {card.x + 12.f, card.y + 8.f}, m_smallFont,
                         nxui::Color(0.95f, 0.95f, 0.98f, alpha), 0.82f);

            char pBuf[16];
            std::snprintf(pBuf, sizeof(pBuf), "%d%%", static_cast<int>(std::round(value * 100.f)));
            nxui::Vec2 psz = m_smallFont->measure(pBuf);
            ren.drawText(pBuf, {card.x + card.width - 12.f - psz.x * 0.82f, card.y + 8.f},
                         m_smallFont, nxui::Color(0.85f, 0.88f, 0.92f, alpha), 0.82f);
        }

        nxui::Rect track = computeSliderTrackRect(idx);
        ren.drawRoundedRect(track, nxui::Color(0.05f, 0.08f, 0.12f, 0.75f * alpha), 7.f);

        float fillW = std::clamp(track.width * value, 10.f, track.width);
        nxui::Rect fillRect = {track.x, track.y, fillW, track.height};
        ren.drawRoundedRect(fillRect, fillColor.withAlpha(alpha), 7.f);

        float knobX = track.x + fillW;
        ren.drawCircle({knobX, track.y + track.height * 0.5f}, 9.5f,
                       nxui::Color(1.f, 1.f, 1.f, alpha));
    };

    // Sliders
    drawSlider(ItemIndex::Brightness, i18n.tr("quicksettings.brightness", "☀ Brightness"),
               m_brightness, nxui::Color(0.15f, 0.75f, 0.98f, 1.f));

    drawSlider(ItemIndex::BgmVolume, i18n.tr("quicksettings.bgm_volume", "♫ Music (BGM)"),
               m_bgmVolume, nxui::Color(0.68f, 0.45f, 0.95f, 1.f));

    drawSlider(ItemIndex::SfxVolume, i18n.tr("quicksettings.sfx_volume", "♪ Sound Effects"),
               m_sfxVolume, nxui::Color(0.18f, 0.82f, 0.55f, 1.f));

    // Helper lambda for rendering a toggle row
    auto drawToggle = [&](ItemIndex idx, const std::string& label, bool enabled) {
        nxui::Rect card = computeItemRect(idx);
        ren.drawRoundedRect(card, nxui::Color(0.12f, 0.18f, 0.26f, 0.45f * alpha), 12.f);
        ren.drawRoundedRectOutline(card, nxui::Color(1.f, 1.f, 1.f, 0.08f * alpha), 12.f, 1.f);

        float textX = card.x + 14.f;
        if (idx == ItemIndex::Wifi) {
            drawWifiIcon(ren, {card.x + 24.f, card.y + card.height * 0.5f + 3.f},
                         nxui::Color(0.95f, 0.95f, 0.98f, alpha), 0.95f);
            textX = card.x + 40.f;
        }

        if (m_smallFont) {
            float ty = card.y + (card.height - m_smallFont->measure(label).y * 0.82f) * 0.5f;
            ren.drawText(label, {textX, ty}, m_smallFont,
                         nxui::Color(0.95f, 0.95f, 0.98f, alpha), 0.82f);
        }

        // Pill Switch
        float pillW = 56.f;
        float pillH = 28.f;
        nxui::Rect pill = {card.x + card.width - pillW - 12.f, card.y + (card.height - pillH) * 0.5f, pillW, pillH};
        nxui::Color pillBg = enabled
            ? nxui::Color(0.20f, 0.82f, 0.45f, 0.90f * alpha)
            : nxui::Color(0.35f, 0.38f, 0.45f, 0.75f * alpha);

        ren.drawRoundedRect(pill, pillBg, pillH * 0.5f);

        float knobX = enabled ? (pill.x + pill.width - pillH * 0.5f) : (pill.x + pillH * 0.5f);
        ren.drawCircle({knobX, pill.y + pillH * 0.5f}, pillH * 0.40f,
                       nxui::Color(1.f, 1.f, 1.f, alpha));

        if (m_smallFont) {
            std::string stateStr = enabled
                ? i18n.tr("quicksettings.on", "ON")
                : i18n.tr("quicksettings.off", "OFF");
            nxui::Vec2 ssz = m_smallFont->measure(stateStr);
            float tx = enabled ? (pill.x + 8.f) : (pill.x + pill.width - ssz.x * 0.65f - 8.f);
            ren.drawText(stateStr, {tx, pill.y + (pillH - ssz.y * 0.65f) * 0.5f}, m_smallFont,
                         nxui::Color(1.f, 1.f, 1.f, 0.95f * alpha), 0.65f);
        }
    };

    // Toggles
    drawToggle(ItemIndex::AirplaneMode, i18n.tr("quicksettings.airplane_mode", "Modo avião"),
               m_airplaneMode);

    drawToggle(ItemIndex::Wifi, i18n.tr("quicksettings.wifi", "Wi-Fi"),
               m_wifiEnabled);

    if (m_extended) {
        drawToggle(ItemIndex::Bluetooth, "Bluetooth", m_bluetoothEnabled);

        // Mini player (C-UI-8): track title, previous / play-pause / next.
        nxui::Rect mp = computeItemRect(ItemIndex::MiniPlayer);
        ren.drawRoundedRect(mp, nxui::Color(0.12f, 0.18f, 0.26f, 0.45f * alpha), 12.f);
        ren.drawRoundedRectOutline(mp, nxui::Color(1.f, 1.f, 1.f, 0.08f * alpha), 12.f, 1.f);
        if (m_smallFont) {
            std::string title = m_callbacks.musicTitle ? m_callbacks.musicTitle() : std::string();
            if (title.empty())
                title = i18n.tr("quicksettings.no_music", "No music playing");
            const float maxW = mp.width - 150.f;
            const float scale = 0.82f;
            while (title.size() > 4 && m_smallFont->measure(title).x * scale > maxW) {
                std::size_t cut = title.size() - 1;
                while (cut > 0 && (static_cast<unsigned char>(title[cut]) & 0xC0) == 0x80) --cut;
                title.erase(cut);
            }
            const float ty = mp.y + (mp.height - m_smallFont->measure("Ag").y * scale) * 0.5f;
            ren.drawText(title, {mp.x + 14.f, ty}, m_smallFont, nxui::Color(0.95f, 0.95f, 0.98f, alpha), scale);
            const bool playing = m_callbacks.musicPlaying && m_callbacks.musicPlaying();
            // Drawn shapes, not glyphs: the UI font has no media symbols.

            const float cyM = mp.y + mp.height * 0.5f;
            const float bx = mp.right() - 120.f;
            const nxui::Color ic(1.f, 1.f, 1.f, alpha);
            // previous: bar + triangle
            ren.drawRect({bx, cyM - 7.f, 3.f, 14.f}, ic);
            ren.drawTriangle({bx + 3.f, cyM}, {bx + 15.f, cyM - 7.f}, {bx + 15.f, cyM + 7.f}, ic);
            // play / pause
            const float px = bx + 44.f;
            if (playing) {
                ren.drawRect({px, cyM - 8.f, 4.f, 16.f}, ic);
                ren.drawRect({px + 9.f, cyM - 8.f, 4.f, 16.f}, ic);
            } else {
                ren.drawTriangle({px, cyM - 8.f}, {px, cyM + 8.f}, {px + 14.f, cyM}, ic);
            }
            // next: triangle + bar
            const float nx = bx + 86.f;
            ren.drawTriangle({nx, cyM - 7.f}, {nx, cyM + 7.f}, {nx + 12.f, cyM}, ic);
            ren.drawRect({nx + 12.f, cyM - 7.f, 3.f, 14.f}, ic);
        }
    }

    // 5. Activity Log Button
    if (!m_extended) {
    nxui::Rect actCard = computeItemRect(ItemIndex::ActivityLog);
    bool isActSelected = (m_selectedItem == ItemIndex::ActivityLog);
    ren.drawRoundedRect(actCard,
                        isActSelected ? nxui::Color(0.20f, 0.55f, 0.85f, 0.45f * alpha)
                                      : nxui::Color(0.12f, 0.18f, 0.28f, 0.65f * alpha),
                        14.f);
    ren.drawRoundedRectOutline(actCard,
                               isActSelected ? nxui::Color(0.25f, 0.75f, 1.00f, 0.85f * alpha)
                                             : nxui::Color(1.f, 1.f, 1.f, 0.14f * alpha),
                               14.f, 1.f);
    if (m_smallFont) {
        std::string actLabel = i18n.tr("activity_log.title", "Daily Activity Log");
        float ty = actCard.y + (actCard.height - m_smallFont->measure(actLabel).y * 0.82f) * 0.5f;
        ren.drawText(actLabel, {actCard.x + 16.f, ty}, m_smallFont,
                     nxui::Color(0.95f, 0.95f, 0.98f, alpha), 0.82f);
        ren.drawText(">", {actCard.right() - 28.f, ty}, m_smallFont,
                     nxui::Color(0.40f, 0.80f, 1.00f, alpha), 0.82f);
    }
    }

    // 6. Power Options Section
    if (m_smallFont) {
        ren.drawText(i18n.tr("quicksettings.power_options", "OPÇÕES DE ENERGIA"),
                     {cx, panel.y + 490.f + (m_extended ? 52.f : 0.f)}, m_smallFont,
                     nxui::Color(0.65f, 0.72f, 0.82f, alpha), 0.72f);
    }

    auto drawPowerBtn = [&](PowerAction pa, const std::string& label, const nxui::Color& tint) {
        nxui::Rect btn = computePowerButtonRect(pa);
        bool focused = (m_selectedItem == ItemIndex::PowerActions && m_selectedPower == pa);

        // Clean frosted translucent card base
        ren.drawRoundedRect(btn, nxui::Color(0.12f, 0.16f, 0.24f, 0.45f * alpha), 12.f);

        // Transparent color wash
        nxui::Color fill = focused
            ? tint.withAlpha(0.42f * alpha)
            : tint.withAlpha(0.18f * alpha);
        nxui::Color border = focused
            ? tint.withAlpha(0.95f * alpha)
            : tint.withAlpha(0.35f * alpha);

        ren.drawRoundedRect(btn, fill, 12.f);
        ren.drawRoundedRectOutline(btn, border, 12.f, focused ? 1.4f : 1.0f);

        // 1px top specular highlight line
        ren.drawLine({btn.x + 8.f, btn.y + 1.f}, {btn.x + btn.width - 8.f, btn.y + 1.f},
                     nxui::Color(1.f, 1.f, 1.f, 0.16f * alpha), 1.f);

        if (m_smallFont) {
            float textScale = 0.70f;
            float iconScale = 0.78f;
            nxui::Vec2 lsz = m_smallFont->measure(label);
            nxui::Color textColor = focused
                ? nxui::Color(1.f, 1.f, 1.f, alpha)
                : nxui::Color(0.92f, 0.94f, 0.98f, 0.88f * alpha);

            if (pa == PowerAction::Hekate) {
                float ty = btn.y + (btn.height - lsz.y * textScale) * 0.5f;
                ren.drawText(label, {btn.x + (btn.width - lsz.x * textScale) * 0.5f, ty}, m_smallFont,
                             textColor, textScale);
            } else if (pa == PowerAction::Sleep) {
                float iconW = 14.f;
                float totalW = iconW + 6.f + lsz.x * textScale;
                float curX = btn.x + (btn.width - totalW) * 0.5f;
                drawMoonIcon(ren, {curX + 6.f, btn.y + btn.height * 0.5f}, textColor, 0.9f);
                float ty = btn.y + (btn.height - lsz.y * textScale) * 0.5f;
                ren.drawText(label, {curX + iconW + 6.f, ty}, m_smallFont, textColor, textScale);
            } else {
                std::string iconGlyph = (pa == PowerAction::Reboot) ? utf8Codepoint(0xE08F) : utf8Codepoint(0xE0B8);
                nxui::Font* fIcon = m_iconFont ? m_iconFont : m_smallFont;
                nxui::Vec2 isz = fIcon ? fIcon->measure(iconGlyph) : nxui::Vec2{16.f, 16.f};
                float actualIconW = isz.x * iconScale;
                float totalW = actualIconW + 6.f + lsz.x * textScale;
                float curX = btn.x + (btn.width - totalW) * 0.5f;

                if (fIcon) {
                    float iy = btn.y + (btn.height - isz.y * iconScale) * 0.5f;
                    ren.drawText(iconGlyph, {curX, iy}, fIcon, textColor, iconScale);
                }
                float ty = btn.y + (btn.height - lsz.y * textScale) * 0.5f;
                ren.drawText(label, {curX + actualIconW + 6.f, ty}, m_smallFont, textColor, textScale);
            }
        }
    };

    drawPowerBtn(PowerAction::Sleep, i18n.tr("quicksettings.sleep", "Suspender"),
                 nxui::Color(0.20f, 0.55f, 0.95f, 1.f));

    drawPowerBtn(PowerAction::Reboot, i18n.tr("quicksettings.reboot", "Reiniciar"),
                 nxui::Color(0.95f, 0.65f, 0.20f, 1.f));

    drawPowerBtn(PowerAction::Shutdown, i18n.tr("quicksettings.power_off", "Desligar"),
                 nxui::Color(0.95f, 0.28f, 0.35f, 1.f));

    if (m_extended)
        drawPowerBtn(PowerAction::Hekate, "Hekate", nxui::Color(0.55f, 0.40f, 0.95f, 1.f));
}
