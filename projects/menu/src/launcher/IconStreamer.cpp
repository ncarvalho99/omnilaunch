#include "IconStreamer.hpp"
#include "widgets/GlossyIcon.hpp"
#include "core/DebugLog.hpp"
#include <nxui/third_party/stb/stb_image.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>
#include <unordered_map>

namespace {

// Decoded-icon cache. SteamGridDB icons are 1024x1024 PNGs: decoding one and
// scaling it to 256x256 takes a few hundred milliseconds on the console, on a
// two-worker pool shared with everything else, so a page of them showed the
// loading spinner for seconds after every HOME return or Cards -> Classic
// switch (the menu process is new each time, nothing survives in memory).
// The scaled RGBA is kept next to the title cache, keyed by the compressed
// bytes it came from, so the next start reads 256 KiB instead of decoding.
// control_cache/ is excluded from the configuration backup.
constexpr const char* kDecodedDir = "sdmc:/config/OmniLaunch/control_cache/decoded";
constexpr std::uint32_t kDecodedMagic = 0x49445553; // "SUDI"
constexpr std::uint32_t kDecodedVersion = 1;
// Small sources (the 256x256 JPEGs from the title cache) decode quickly; only
// cache what is worth the card space.
constexpr std::size_t kCacheSourcesFrom = 96 * 1024;

struct DecodedHeader {
    std::uint32_t magic = kDecodedMagic;
    std::uint32_t version = kDecodedVersion;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t sourceSize = 0;
    std::uint64_t sourceHash = 0;
};
static_assert(sizeof(DecodedHeader) == 32);

std::uint64_t fnv1a(const std::vector<uint8_t>& data) {
    std::uint64_t h = 1469598103934665603ULL;
    for (uint8_t b : data) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string decodedPath(uint64_t titleId, bool artwork) {
    char name[64];
    std::snprintf(name, sizeof(name), "/%016llX.%s.rgba", static_cast<unsigned long long>(titleId),
                  artwork ? "art" : "icon");
    return std::string(kDecodedDir) + name;
}

bool loadDecoded(const std::string& path, const std::vector<uint8_t>& source, std::uint64_t hash,
                 IconStreamer::DecodedIcon& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    DecodedHeader h{};
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!in || h.magic != kDecodedMagic || h.version != kDecodedVersion || h.width == 0 || h.height == 0
        || h.width > 1024 || h.height > 1024 || h.sourceSize != source.size() || h.sourceHash != hash)
        return false;
    out.rgba.resize(static_cast<std::size_t>(h.width) * h.height * 4);
    in.read(reinterpret_cast<char*>(out.rgba.data()), static_cast<std::streamsize>(out.rgba.size()));
    if (!in) {
        out.rgba.clear();
        return false;
    }
    out.w = static_cast<int>(h.width);
    out.h = static_cast<int>(h.height);
    return true;
}

void saveDecoded(const std::string& path, const std::vector<uint8_t>& source, std::uint64_t hash,
                 const IconStreamer::DecodedIcon& icon) {
    std::error_code ec;
    std::filesystem::create_directories(kDecodedDir, ec);
    DecodedHeader h{};
    h.width = static_cast<std::uint32_t>(icon.w);
    h.height = static_cast<std::uint32_t>(icon.h);
    h.sourceSize = source.size();
    h.sourceHash = hash;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        out.write(reinterpret_cast<const char*>(icon.rgba.data()), static_cast<std::streamsize>(icon.rgba.size()));
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return;
        }
    }
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
    if (ec)
        std::filesystem::remove(tmp, ec);
}

// Visible-page snapshot (issue #4): the decoded icons of the last page that
// was fully shown, in one file, read once at start-up. Next to the decoded
// cache, so it is excluded from the configuration backup as well.
constexpr const char* kSnapshotPath = "sdmc:/config/OmniLaunch/control_cache/decoded/visible.snap";
constexpr std::uint32_t kSnapshotMagic = 0x4E535553; // "SUSN"
constexpr std::uint32_t kSnapshotVersion = 1;
constexpr std::uint32_t kSnapshotMaxFileEntries = 32;
constexpr std::uint64_t kArtworkKeySalt = 0x9E3779B97F4A7C15ULL;

struct SnapshotHeader {
    std::uint32_t magic = kSnapshotMagic;
    std::uint32_t version = kSnapshotVersion;
    std::uint32_t count = 0;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(SnapshotHeader) == 16);

struct SnapshotEntryHeader {
    std::uint64_t titleId = 0;
    std::uint64_t key = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t flags = 0; // bit 0: custom artwork
    std::uint32_t reserved = 0;
};
static_assert(sizeof(SnapshotEntryHeader) == 32);

std::uint64_t sourceKey(const std::vector<uint8_t>& compressed, bool customArtwork) {
    return fnv1a(compressed) ^ (customArtwork ? kArtworkKeySalt : 0ULL);
}

std::vector<IconStreamer::SnapshotEntry> readSnapshot() {
    std::vector<IconStreamer::SnapshotEntry> out;
    std::ifstream in(kSnapshotPath, std::ios::binary);
    if (!in)
        return out;
    SnapshotHeader h{};
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!in || h.magic != kSnapshotMagic || h.version != kSnapshotVersion || h.count > kSnapshotMaxFileEntries)
        return out;
    out.reserve(h.count);
    for (std::uint32_t i = 0; i < h.count; ++i) {
        SnapshotEntryHeader e{};
        in.read(reinterpret_cast<char*>(&e), sizeof(e));
        if (!in || e.titleId == 0 || e.width == 0 || e.height == 0 || e.width > 1024 || e.height > 1024)
            return {};
        auto icon = std::make_shared<IconStreamer::DecodedIcon>();
        icon->w = static_cast<int>(e.width);
        icon->h = static_cast<int>(e.height);
        icon->rgba.resize(static_cast<std::size_t>(e.width) * e.height * 4);
        in.read(reinterpret_cast<char*>(icon->rgba.data()), static_cast<std::streamsize>(icon->rgba.size()));
        if (!in)
            return {};
        IconStreamer::SnapshotEntry entry;
        entry.titleId = e.titleId;
        entry.key = e.key;
        entry.customArtwork = (e.flags & 1u) != 0;
        entry.icon = std::move(icon);
        out.push_back(std::move(entry));
    }
    return out;
}

