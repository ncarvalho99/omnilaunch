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
    /// Limits of one page file. Items are written in priority order (the
    /// cards the page opens on first); what exceeds either limit is dropped.
    static constexpr std::uint32_t kMaxItems = 64;
    static constexpr std::size_t kMaxBytes = 8u * 1024u * 1024u;

    ~CardsSnapshotCache() { wait(); }

    /// Starts reading the file of `page` on the pool (a result not taken yet
    /// is dropped). Items are published as they are read: those of the cards
    /// around `focus` (0: the start of the page) first.
    void load(nxui::ThreadPool& pool, int page, std::uint64_t focus = 0);
    /// Main thread. Moves out the items of `page` read since the last call
    /// (appended to `out`); true when it moved any or the read has ended.
    bool take(int page, std::vector<SnapItem>& out);

    /// Main thread, every frame. `items` holds the images of `page` in card
    /// order, at most kMaxItems / kMaxBytes (the excess is dropped from the
    /// end); `pending` counts the images still waiting
    /// for their fresh decode (`firstPending` / `firstKind` name the first
    /// one, for the log). Once the items have stayed the same for a moment
    /// (longer while some are pending) they are written on the pool, unless
    /// the file already holds the same content.
    void offer(nxui::ThreadPool& pool, int page, std::vector<SnapItem> items, int pending = 0,
               std::uint64_t firstPending = 0, int firstKind = 0);

    /// Waits for the read and write in flight (onDestroy).
    void wait();

private:
    struct Shared {
        std::mutex mutex;
        std::map<int, std::uint64_t> stored; // content signature of each page's file
    };
    struct LoadState {
        std::mutex mutex;
        std::vector<SnapItem> items; // read, not taken yet
        bool done = false;
    };
    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    int m_loadPage = -1;
    std::shared_ptr<LoadState> m_loadResult;
    std::future<void> m_loadFuture;
    std::future<void> m_writeFuture;
    std::uint64_t m_pendingSig = 0;
    int m_pendingPage = -1;
    std::chrono::steady_clock::time_point m_pendingSince{};
};

} // namespace cards
