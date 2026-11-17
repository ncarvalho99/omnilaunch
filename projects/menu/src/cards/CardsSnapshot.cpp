#include "cards/CardsSnapshot.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/ThreadPool.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace cards {

namespace {
// Next to the Classic decoded-icon cache, so it is excluded from the
// configuration backup as well. A cache: deleting it costs one slow start.
constexpr const char* kDir = "sdmc:/config/OmniLaunch/control_cache/decoded";
constexpr std::uint32_t kMagic = 0x50534E43; // "CNSP"
constexpr std::uint32_t kVersion = 1;
constexpr auto kSettle = std::chrono::milliseconds(1500);
constexpr auto kSettlePartial = std::chrono::milliseconds(5000);

struct Header {
    std::uint32_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::int32_t page = 0;
    std::uint32_t count = 0;
};
static_assert(sizeof(Header) == 16);

struct ItemHeader {
    std::uint64_t titleId = 0;
    std::uint64_t hash = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t kind = 0;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(ItemHeader) == 32);

std::string pathFor(int page) {
    char name[48];
    std::snprintf(name, sizeof(name), "/cards_page%d.snap", page);
    return std::string(kDir) + name;
}

std::uint64_t signature(const std::vector<SnapItem>& items) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const auto& it : items) {
        h = fnv1a(&it.titleId, sizeof(it.titleId), h);
        h = fnv1a(&it.kind, sizeof(it.kind), h);
        h = fnv1a(&it.hash, sizeof(it.hash), h);
    }
    return h;
}

// The file holds the images card by card (a title's icon and cover next to
// each other). The item headers are read first (the pixels are skipped);
// then the cards around `focus` -- the card the page opens on now, which
// moves when a title is started from Classic -- are read and handed to `sink`
// first (one card before it, five after: the window decoded first), then the
// rest by distance. So the cards on screen show while the rest of a large
// page is still being read. Returns every item in file order (for the stored
// signature). A damaged file stops the read; the items already handed over
// stay valid images.
template <class Sink>
std::vector<SnapItem> readFile(int page, std::uint64_t focus, Sink&& sink) {
    std::ifstream in(pathFor(page), std::ios::binary);
    if (!in)
        return {};
    Header h{};
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!in || h.magic != kMagic || h.version != kVersion || h.page != page ||
        h.count > CardsSnapshotCache::kMaxItems)
        return {};
    struct Entry {
        ItemHeader e;
        std::streamoff offset;
        int card;
    };
    std::vector<Entry> entries;
    entries.reserve(h.count);
    std::streamoff pos = static_cast<std::streamoff>(sizeof(Header));
    std::uint64_t lastId = 0;
    int cards = -1, focusCard = -1;
    for (std::uint32_t i = 0; i < h.count; ++i) {
        ItemHeader e{};
        in.seekg(pos);
        in.read(reinterpret_cast<char*>(&e), sizeof(e));
        if (!in || e.titleId == 0 || e.kind > 1 || e.width == 0 || e.height == 0 || e.width > 1024 ||
            e.height > 1024)
            return {};
        if (e.titleId != lastId) {
            lastId = e.titleId;
            ++cards;
            if (focus != 0 && e.titleId == focus && focusCard < 0)
                focusCard = cards;
        }
        pos += static_cast<std::streamoff>(sizeof(e));
        entries.push_back({e, pos, cards});
        pos += static_cast<std::streamoff>(e.width) * e.height * 4;
    }
    std::vector<std::size_t> order(entries.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    if (focusCard >= 0) {
        const auto rank = [focusCard](int card) {
            const bool window = card >= focusCard - 1 && card <= focusCard + 5;
            return (window ? 0 : 1000) + std::abs(card - focusCard);
        };
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return rank(entries[a].card) < rank(entries[b].card);
        });
    }
    std::vector<SnapItem> out(entries.size());
    for (std::size_t i : order) {
        const ItemHeader& e = entries[i].e;
        auto px = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(e.width) * e.height * 4);
        in.clear();
        in.seekg(entries[i].offset);
        in.read(reinterpret_cast<char*>(px->data()), static_cast<std::streamsize>(px->size()));
        if (!in)
            return {};
        SnapItem item;
        item.titleId = e.titleId;
        item.kind = static_cast<std::uint8_t>(e.kind);
        item.w = static_cast<int>(e.width);
        item.h = static_cast<int>(e.height);
        item.hash = e.hash;
        item.rgba = std::move(px);
        sink(item);
        out[i] = std::move(item);
    }
    return out;
}

