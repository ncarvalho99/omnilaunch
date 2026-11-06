#pragma once
// Cards HOME pages (HomeNX E1/E2, requirements C-PG-1,3,4,5 and C-IN-5).
// Pure helpers over AppConfig::CardsPrefs, kept free of GPU and libnx so the
// host test (tests/cards_pages_test.cpp) covers them. Page model adapted from
// upstream/homenx @ 0448cd4, projects/cards/src/CardsActivity.cpp:1430-1455
// (pagePos / pageHidden / firstShownPage / switchPage); see
// integration/provenance/homenx-intake.md.

#include "core/Config.hpp"
#include <cstdint>
#include <vector>

namespace cards::pages {

using Prefs = AppConfig::CardsPrefs;

/// Pages in the user's order, hidden ones left out (never empty).
std::vector<int> shown(const Prefs& prefs);
/// Where the frontend opens: the first shown page.
int first(const Prefs& prefs);
/// The next (dir > 0) or previous shown page from `current`, wrapping.
int step(const Prefs& prefs, int current, int dir);
/// Moves `page` one place earlier (dir < 0) or later in the order.
bool movePage(Prefs& prefs, int page, int dir);
/// Shows or hides `page`. Refuses to hide the last shown page.
bool togglePage(Prefs& prefs, int page);

/// `ids` reordered: those listed in `order` first, in that order; the rest
/// after them, keeping their relative order (stable).
std::vector<std::uint64_t> ordered(const std::vector<std::uint64_t>& ids,
                                   const std::vector<std::uint64_t>& order);
/// Index of `id` in `order`, or order.size() when absent.
std::size_t rank(const std::vector<std::uint64_t>& order, std::uint64_t id);

/// Moves `id` into or out of the Media page by hand. Returns true on change.
bool setMedia(Prefs& prefs, std::uint64_t id, bool media);

/// Drops every Cards preference that names `id` (orders, Media membership,
/// image choices, the panel shortcut), after the title was deleted (E4).
/// Returns true when anything changed.
bool forgetTitle(Prefs& prefs, std::uint64_t id);

/// Synthetic ids for the Nintendo page tiles. Real application ids never use
/// the top byte 0xFF.
inline constexpr std::uint64_t kSystemIdBase = 0xFF00000000000000ULL;
inline bool isSystemId(std::uint64_t id) { return (id & 0xFF00000000000000ULL) == kSystemIdBase; }

/// Nintendo page tiles this build can open (C-PG-3). eShop, Friends, Switch
/// Online, News, Browser and System settings arrive with todo F1/G3.
enum class SystemTile : int { Album = 1, Controllers, MiiEditor, Internet, Users, HomeSettings };
inline std::uint64_t systemId(SystemTile t) { return kSystemIdBase | static_cast<std::uint64_t>(t); }
inline SystemTile systemTileOf(std::uint64_t id) { return static_cast<SystemTile>(id & 0xFFu); }

} // namespace cards::pages
