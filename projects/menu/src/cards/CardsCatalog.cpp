#include "cards/CardsCatalog.hpp"
#include "cards/CardsPages.hpp"
#include "core/Config.hpp"
#include "core/DebugLog.hpp"
#include "homebrew/HomebrewStore.hpp"
#include "launcher/AppListLoader.hpp"
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <algorithm>

namespace cards {

void CardsCatalog::startLoad(AppListLoader& loader, nxui::ThreadPool& pool) {
    // Classic installs its own transform and prefetch choice in onCreate; this
    // frontend wants the raw list and decodes icons lazily per card.
    loader.setPendingTransform({});
    loader.setPrefetchIcons(false);
    loader.startAsync(pool);
    m_loading = true;
}

bool CardsCatalog::poll(AppListLoader& loader, const AppConfig& config) {
    if (!m_loading || !loader.isReady())
        return false;
    m_loading = false;

    // finalize() fills a model and an icon streamer; the streamer only holds
    // compressed bytes here (no textures) and is dropped at once.
    GridModel model;
    IconStreamer scratch;
    if (!loader.finalize(model, scratch)) {
        DebugLog::log("[cards] catalogue load failed; keeping %zu cards", m_entries.size());
        return false;
    }

    std::vector<AppEntry> apps;
    for (const auto& e : model.entries())
        if (e.isApplication() && e.titleId != 0)
            apps.push_back(e);
    releaseAll();
    m_all = std::move(apps);
    m_entries.clear();
    setPage(m_page, config);
    DebugLog::log("[cards] catalogue %zu titles (sort %d)", m_all.size(), config.cardsSort);
    return true;
}

void CardsCatalog::setPage(int page, const AppConfig& config) {
    namespace pg = cards::pages;
    // Textures follow the title id: keep the ones still on the page.
    std::vector<CardEntry> old = std::move(m_entries);
    m_entries.clear();
    // A decode still pending keeps its request flag, so it is not queued twice.
    auto adopt = [&old](CardEntry& c) {
        for (auto& o : old)
            if (o.app.titleId == c.app.titleId) {
                c.icon = std::move(o.icon);
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
            c.app.title = i18n.tr(t.key, t.fallback);
            c.app.kind = GridEntryKind::Application;
            c.app.userRequired = false;
            c.iconFile = m_assetsRoot + "/icons/" + t.icon;
            adopt(c);
            m_entries.push_back(std::move(c));
        }
    } else if (page == pg::Prefs::kHomebrew) {
#ifdef SWITCHU_MENU
        for (HomebrewApp& hb : scanHomebrew("sdmc:/switch/OmniLaunch/OmniLaunch.nro", false)) {
            CardEntry c;
            c.system = false;
            c.isHomebrew = true;
            c.homebrewPath = hb.path;
            c.author = std::move(hb.author);
            c.version = std::move(hb.version);
            c.app.title = hb.name.empty() ? std::filesystem::path(c.homebrewPath).stem().string() : std::move(hb.name);
            c.app.englishTitle = c.author;
            c.app.titleId = pg::homebrewId(c.homebrewPath);
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
        sort(config);
    }
    // A title can reappear after leaving its page while its decode is still
    // pending. Reconstruct request ownership from the job list as well.
    for (auto& c : m_entries)
        for (const auto& p : m_pending)
            if (p.titleId == c.app.titleId)
                c.iconRequested = true;
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
    if (m_page == pg::Prefs::kNintendo)
        return; // fixed tile order
    if (m_page == pg::Prefs::kHomebrew) {
        std::stable_sort(m_entries.begin(), m_entries.end(), [](const CardEntry& a, const CardEntry& b) {
            return lowered(a.app.title) < lowered(b.app.title);
        });
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
        const int index = indexOf(it->titleId);
        if (index >= 0 && uploadBudget <= 0)
            break; // leave the rest (futures unconsumed) for the next frame
        try { it->future.get(); } catch (...) {}
        if (index >= 0) {
            auto& card = m_entries[static_cast<std::size_t>(index)];
            const auto& d = *it->decoded;
            if (!d.rgba.empty() && d.w > 0 && d.h > 0) {
                auto tex = std::make_unique<nxui::Texture>();
                if (tex->loadFromPixels(gpu, ren, d.rgba.data(), d.w, d.h))
                    card.icon = std::move(tex);
                else
                    card.iconFailed = true;
            } else {
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
        if (i >= first - keep && i <= last + keep)
            continue;
        auto& card = m_entries[static_cast<std::size_t>(i)];
        if (card.icon) {
            card.icon.reset();
            card.iconRequested = false;
        }
    }

    // Queue decodes for the visible window.
    for (int i = first; i <= last; ++i) {
        auto& card = m_entries[static_cast<std::size_t>(i)];
        if (card.icon || card.iconRequested || card.iconFailed)
            continue;
        card.iconRequested = true;
        PendingIcon p;
        p.titleId = card.app.titleId;
        p.decoded = std::make_shared<IconStreamer::DecodedIcon>();
        p.cancelled = std::make_shared<std::atomic<bool>>(false);
        auto decoded = p.decoded;
        auto cancelled = p.cancelled;
        const std::uint64_t titleId = p.titleId;
        const std::string iconFile = card.system ? card.iconFile : std::string();
        const std::string hbPath = card.isHomebrew ? card.homebrewPath : std::string();
        try {
            p.future = pool.submit([decoded, cancelled, titleId, iconFile, hbPath]() {
                if (cancelled->load())
                    return;
                if (!iconFile.empty()) {
                    // Nintendo page tile: a 256 px PNG from the assets.
                    nxui::DecodedImage img = nxui::Texture::decodeFile(iconFile, 256);
                    if (img.valid()) {
                        decoded->rgba = std::move(img.rgba);
                        decoded->w = img.width;
                        decoded->h = img.height;
                    }
                    return;
                }
#ifdef SWITCHU_MENU
                if (!hbPath.empty()) {
                    std::vector<std::uint8_t> bytes = readHomebrewIcon(hbPath);
                    if (!bytes.empty()) {
                        *decoded = IconStreamer::decodeIconData(bytes);
                        return;
                    }
                }
#endif
                *decoded = IconStreamer::decodeIconData(AppListLoader::loadIconData(titleId));
            });
        } catch (...) {
            card.iconRequested = false; // pool shutting down
            continue;
        }
        m_pending.push_back(std::move(p));
    }
}

void CardsCatalog::releaseAll() {
    for (auto& p : m_pending)
        if (p.cancelled)
            p.cancelled->store(true);
    for (auto& p : m_pending)
        if (p.future.valid())
            p.future.wait();
    m_pending.clear();
    for (auto& card : m_entries) {
        card.icon.reset();
        card.iconRequested = false;
    }
}

} // namespace cards
