#pragma once

#include "core/PlayTimeEvents.hpp"
#include <mutex>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct AppConfig;
namespace switchu::menu::playtime {

// Same preference as HOME: valid default profile, otherwise last-opened user,
// otherwise the first local account. Resolve on the main thread and capture it
// by value in jobs; no worker reads mutable configuration.
Profile currentProfile(const AppConfig& config);
std::string profileKey(Profile profile);
// libnx owns one pdm:qry session, also used by Activity Log and Friends.
std::mutex& queryMutex();

// Total play time the system recorded for one title, in nanoseconds, as the
// Play Data Manager reports it. nullopt when pdm could not answer, which is
// not the same as a title nobody has played: that one answers zero.
std::optional<std::uint64_t> query(std::uint64_t titleId, Profile profile = {});

// The same question for a whole catalogue. pdm:qry is opened once for the
// batch rather than once per title: the details panel's one-off query can
// afford a session of its own, a catalogue of a few hundred titles cannot.
// Blocks for one IPC round trip per title, so it belongs on a worker thread.
// A title whose query fails is logged and left out rather than failing the
// batch; the caller keeps whatever it knew about it before.
//
// Each query costs roughly a quarter of a second on hardware, so a full
// catalogue is half a minute of work. The menu drains its thread pool before
// handing the console to a game, which means a batch still running is time the
// player spends looking at a frozen launch animation -- so the caller passes a
// flag it can raise to stop the batch where it stands.
std::vector<std::pair<std::uint64_t, std::uint64_t>> queryAll(
    const std::vector<std::uint64_t>& titleIds,
    const std::atomic<bool>* cancelled = nullptr, Profile profile = {});

// When the system last recorded this title being played, as POSIX seconds;
// zero for a title never played, nullopt when pdm could not answer. One IPC
// round trip, so it belongs on a worker thread.
std::optional<std::uint64_t> queryLastPlayed(std::uint64_t titleId, Profile profile = {});

// How many times this title was started; nullopt when pdm could not answer.
// A profile query may scan the bounded event log: call it on a worker thread.
std::optional<std::uint32_t> queryLaunches(std::uint64_t titleId, Profile profile = {});

// "12 h 5 min", or "40 min" under an hour. Empty for zero.
std::string format(std::uint64_t nanoseconds);

// "152 h", or "40 min" under an hour -- the grid badge has room for one unit.
// Empty for zero.
std::string formatCompact(std::uint64_t nanoseconds);

} // namespace switchu::menu::playtime
