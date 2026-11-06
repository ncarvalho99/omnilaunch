#pragma once
// Lock screen password (HomeNX E9, C-UI-16): 4 to 8 D-pad presses, stored as a
// salted SHA-256 plus the length in sdmc:/config/OmniLaunch/lock_password.
// Model adapted from HomeNX (GPL-3.0) @ 0448cd4,
// projects/common/include/switchu/lock_password.hpp; the hash is passed in so
// this file stays free of libnx and the host test can drive it.
//
// Recovery for a forgotten password (owner decision 2026-11-06: HOME + button
// combo). HOME is a system button the menu only sees as a daemon notification,
// so the gesture is: press HOME on the password lock, then within
// kRecoveryWindow seconds hold L + R + ZL + ZR + Minus for kRecoveryHold
// seconds. It clears the password and unlocks. Deleting the file from the SD
// card on a PC also clears it. With a password set, HOME alone no longer
// unlocks.

#include <cstddef>
#include <functional>
#include <string>

namespace lockpw {

inline constexpr const char* kPath = "sdmc:/config/OmniLaunch/lock_password";
inline constexpr std::size_t kMin = 4, kMax = 8;
inline constexpr float kRecoveryWindow = 10.f;
inline constexpr float kRecoveryHold = 3.f;

/// 64 lowercase hex characters of SHA-256 over `data`.
using HashFn = std::function<std::string(const std::string& data)>;

/// Presses are letters: U D L R.
std::string digest(const HashFn& hash, const std::string& presses);
/// Length of the stored password, 0 when none (or the file is malformed).
std::size_t storedLength(const std::string& path = kPath);
bool check(const HashFn& hash, const std::string& presses, const std::string& path = kPath);
/// Empty `presses` removes the password. Returns false on a bad length or a
/// write failure.
bool save(const HashFn& hash, const std::string& presses, const std::string& path = kPath);

/// Console SHA-256 (libnx); empty string on host builds.
std::string sha256Hex(const std::string& data);

} // namespace lockpw

class LockScreen;
namespace lockpw {
/// Applies the stored password (or none) to `lock`, wiring check and the
/// recovery clear. Call at frontend start and after a change.
void attach(LockScreen& lock);

} // namespace lockpw
