#include "cards/CardsCatalog.hpp"
#include "core/IconPack.hpp"
#include "cards/CardsPages.hpp"
#include "core/Config.hpp"
#include "core/DebugLog.hpp"
#include "home/AppFetch.hpp"
#include "homebrew/HomebrewStore.hpp"
#include "launcher/AppListLoader.hpp"
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>

namespace cards {

void CardsCatalog::startLoad(AppListLoader& loader, nxui::ThreadPool& pool) {
    // Classic installs its own transform and prefetch choice in onCreate; this
    // frontend wants the raw list and decodes icons lazily per card.
    loader.setPendingTransform({});
    loader.setPrefetchIcons(false);
    loader.startAsync(pool);
    m_loading = true;
    refreshPageFiles(pool);
}

bool CardsCatalog::poll(AppListLoader& loader, const AppConfig& config) {
    if (!m_loading || !loader.isReady())
        return false;
    m_loading = false;

    // Render-thread cost of the hand-over, logged so a slow first frame can be
    // attributed to finalize() or to the page build (sort, filter).
    using Clock = std::chrono::steady_clock;
    const auto msSince = [](Clock::time_point from) {
        return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
    };
    const auto pollStart = Clock::now();
    // finalize() fills a model and an icon streamer; the streamer only holds
    // compressed bytes here (no textures) and is dropped at once.
    GridModel model;
    IconStreamer scratch;
    const bool finalized = loader.finalize(model, scratch);
    const double finalizeMs = msSince(pollStart);
    if (!finalized) {
        DebugLog::log("[cards] catalogue load failed; keeping %zu cards", m_entries.size());
        return false;
    }

    std::vector<AppEntry> apps;
    for (const auto& e : model.entries())
        if (e.isApplication() && e.titleId != 0)
            apps.push_back(e);
    // Refresh invalidates images, but must not join a decode doing SD I/O.
    // Jobs capture only shared result storage; abandoned results cannot reattach.
    for (auto& pending : m_pending) pending.cancelled->store(true);
    m_pending.clear();
    m_parked.clear();
    m_entries.clear();
    m_all = std::move(apps);
    const auto pageStart = Clock::now();
    setPage(m_page, config);
    DebugLog::log("[cards] catalogue %zu titles (sort %d) finalize %.1f ms, page %.1f ms, total %.1f ms",
                  m_all.size(), config.cardsSort, finalizeMs, msSince(pageStart), msSince(pollStart));
    return true;
}

void CardsCatalog::refreshPageFiles(nxui::ThreadPool& pool) {
    if (m_fileFuture.valid()) {
        m_rescanQueued = true;
        return;
    }
    auto result = std::make_shared<PageFiles>();
    try {
        m_fileFuture = pool.submit([result] {
#ifdef SWITCHU_MENU
            result->homebrew = scanHomebrew("sdmc:/switch/OmniLaunch/OmniLaunch.nro", false);
            std::error_code ec;
            if (std::filesystem::exists(pages::kTikTokNro, ec)) result->tiktok = pages::kTikTokNro;
            result->browser = home::installedBrowserPath();
#endif
        });
        m_fileResult = std::move(result);
    } catch (...) {}
}

bool CardsCatalog::pollPageFiles(nxui::ThreadPool& pool, const AppConfig& config) {
    if (!m_fileFuture.valid() ||
        m_fileFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    bool changed = false;
    try {
        m_fileFuture.get();
        // A rescan requested during this read supersedes it (e.g. install).
        if (!m_rescanQueued) { m_files = std::move(*m_fileResult); changed = true; }
    } catch (...) {}
    m_fileResult.reset();
    if (changed && (m_page == pages::Prefs::kHomebrew || m_page == pages::Prefs::kMedia))
        setPage(m_page, config);
    if (m_rescanQueued) {
        m_rescanQueued = false;
        refreshPageFiles(pool);
    }
    return changed;
}

void CardsCatalog::noteInstalled(std::uint64_t titleId) {
    if (titleId == pages::homebrewId(pages::kTikTokNro)) m_files.tiktok = pages::kTikTokNro;
    if (titleId == pages::homebrewId(home::kBrowserNro)) m_files.browser = home::kBrowserNro;
}

void CardsCatalog::setPage(int page, const AppConfig& config) {
    namespace pg = cards::pages;
    // Textures follow the title id: keep the ones still on the page.
    for (auto& entry : m_entries) {
        if (!entry.icon) continue;
        const auto id = entry.app.titleId;
        std::erase_if(m_parked, [id](const CardEntry& e) { return e.app.titleId == id; });
        m_parked.push_back(std::move(entry));
    }
    m_entries.clear();
    // A decode still pending keeps its request flag, so it is not queued twice.
    auto adopt = [this](CardEntry& c) {
        for (auto& o : m_parked)
            if (o.app.titleId == c.app.titleId && o.iconFile == c.iconFile &&
                o.homebrewPath == c.homebrewPath && o.version == c.version && o.needsDownload == c.needsDownload) {
                c.icon = std::move(o.icon);
                c.iconFresh = o.iconFresh;
                c.iconSnap = std::move(o.iconSnap);
                c.iconRequested = o.iconRequested;
                c.iconFailed = o.iconFailed;
                return;
            }
    };
    m_page = page;
    if (page == pg::Prefs::kNintendo) {
        auto& i18n = nxui::I18n::instance();
        struct Tile { pg::SystemTile id; const char* key; const char* fallback; const char* icon; };
        static const Tile kTiles[] = {
            {pg::SystemTile::News, "cards.tile_news", "News", "cards/news.png"},
            {pg::SystemTile::Album, "cards.tile_album", "Album", "album.png"},
            {pg::SystemTile::Controllers, "cards.tile_controllers", "Controllers", "controller.png"},
            {pg::SystemTile::MiiEditor, "cards.tile_mii", "Mii Maker", "mii_editor.png"},
            {pg::SystemTile::Internet, "cards.tile_internet", "Internet", "network.png"},
            {pg::SystemTile::Users, "cards.tile_users", "Users", "switch_logo_white.png"},
            {pg::SystemTile::HomeSettings, "cards.tile_settings", "HOME settings", "settings.png"},
        };
        for (const auto& t : kTiles) {
            CardEntry c;
            c.system = true;
            c.app.titleId = pg::systemId(t.id);
            c.app.title = config.customTitle(c.app.titleId, i18n.tr(t.key, t.fallback));
            c.app.kind = GridEntryKind::Application;
            c.app.userRequired = false;
            c.iconFile = iconpack::resolve(t.icon, m_assetsRoot + "/icons/" + t.icon);
            adopt(c);
            m_entries.push_back(std::move(c));
        }
    } else if (page == pg::Prefs::kHomebrew) {
#ifdef SWITCHU_MENU
        for (const HomebrewApp& hb : m_files.homebrew) {
            CardEntry c;
            c.system = false;
            c.isHomebrew = true;
            c.homebrewPath = hb.path;
            c.author = hb.author;
            c.version = hb.version;
            c.app.title = hb.name.empty() ? std::filesystem::path(c.homebrewPath).stem().string() : hb.name;
            c.app.englishTitle = c.author;
            c.app.titleId = pg::homebrewId(c.homebrewPath);
            c.app.title = config.customTitle(c.app.titleId, c.app.title);
            c.app.kind = GridEntryKind::Application;
            c.app.userRequired = false;
            adopt(c);
            m_entries.push_back(std::move(c));
        }
#endif
    } else {
        const bool media = page == pg::Prefs::kMedia;
        for (const auto& a : m_all) {
            if (config.cards.isMedia(a.titleId) != media)
                continue;
            CardEntry c;
            c.app = a;
            c.app.title = config.customTitle(a.titleId, a.title);
            adopt(c);
            m_entries.push_back(std::move(c));
        }
        if (media) {
            // Media apps fetched on first use (as CardsNX): TikTok and the
            // BrowseNX browser. The card keeps one stable id before and after
            // the download; A downloads it (progress dialog), then opens it.
            auto& i18n = nxui::I18n::instance();
            auto fetchCard = [&](const std::string& installed, const char* dest, const char* url,
                                 const std::string& title) {
                CardEntry c;
                c.isHomebrew = true;
                c.app.title = config.customTitle(pg::homebrewId(dest), title);
                c.app.titleId = pg::homebrewId(dest);
                c.app.kind = GridEntryKind::Application;
                c.app.userRequired = false;
                if (!installed.empty()) {
                    c.homebrewPath = installed;
                    c.author = "ad3333m";
                } else {
                    c.needsDownload = true;
                    c.homebrewPath = dest;
                    c.downloadUrl = url;
                    c.author = i18n.tr("cards.download_from_github", "Download from GitHub");
                    c.iconFile = m_assetsRoot + "/icons/cards/download.png";
                }
                adopt(c);
                m_entries.push_back(std::move(c));
            };
            fetchCard(m_files.tiktok,
                      pg::kTikTokNro, pg::kTikTokUrl, "TikTok");
            fetchCard(m_files.browser, home::kBrowserNro, home::kBrowserUrl,
                      i18n.tr("cards.tile_browser", "Browser"));
        }
        sort(config);
    }
    // A title can reappear after leaving its page while its decode is still
    // pending. Reconstruct request ownership from the job list as well.
    std::erase_if(m_parked, [](const CardEntry& e) { return !e.icon; });
    while (m_parked.size() > 48) m_parked.erase(m_parked.begin());
    for (auto& c : m_entries) {
        c.iconRequested = false;
        for (const auto& p : m_pending)
            if (p.titleId == c.app.titleId && p.iconFile == c.iconFile &&
                p.homebrewPath == c.homebrewPath && p.version == c.version && p.needsDownload == c.needsDownload)
                c.iconRequested = true;
    }
    // Pending decodes for absent titles are discarded by pumpIcons.
}

std::vector<std::uint64_t> CardsCatalog::pageIds() const {
    std::vector<std::uint64_t> ids;
    ids.reserve(m_entries.size());
    for (const auto& e : m_entries)
        ids.push_back(e.app.titleId);
    return ids;
}

void CardsCatalog::swapEntries(int i, int j) {
    if (i < 0 || j < 0 || i >= count() || j >= count() || i == j)
        return;
    std::swap(m_entries[static_cast<std::size_t>(i)], m_entries[static_cast<std::size_t>(j)]);
}

namespace {
std::string lowered(const std::string& s) {
    std::string out(s);
    for (auto& ch : out)
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    return out;
}
} // namespace

void CardsCatalog::sort(const AppConfig& config) {
    namespace pg = cards::pages;
    // Applets and homebrew moved by hand (+ > Move) keep that place: their
    // synthetic ids (0xFF / 0xFE) share the "my order" list with games.
    auto byHand = [this, &config]() {
        const auto& own = config.cards.gamesOrder;
        std::stable_sort(m_entries.begin(), m_entries.end(), [&own](const CardEntry& a, const CardEntry& b) {
            return pg::rank(own, a.app.titleId) < pg::rank(own, b.app.titleId);
        });
    };
    if (m_page == pg::Prefs::kNintendo) {
        byHand(); // fixed tile order unless moved
        return;
    }
    if (m_page == pg::Prefs::kHomebrew) {
        std::stable_sort(m_entries.begin(), m_entries.end(), [](const CardEntry& a, const CardEntry& b) {
            return lowered(a.app.title) < lowered(b.app.title);
        });
        byHand();
        return;
    }
    // Ties keep last-opened order, so "most played" with no figures yet and
    // equal names stay where the owner expects them.
    auto recent = [&config](const CardEntry& a, const CardEntry& b) {
        return config.lastOpenedAt(a.app.titleId) > config.lastOpenedAt(b.app.titleId);
    };
    std::stable_sort(m_entries.begin(), m_entries.end(), recent);
    // Media keeps its own hand order; Games uses "my order" when chosen.
    const std::vector<std::uint64_t>* own = nullptr;
    if (m_page == pg::Prefs::kMedia)
        own = &config.cards.mediaOrder;
    else if (config.cardsSort == 3)
        own = &config.cards.gamesOrder;
    if (own) {
        std::stable_sort(m_entries.begin(), m_entries.end(), [own](const CardEntry& a, const CardEntry& b) {
            return pg::rank(*own, a.app.titleId) < pg::rank(*own, b.app.titleId);
        });
    } else if (config.cardsSort == 1) {
        std::stable_sort(m_entries.begin(), m_entries.end(), [](const CardEntry& a, const CardEntry& b) {
            return lowered(a.app.title) < lowered(b.app.title);
        });
    } else if (config.cardsSort == 2) {
        std::stable_sort(m_entries.begin(), m_entries.end(), [&config](const CardEntry& a, const CardEntry& b) {
            return config.playtimeOf(a.app.titleId) > config.playtimeOf(b.app.titleId);
        });
    }
}

int CardsCatalog::indexOf(std::uint64_t titleId) const {
    for (int i = 0; i < count(); ++i)
        if (m_entries[static_cast<std::size_t>(i)].app.titleId == titleId)
            return i;
    return -1;
}

void CardsCatalog::updateViewFlags(std::uint64_t titleId, std::uint32_t flags) {
    for (auto& a : m_all)
        if (a.titleId == titleId)
            a.viewFlags = flags; // kept for the next page rebuild
    const int i = indexOf(titleId);
    if (i >= 0)
        m_entries[static_cast<std::size_t>(i)].app.viewFlags = flags;
}

void CardsCatalog::pumpIcons(nxui::GpuDevice& gpu, nxui::Renderer& ren, nxui::ThreadPool& pool,
                             int first, int last, int keep, int uploadBudget) {
    // Upload finished decodes.
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (!it->future.valid() ||
            it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        int index = indexOf(it->titleId);
        if (index >= 0) {
            const auto& card = m_entries[static_cast<std::size_t>(index)];
            if (it->iconFile != card.iconFile || it->homebrewPath != card.homebrewPath ||
                it->version != card.version || it->needsDownload != card.needsDownload) index = -1;
        }
        if (index >= 0 && uploadBudget <= 0)
            break; // leave the rest (futures unconsumed) for the next frame
        try { it->future.get(); } catch (...) {}
        if (index >= 0) {
            auto& card = m_entries[static_cast<std::size_t>(index)];
            const auto& d = *it->decoded;
            auto tex = std::make_unique<nxui::Texture>();
            if (!d.rgba.empty() && d.w > 0 && d.h > 0 && tex->loadFromPixels(gpu, ren, d.rgba.data(), d.w, d.h)) {
                // Replaces a snapshot icon shown meanwhile.
                card.icon = std::move(tex);
                card.iconFresh = true;
                if (it->snap && it->snap->rgba)
                    card.iconSnap = it->snap;
            } else {
                // No source any more: a snapshot icon must not stay.
                card.icon.reset();
                card.iconFresh = false;
                card.iconSnap.reset();
                card.iconFailed = true;
            }
            --uploadBudget;
        }
        it = m_pending.erase(it);
    }

    if (m_entries.empty())
        return;
    first = std::max(0, first);
    last = std::min(count() - 1, last);

    // Release textures far from the focus.
    for (int i = 0; i < count(); ++i) {
        if (count() <= 48 || (i >= first - keep && i <= last + keep))
            continue;
        auto& card = m_entries[static_cast<std::size_t>(i)];
        if (card.icon) {
            card.icon.reset();
            card.iconFresh = false;
            card.iconSnap.reset();
            card.iconRequested = false;
        }
    }

    const auto active = std::count_if(m_entries.begin(), m_entries.end(),
                                      [](const CardEntry& c) { return bool(c.icon); });
    while (!m_parked.empty() && m_parked.size() + active > 48) m_parked.erase(m_parked.begin());

    // Queue decodes for the visible window. A snapshot icon is still decoded
    // again from its source, and replaced.
    for (int i = first; i <= last && m_pending.size() < 16; ++i) {
        auto& card = m_entries[static_cast<std::size_t>(i)];
        if ((card.icon && card.iconFresh) || card.iconRequested || card.iconFailed)
            continue;
        card.iconRequested = true;
        PendingIcon p;
        p.titleId = card.app.titleId;
        p.iconFile = card.iconFile;
        p.homebrewPath = card.homebrewPath;
        p.version = card.version;
        p.needsDownload = card.needsDownload;
        p.decoded = std::make_shared<IconStreamer::DecodedIcon>();
        p.cancelled = std::make_shared<std::atomic<bool>>(false);
        p.snap = std::make_shared<SnapItem>();
        auto decoded = p.decoded;
        auto snap = p.snap;
        auto cancelled = p.cancelled;
        const std::uint64_t titleId = p.titleId;
        const std::string iconFile = !card.iconFile.empty() ? card.iconFile : std::string();
        const std::string hbPath = card.isHomebrew ? card.homebrewPath : std::string();
        try {
            p.future = pool.submit([decoded, snap, cancelled, titleId, iconFile, hbPath]() {
                if (cancelled->load())
                    return;
                decodeCardIcon(*decoded, titleId, iconFile, hbPath);
                // Pixels kept for the page snapshot, made here off the UI thread.
                if (!decoded->rgba.empty() && decoded->w > 0 && decoded->h > 0 && !cancelled->load())
                    *snap = makeSnapItem(titleId, 0, decoded->rgba.data(), decoded->w, decoded->h, 160); // drawn at most ~155 px
            });
        } catch (...) {
            card.iconRequested = false; // pool shutting down
            continue;
        }
        m_pending.push_back(std::move(p));
    }
}

void CardsCatalog::decodeCardIcon(IconStreamer::DecodedIcon& out, std::uint64_t titleId, const std::string& iconFile,
                                  const std::string& hbPath) {
    if (!iconFile.empty()) {
        // Nintendo page tile: a 256 px PNG from the assets.
        nxui::DecodedImage img = nxui::Texture::decodeFile(iconFile, 256);
        if (img.valid()) {
            out.rgba = std::move(img.rgba);
            out.w = img.width;
            out.h = img.height;
        }
        return;
    }
#ifdef SWITCHU_MENU
    if (!hbPath.empty()) {
        std::vector<std::uint8_t> bytes = readHomebrewIcon(hbPath);
        if (!bytes.empty()) {
            out = IconStreamer::decodeIconData(bytes);
            return;
        }
    }
#else
    (void)hbPath;
#endif
    out = IconStreamer::decodeIconData(AppListLoader::loadIconData(titleId));
}

int CardsCatalog::adoptSnapshotIcon(nxui::GpuDevice& gpu, nxui::Renderer& ren, const SnapItem& item) {
    const int index = indexOf(item.titleId);
    if (index < 0 || !item.rgba)
        return -1;
    auto& card = m_entries[static_cast<std::size_t>(index)];
    if (card.icon || card.iconFailed)
        return 0; // already shown, or a fresh decode got there first
    auto tex = std::make_unique<nxui::Texture>();
    if (!tex->loadFromPixels(gpu, ren, item.rgba->data(), item.w, item.h))
        return 0;
    card.icon = std::move(tex);
    card.iconFresh = false;
    return 1;
}

void CardsCatalog::releaseAll() {
    // Only teardown/reload calls this; page navigation never joins a worker.
    if (m_fileFuture.valid()) {
        m_fileFuture.wait();
        try { m_fileFuture.get(); } catch (...) {}
    }
    m_fileResult.reset();
    m_rescanQueued = false;
    releaseIcons();
}

void CardsCatalog::releaseIcons() {
    m_parked.clear();
    for (auto& p : m_pending)
        if (p.cancelled)
            p.cancelled->store(true);
    for (auto& p : m_pending)
        if (p.future.valid())
            p.future.wait();
    m_pending.clear();
    for (auto& card : m_entries) {
        card.icon.reset();
        card.iconFresh = false;
        card.iconSnap.reset();
        card.iconRequested = false;
    }
}

} // namespace cards
