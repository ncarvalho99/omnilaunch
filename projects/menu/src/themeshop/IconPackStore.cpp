#include "IconPackStore.hpp"

#include "ThemeHttp.hpp"
#include "ZipReader.hpp"

#include "core/DebugLog.hpp"
#include "core/IconPack.hpp"

#include <switchu/fs_remove.hpp>

#include <nlohmann/json.hpp>
#include <nxui/core/I18n.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>

namespace iconstore {

namespace {

std::string absoluteUrl(const std::string& relative) {
    if (relative.empty() || relative.rfind("https://", 0) == 0 || relative.rfind("http://", 0) == 0)
        return relative;
    std::string rel = relative;
    while (!rel.empty() && rel.front() == '/')
        rel.erase(rel.begin());
    const std::string base(kCatalogUrl);
    return base.substr(0, base.find_last_of('/') + 1) + rel;
}

std::string str(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

std::uint64_t u64(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number_unsigned()) ? it->get<std::uint64_t>() : 0;
}

bool removeTree(const std::string& path) {
    std::string failed;
    if (switchu::removeRecursive(path, &failed))
        return true;
    DebugLog::log("[icon-store] could not remove %s (stopped at %s)", path.c_str(), failed.c_str());
    return false;
}

bool makeDirs(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return std::filesystem::is_directory(path, ec);
}

} // namespace

Catalog fetchCatalog() {
    Catalog out;
    try {
        const std::string body = themeshop::http::getText(kCatalogUrl);
        const auto root = nlohmann::json::parse(body);
        auto packs = root.find("packs");
        if (packs == root.end() || !packs->is_array())
            throw std::runtime_error("icon catalogue has no packs array");
        for (const auto& item : *packs) {
            if (!item.is_object())
                continue;
            ThemeCatalogClient::Entry e;
            e.id = str(item, "id");
            e.name = str(item, "name");
            if (!iconpack::safeId(e.id) || e.name.empty())
                continue;
            e.author = str(item, "author");
            e.version = str(item, "version");
            e.package = str(item, "package");
            e.packageBytes = u64(item, "packageBytes");
            e.installedBytes = u64(item, "installedBytes");
            if (auto f = item.find("files"); f != item.end() && f->is_number_integer())
                e.files = f->get<int>();
            if (auto b = item.find("builtin"); b != item.end() && b->is_boolean())
                e.builtin = b->get<bool>();
            e.cover = absoluteUrl(str(item, "preview"));
            if (auto shots = item.find("screenshots"); shots != item.end() && shots->is_array()) {
                for (const auto& s : *shots)
                    if (s.is_string())
                        e.screenshots.push_back(absoluteUrl(s.get<std::string>()));
            }
            if (!e.cover.empty() && (e.screenshots.empty() || e.screenshots.front() != e.cover))
                e.screenshots.insert(e.screenshots.begin(), e.cover);
            e.catalogUrl = kCatalogUrl;
            e.catalogOrder = out.entries.size();
            if (!e.builtin && e.package.empty())
                continue; // nothing to install
            out.entries.push_back(std::move(e));
        }
        out.ok = true;
        DebugLog::log("[icon-store] catalogue: %zu pack(s)", out.entries.size());
    } catch (const std::exception& ex) {
        out.error = ex.what();
        DebugLog::log("[icon-store] catalogue failed: %s", ex.what());
    } catch (...) {
        out.error = "unknown error";
    }
    return out;
}

InstallResult install(const ThemeCatalogClient::Entry& entry, const Progress& progress,
                      const std::atomic<bool>* cancel) {
    InstallResult result;
    auto& i18n = nxui::I18n::instance();
    if (!iconpack::safeId(entry.id) || entry.package.empty()) {
        result.message = i18n.tr("iconstore.install_failed", "The icon pack could not be installed.");
        return result;
    }
    themeshop::http::ThreadCancelGuard guard(cancel);
    const std::string dest = iconpack::installPath(entry.id);
    const std::string staging = dest + ".installing";
    const std::string archive = staging + ".part";
    const std::string previous = dest + ".previous";
    try {
        if (!makeDirs(iconpack::kRoot))
            throw std::runtime_error("cannot create the icon pack folder");
        removeTree(staging);
        std::remove(archive.c_str());
        if (!makeDirs(staging))
            throw std::runtime_error("cannot create the staging folder");

        const std::string url = absoluteUrl(entry.package);
        DebugLog::log("[icon-store] download %s", url.c_str());
        if (progress)
            progress(i18n.tr("iconstore.downloading", "Downloading icons"), 0.05f);
        const std::uint64_t expected = entry.packageBytes;
        const std::uint64_t got = themeshop::http::getToFile(
            url, archive,
            [&](std::uint64_t received, std::uint64_t total) {
                const std::uint64_t all = expected ? expected : total;
                if (progress && all > 0)
                    progress(i18n.tr("iconstore.downloading", "Downloading icons"),
                             0.05f + 0.75f * (float)std::min(received, all) / (float)all);
            },
            cancel);
        if (got == 0 || (expected > 0 && got != expected)) {
            DebugLog::log("[icon-store] size mismatch: expected=%llu got=%llu",
                          (unsigned long long)expected, (unsigned long long)got);
            throw std::runtime_error("download size mismatch");
        }

        if (progress)
            progress(i18n.tr("iconstore.unpacking", "Installing icons"), 0.85f);
        // Images and the manifest only, as the theme packages.
        const auto extracted = themeshop::extractZipFile(archive, staging);
        std::remove(archive.c_str());
        if (!extracted.success)
            throw std::runtime_error("unpack failed: " + extracted.error);
        struct stat st {};
        if (stat((staging + "/pack.json").c_str(), &st) != 0)
            throw std::runtime_error("pack.json missing from the archive");

        // Swap: the old copy aside, the new one in, the old one removed.
        removeTree(previous);
        const bool hadOld = stat(dest.c_str(), &st) == 0;
        if (hadOld && std::rename(dest.c_str(), previous.c_str()) != 0)
            throw std::runtime_error(std::string("cannot move the old pack aside: ") + std::strerror(errno));
        if (std::rename(staging.c_str(), dest.c_str()) != 0) {
            const std::string err = std::strerror(errno);
            if (hadOld)
                std::rename(previous.c_str(), dest.c_str());
            throw std::runtime_error("cannot move the new pack in: " + err);
        }
        if (hadOld)
            removeTree(previous);
        result.ok = true;
        result.message = i18n.tr("iconstore.installed_ok", "Icon pack installed.");
        DebugLog::log("[icon-store] installed %s (%d files, %llu bytes)", entry.id.c_str(),
                      extracted.filesWritten, (unsigned long long)extracted.bytesWritten);
    } catch (const themeshop::http::Cancelled&) {
        result.cancelled = true;
        result.message = i18n.tr("iconstore.cancelled", "Download cancelled.");
    } catch (const std::exception& ex) {
        DebugLog::log("[icon-store] install %s failed: %s", entry.id.c_str(), ex.what());
        result.message = i18n.tr("iconstore.install_failed", "The icon pack could not be installed.");
    }
    if (!result.ok) {
        std::remove(archive.c_str());
        removeTree(staging);
    }
    return result;
}

bool remove(const std::string& id, std::string& error) {
    if (!iconpack::safeId(id)) {
        error = "invalid id";
        return false;
    }
    if (!removeTree(iconpack::installPath(id))) {
        error = "could not remove the pack";
        return false;
    }
    return true;
}

} // namespace iconstore
