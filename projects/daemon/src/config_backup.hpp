#pragma once

namespace switchu::daemon::config_backup {

// Keeps a copy of config/OmniLaunch in /backup/OmniLaunch, and puts it back when the
// configuration has disappeared.
//
// The CNX Updater's clean install deletes every folder at the root of the card
// except a fixed list, and /backup is on that list (CostelaCNX/CNX-Updater,
// app-rcm/bootloader/main.c, canDeleteFolder). A console that went through one
// came back with the launcher at its first-run tutorial: settings, SteamGridDB key,
// folders, themes and artwork all gone.
//
// Called once at boot, before the menu is launched, so the menu either finds
// its configuration restored or leaves behind a copy of what it had. When
// config/OmniLaunch/config.json is missing and the backup has one, the backup is
// restored; otherwise the backup is brought up to date with what changed.
// Caches, logs, update staging, and leave_frame.bin are left out: they rebuild themselves.
void restoreOrBackUp();

} // namespace switchu::daemon::config_backup