bool writeFile(int page, const std::vector<SnapItem>& items) {
    // A write of another page may still run on the other worker.
    static std::mutex writeMutex;
    std::lock_guard<std::mutex> lock(writeMutex);
    std::error_code ec;
    std::filesystem::create_directories(kDir, ec);
    const std::string path = pathFor(page);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        Header h{};
        h.page = page;
        h.count = static_cast<std::uint32_t>(items.size());
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        for (const auto& it : items) {
            ItemHeader e{};
            e.titleId = it.titleId;
            e.hash = it.hash;
            e.width = static_cast<std::uint32_t>(it.w);
            e.height = static_cast<std::uint32_t>(it.h);
            e.kind = it.kind;
            out.write(reinterpret_cast<const char*>(&e), sizeof(e));
            out.write(reinterpret_cast<const char*>(it.rgba->data()), static_cast<std::streamsize>(it.rgba->size()));
        }
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool running(const std::future<void>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}
} // namespace

std::uint64_t fnv1a(const void* data, std::size_t size, std::uint64_t seed) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

SnapItem makeSnapItem(std::uint64_t titleId, std::uint8_t kind, const std::uint8_t* rgba, int w, int h,
                      int maxSide) {
    SnapItem item;
    item.titleId = titleId;
    item.kind = kind;
    if (!rgba || w <= 0 || h <= 0)
        return item;
    int dw = w, dh = h;
    if (maxSide > 0 && (w > maxSide || h > maxSide)) {
        const float s = std::min(static_cast<float>(maxSide) / w, static_cast<float>(maxSide) / h);
        dw = std::max(1, static_cast<int>(w * s));
        dh = std::max(1, static_cast<int>(h * s));
    }
    auto out = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(dw) * dh * 4);
    if (dw == w && dh == h) {
        std::copy(rgba, rgba + out->size(), out->begin());
    } else {
        // Box filter over the source pixels each destination pixel covers.
        for (int y = 0; y < dh; ++y) {
            const int y0 = y * h / dh, y1 = std::max(y0 + 1, (y + 1) * h / dh);
            for (int x = 0; x < dw; ++x) {
                const int x0 = x * w / dw, x1 = std::max(x0 + 1, (x + 1) * w / dw);
                unsigned sum[4] = {0, 0, 0, 0};
                for (int sy = y0; sy < y1; ++sy)
                    for (int sx = x0; sx < x1; ++sx) {
                        const std::uint8_t* p = rgba + (static_cast<std::size_t>(sy) * w + sx) * 4;
                        for (int c = 0; c < 4; ++c)
                            sum[c] += p[c];
                    }
                const unsigned n = static_cast<unsigned>((y1 - y0) * (x1 - x0));
                std::uint8_t* d = out->data() + (static_cast<std::size_t>(y) * dw + x) * 4;
                for (int c = 0; c < 4; ++c)
                    d[c] = static_cast<std::uint8_t>(sum[c] / n);
            }
        }
    }
    item.w = dw;
    item.h = dh;
    item.hash = fnv1a(out->data(), out->size());
    item.rgba = std::move(out);
    return item;
}

