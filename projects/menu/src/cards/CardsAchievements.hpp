#pragma once
// Achievements for the Cards HOME (HomeNX E11, requirement C-UI-5): milestones
// every game has, from its play data (pdm play time and launch count), kept in
// sdmc:/config/OmniLaunch/achievements.txt. Milestone table and file shape
// adapted from upstream/homenx @ 0448cd4,
// projects/common/include/switchu/achievements.hpp (GPL-3.0, as this
// product); see integration/provenance/homenx-intake.md. Per-game memory packs
// (dmnt:cht) and the daemon watcher are out of scope here.

#include <cstdint>
#include <map>
#include <istream>
#include <string>
#include <vector>

namespace cards::achievements {

struct Milestone {
    const char* id;    // as kept in the file
    const char* key;   // i18n key for the name
    const char* name;  // English fallback
    const char* about;
    bool hours;        // goal in hours played, else in times started
    std::uint32_t goal;
};

inline constexpr Milestone kPerGame[] = {
    {"first", "cards.ach_first", "First steps", "Start it", false, 1},
    {"h1", "cards.ach_h1", "Getting into it", "Play it for 1 hour", true, 1},
    {"h10", "cards.ach_h10", "Hooked", "Play it for 10 hours", true, 10},
    {"h25", "cards.ach_h25", "Dedicated", "Play it for 25 hours", true, 25},
    {"h50", "cards.ach_h50", "Devoted", "Play it for 50 hours", true, 50},
    {"h100", "cards.ach_h100", "Legend", "Play it for 100 hours", true, 100},
    {"l10", "cards.ach_l10", "Regular player", "Start it 10 times", false, 10},
    {"l50", "cards.ach_l50", "Old friend", "Start it 50 times", false, 50},
};

struct Play {
    std::uint64_t seconds = 0;
    std::uint32_t launches = 0;
};

/// How far along, 0..1 (1: earned).
float progress(const Milestone& m, const Play& p);

/// Earned milestones: "<TITLEID16> <id>" -> POSIX seconds when first seen.
struct Earned {
    std::map<std::string, std::int64_t> when;
};

std::string key(std::uint64_t titleId, const char* id);
enum class LoadStatus { Missing, Complete, Error };
// Parsing never publishes a partial snapshot on failure.
LoadStatus readStream(std::istream& in, Earned& out);
LoadStatus loadChecked(const std::string& path, Earned& out);
Earned load(const std::string& path);
bool save(const std::string& path, const Earned& e);

/// Records every milestone `play` reaches for `titleId` that is not in `e`
/// yet, stamped `now`. Returns the newly earned ones.
std::vector<const Milestone*> record(Earned& e, std::uint64_t titleId, const Play& play, std::int64_t now);

/// One transaction under a process-wide lock: load `path`, record `play` for
/// `titleId`, save when something is new. Returns the new milestones, and
/// sets `ok` false when the save failed (the previous file is kept).
std::vector<const Milestone*> recordAndSave(const std::string& path, std::uint64_t titleId, const Play& play,
                                            std::int64_t now, bool& ok);

inline constexpr const char* kPath = "sdmc:/config/OmniLaunch/achievements.txt";

} // namespace cards::achievements
