#include "core/IconPack.hpp"

#include "core/DebugLog.hpp"

#include <nlohmann/json.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace iconpack {

namespace {
std::mutex g_mutex;
std::string g_activeId;
std::string g_activeDir;
std::atomic<std::uint32_t> g_generation{1};

bool regularFile(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

#ifdef SWITCHU_HOMEBREW
constexpr const char* kSdIcons = nullptr;
#else
constexpr const char* kSdIcons = "sdmc:/switch/OmniLaunch/icons/";
#endif
} // namespace

bool safeId(const std::string& id) {
    if (id.size() < 2 || id.size() > 64)
        return false;
    for (unsigned char ch : id)
        if (!std::islower(ch) && !std::isdigit(ch) && ch != '-' && ch != '_')
            return false;
    return true;
}

std::string installPath(const std::string& id) {
    return std::string(kRoot) + "/" + id;
}

void setActive(const std::string& id) {
    std::string dir;
    if (safeId(id)) {
        std::error_code ec;
        const std::string candidate = installPath(id);
        if (std::filesystem::is_directory(candidate, ec))
            dir = candidate;
        else
            DebugLog::log("[icon-pack] '%s' is not installed; using built-in icons", id.c_str());
    }
    std::lock_guard<std::mutex> lk(g_mutex);
    const std::string newId = dir.empty() ? std::string() : id;
    if (newId == g_activeId && dir == g_activeDir)
        return;
    g_activeId = newId;
    g_activeDir = dir;
    g_generation.fetch_add(1);
    DebugLog::log("[icon-pack] active=%s gen=%u", newId.empty() ? "<built-in>" : newId.c_str(),
                  (unsigned)g_generation.load());
}

void refresh() {
    g_generation.fetch_add(1);
    DebugLog::log("[icon-pack] refresh gen=%u", (unsigned)g_generation.load());
}

std::string activeId() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_activeId;
}

std::string activeDir() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_activeDir;
}

std::uint32_t generation() {
    return g_generation.load();
}

std::string resolve(const std::string& fileName, const std::string& fallbackPath) {
    const std::string dir = activeDir();
    if (!dir.empty() && !fileName.empty() && fileName.find("..") == std::string::npos) {
        std::string candidate = dir + "/" + fileName;
        if (regularFile(candidate))
            return candidate;
    }
    return fallbackPath;
}

std::vector<std::string> candidates(const std::string& fileName) {
    std::vector<std::string> out;
    const std::string dir = activeDir();
    if (!dir.empty())
        out.push_back(dir + "/" + fileName);
    if (kSdIcons)
        out.push_back(std::string(kSdIcons) + fileName);
    out.push_back(std::string("romfs:/icons/") + fileName);
    return out;
}

bool loadInto(nxui::Texture& tex, nxui::Renderer& ren, const std::string& fileName, int maxSide) {
    for (const auto& path : candidates(fileName)) {
        if (path.rfind("romfs:", 0) != 0 && !regularFile(path))
            continue;
        if (tex.loadFromFile(ren.gpu(), ren, path, maxSide))
            return true;
    }
    return false;
}

std::vector<Installed> scanInstalled() {
    std::vector<Installed> packs;
    std::error_code ec;
    if (!std::filesystem::is_directory(kRoot, ec))
        return packs;
    for (const auto& entry : std::filesystem::directory_iterator(kRoot, ec)) {
        if (ec)
            break;
        std::error_code dirEc;
        if (!entry.is_directory(dirEc))
            continue;
        const std::string id = entry.path().filename().string();
        // Half-written installs end in ".installing"; safeId refuses the dot.
        if (!safeId(id))
            continue;
        Installed pack;
        pack.id = id;
        pack.name = id;
        pack.path = entry.path().string();
        try {
            std::ifstream in(pack.path + "/pack.json");
            if (in) {
                const auto j = nlohmann::json::parse(in, nullptr, false);
                if (j.is_object()) {
                    if (j.contains("name") && j["name"].is_string()) pack.name = j["name"].get<std::string>();
                    if (j.contains("author") && j["author"].is_string()) pack.author = j["author"].get<std::string>();
                    if (j.contains("version") && j["version"].is_string()) pack.version = j["version"].get<std::string>();
                }
            }
        } catch (...) {
        }
        {
            std::ifstream marker(pack.path + "/" + kPackageMarker);
            std::string sha;
            if (marker && std::getline(marker, sha)) {
                while (!sha.empty() && (sha.back() == '\r' || sha.back() == ' '))
                    sha.pop_back();
                pack.packageSha256 = sha;
            }
        }
        for (const auto& file : std::filesystem::directory_iterator(pack.path, dirEc)) {
            if (dirEc)
                break;
            std::error_code fEc;
            if (!file.is_regular_file(fEc))
                continue;
            pack.bytes += static_cast<std::uint64_t>(file.file_size(fEc));
            if (file.path().extension() == ".png")
                ++pack.files;
        }
        packs.push_back(std::move(pack));
    }
    std::sort(packs.begin(), packs.end(), [](const Installed& a, const Installed& b) { return a.name < b.name; });
    return packs;
}

} // namespace iconpack
