#pragma once
#include <chrono>
#include <nxui/core/Texture.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include <future>
#include <atomic>

class GlossyIcon;

// Streams icon textures on demand based on the currently visible page.
// Loaded textures are kept for a small window around the current page, so
// nearby navigation is instant without letting GPU memory grow unbounded.
class IconStreamer {
public:
    using IconDataLoader = std::function<std::vector<uint8_t>(uint64_t titleId)>;

    struct DecodedIcon {
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
    };

    // CPU-only helper. Safe on a ThreadPool worker; upload stays on the UI thread.
    static DecodedIcon decodeIconData(const std::vector<uint8_t>& data);

    // Prepare icon metadata for all apps after the app list has been fetched.
    void init(int appCount);
    void setIconDataLoader(IconDataLoader loader);
    void setArtworkDataLoader(IconDataLoader loader);
    void setThreadPool(nxui::ThreadPool* pool) { m_threadPool = pool; }
    void setTitleId(int appIndex, uint64_t titleId);
    void setIconData(int appIndex, std::vector<uint8_t> compressed);
    void resize(int appCount);
    // The dynamic line is a ring: the icons drawn either side of the first one
    // are the last ones. Without this the cache window stays linear and clamps
    // at both ends, so those wrapped neighbours are never requested and sit on
    // their loading spinner.
    void setRingMode(bool ring) { m_ringMode = ring; }
    void setPinnedIndex(int appIndex);
    void clearPinnedIndex();

    // Call when the visible page changes (or on first display).
    // Decodes + uploads textures for the new visible range and evicts
    // textures that are no longer needed.  Updates GlossyIcon texture
    // pointers directly.
    void onPageChanged(int currentPage, int iconsPerPage,
                       nxui::GpuDevice& gpu, nxui::Renderer& ren,
                       const std::vector<std::shared_ptr<GlossyIcon>>& allIcons);

    // Force-reload textures for the current page (e.g. after a theme change
    // that resets the GPU pool).
    void forceReload(int currentPage, int iconsPerPage,
                     nxui::GpuDevice& gpu, nxui::Renderer& ren,
                     const std::vector<std::shared_ptr<GlossyIcon>>& allIcons);

    // Reload one title in place (for an icon override) without destroying the
    // complete GPU pool or stalling on unrelated textures.
    void reloadTitle(uint64_t titleId, int currentPage, int iconsPerPage,
                     nxui::GpuDevice& gpu, nxui::Renderer& ren,
                     const std::vector<std::shared_ptr<GlossyIcon>>& allIcons);

    // Temporarily allow more decodes and uploads per frame, e.g. right after a
    // fast HOME return so the visible page fills in at once instead of an icon
    // or two per frame. Reverts to the normal limits by itself.
    void startBurst(float seconds);

    // Release everything (textures + compressed data).
    void clear();
    void cancelPending();

    // Keep internal compressed data and loaded-slot mappings aligned when
    // app entries are swapped in the grid model.
    bool swapIndices(int a, int b);

    // Update indices after a catalogue refresh while retaining textures for
    // titles that still exist. This avoids tearing down the complete GPU icon
    // cache just because one application was installed or removed.
    void reconcileTitleIds(const std::vector<uint64_t>& titleIds);
    void reconcileCatalog(IconStreamer&& catalog);

    int  iconCount()         const { return (int)m_appToSlot.size(); }
    bool hasData(int index)  const;
    bool needsVisibleLoads(int currentPage, int iconsPerPage) const;

private:
    struct DecodeState {
        DecodedIcon decoded;
        bool failed = false;
        bool customArtwork = false;
        std::atomic<bool> cancelled{false};
    };

    struct PendingDecode {
        uint64_t titleId = 0;
        std::shared_ptr<DecodeState> state;
        std::future<void> future;
    };

    // Transient compressed JPEG/PNG bytes, normally only used for prefetched
    // data. App icons are otherwise fetched on demand and released after upload.
    std::vector<std::vector<uint8_t>> m_compressed;
    std::vector<uint64_t> m_titleIds;
    IconDataLoader m_iconLoader;
    IconDataLoader m_artworkLoader;
    std::vector<bool> m_customArtwork;
    nxui::ThreadPool* m_threadPool = nullptr;
    std::vector<PendingDecode> m_pendingDecodes;
    std::vector<uint64_t> m_failedTitleIds;

    // Pool of reusable GPU textures.
    struct TexSlot {
        nxui::Texture texture;
        int appIndex = -1;   // which app currently occupies this slot (-1 = free)
    };
    std::vector<std::unique_ptr<TexSlot>> m_pool;

    // Maps app index → pool slot index (-1 = not loaded).
    std::vector<int> m_appToSlot;

    // Indices of free pool slots.
    std::vector<int> m_freeSlots;

    int m_lastPage = -1;
    int m_lastIconsPerPage = -1;
    int m_pinnedIndex = -1;
    bool m_ringMode = false;

    static constexpr int kPageCacheRadius = 4;
    // The line view puts one icon on a logical page, so the radius above counts
    // icons there and kept only nine hot. Every fifth step evicted an icon and
    // reloaded it a moment later, which is the loading spinner that flashes
    // while scrolling the row. Thirty-three 160px textures is a few megabytes
    // and covers a small library outright.
    static constexpr int kLineCacheRadius = 16;
    // Last reported window state, so the diagnostic only speaks when it changes.
    mutable int m_lastWantedMissing = -1;
    mutable int m_lastNoData = -1;
    // Native Switch game icons from control data are 256x256. Keep full 256px
    // resolution for crisp rendering on high-DPI displays and enlarged tiles.
    static constexpr int kIconSize        = 256;
    // Two uploads and six decodes in flight. A mode switch evicts the whole
    // ring -- the grid model is a different size, so every icon leaves the
    // window and comes back -- and refilling sixteen icons one upload per frame
    // behind four decodes is the row of spinners seen while moving straight
    // after a switch. Uploads are synchronous on the render thread, so this
    // stays small: two is a second icon per frame, not a page of them.
    static constexpr int kUploadsPerFrame = 2;
    static constexpr int kMaxPendingDecodes = 6;
    static constexpr int kBurstUploadsPerFrame = 4;
    static constexpr int kBurstMaxPendingDecodes = 8;
    std::chrono::steady_clock::time_point m_burstUntil{};
    bool inBurst() const { return std::chrono::steady_clock::now() < m_burstUntil; }
};
