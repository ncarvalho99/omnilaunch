#pragma once
// Cards view of the HomeCore catalogue (HomeNX D1a). The title list comes from
// the Core's AppListLoader; this frontend keeps only its own ordering and the
// GPU icon textures of the cards near the focus. Those textures belong to the
// frontend and are released in onDestroy (audit §6.1 rule 3).

#include "cards/CardsSnapshot.hpp"
#include "core/GridModel.hpp"
#include "launcher/IconStreamer.hpp"
#include <nxui/core/Texture.hpp>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

struct AppConfig;
class AppListLoader;
namespace nxui { class GpuDevice; class Renderer; class ThreadPool; }

namespace cards {

struct CardEntry {
    AppEntry app;
    std::unique_ptr<nxui::Texture> icon;
    bool iconRequested = false;
    bool iconFailed = false;
    // false while `icon` comes from the page snapshot (CardsSnapshot) and the
    // decode from its source is still pending. `iconSnap` holds the pixels of
    // the fresh decode, for the next snapshot.
    bool iconFresh = false;
    std::shared_ptr<const SnapItem> iconSnap;
    // Nintendo page tile (cards::pages::SystemTile): icon from the assets,
    // opened through AppletLauncher, no play data (HomeNX E1).
    bool system = false;
    std::string iconFile;
    // Homebrew NRO (Part F): path to .nro on SD card.
    bool isHomebrew = false;
    bool needsDownload = false;
    std::string downloadUrl;
    std::string homebrewPath;
    std::string author;
    std::string version;
};

class CardsCatalog {
public:
    /// Starts an asynchronous catalogue fetch on the Core loader.
    void startLoad(AppListLoader& loader, nxui::ThreadPool& pool);
    /// Main thread. Adopts a finished fetch, ordered by config.cardsSort.
    /// Returns true when the list was replaced.
    bool poll(AppListLoader& loader, const AppConfig& config);
    /// Reorders the cards by config.cardsSort (0 last opened, 1 title A-Z,
    /// 2 most played, 3 my order). Media keeps its own order; Nintendo is
    /// fixed. Icon textures and pending decodes follow by title id.
    void sort(const AppConfig& config);
    bool loading() const { return m_loading; }

    /// Pages (HomeNX E1). Rebuilds the visible cards for `page` from the last
    /// loaded catalogue; textures of cards leaving the page are released.
    void setPage(int page, const AppConfig& config);
    int page() const { return m_page; }
    /// Rebuilds the current page (after a rename or a move to/from Media).
    void rebuild(const AppConfig& config) { setPage(m_page, config); }
    /// Where the Nintendo page finds its tile icons.
    void setAssetsRoot(std::string root) { m_assetsRoot = std::move(root); }
    /// Move mode (HomeNX E2): swaps cards i and j on the current page.
    void swapEntries(int i, int j);
    /// Title ids of the current page in their shown order.
    std::vector<std::uint64_t> pageIds() const;
    /// Number of installed titles on Games + Media (About section).
    int installedCount() const { return static_cast<int>(m_all.size()); }
    /// Every installed title (all pages), loader order.
    const std::vector<AppEntry>& all() const { return m_all; }

    int count() const { return static_cast<int>(m_entries.size()); }
    CardEntry& at(int i) { return m_entries[static_cast<std::size_t>(i)]; }
    const CardEntry& at(int i) const { return m_entries[static_cast<std::size_t>(i)]; }
    int indexOf(std::uint64_t titleId) const;
    void updateViewFlags(std::uint64_t titleId, std::uint32_t flags);

    /// Queues icon decodes on the pool for cards in [first, last] and uploads
    /// finished ones (main thread, at most `uploadBudget` per call). Textures
    /// outside [first - keep, last + keep] are released.
    void pumpIcons(nxui::GpuDevice& gpu, nxui::Renderer& ren, nxui::ThreadPool& pool,
                   int first, int last, int keep, int uploadBudget);

    /// Shows a snapshot icon on its card until the fresh decode replaces it.
    /// Returns -1 while the title is not on the current page, 1 when a texture
    /// was uploaded, 0 when nothing was needed.
    int adoptSnapshotIcon(nxui::GpuDevice& gpu, nxui::Renderer& ren, const SnapItem& item);

    /// Stops pending decodes and drops every texture (onDestroy).
    void releaseAll();

private:
    struct PendingIcon {
        std::uint64_t titleId = 0;
        std::shared_ptr<IconStreamer::DecodedIcon> decoded;
        std::shared_ptr<std::atomic<bool>> cancelled;
        std::shared_ptr<SnapItem> snap;
        std::future<void> future;
    };
    static void decodeCardIcon(IconStreamer::DecodedIcon& out, std::uint64_t titleId, const std::string& iconFile,
                               const std::string& hbPath);

    std::vector<AppEntry> m_all;        // every installed title, loader order
    std::vector<CardEntry> m_entries;   // the current page
    std::vector<PendingIcon> m_pending;
    bool m_loading = false;
    int m_page = 0;
    std::string m_assetsRoot;
};

} // namespace cards