void writeSnapshot(const std::vector<IconStreamer::SnapshotEntry>& entries) {
    // A catalogue refresh replaces the streamer; a write of the old one may
    // still be running on the other worker. One writer at a time.
    static std::mutex writeMutex;
    std::lock_guard<std::mutex> lock(writeMutex);
    std::error_code ec;
    std::filesystem::create_directories(kDecodedDir, ec);
    const std::string tmp = std::string(kSnapshotPath) + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        SnapshotHeader h{};
        h.count = static_cast<std::uint32_t>(entries.size());
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        for (const auto& entry : entries) {
            SnapshotEntryHeader e{};
            e.titleId = entry.titleId;
            e.key = entry.key;
            e.width = static_cast<std::uint32_t>(entry.icon->w);
            e.height = static_cast<std::uint32_t>(entry.icon->h);
            e.flags = entry.customArtwork ? 1u : 0u;
            out.write(reinterpret_cast<const char*>(&e), sizeof(e));
            out.write(reinterpret_cast<const char*>(entry.icon->rgba.data()),
                      static_cast<std::streamsize>(entry.icon->rgba.size()));
        }
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return;
        }
    }
    std::filesystem::remove(kSnapshotPath, ec);
    ec.clear();
    std::filesystem::rename(tmp, kSnapshotPath, ec);
    if (ec)
        std::filesystem::remove(tmp, ec);
}

} // namespace


void IconStreamer::init(int appCount) {
    clear();
    m_compressed.resize(appCount);
    m_titleIds.assign(appCount, 0);
    m_appToSlot.assign(appCount, -1);
    m_customArtwork.assign(appCount, false);
}

void IconStreamer::setIconDataLoader(IconDataLoader loader) {
    m_iconLoader = std::move(loader);
}

void IconStreamer::setArtworkDataLoader(IconDataLoader loader) {
    m_artworkLoader = std::move(loader);
}

void IconStreamer::setTitleId(int appIndex, uint64_t titleId) {
    if (appIndex >= 0 && appIndex < (int)m_titleIds.size())
        m_titleIds[appIndex] = titleId;
}

void IconStreamer::setIconData(int appIndex, std::vector<uint8_t> compressed) {
    if (appIndex >= 0 && appIndex < (int)m_compressed.size())
        m_compressed[appIndex] = std::move(compressed);
}

void IconStreamer::resize(int appCount) {
    if (appCount < 0)
        appCount = 0;

    for (auto& slot : m_pool) {
        if (slot && slot->appIndex >= appCount)
            slot->appIndex = -1;
    }

    m_compressed.resize(appCount);
    m_titleIds.resize(appCount, 0);
    m_appToSlot.resize(appCount, -1);
    m_customArtwork.resize(appCount, false);
    if (m_pinnedIndex >= appCount)
        m_pinnedIndex = -1;
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
}

void IconStreamer::setPinnedIndex(int appIndex) {
    m_pinnedIndex = (appIndex >= 0 && appIndex < (int)m_appToSlot.size()) ? appIndex : -1;
}

void IconStreamer::clearPinnedIndex() {
    m_pinnedIndex = -1;
}

void IconStreamer::clear() {
    cancelPending();
    // Snapshot data read from the SD (m_snapMap, the load in flight, what the
    // file holds) survives: init() runs this after beginSnapshotLoad(). What
    // describes textures in the pool goes with the pool.
    m_snapVerify.clear();
    m_shownKey.clear();
    m_snapSettling = false;
    m_pool.clear();
    m_compressed.clear();
    m_titleIds.clear();
    m_appToSlot.clear();
    m_customArtwork.clear();
    m_freeSlots.clear();
    m_pendingDecodes.clear();
    m_failedTitleIds.clear();
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
    m_pinnedIndex = -1;
}

void IconStreamer::cancelPending() {
    for (auto& pending : m_pendingDecodes) {
        if (pending.state)
            pending.state->cancelled.store(true);
    }
}

bool IconStreamer::swapIndices(int a, int b) {
    if (a < 0 || b < 0)
        return false;
    if (a == b)
        return true;

    const int maxIdx = std::max(a, b);
    if (maxIdx >= (int)m_appToSlot.size()) {
        m_appToSlot.resize(static_cast<std::size_t>(maxIdx + 1), -1);
    }
    if (maxIdx >= (int)m_compressed.size()) {
        m_compressed.resize(static_cast<std::size_t>(maxIdx + 1));
    }
    if (maxIdx >= (int)m_titleIds.size()) {
        m_titleIds.resize(static_cast<std::size_t>(maxIdx + 1), 0);
    }
    if (maxIdx >= (int)m_customArtwork.size()) {
        m_customArtwork.resize(static_cast<std::size_t>(maxIdx + 1), false);
    }

    std::swap(m_compressed[a], m_compressed[b]);
    std::swap(m_titleIds[a], m_titleIds[b]);
    const bool artworkA = m_customArtwork[a];
    m_customArtwork[a] = m_customArtwork[b];
    m_customArtwork[b] = artworkA;

    int slotA = m_appToSlot[a];
    int slotB = m_appToSlot[b];
    std::swap(m_appToSlot[a], m_appToSlot[b]);

    if (slotA >= 0 && slotA < (int)m_pool.size())
        m_pool[slotA]->appIndex = b;
    if (slotB >= 0 && slotB < (int)m_pool.size())
        m_pool[slotB]->appIndex = a;

    if (m_pinnedIndex == a)
        m_pinnedIndex = b;
    else if (m_pinnedIndex == b)
        m_pinnedIndex = a;

    m_lastPage = -1;
    m_lastIconsPerPage = -1;
    return true;
}

