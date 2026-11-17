#pragma once
// Moves a legacy SwitchU folder into its OmniLaunch replacement without ever
// deleting a file that did not reach its new place.
//
// - A file whose destination is free is renamed into place.
// - A file whose destination already holds the same bytes is a duplicate: the
//   legacy copy is removed.
// - A file whose destination holds different bytes is a conflict: both are
//   kept, the legacy copy stays where it is.
// - A failed rename, directory creation or status query is a failure: the
//   file stays where it is and the migration is retried on the next start.
// Only directories that ended up empty are removed.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace switchu::legacy {

struct MergeResult {
    unsigned moved = 0;
    unsigned duplicates = 0;
    unsigned conflicts = 0;
    unsigned failures = 0;
};

inline bool sameFileContents(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    const auto sa = std::filesystem::file_size(a, ec);
    if (ec) return false;
    const auto sb = std::filesystem::file_size(b, ec);
    if (ec || sa != sb) return false;
    FILE* fa = std::fopen(a.string().c_str(), "rb");
    if (!fa) return false;
    FILE* fb = std::fopen(b.string().c_str(), "rb");
    if (!fb) { std::fclose(fa); return false; }
    bool same = true;
    char ba[4096], bb[4096];
    for (;;) {
        const std::size_t na = std::fread(ba, 1, sizeof(ba), fa);
        const std::size_t nb = std::fread(bb, 1, sizeof(bb), fb);
        if (na != nb || std::char_traits<char>::compare(ba, bb, na) != 0) { same = false; break; }
        if (na == 0) break;
    }
    if (std::ferror(fa) || std::ferror(fb)) same = false;
    std::fclose(fa);
    std::fclose(fb);
    return same;
}

inline void mergeInto(const std::filesystem::path& src, const std::filesystem::path& dst, MergeResult& r) {
    std::error_code ec;
    std::filesystem::create_directories(dst, ec);
    if (ec || !std::filesystem::is_directory(dst, ec)) { ++r.failures; return; }
    std::filesystem::directory_iterator it(src, ec);
    if (ec) { ++r.failures; return; }
    for (const std::filesystem::directory_iterator end; it != end; it.increment(ec)) {
        const auto source = it->path();
        const auto target = dst / source.filename();
        std::error_code typeEc;
        const bool isDir = it->is_directory(typeEc);
        if (typeEc) { ++r.failures; continue; }
        if (isDir) {
            mergeInto(source, target, r);
            std::error_code rmEc;
            if (std::filesystem::is_empty(source, rmEc) && !rmEc)
                std::filesystem::remove(source, rmEc);
            continue;
        }
        std::error_code exEc;
        const bool targetExists = std::filesystem::exists(target, exEc);
        if (exEc) {
            ++r.failures;
        } else if (!targetExists) {
            std::error_code mvEc;
            std::filesystem::rename(source, target, mvEc);
            if (mvEc) ++r.failures; else ++r.moved;
        } else if (sameFileContents(source, target)) {
            std::error_code rmEc;
            if (std::filesystem::remove(source, rmEc) && !rmEc) ++r.duplicates; else ++r.failures;
        } else {
            ++r.conflicts;
        }
    }
    if (ec) ++r.failures; // the listing stopped early: the rest is retried
}

/// Migrates `legacy` into `current`. Renames the whole folder when `current`
/// does not exist yet; otherwise merges file by file. The legacy folder is
/// removed only when nothing is left in it.
inline MergeResult migrateDirectory(const std::filesystem::path& legacy, const std::filesystem::path& current) {
    MergeResult r;
    std::error_code ec;
    const bool legacyExists = std::filesystem::exists(legacy, ec);
    if (ec) { ++r.failures; return r; } // unknown is not "absent": retry later
    if (!legacyExists)
        return r;
    const bool currentExists = std::filesystem::exists(current, ec);
    if (ec) { ++r.failures; return r; }
    if (!currentExists) {
        std::filesystem::rename(legacy, current, ec);
        if (!ec) { ++r.moved; return r; }
        ec.clear();
    }
    mergeInto(legacy, current, r);
    if (std::filesystem::is_empty(legacy, ec) && !ec)
        std::filesystem::remove(legacy, ec);
    return r;
}

/// Removes the legacy Manager folder only when it holds nothing but the legacy
/// Manager NRO and the new Manager is installed. Anything else in it is kept.
inline bool removeLegacyManager(const std::filesystem::path& legacyDir,
                                const std::filesystem::path& legacyNroName,
                                const std::filesystem::path& newManagerNro) {
    std::error_code ec;
    if (!std::filesystem::exists(legacyDir, ec) || ec) return false;
    if (!std::filesystem::exists(newManagerNro, ec) || ec) return false;
    std::filesystem::directory_iterator it(legacyDir, ec);
    if (ec) return false;
    for (const std::filesystem::directory_iterator end; it != end; it.increment(ec)) {
        std::error_code typeEc;
        if (it->path().filename() != legacyNroName || !it->is_regular_file(typeEc) || typeEc)
            return false;
    }
    if (ec) return false;
    std::filesystem::remove(legacyDir / legacyNroName, ec);
    if (ec) return false;
    return std::filesystem::remove(legacyDir, ec) && !ec;
}

} // namespace switchu::legacy
