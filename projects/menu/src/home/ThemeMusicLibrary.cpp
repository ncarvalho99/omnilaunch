#include "home/ThemeMusicLibrary.hpp"
#include "home/HomeAudio.hpp"
#include "core/DebugLog.hpp"
#include "core/ThemePreset.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <set>

namespace home {

namespace {
bool installLeftover(const std::string& dirName) {
    for (const char* suffix : {".installing", ".installing.part", ".previous"}) {
        const std::size_t n = std::strlen(suffix);
        if (dirName.size() > n && dirName.compare(dirName.size() - n, n, suffix) == 0)
            return true;
    }
    return false;
}
} // namespace

std::vector<TrackInfo> scanThemeMusicLibrary(const std::string& assetsRoot) {
    struct Item { std::string theme; TrackInfo track; };
    std::vector<Item> items;
    std::set<std::string> seenDirs, seenPaths;
    static constexpr const char* kDirs[] = {
        "sdmc:/config/OmniLaunch/themes", "sdmc:/switchu/themes", "sdmc:/slaunch/themes",
    };
    for (const char* base : kDirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(base, ec))
            continue;
        for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
            if (ec)
                break;
            std::error_code dirEc;
            if (!entry.is_directory(dirEc))
                continue;
            const std::string dirName = entry.path().filename().string();
            if (installLeftover(dirName) || !seenDirs.insert(dirName).second)
                continue;
            const auto theme = ThemePreset::loadSinglePackage(dirName);
            if (!theme || theme->music.empty())
                continue;
            std::vector<std::string> paths;
            for (const auto& raw : theme->music) {
                if (raw.empty())
                    continue;
                std::string path;
                if (raw.front() == '/' || raw.find(":/") != std::string::npos)
                    path = raw;
                else if (!theme->installPath.empty())
                    path = theme->installPath + (theme->installPath.back() == '/' ? "" : "/") + raw;
                else
                    path = assetsRoot + "/" + raw;
                std::error_code fe;
                if (std::filesystem::is_regular_file(path, fe) && seenPaths.insert(path).second)
                    paths.push_back(std::move(path));
            }
            const std::string name = theme->name.empty() ? dirName : theme->name;
            for (std::size_t i = 0; i < paths.size(); ++i)
                items.push_back({name, {paths[i], HomeAudio::themeTrackTitle(name, paths[i], i, paths.size())}});
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.theme < b.theme;
    });
    std::vector<TrackInfo> out;
    out.reserve(items.size());
    for (auto& item : items)
        out.push_back(std::move(item.track));
    DebugLog::log("[audio] theme music library: %zu song(s)", out.size());
    return out;
}

} // namespace home