void IconStreamer::reconcileTitleIds(const std::vector<uint64_t>& titleIds) {
    const std::vector<uint64_t> oldTitleIds = m_titleIds;
    const int oldPinnedIndex = m_pinnedIndex;
    const uint64_t pinnedTitle = oldPinnedIndex >= 0 && oldPinnedIndex < (int)oldTitleIds.size()
        ? oldTitleIds[(size_t)oldPinnedIndex] : 0;

    std::unordered_map<uint64_t, int> newIndexByTitle;
    newIndexByTitle.reserve(titleIds.size());
    for (int i = 0; i < (int)titleIds.size(); ++i) {
        if (titleIds[(size_t)i] != 0)
            newIndexByTitle.emplace(titleIds[(size_t)i], i);
    }

    std::vector<int> newAppToSlot(titleIds.size(), -1);
    m_freeSlots.clear();
    for (int slotIndex = 0; slotIndex < (int)m_pool.size(); ++slotIndex) {
        auto& slot = m_pool[(size_t)slotIndex];
        if (!slot || slot->appIndex < 0 || slot->appIndex >= (int)oldTitleIds.size()) {
            if (slot)
                slot->appIndex = -1;
            m_freeSlots.push_back(slotIndex);
            continue;
        }

        const uint64_t titleId = oldTitleIds[(size_t)slot->appIndex];
        auto next = newIndexByTitle.find(titleId);
        if (titleId == 0 || next == newIndexByTitle.end() ||
            newAppToSlot[(size_t)next->second] >= 0) {
            slot->appIndex = -1;
            m_freeSlots.push_back(slotIndex);
            continue;
        }

        slot->appIndex = next->second;
        newAppToSlot[(size_t)next->second] = slotIndex;
    }

    std::vector<std::vector<uint8_t>> newCompressed(titleIds.size());
    std::vector<bool> newCustomArtwork(titleIds.size(), false);
    for (int oldIndex = 0; oldIndex < (int)oldTitleIds.size(); ++oldIndex) {
        auto next = newIndexByTitle.find(oldTitleIds[(size_t)oldIndex]);
        if (next != newIndexByTitle.end()) {
            if (oldIndex < (int)m_compressed.size())
                newCompressed[(size_t)next->second] = std::move(m_compressed[(size_t)oldIndex]);
            if (oldIndex < (int)m_customArtwork.size())
                newCustomArtwork[(size_t)next->second] = m_customArtwork[(size_t)oldIndex];
        }
    }

    m_titleIds = titleIds;
    m_compressed = std::move(newCompressed);
    m_appToSlot = std::move(newAppToSlot);
    m_customArtwork = std::move(newCustomArtwork);
    m_pinnedIndex = -1;
    if (pinnedTitle != 0) {
        auto next = newIndexByTitle.find(pinnedTitle);
        if (next != newIndexByTitle.end())
            m_pinnedIndex = next->second;
    }

    m_failedTitleIds.clear();
    for (auto it = m_snapVerify.begin(); it != m_snapVerify.end();) {
        if (!newIndexByTitle.count(it->first)) {
            m_shownKey.erase(it->first);
            it = m_snapVerify.erase(it);
        } else {
            ++it;
        }
    }
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
}

void IconStreamer::reconcileCatalog(IconStreamer&& catalog) {
    reconcileTitleIds(catalog.m_titleIds);
    const size_t count = std::min(m_compressed.size(), catalog.m_compressed.size());
    for (size_t i = 0; i < count; ++i) {
        if (m_compressed[i].empty() && !catalog.m_compressed[i].empty())
            m_compressed[i] = std::move(catalog.m_compressed[i]);
    }
}

bool IconStreamer::hasData(int index) const {
    if (index < 0 || index >= (int)m_appToSlot.size())
        return false;
    if (index < (int)m_compressed.size() && !m_compressed[index].empty())
        return true;
    return index < (int)m_titleIds.size() && m_titleIds[index] != 0 &&
           ((bool)m_artworkLoader || (bool)m_iconLoader);
}

bool IconStreamer::needsVisibleLoads(int currentPage, int iconsPerPage) const {
    if (iconsPerPage <= 0 || m_appToSlot.empty())
        return false;
    // The snapshot is still being read, checked or saved: keep pumping.
    if (m_snapLoadFuture.valid() || !m_snapVerify.empty() || m_snapSettling)
        return true;
    const int totalApps = static_cast<int>(m_appToSlot.size());
    const int totalPages = (totalApps + iconsPerPage - 1) / iconsPerPage;
    currentPage = std::clamp(currentPage, 0, std::max(0, totalPages - 1));
    int begin = currentPage * iconsPerPage;
    int end = std::min(totalApps, begin + iconsPerPage);
    // In single-row mode callers use one icon per logical page, so the radius
    // counts icons rather than pages and gets its own, much wider value.
    const int cacheRadius = iconsPerPage == 1 ? kLineCacheRadius : kPageCacheRadius;
    std::vector<int> window;
    if (iconsPerPage == 1) {
        begin = std::max(0, currentPage - cacheRadius);
        end = std::min(totalApps, currentPage + cacheRadius + 1);
        if (m_ringMode) {
            for (int step = -cacheRadius; step <= cacheRadius; ++step)
                window.push_back(((currentPage + step) % totalApps + totalApps) % totalApps);
        }
    }
    if (window.empty())
        for (int i = begin; i < end; ++i)
            window.push_back(i);
    int wantedMissing = 0;
    int noData = 0;
    bool needs = false;
    for (int i : window) {
        const uint64_t titleId = i < (int)m_titleIds.size() ? m_titleIds[(size_t)i] : 0;
        const bool failed = std::find(m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId)
            != m_failedTitleIds.end();
        const bool pending = std::any_of(
            m_pendingDecodes.begin(), m_pendingDecodes.end(),
            [titleId](const PendingDecode& decode) { return decode.titleId == titleId; });
        if (m_appToSlot[i] < 0 && !failed) {
            ++wantedMissing;
            if (pending || hasData(i)) needs = true;
            else ++noData;
        }
    }
    // Icons that stay on their spinner until the selection reaches them mean
    // this said "nothing to do" while they were still empty. The counts say
    // which: an entry with no slot and no data is one the streamer will never
    // schedule, whatever the window is.
    if (wantedMissing != m_lastWantedMissing || noData != m_lastNoData) {
        m_lastWantedMissing = wantedMissing;
        m_lastNoData = noData;
        DebugLog::log("[streamer] window=%d missing=%d nodata=%d pending=%d needs=%d",
                      (int)window.size(), wantedMissing, noData,
                      (int)m_pendingDecodes.size(), needs ? 1 : 0);
    }
    return needs;
}

