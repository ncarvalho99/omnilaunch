#pragma once
// Console (system) settings shared by the Classic settings tabs and the Cards
// settings sheet: TV resolution, controller vibration, time zone and manual
// date/time. Each call reads or writes the system setting directly; nothing
// is cached, so both frontends always show the console's current value.

#include <switch.h>
#include <string>
#include <vector>

namespace settings::system_prefs {

/// HDMI output resolution as the system stores it: 0 Auto, 1 1080p, 2 720p, 3 480p.
constexpr int kTvResolutionCount = 4;
int tvResolution();
bool setTvResolution(int value);

/// Controller vibration master volume, 0..1.
float vibrationVolume();
bool setVibrationVolume(float value);

/// Every location name the console knows, in its own order.
std::vector<std::string> timezoneNames();
std::string currentTimezone();
Result setTimezone(const std::string& name);

struct DateTime {
    int year = 2000;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
};
/// The user clock in the device time zone.
bool currentDateTime(DateTime& out);
/// Queues the manual clock change with the daemon (it answers with
/// TimeSettingApplied). Only meaningful while Internet time sync is off.
Result setManualDateTime(const DateTime& value);
/// True when the user clock is corrected from the network.
bool internetTimeSync();

/// Docked 1080p rendering (J2): HOME draws at 1920x1080 on a TV instead of
/// scaling a 1280x720 frame. Read when HOME starts, so a change applies at the
/// next start. Off by default.
bool dockedNativeRender();
bool setDockedNativeRender(bool enabled);

} // namespace settings::system_prefs
