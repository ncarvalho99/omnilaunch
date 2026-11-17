// Host test for switchu/legacy_migration.hpp: a legacy file is never deleted
// unless it reached its new place or is a byte-identical duplicate.
#include <switchu/legacy_migration.hpp>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

static void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

static std::string get(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int main() {
    const fs::path root = fs::temp_directory_path() / "omnilaunch_migration_test";
    fs::remove_all(root);

    // 1. No destination: the whole folder is renamed.
    {
        const auto legacy = root / "a/SwitchU", current = root / "a/OmniLaunch";
        put(legacy / "config.json", "old");
        const auto r = switchu::legacy::migrateDirectory(legacy, current);
        assert(r.failures == 0 && !fs::exists(legacy) && get(current / "config.json") == "old");
    }

    // 2. Merge: new file moved, duplicate dropped, conflict keeps both.
    {
        const auto legacy = root / "b/SwitchU", current = root / "b/OmniLaunch";
        put(legacy / "only-old.txt", "1");
        put(legacy / "same.txt", "same");
        put(current / "same.txt", "same");
        put(legacy / "sub/differs.txt", "old");
        put(current / "sub/differs.txt", "new");
        const auto r = switchu::legacy::migrateDirectory(legacy, current);
        assert(r.failures == 0 && r.moved == 1 && r.duplicates == 1 && r.conflicts == 1);
        assert(get(current / "only-old.txt") == "1");
        assert(!fs::exists(legacy / "same.txt") && get(current / "same.txt") == "same");
        assert(get(legacy / "sub/differs.txt") == "old" && get(current / "sub/differs.txt") == "new");
        assert(fs::exists(legacy)); // the conflict keeps the legacy folder
    }

    // 3. Failure: a destination folder that cannot be created (a file is in
    // its way) keeps every legacy file and reports the failure for a retry.
    {
        const auto legacy = root / "c/SwitchU", current = root / "c/OmniLaunch";
        put(legacy / "themes/t.json", "theme");
        put(current / "themes", "not a folder");
        const auto r = switchu::legacy::migrateDirectory(legacy, current);
        assert(r.failures > 0);
        assert(get(legacy / "themes/t.json") == "theme");

        // 4. Retry after the obstacle is gone completes the migration.
        fs::remove(current / "themes");
        const auto again = switchu::legacy::migrateDirectory(legacy, current);
        assert(again.failures == 0 && get(current / "themes/t.json") == "theme" && !fs::exists(legacy));
    }

    // 5. Legacy Manager folder: removed only when it holds just its NRO.
    {
        const auto legacy = root / "d/SwitchU-Manager", nro = root / "d/OmniLaunch-Manager/OmniLaunch-Manager.nro";
        put(nro, "new");
        put(legacy / "SwitchU-Manager.nro", "old");
        put(legacy / "notes.txt", "keep");
        assert(!switchu::legacy::removeLegacyManager(legacy, "SwitchU-Manager.nro", nro));
        assert(get(legacy / "notes.txt") == "keep" && get(legacy / "SwitchU-Manager.nro") == "old");
        fs::remove(legacy / "notes.txt");
        assert(switchu::legacy::removeLegacyManager(legacy, "SwitchU-Manager.nro", nro));
        assert(!fs::exists(legacy));
    }

    fs::remove_all(root);
    std::cout << "legacy migration keeps every unmigrated file PASS\n";
}