// ---------------------------------------------------------------------------
// Decode a single compressed icon to RGBA, downscaling to kIconSize if needed.
// ---------------------------------------------------------------------------
IconStreamer::DecodedIcon IconStreamer::decodeIconData(const std::vector<uint8_t>& data) {
    DecodedIcon out{};
    if (data.empty()) return out;

    int w, h, ch;
    uint8_t* raw = stbi_load_from_memory(data.data(), (int)data.size(),
                                         &w, &h, &ch, 4);
    if (!raw) return out;
    std::unique_ptr<uint8_t, decltype(&stbi_image_free)> full(raw, &stbi_image_free);

    if (w != kIconSize || h != kIconSize) {
        int dstW = kIconSize, dstH = kIconSize;
        std::vector<uint8_t> scaled((size_t)dstW * dstH * 4);
        if (!scaled.empty()) {
            float scaleX = (float)w / dstW;
            float scaleY = (float)h / dstH;
            for (int y = 0; y < dstH; ++y) {
                float srcYf = (y + 0.5f) * scaleY - 0.5f;
                int y0 = (int)srcYf; if (y0 < 0) y0 = 0;
                int y1 = y0 + 1;     if (y1 >= h) y1 = h - 1;
                float fy = srcYf - y0;
                for (int x = 0; x < dstW; ++x) {
                    float srcXf = (x + 0.5f) * scaleX - 0.5f;
                    int x0 = (int)srcXf; if (x0 < 0) x0 = 0;
                    int x1 = x0 + 1;     if (x1 >= w) x1 = w - 1;
                    float fx = srcXf - x0;
                    const uint8_t* p00 = full.get() + ((size_t)y0 * w + x0) * 4;
                    const uint8_t* p10 = full.get() + ((size_t)y0 * w + x1) * 4;
                    const uint8_t* p01 = full.get() + ((size_t)y1 * w + x0) * 4;
                    const uint8_t* p11 = full.get() + ((size_t)y1 * w + x1) * 4;
                    uint8_t* dst = scaled.data() + ((size_t)y * dstW + x) * 4;
                    for (int c = 0; c < 4; ++c) {
                        dst[c] = (uint8_t)(
                            p00[c] * (1 - fx) * (1 - fy) +
                            p10[c] * fx       * (1 - fy) +
                            p01[c] * (1 - fx) * fy       +
                            p11[c] * fx       * fy       + 0.5f);
                    }
                }
            }
            out.rgba = std::move(scaled);
            out.w = dstW;
            out.h = dstH;
        }
    } else {
        out.rgba.assign(full.get(), full.get() + (size_t)w * h * 4);
        out.w = w;
        out.h = h;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Core streaming logic.
// ---------------------------------------------------------------------------
void IconStreamer::startBurst(float seconds) {
    m_burstUntil = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<int>(seconds * 1000.f));
}

void IconStreamer::beginSnapshotLoad() {
    if (!m_threadPool || m_snapLoadFuture.valid())
        return;
    auto result = std::make_shared<std::vector<SnapshotEntry>>();
    m_snapLoadResult = result;
    // Normal decodes wait for it at most this long; it is one sequential read
    // of a few MiB queued ahead of the catalogue load.
    m_snapLoadDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
    try {
        m_snapLoadFuture = m_threadPool->submit([result]() {
            const auto start = std::chrono::steady_clock::now();
            *result = readSnapshot();
            DebugLog::log("[streamer] snapshot read: %zu icon(s) in %lldms", result->size(),
                          static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start).count()));
        });
    } catch (...) {
        m_snapLoadResult.reset();
    }
}

bool IconStreamer::snapshotLoadInFlight() const {
    return m_snapLoadFuture.valid()
        && m_snapLoadFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready
        && std::chrono::steady_clock::now() < m_snapLoadDeadline;
}

