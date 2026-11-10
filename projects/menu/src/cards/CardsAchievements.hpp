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

// Across every game (HomeNX build 185 "overall" set): Play.launches counts
// the different games played, Play.seconds all the play time together.
inline constexpr Milestone kOverall[] = {
    {"games5", "cards.ach_games5", "Explorer", "Play 5 different games", false, 5},
    {"games15", "cards.ach_games15", "Globetrotter", "Play 15 different games", false, 15},
    {"total100", "cards.ach_total100", "Marathon", "Play for 100 hours in all", true, 100},
    {"total500", "cards.ach_total500", "Lifer", "Play for 500 hours in all", true, 500},
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
/// File key of an overall milestone: "all <id>".
std::string overallKey(const char* id);
enum class LoadStatus { Missing, Complete, Error };
// Parsing never publishes a partial snapshot on failure.
LoadStatus readStream(std::istream& in, Earned& out);
LoadStatus loadChecked(const std::string& path, Earned& out);
Earned load(const std::string& path);
bool save(const std::string& path, const Earned& e);

/// Records every milestone `play` reaches for `titleId` that is not in `e`
/// yet, stamped `now`. Returns the newly earned ones.
std::vector<const Milestone*> record(Earned& e, std::uint64_t titleId, const Play& play, std::int64_t now);

/// Records every overall milestone `total` reaches that is not in `e` yet.
std::vector<const Milestone*> recordOverall(Earned& e, const Play& total, std::int64_t now);

/// One transaction under a process-wide lock: load `path`, record `play` for
/// `titleId`, save when something is new. Returns the new milestones, and
/// sets `ok` false when the save failed (the previous file is kept).
std::vector<const Milestone*> recordAndSave(const std::string& path, std::uint64_t titleId, const Play& play,
                                            std::int64_t now, bool& ok);
/// Same transaction for the overall set (titleId-independent).
std::vector<const Milestone*> recordOverallAndSave(const std::string& path, const Play& total, std::int64_t now,
                                                   bool& ok);

inline constexpr const char* kPath = "sdmc:/config/OmniLaunch/achievements.txt";

} // namespace cards::achievements
