#pragma once
// GameTDB index lookup for Switch game box covers (HomeNX G2, C-NET-4).
// Reads romfs/data/gametdb.tsv, generated offline by tools/make_gametdb_index.py.
//
// Matches a game title to its GameTDB identifier and region, and formats the
// art.gametdb.com cover URL. Safe to call from worker threads.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cards {

class GameTdbIndex {
public:
    struct Entry {
        std::string id;
        char region = 'A'; // 'U' (USA), 'E' (Europe), 'J' (Japan), 'A' (All/other)
    };

    /// Normalizes a game title into a lowercase alphanumeric key with accents
    /// folded to base ASCII (U+00C0..U+017F). Matches tools/make_gametdb_index.py.
    static std::string nameKey(std::string_view name);

    /// Looks up a title's GameTDB entry in romfs/data/gametdb.tsv (or tsvPath override).
    /// Lazy-loads the index into memory on the first call.
    static std::optional<Entry> lookup(std::string_view title, const std::string& tsvPath = "");

    /// Formats the cover URL for a GameTDB entry and regional trial attempt (0, 1, 2).
    /// Cycles through region precedence:
    /// 'E': EN, US, JA
    /// 'J': JA, US, EN
    /// 'U'/'A': US, EN, JA
    /// Returns empty string if attempt >= 3 or id is empty.
    static std::string coverUrl(std::string_view gid, char region, int attempt = 0);

    /// Frees the cached index data from memory.
    static void unload();

    /// Returns true if the index is currently loaded in memory.
    static bool isLoaded();
};

} // namespace cards
