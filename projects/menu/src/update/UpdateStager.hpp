#pragma once
// Downloads, verifies and stages an OmniLaunch release for the daemon to apply
// at the next boot. Shared by every HOME frontend (Classic and Cards), so the
// rules for what may reach the card live in one place.

#include "update/UpdateClient.hpp"

#include <cstdint>
#include <functional>

namespace update {

/// True while a verified package waits on the card for the restart that
/// applies it (the daemon removes the marker once the files are in place).
bool restartPending();

/// Runs on a worker thread. Downloads the release ZIP, checks its size and
/// published SHA-256, inspects every entry, writes the ready marker and puts
/// the archive's daemon in place under the rollback journal. Throws
/// std::runtime_error on any failure, after removing whatever it staged.
/// `onProgress` receives (received, total); `onApplying` is called once the
/// download is verified and the archive is being checked.
void stageRelease(const UpdateClient::Release& release,
                  const std::function<void(std::uint64_t, std::uint64_t)>& onProgress,
                  const std::function<void()>& onApplying);

} // namespace update
