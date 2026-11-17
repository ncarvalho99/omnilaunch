#include "core/DecodedAssetCache.hpp"
#include "core/DebugLog.hpp"
#include "core/IconPack.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace decoded_assets {
namespace {

using Key = std::pair<std::string, int>;

struct Entry {
    std::shared_ptr<const nxui::DecodedImage> image; // null while decoding
};

std::mutex g_mutex;
std::map<Key, Entry> g_entries;
std::uint32_t g_generation = 0;

// Caller holds g_mutex. An icon pack switch or update replaces the files
// behind the same paths, so everything decoded before it is stale.
void syncGenerationLocked() {
    const std::uint32_t gen = iconpack::generation();
    if (gen != g_generation) {
        g_entries.clear();
        g_generation = gen;
    }
}

} // namespace

bool load(nxui::Texture& tex, nxui::GpuDevice& gpu, nxui::Renderer& ren,
          const std::string& path, int maxSide) {
    // DDS frames and the like keep their own path.
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".dds") == 0)
        return tex.loadFromFile(gpu, ren, path, maxSide);

    const Key key{path, maxSide};
    std::shared_ptr<const nxui::DecodedImage> image;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        syncGenerationLocked();
        auto it = g_entries.find(key);
        if (it != g_entries.end())
            image = it->second.image;
    }
    if (!image) {
        // Not cached, or a prewarm still decoding it: decode here, as before.
        auto decoded = std::make_shared<nxui::DecodedImage>(nxui::Texture::decodeFile(path, maxSide));
        if (!decoded->valid())
            return tex.loadFromDecoded(gpu, ren, *decoded); // fails, as loadFromFile did
        image = decoded;
        std::lock_guard<std::mutex> lk(g_mutex);
        syncGenerationLocked();
        g_entries[key].image = image;
    }
    return tex.loadFromDecoded(gpu, ren, *image);
}

void prewarm(nxui::ThreadPool& pool, const std::vector<std::string>& paths, int maxSide) {
    std::vector<std::string> wanted;
    std::uint32_t gen = 0;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        syncGenerationLocked();
        gen = g_generation;
        for (const auto& path : paths) {
            if (path.empty())
                continue;
            const Key key{path, maxSide};
            if (g_entries.count(key))
                continue;
            g_entries.emplace(key, Entry{});
            wanted.push_back(path);
        }
    }
    if (wanted.empty() || !pool.acceptingTasks())
        return;
    const std::size_t count = wanted.size();
    pool.submit([wanted = std::move(wanted), maxSide, gen]() {
        for (const auto& path : wanted) {
            auto decoded = std::make_shared<nxui::DecodedImage>(nxui::Texture::decodeFile(path, maxSide));
            std::lock_guard<std::mutex> lk(g_mutex);
            if (g_generation != gen)
                return; // the icon pack changed meanwhile; these are stale
            auto it = g_entries.find(Key{path, maxSide});
            if (it == g_entries.end())
                continue;
            if (decoded->valid())
                it->second.image = std::move(decoded);
            else
                g_entries.erase(it); // load() will try (and log) it itself
        }
    });
    DebugLog::log("[asset-cache] prewarm requested: %zu file(s)", count);
}

bool ready(const std::vector<std::string>& paths, int maxSide) {
    std::lock_guard<std::mutex> lk(g_mutex);
    syncGenerationLocked();
    for (const auto& path : paths) {
        const auto it = g_entries.find(Key{path, maxSide});
        if (it != g_entries.end() && !it->second.image)
            return false; // still decoding on the pool
    }
    return true;
}

} // namespace decoded_assets
