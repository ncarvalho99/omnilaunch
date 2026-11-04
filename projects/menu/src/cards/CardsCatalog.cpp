#include "cards/CardsCatalog.hpp"
#include "core/Config.hpp"
#include "core/DebugLog.hpp"
#include "launcher/AppListLoader.hpp"
#include <nxui/core/GpuDevice.hpp>
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
    m_entries.clear();
    m_entries.reserve(apps.size());
    for (auto& a : apps) {
        CardEntry c;
        c.app = std::move(a);
        m_entries.push_back(std::move(c));
    }
    sort(config);
    DebugLog::log("[cards] catalogue %zu titles (sort %d)", m_entries.size(), config.cardsSort);
    return true;
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
    // Ties keep last-opened order, so "most played" with no figures yet and
    // equal names stay where the owner expects them.
    auto recent = [&config](const CardEntry& a, const CardEntry& b) {
        return config.lastOpenedAt(a.app.titleId) > config.lastOpenedAt(b.app.titleId);
    };
    std::stable_sort(m_entries.begin(), m_entries.end(), recent);
    if (config.cardsSort == 1) {
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
        try {
            p.future = pool.submit([decoded, cancelled, titleId]() {
                if (cancelled->load())
                    return;
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
