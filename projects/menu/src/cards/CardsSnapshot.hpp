#pragma once
// Per-page snapshot of the Cards view (issue #4, Cards side). Every frontend
// switch or HOME return starts a new menu process, so the first frames of a
// page drew each card as its square icon on glass until its cover was decoded
// again, and only then took the cover. The decoded icons and (reduced) covers
// of the cards last fully shown on a page are kept in one file per page, read
// on a worker when that page opens, and uploaded in the first frames. The
// normal decodes still run and replace them, so a changed or removed source
// never stays on screen; a file is rewritten only when its content changed.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace nxui { class ThreadPool; }

namespace cards {

struct SnapItem {
    std::uint64_t titleId = 0;
    std::uint8_t kind = 0; // 0 icon, 1 cover
    int w = 0, h = 0;
    std::uint64_t hash = 0; // of the pixels, computed where they were made
    std::shared_ptr<const std::vector<std::uint8_t>> rgba; // w * h * 4
};

std::uint64_t fnv1a(const void* data, std::size_t size, std::uint64_t seed = 1469598103934665603ULL);

/// Worker thread. A copy of `rgba` (w x h) whose longer side is at most
/// `maxSide`, as a snapshot item of `kind` with its pixel hash.
SnapItem makeSnapItem(std::uint64_t titleId, std::uint8_t kind, const std::uint8_t* rgba, int w, int h,
                      int maxSide);

class CardsSnapshotCache {
public:
    ~CardsSnapshotCache() { wait(); }

    /// Starts reading the file of `page` on the pool (a result not taken yet
    /// is dropped).
    void load(nxui::ThreadPool& pool, int page);
    /// Main thread. Moves out a finished read for `page`; false otherwise.
    bool take(int page, std::vector<SnapItem>& out);

    /// Main thread, every frame. `items` is the shown window of `page`;
    /// `complete` says every image in it is a fresh decode. Once the window
    /// has stayed the same for a moment it is written on the pool, unless the
    /// file already holds the same content.
    void offer(nxui::ThreadPool& pool, int page, std::vector<SnapItem> items, bool complete);

    /// Waits for the read and write in flight (onDestroy).
    void wait();

private:
    struct Shared {
        std::mutex mutex;
        std::map<int, std::uint64_t> stored; // content signature of each page's file
    };
    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    int m_loadPage = -1;
    std::shared_ptr<std::vector<SnapItem>> m_loadResult;
    std::future<void> m_loadFuture;
    std::future<void> m_writeFuture;
    std::uint64_t m_pendingSig = 0;
    int m_pendingPage = -1;
    std::chrono::steady_clock::time_point m_pendingSince{};
};

} // namespace cards