void IconStreamer::consumeSnapshotLoad() {
    if (!m_snapLoadFuture.valid()
        || m_snapLoadFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try {
        m_snapLoadFuture.get();
    } catch (...) {
    }
    if (m_snapLoadResult) {
        m_snapStored.clear();
        for (auto& entry : *m_snapLoadResult) {
            m_snapStored.emplace_back(entry.titleId, entry.key);
            m_snapMap[entry.titleId] = std::move(entry);
        }
    }
    m_snapLoadResult.reset();
}

void IconStreamer::forgetSnapshotTitle(uint64_t titleId) {
    m_snapVerify.erase(titleId);
    m_shownKey.erase(titleId);
}

void IconStreamer::rememberDecoded(uint64_t titleId, uint64_t key, bool customArtwork,
                                   std::shared_ptr<const DecodedIcon> icon) {
    if (titleId == 0 || !icon || icon->rgba.empty())
        return;
    SnapshotEntry entry;
    entry.titleId = titleId;
    entry.key = key;
    entry.customArtwork = customArtwork;
    entry.icon = std::move(icon);
    m_snapCollect[titleId] = std::move(entry);
    m_snapCollectOrder.erase(std::remove(m_snapCollectOrder.begin(), m_snapCollectOrder.end(), titleId),
                             m_snapCollectOrder.end());
    m_snapCollectOrder.push_back(titleId);
    // Bounded: drop the oldest pixels that are not part of the page last seen.
    for (std::size_t i = 0; m_snapCollect.size() > kSnapshotCollectMax && i < m_snapCollectOrder.size();) {
        const uint64_t old = m_snapCollectOrder[i];
        if (std::find(m_snapWindowIds.begin(), m_snapWindowIds.end(), old) != m_snapWindowIds.end()) {
            ++i;
            continue;
        }
        m_snapCollect.erase(old);
        m_snapCollectOrder.erase(m_snapCollectOrder.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

bool IconStreamer::uploadIcon(int appIndex, const DecodedIcon& icon, bool customArtwork,
                              nxui::GpuDevice& gpu, nxui::Renderer& ren,
                              const std::vector<std::shared_ptr<GlossyIcon>>& allIcons) {
    int poolIdx = appIndex >= 0 && appIndex < (int)m_appToSlot.size() ? m_appToSlot[(size_t)appIndex] : -1;
    const bool reuse = poolIdx >= 0 && poolIdx < (int)m_pool.size() && m_pool[(size_t)poolIdx];
    if (!reuse) {
        if (!m_freeSlots.empty()) {
            poolIdx = m_freeSlots.back();
            m_freeSlots.pop_back();
        } else {
            poolIdx = static_cast<int>(m_pool.size());
            m_pool.emplace_back(std::make_unique<TexSlot>());
        }
    }

    auto& slot = *m_pool[(size_t)poolIdx];
    // A recycled pool texture keeps the same descriptor and address.
    // Detach every stale consumer before overwriting it, including icons
    // that moved to another index during a reflow.
    for (const auto& w : allIcons) {
        if (w && w->texture() == &slot.texture)
            w->setTexture(nullptr);
    }
    if (!slot.texture.loadFromPixels(gpu, ren, icon.rgba.data(), icon.w, icon.h)) {
        slot.appIndex = -1;
        m_appToSlot[(size_t)appIndex] = -1;
        m_freeSlots.push_back(poolIdx);
        return false;
    }
    slot.appIndex = appIndex;
    m_appToSlot[(size_t)appIndex] = poolIdx;
    if (appIndex < (int)allIcons.size())
        allIcons[(size_t)appIndex]->setTexture(&slot.texture);
    if (appIndex < (int)m_customArtwork.size())
        m_customArtwork[(size_t)appIndex] = customArtwork;
    if (appIndex < (int)allIcons.size())
        allIcons[(size_t)appIndex]->setCustomArtwork(customArtwork);
    return true;
}

// Saves the page once every icon on it is on screen from a fresh (or
// verified) decode and the page has stayed the same for a moment, and only
// when it differs from what the file already holds.
void IconStreamer::maybeWriteSnapshot(const std::vector<int>& window) {
    std::vector<uint64_t> ids;
    ids.reserve(window.size());
    for (int appIndex : window) {
        if (appIndex < 0 || appIndex >= (int)m_titleIds.size())
            continue;
        const uint64_t titleId = m_titleIds[(size_t)appIndex];
        if (titleId == 0 || !hasData(appIndex))
            continue;
        if (std::find(m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId) != m_failedTitleIds.end())
            continue;
        if (std::find(ids.begin(), ids.end(), titleId) != ids.end())
            continue;
        ids.push_back(titleId);
        if (ids.size() >= kSnapshotMaxEntries)
            break;
    }
    m_snapWindowIds = ids;

    SnapshotList list;
    list.reserve(ids.size());
    for (uint64_t titleId : ids) {
        const auto shown = m_shownKey.find(titleId);
        const auto it = std::find(m_titleIds.begin(), m_titleIds.end(), titleId);
        const int appIndex = it == m_titleIds.end() ? -1 : static_cast<int>(it - m_titleIds.begin());
        if (shown == m_shownKey.end() || appIndex < 0 || m_appToSlot[(size_t)appIndex] < 0
            || m_snapVerify.count(titleId) || !m_snapCollect.count(titleId)) {
            m_snapSettling = false; // page not complete yet
            return;
        }
        list.emplace_back(titleId, shown->second);
    }
    // Not while the file is still being read, and never the same page twice.
    if (list.empty() || list == m_snapStored || m_snapLoadFuture.valid()) {
        m_snapSettling = false;
        return;
    }

    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& [titleId, key] : list) {
        hash = (hash ^ titleId) * 1099511628211ULL;
        hash = (hash ^ key) * 1099511628211ULL;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!m_snapSettling || hash != m_snapPendingHash) {
        m_snapPendingHash = hash;
        m_snapSettleSince = now;
        m_snapSettling = true;
        return;
    }
    if (now - m_snapSettleSince < std::chrono::milliseconds(1500))
        return;
    if (m_snapWriteFuture.valid()
        && m_snapWriteFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    if (!m_threadPool) {
        m_snapSettling = false;
        return;
    }

    std::vector<SnapshotEntry> entries;
    entries.reserve(list.size());
    for (const auto& item : list)
        entries.push_back(m_snapCollect[item.first]);
    try {
        m_snapWriteFuture = m_threadPool->submit([entries = std::move(entries)]() {
            writeSnapshot(entries);
        });
        DebugLog::log("[streamer] snapshot saved: %zu icon(s)", list.size());
        m_snapStored = std::move(list);
    } catch (...) {
    }
    m_snapSettling = false;
}

void IconStreamer::onPageChanged(int currentPage, int iconsPerPage,
                                 nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                 const std::vector<std::shared_ptr<GlossyIcon>>& allIcons)
{
    consumeSnapshotLoad();
    int totalApps  = (int)m_appToSlot.size();
    if (totalApps == 0 || iconsPerPage <= 0) {
        m_lastPage = currentPage;
        m_lastIconsPerPage = iconsPerPage;
        return;
    }

    int totalPages = (totalApps + iconsPerPage - 1) / iconsPerPage;
    currentPage = std::clamp(currentPage, 0, totalPages - 1);

    m_lastPage = currentPage;
    m_lastIconsPerPage = iconsPerPage;

    int visibleStartApp = currentPage * iconsPerPage;
    int visibleEndApp   = std::min(totalApps, visibleStartApp + iconsPerPage);
    // Icons, not pages, once the line puts one icon on each page.
    const int cacheRadius = iconsPerPage == 1 ? kLineCacheRadius : kPageCacheRadius;
    int cacheStartPage  = std::max(0, currentPage - cacheRadius);
    int cacheEndPage    = std::min(totalPages - 1, currentPage + cacheRadius);
    int cacheStartApp   = cacheStartPage * iconsPerPage;
    int cacheEndApp     = std::min(totalApps, (cacheEndPage + 1) * iconsPerPage);

    // In the line the window wraps, so membership is a ring distance rather than
    // a range. Eviction, completion and scheduling all ask through here so they
    // cannot disagree about which icons are wanted.
    const bool ringWindow = m_ringMode && iconsPerPage == 1 && totalApps > 0;
    const auto wanted = [&](int appIndex) {
        if (appIndex < 0 || appIndex >= totalApps)
            return false;
        if (!ringWindow)
            return appIndex >= cacheStartApp && appIndex < cacheEndApp;
        const int raw = std::abs(appIndex - currentPage);
        return std::min(raw, totalApps - raw) <= cacheRadius;
    };

    // 1. Evict textures outside the local page window. This keeps GPU memory
    //    bounded while preserving quick navigation to nearby pages.
    for (int i = 0; i < (int)m_pool.size(); ++i) {
        if (!m_pool[i])
            continue;

        int app = m_pool[i]->appIndex;
        if (app < 0 || app == m_pinnedIndex)
            continue;

        if (!wanted(app)) {
            if (app < (int)m_titleIds.size())
                forgetSnapshotTitle(m_titleIds[(size_t)app]);
            if (app < (int)allIcons.size())
                allIcons[app]->setTexture(nullptr);
            if (app < (int)allIcons.size())
                allIcons[app]->setCustomArtwork(false);
            if (app < (int)m_appToSlot.size())
                m_appToSlot[app] = -1;
            m_pool[i]->appIndex = -1;
            m_freeSlots.push_back(i);
        }
    }

    // 2. Re-attach already-loaded slots to the current widget order. This is
    //    needed after grid relayouts and swaps where the GlossyIcon objects
    //    may have moved while the GPU texture pool stayed valid.
    //
    //    Every wanted index, not just the visible one. The dynamic line puts a
    //    single icon on a logical page, so this ran for exactly one of them:
    //    switching into the line rebuilds the grid, the pool and m_appToSlot
    //    survive that untouched, so needsVisibleLoads() correctly reports
    //    nothing to fetch -- and every icon except the focused one was left
    //    holding the null texture its fresh widget was born with. That is the
    //    spinner that clears only when the selection lands on it. Attaching a
    //    texture the pool already holds costs a pointer assignment.
    for (int i = 0; i < totalApps; ++i) {
        int slotIdx = m_appToSlot[i];
        if (slotIdx < 0)
            continue;

        bool validSlot = slotIdx < (int)m_pool.size() &&
                         m_pool[slotIdx] &&
                         m_pool[slotIdx]->appIndex == i &&
                         m_pool[slotIdx]->texture.valid();
        if (validSlot) {
            if (i < (int)allIcons.size())
                allIcons[i]->setTexture(&m_pool[slotIdx]->texture);
            if (i < (int)allIcons.size())
                allIcons[i]->setCustomArtwork(
                    i < (int)m_customArtwork.size() && m_customArtwork[i]);
        } else {
            m_appToSlot[i] = -1;
            if (i < (int)allIcons.size())
                allIcons[i]->setTexture(nullptr);
            if (i < (int)allIcons.size())
                allIcons[i]->setCustomArtwork(false);
        }
    }

    // 2b. Icons of the last session's visible page, from the snapshot file:
    //     already decoded, so they cost an upload each. Shown at once; the
    //     normal decode below still runs and replaces any that changed.
    if (!m_snapMap.empty()) {
        int snapUploads = 0;
        for (auto snap = m_snapMap.begin();
             snap != m_snapMap.end() && snapUploads < kSnapshotUploadsPerFrame;) {
            const uint64_t titleId = snap->first;
            auto it = std::find(m_titleIds.begin(), m_titleIds.end(), titleId);
            const int appIndex = it == m_titleIds.end() ? -1 : static_cast<int>(it - m_titleIds.begin());
            if (appIndex < 0 || !wanted(appIndex) || m_appToSlot[(size_t)appIndex] >= 0) {
                ++snap; // not on this page (yet): kept until the page settles
                continue;
            }
            SnapshotEntry entry = std::move(snap->second);
            snap = m_snapMap.erase(snap);
            if (!entry.icon || !uploadIcon(appIndex, *entry.icon, entry.customArtwork, gpu, ren, allIcons))
                continue;
            m_shownKey[titleId] = entry.key;
            m_snapVerify[titleId] = std::move(entry);
            ++snapUploads;
        }
        // Whatever the file had for this window is on screen: the rest is a
        // page not shown now and is not worth keeping in memory.
        if (snapUploads < kSnapshotUploadsPerFrame)
            m_snapMap.clear();
    }

    // 3. Consume completed background decodes without ever waiting for them.
    // GPU uploads remain on this thread, but are capped to one per frame so a
    // page cannot monopolise rendering or starve audio updates. Keep the
    // established synchronous upload handoff here: presenting a frame while a
    // newly registered descriptor was still being filled produced a black
    // screen on hardware even though the menu process stayed alive.
    int uploads = 0;
    for (size_t pendingIndex = 0;
         pendingIndex < m_pendingDecodes.size() &&
         uploads < (inBurst() ? kBurstUploadsPerFrame : kUploadsPerFrame);) {
        auto& pending = m_pendingDecodes[pendingIndex];
        if (pending.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++pendingIndex;
            continue;
        }

        try {
            pending.future.get();
        } catch (...) {
            pending.state->failed = true;
        }

        const uint64_t titleId = pending.titleId;
        auto titleIt = std::find(m_titleIds.begin(), m_titleIds.end(), titleId);
        const int appIndex = titleIt == m_titleIds.end()
            ? -1 : static_cast<int>(titleIt - m_titleIds.begin());
        const bool stillWanted = wanted(appIndex);
        auto state = std::move(pending.state);
        m_pendingDecodes[pendingIndex] = std::move(m_pendingDecodes.back());
        m_pendingDecodes.pop_back();

        if (state->cancelled.load())
            continue;

        // The check of an icon that came from the snapshot.
        auto verify = m_snapVerify.find(titleId);
        if (verify != m_snapVerify.end()) {
            SnapshotEntry shown = std::move(verify->second);
            m_snapVerify.erase(verify);
            // A decode scheduled before the snapshot arrived still carries the
            // source key, so the comparison holds for it too.
            if (state->sameAsShown || (state->key != 0 && state->key == shown.key)) {
                // Unchanged: the snapshot pixels are this icon's pixels.
                rememberDecoded(titleId, shown.key, shown.customArtwork, shown.icon);
                continue;
            }
            if (state->failed || state->decoded.rgba.empty()) {
                // The source is gone: do not keep showing the old picture.
                if (stillWanted && m_appToSlot[(size_t)appIndex] >= 0) {
                    const int slotIdx = m_appToSlot[(size_t)appIndex];
                    if (appIndex < (int)allIcons.size())
                        allIcons[(size_t)appIndex]->setTexture(nullptr);
                    m_pool[(size_t)slotIdx]->appIndex = -1;
                    m_appToSlot[(size_t)appIndex] = -1;
                    m_freeSlots.push_back(slotIdx);
                }
                m_shownKey.erase(titleId);
                m_failedTitleIds.push_back(titleId);
                continue;
            }
            if (!stillWanted || m_appToSlot[(size_t)appIndex] < 0)
                continue;
            // Changed since the snapshot (new cover, new icon): replace it in
            // its own slot.
            auto fresh = std::make_shared<DecodedIcon>(std::move(state->decoded));
            if (uploadIcon(appIndex, *fresh, state->customArtwork, gpu, ren, allIcons)) {
                m_shownKey[titleId] = state->key;
                rememberDecoded(titleId, state->key, state->customArtwork, std::move(fresh));
            }
            ++uploads;
            continue;
        }

        if (state->failed || state->decoded.rgba.empty()) {
            m_failedTitleIds.push_back(titleId);
            DebugLog::log("[streamer] decode failed title=0x%016lX",
                          static_cast<unsigned long>(titleId));
            continue;
        }
        if (!stillWanted || m_appToSlot[(size_t)appIndex] >= 0)
            continue;

        auto fresh = std::make_shared<DecodedIcon>(std::move(state->decoded));
        if (uploadIcon(appIndex, *fresh, state->customArtwork, gpu, ren, allIcons)) {
            m_shownKey[titleId] = state->key;
            rememberDecoded(titleId, state->key, state->customArtwork, std::move(fresh));
        } else {
            m_failedTitleIds.push_back(titleId);
            DebugLog::log("[streamer] upload failed title=0x%016lX app=%d",
                          static_cast<unsigned long>(titleId), appIndex);
        }
        ++uploads;
    }

    // 4. Fill the bounded decode queue for visible icons. In single-row mode,
    // include the nearest neighbours in cursor-distance order. Both the SD read and
    // image conversion happen on a worker; this call returns immediately.
    std::vector<int> loadOrder;
    if (iconsPerPage == 1) {
        loadOrder.push_back(visibleStartApp);
        for (int distance = 1; distance <= cacheRadius; ++distance) {
            if (ringWindow) {
                loadOrder.push_back(((visibleStartApp + distance) % totalApps
                                     + totalApps) % totalApps);
                loadOrder.push_back(((visibleStartApp - distance) % totalApps
                                     + totalApps) % totalApps);
                continue;
            }
            if (visibleStartApp + distance < cacheEndApp)
                loadOrder.push_back(visibleStartApp + distance);
            if (visibleStartApp - distance >= cacheStartApp)
                loadOrder.push_back(visibleStartApp - distance);
        }
    } else {
        for (int appIndex = visibleStartApp; appIndex < visibleEndApp; ++appIndex)
            loadOrder.push_back(appIndex);
    }

    // Snapshot bookkeeping: the window it would save is the visible page (in
    // the line, the focused icon and the ones drawn beside it).
    {
        std::vector<int> snapWindow;
        if (iconsPerPage == 1) {
            for (std::size_t k = 0; k < loadOrder.size() && k < 9; ++k)
                snapWindow.push_back(loadOrder[k]);
        } else {
            snapWindow.assign(loadOrder.begin(), loadOrder.end());
        }
        maybeWriteSnapshot(snapWindow);
    }

    // Icons shown from the snapshot whose page was left before their check
    // ran: still check them while they stay in the cache window.
    for (const auto& item : m_snapVerify) {
        auto it = std::find(m_titleIds.begin(), m_titleIds.end(), item.first);
        if (it == m_titleIds.end())
            continue;
        const int appIndex = static_cast<int>(it - m_titleIds.begin());
        if (wanted(appIndex) && std::find(loadOrder.begin(), loadOrder.end(), appIndex) == loadOrder.end())
            loadOrder.push_back(appIndex);
    }

    // The snapshot is read in parallel with the first frames. Decoding the
    // same icons meanwhile would only compete with it for the SD card and the
    // two workers; wait for it a short, bounded moment.
    if (snapshotLoadInFlight())
        return;

    for (int appIndex : loadOrder) {
        if ((int)m_pendingDecodes.size() >= (inBurst() ? kBurstMaxPendingDecodes : kMaxPendingDecodes))
            break;
        const uint64_t loadTitle = appIndex < (int)m_titleIds.size() ? m_titleIds[(size_t)appIndex] : 0;
        auto verifyIt = m_snapVerify.find(loadTitle);
        const bool verifying = verifyIt != m_snapVerify.end();
        if ((m_appToSlot[(size_t)appIndex] >= 0 && !verifying) || !hasData(appIndex))
            continue;

        const uint64_t titleId = m_titleIds[(size_t)appIndex];
        const bool failed = std::find(m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId)
            != m_failedTitleIds.end();
        const bool pending = std::any_of(
            m_pendingDecodes.begin(), m_pendingDecodes.end(),
            [titleId](const PendingDecode& decode) { return decode.titleId == titleId; });
        if (verifying && (titleId == 0 || failed)) {
            m_snapVerify.erase(titleId);
            continue;
        }
        if (titleId == 0 || failed || pending)
            continue;

        std::vector<uint8_t> compressed;
        if (appIndex < (int)m_compressed.size())
            compressed = std::move(m_compressed[(size_t)appIndex]);

        auto state = std::make_shared<DecodeState>();
        state->expectKey = verifying ? verifyIt->second.key : 0;
        IconDataLoader loader = m_iconLoader;
        IconDataLoader artworkLoader = m_artworkLoader;
        auto work = [state, titleId, loader, artworkLoader,
                     compressed = std::move(compressed)]() mutable {
            if (state->cancelled.load())
                return;
            if (artworkLoader)
                compressed = artworkLoader(titleId);
            state->customArtwork = !compressed.empty();
            if (compressed.empty() && loader)
                compressed = loader(titleId);
            if (state->cancelled.load())
                return;
            if (compressed.empty()) {
                state->failed = true;
                return;
            }
            state->key = sourceKey(compressed, state->customArtwork);
            if (state->expectKey != 0 && state->key == state->expectKey) {
                state->sameAsShown = true; // the snapshot already shows this icon
                return;
            }
            if (compressed.size() >= kCacheSourcesFrom) {
                const std::uint64_t hash = fnv1a(compressed);
                const std::string cachePath = decodedPath(titleId, state->customArtwork);
                if (loadDecoded(cachePath, compressed, hash, state->decoded))
                    return;
                if (state->cancelled.load())
                    return;
                state->decoded = IconStreamer::decodeIconData(compressed);
                state->failed = state->decoded.rgba.empty();
                if (!state->failed)
                    saveDecoded(cachePath, compressed, hash, state->decoded);
                return;
            }
            state->decoded = IconStreamer::decodeIconData(compressed);
            state->failed = state->decoded.rgba.empty();
        };

        PendingDecode decode;
        decode.titleId = titleId;
        decode.state = state;
        if (m_threadPool) {
            decode.future = m_threadPool->submit(std::move(work));
        } else {
            std::promise<void> completed;
            decode.future = completed.get_future();
            try {
                work();
                completed.set_value();
            } catch (...) {
                completed.set_exception(std::current_exception());
            }
        }
        m_pendingDecodes.push_back(std::move(decode));
        DebugLog::log("[streamer] scheduled title=0x%016lX app=%d pending=%d",
                      static_cast<unsigned long>(titleId), appIndex,
                      static_cast<int>(m_pendingDecodes.size()));
    }
}

void IconStreamer::forceReload(int currentPage, int iconsPerPage,
                                nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                const std::vector<std::shared_ptr<GlossyIcon>>& allIcons)
{
    // Widgets keep non-owning pointers into m_pool. Detach every pointer before
    // destroying the pool; otherwise the next frame can submit a descriptor
    // backed by a freed MemBlock and put deko3d into a permanent page-fault
    // state. This is especially visible when an overridden icon is applied
    // while edit/move mode still owns a ghost of the focused application.
    for (const auto& icon : allIcons) {
        if (icon)
            icon->setTexture(nullptr);
    }

    // Throw away all loaded state so onPageChanged re-does everything.
    for (auto& slot : m_pool) slot->appIndex = -1;
    m_freeSlots.clear();
    for (int i = 0; i < (int)m_pool.size(); ++i) m_freeSlots.push_back(i);
    std::fill(m_appToSlot.begin(), m_appToSlot.end(), -1);

    // Clear the Texture objects themselves (GPU memory + descriptor slots)
    // because a forceReload typically follows a full GPU reset.
    m_pool.clear();
    m_freeSlots.clear();
    cancelPending();
    m_pendingDecodes.clear();
    m_failedTitleIds.clear();
    m_snapVerify.clear();
    m_shownKey.clear();
    m_snapMap.clear();

    m_lastPage = -1;
    m_lastIconsPerPage = -1;
    onPageChanged(currentPage, iconsPerPage, gpu, ren, allIcons);
}

void IconStreamer::reloadTitle(
    uint64_t titleId, int currentPage, int iconsPerPage,
    nxui::GpuDevice& gpu, nxui::Renderer& ren,
    const std::vector<std::shared_ptr<GlossyIcon>>& allIcons) {
    const auto found = std::find(m_titleIds.begin(), m_titleIds.end(), titleId);
    if (found == m_titleIds.end()) return;
    const int appIndex = static_cast<int>(found - m_titleIds.begin());

    for (auto& pending : m_pendingDecodes) {
        if (pending.titleId == titleId && pending.state)
            pending.state->cancelled.store(true);
    }
    m_failedTitleIds.erase(
        std::remove(m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId),
        m_failedTitleIds.end());
    forgetSnapshotTitle(titleId);
    m_snapMap.erase(titleId);
    m_snapCollect.erase(titleId);
    if (appIndex < static_cast<int>(m_compressed.size()))
        m_compressed[(size_t)appIndex].clear();

    const int slotIndex = appIndex < static_cast<int>(m_appToSlot.size())
        ? m_appToSlot[(size_t)appIndex] : -1;
    if (appIndex < static_cast<int>(allIcons.size()) && allIcons[(size_t)appIndex])
        allIcons[(size_t)appIndex]->setTexture(nullptr);
    if (slotIndex >= 0 && slotIndex < static_cast<int>(m_pool.size()) && m_pool[(size_t)slotIndex]) {
        m_pool[(size_t)slotIndex]->appIndex = -1;
        m_appToSlot[(size_t)appIndex] = -1;
        if (std::find(m_freeSlots.begin(), m_freeSlots.end(), slotIndex) == m_freeSlots.end())
            m_freeSlots.push_back(slotIndex);
    }

    m_lastPage = -1;
    m_lastIconsPerPage = -1;
    onPageChanged(currentPage, iconsPerPage, gpu, ren, allIcons);
}
