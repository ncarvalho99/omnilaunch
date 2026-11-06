#pragma once
// Reboot to Hekate's menu (HomeNX E5, C-UI-9).
//
// OWNER-AUTHORIZED EXCEPTION (2026-11-06): this is the only menu code that
// talks to Atmosphere's bpc:ams port (command 65001, SetRebootPayload). It only
// stages the payload; the reboot itself is the unchanged
// AppletLauncher::reboot() (appletStartRebootSequence), which Atmosphere's bpc
// mitm on am/omm turns into a reboot to that payload. No splSetConfig, no raw
// reboot call. Restart and Turn Off never reach this file.
//
// Models that cannot reboot to a payload (Mariko) use hekate's RTC reboot
// reason instead (switchu/hekate_rtc.hpp), then the same standard reboot.

#include <string>

namespace hekate {

/// Prepares the next reboot to open Hekate's menu. Main thread, before
/// AppletLauncher::reboot(). On failure returns false and a player-facing
/// reason in `error`; nothing was changed in that case except on the RTC path.
bool prepareMenuReboot(std::string& error);

} // namespace hekate