void CardsSnapshotCache::load(nxui::ThreadPool& pool, int page, std::uint64_t focus) {
    // A read of another page still running is abandoned, not waited for: the
    // pool's futures do not block on release and its result is shared.
    m_loadFuture = {};
    m_loadPage = page;
    auto result = std::make_shared<LoadState>();
    m_loadResult = result;
    auto shared = m_shared;
    try {
        m_loadFuture = pool.submit([result, shared, page, focus]() {
            const auto start = std::chrono::steady_clock::now();
            const auto all = readFile(page, focus, [&result](const SnapItem& item) {
                std::lock_guard<std::mutex> lock(result->mutex);
                result->items.push_back(item);
            });
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                shared->stored[page] = all.empty() ? 0 : signature(all);
            }
            {
                std::lock_guard<std::mutex> lock(result->mutex);
                result->done = true;
            }
            DebugLog::log("[cards-snap] page %d read: %zu image(s) in %lldms", page, all.size(),
                          static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start).count()));
        });
    } catch (...) {
        m_loadResult.reset();
        m_loadPage = -1;
    }
}

bool CardsSnapshotCache::take(int page, std::vector<SnapItem>& out) {
    if (page != m_loadPage || !m_loadResult)
        return false;
    // Checked before draining: once the read has ended nothing is added, so
    // the drain below sees every item it produced.
    const bool ended = !m_loadFuture.valid() || !running(m_loadFuture);
    bool done = false;
    bool moved = false;
    {
        std::lock_guard<std::mutex> lock(m_loadResult->mutex);
        moved = !m_loadResult->items.empty();
        for (auto& item : m_loadResult->items)
            out.push_back(std::move(item));
        m_loadResult->items.clear();
        // `ended` without `done`: the read stopped early (it threw).
        done = m_loadResult->done || ended;
    }
    if (done) {
        if (m_loadFuture.valid()) {
            try {
                m_loadFuture.get();
            } catch (...) {
            }
        }
        m_loadResult.reset();
    }
    return moved || done;
}

void CardsSnapshotCache::offer(nxui::ThreadPool& pool, int page, std::vector<SnapItem> items, int pending,
                               std::uint64_t firstPending, int firstKind) {
    // Lowest-priority items (the end of the list) go first past the limits.
    if (items.size() > kMaxItems)
        items.resize(kMaxItems);
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        bytes += items[i].rgba ? items[i].rgba->size() : 0;
        if (bytes > kMaxBytes) {
            items.resize(i);
            break;
        }
    }
    if (items.empty() || (page == m_loadPage && m_loadResult)) {
        m_pendingPage = -1; // not saveable now (or the file is still being read)
        return;
    }
    const std::uint64_t sig = signature(items);
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        const auto it = m_shared->stored.find(page);
        if (it != m_shared->stored.end() && it->second == sig) {
            m_pendingPage = -1;
            return;
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (page != m_pendingPage || sig != m_pendingSig) {
        m_pendingPage = page;
        m_pendingSig = sig;
        m_pendingSince = now;
        return;
    }
    // A window with images still decoding waits longer: they normally arrive
    // within a second and would change the content again.
    if (now - m_pendingSince < (pending > 0 ? kSettlePartial : kSettle) || running(m_writeFuture))
        return;
    auto shared = m_shared;
    try {
        if (m_writeFuture.valid())
            m_writeFuture.get();
        const std::size_t n = items.size();
        m_writeFuture = pool.submit([shared, page, sig, items = std::move(items)]() {
            if (writeFile(page, items)) {
                std::lock_guard<std::mutex> lock(shared->mutex);
                shared->stored[page] = sig;
            }
        });
        if (pending > 0)
            DebugLog::log("[cards-snap] page %d saved: %zu image(s), %d pending (first %016llX %s)", page, n,
                          pending, static_cast<unsigned long long>(firstPending), firstKind == 1 ? "cover" : "icon");
        else
            DebugLog::log("[cards-snap] page %d saved: %zu image(s)", page, n);
    } catch (...) {
    }
    m_pendingPage = -1;
}

void CardsSnapshotCache::wait() {
    if (m_loadFuture.valid())
        m_loadFuture.wait();
    if (m_writeFuture.valid())
        m_writeFuture.wait();
}

} // namespace cards
