#include "cards/CardsArt.hpp"
#include "core/DebugLog.hpp"
#include "core/Config.hpp"
#include "steamgriddb/SteamGridDbManager.hpp"
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

namespace cards {

namespace {

// Covers are 600x900 or larger; a card is 250x354 at most. Decoding at 512
// keeps one cover near 1 MB of GPU memory instead of 2-4 MB.
constexpr int kCoverMaxSide = 512;
// The background is drawn full screen; decode at 1280 for sharp 720p presentation.
constexpr int kBackgroundMaxSide = 1280;
// Snapshot covers only bridge the first frames; 320 px keeps a page file small.
constexpr int kSnapCoverMaxSide = 320;

bool isFile(const std::string& path) {
    struct stat st{};
    return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

} // namespace

std::string CardsArt::coverPath(std::uint64_t titleId) {
    if (titleId == 0)
        return {};
    char upper[17];
    char lower[17];
    std::snprintf(upper, sizeof(upper), "%016llX", static_cast<unsigned long long>(titleId));
    std::snprintf(lower, sizeof(lower), "%016llx", static_cast<unsigned long long>(titleId));
    // Same places, same order as the Classic flow covers (IconGrid.cpp
    // resolveFlowCoverPath): OmniLaunch covers, app covers, game_art, sLaunch.
    static const char* const kExts[] = {".jpg", ".png", ".jpeg"};
    for (const char* ext : kExts) {
        for (const char* id : {upper, lower}) {
            const std::string candidates[] = {
                std::string("sdmc:/config/OmniLaunch/covers/") + id + ext,
                std::string("sdmc:/switch/OmniLaunch/covers/") + id + ext,
                std::string("sdmc:/config/OmniLaunch/game_art/") + id + "/cover" + ext,
                std::string("sdmc:/slaunch/covers/") + id + ext,
            };
            for (const auto& p : candidates)
                if (isFile(p))
                    return p;
        }
    }
    const std::string sgdb = SteamGridDbManager::coverPath(titleId);
    return isFile(sgdb) ? sgdb : std::string();
}

std::string CardsArt::heroPath(std::uint64_t titleId) {
    const std::string p = SteamGridDbManager::heroPath(titleId);
    return isFile(p) ? p : std::string();
}

const nxui::Texture* CardsArt::cover(std::uint64_t titleId) const {
    auto it = m_covers.find(titleId);
    return it != m_covers.end() && it->second.tex && it->second.tex->valid() ? it->second.tex.get() : nullptr;
}

const nxui::Texture* CardsArt::background(std::uint64_t titleId) const {
    auto it = m_backgrounds.find(titleId);
    return it != m_backgrounds.end() && it->second.tex && it->second.tex->valid() ? it->second.tex.get()
                                                                                    : nullptr;
}

float CardsArt::coverAge(std::uint64_t titleId, float now) const {
    auto it = m_covers.find(titleId);
    return it != m_covers.end() && it->second.tex ? now - it->second.shownAt : 0.f;
}

float CardsArt::backgroundAge(std::uint64_t titleId, float now) const {
    auto it = m_backgrounds.find(titleId);
    return it != m_backgrounds.end() && it->second.tex ? now - it->second.shownAt : 0.f;
}

void CardsArt::request(nxui::ThreadPool& pool, std::uint64_t titleId, int kind) {
    // Rapid navigation cannot grow the worker queue indefinitely; cancelled
    // decodes still finishing count against the same budget.
    if (m_jobs.size() + m_retired.size() >= 16) return;
    auto& slot = (kind == 2 ? m_logos : kind == 1 ? m_backgrounds : m_covers)[titleId];
    // A snapshot cover is decoded again from its source, and replaced.
    if ((slot.tex && slot.fresh) || slot.requested || slot.missing)
        return;
    slot.requested = true;
    Job job;
    job.titleId = titleId;
    job.kind = kind;
    job.image = std::make_shared<nxui::DecodedImage>();
    job.cancelled = std::make_shared<std::atomic<bool>>(false);
    if (kind == 0)
        job.snap = std::make_shared<SnapItem>();
    auto image = job.image;
    auto snap = job.snap;
    auto cancelled = job.cancelled;
    const auto contains = [titleId](const std::vector<std::uint64_t>& list) {
        return std::find(list.begin(), list.end(), titleId) != list.end();
    };
    const bool coverBg = m_config && contains(m_config->cards.coverBackgroundTitles);
    try {
        job.future = pool.submit([image, snap, cancelled, titleId, kind, coverBg]() {
            if (cancelled->load())
                return;
            std::string path = kind == 2 ? SteamGridDbManager::logoPath(titleId)
                             : kind == 1 ? (coverBg ? coverPath(titleId) : heroPath(titleId))
                             : coverPath(titleId);
            if (path.empty() || cancelled->load())
                return;
            *image = nxui::Texture::decodeFile(path, kind == 1 ? kBackgroundMaxSide : kCoverMaxSide);
            // Cover pixels for the page snapshot, reduced and made off the UI thread.
            if (snap && image->valid() && !cancelled->load())
                *snap = makeSnapItem(titleId, 1, image->rgba.data(), image->width, image->height, kSnapCoverMaxSide);
        });
    } catch (...) {
        slot.requested = false; // pool shutting down
        return;
    }
    m_jobs.push_back(std::move(job));
}

void CardsArt::pump(nxui::GpuDevice& gpu, nxui::Renderer& ren, nxui::ThreadPool& pool,
                    const std::vector<std::uint64_t>& wanted, const std::vector<std::uint64_t>& keep,
                    std::uint64_t focused, float now, int uploadBudget) {
    // Cancelled decodes are collected once they end, never waited for here.
    for (auto it = m_retired.begin(); it != m_retired.end();) {
        if (!it->valid() || it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try { if (it->valid()) it->get(); } catch (...) {}
            it = m_retired.erase(it);
        } else {
            ++it;
        }
    }
    // Uploads. A future is consumed only when its result is uploaded now.
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
        if (!it->future.valid() ||
            it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        if (uploadBudget <= 0)
            break;
        try { it->future.get(); } catch (...) {}
        auto& slot = (it->kind == 2 ? m_logos : it->kind == 1 ? m_backgrounds : m_covers)[it->titleId];
        slot.requested = false;
        if (it->image->valid()) {
            auto tex = std::make_unique<nxui::Texture>();
            if (tex->loadFromDecoded(gpu, ren, *it->image)) {
                if (!slot.tex)
                    slot.shownAt = now; // replacing a snapshot cover: no second fade
                slot.tex = std::move(tex);
                slot.fresh = true;
                if (it->snap && it->snap->rgba)
                    slot.snap = it->snap;
            } else {
                slot.tex.reset();
                slot.snap.reset();
                slot.missing = true;
            }
            --uploadBudget;
        } else {
            // No cover any more: a snapshot cover must not stay.
            slot.tex.reset();
            slot.snap.reset();
            slot.missing = true;
        }
        it = m_jobs.erase(it);
    }

    // Pages are projections, not cache lifetimes. Keep recently used covers
    // across Games/Homebrew/Media/Applets; evict only past the session budget.
    for (auto id : keep) {
        const auto it = m_covers.find(id);
        if (it != m_covers.end()) it->second.usedAt = now;
    }
    auto resident = std::count_if(m_covers.begin(), m_covers.end(),
        [](const auto& entry) { return bool(entry.second.tex); });
    while (resident > 48 || m_covers.size() > 128) {
        auto victim = m_covers.end();
        for (auto it = m_covers.begin(); it != m_covers.end(); ++it) {
            if (it->second.requested || (resident > 48 && !it->second.tex) ||
                std::find(keep.begin(), keep.end(), it->first) != keep.end()) continue;
            if (victim == m_covers.end() || it->second.usedAt < victim->second.usedAt)
                victim = it;
        }
        if (victim == m_covers.end()) break;
        if (victim->second.tex) --resident;
        m_covers.erase(victim);
    }
    // Once the focused title's background has finished fading in (0.6 s), the
    // previous ones are no longer drawn.
    const bool focusedSettled = focused != 0 && background(focused) &&
                                backgroundAge(focused, now) > 0.6f;
    for (auto it = m_backgrounds.begin(); it != m_backgrounds.end();) {
        if (it->first != focused &&
            (focusedSettled || (!it->second.tex && !it->second.requested))) {
            it = m_backgrounds.erase(it);
            continue;
        }
        ++it;
    }

    auto has = [](const std::vector<std::uint64_t>& v, std::uint64_t id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    };
    for (std::uint64_t id : wanted)
        if (!m_config || !has(m_config->cards.iconTitles, id))
            request(pool, id, 0);
    if (focused != 0) {
        if (!m_config || !has(m_config->cards.plainBackgroundTitles, focused))
            request(pool, focused, 1);
        if (m_config && m_config->cards.titlePics && has(m_config->cards.logoTitles, focused))
            request(pool, focused, 2);
    }
    for (auto it = m_logos.begin(); it != m_logos.end();)
        if (it->first != focused && !it->second.requested) it = m_logos.erase(it);
        else ++it;
}

bool CardsArt::adoptSnapshotCover(nxui::GpuDevice& gpu, nxui::Renderer& ren, const SnapItem& item, float now) {
    if (item.kind != 1 || !item.rgba)
        return false;
    if (m_config && std::find(m_config->cards.iconTitles.begin(), m_config->cards.iconTitles.end(), item.titleId) !=
                        m_config->cards.iconTitles.end())
        return false; // this card shows its icon, not a cover
    auto& slot = m_covers[item.titleId];
    if (slot.tex || slot.missing)
        return false;
    auto tex = std::make_unique<nxui::Texture>();
    if (!tex->loadFromPixels(gpu, ren, item.rgba->data(), item.w, item.h))
        return false;
    slot.tex = std::move(tex);
    slot.fresh = false;
    slot.shownAt = now - 10.f; // already "faded in": the card opens with its cover
    return true;
}

CardsArt::SnapState CardsArt::coverSnapshot(std::uint64_t titleId, std::shared_ptr<const SnapItem>& out) const {
    if (m_config && std::find(m_config->cards.iconTitles.begin(), m_config->cards.iconTitles.end(), titleId) !=
                        m_config->cards.iconTitles.end())
        return SnapState::None;
    const auto it = m_covers.find(titleId);
    if (it == m_covers.end())
        return SnapState::Pending;
    const Slot& slot = it->second;
    if (slot.missing)
        return SnapState::None;
    if (!slot.tex || !slot.fresh || !slot.snap)
        return SnapState::Pending;
    out = slot.snap;
    return SnapState::Ready;
}

const nxui::Texture* CardsArt::logo(std::uint64_t titleId) const {
    const auto it = m_logos.find(titleId);
    return it == m_logos.end() ? nullptr : it->second.tex.get();
}

void CardsArt::releaseAll() {
    for (auto& j : m_jobs)
        if (j.cancelled)
            j.cancelled->store(true);
    for (auto& j : m_jobs)
        if (j.future.valid())
            j.future.wait();
    for (auto& f : m_retired)
        if (f.valid())
            f.wait();
    m_jobs.clear();
    m_retired.clear();
    m_covers.clear();
    m_backgrounds.clear();
    m_logos.clear();
}

void CardsArt::invalidate() {
    ++m_generation;
    for (auto& j : m_jobs) {
        if (j.cancelled)
            j.cancelled->store(true);
        if (j.future.valid())
            m_retired.push_back(std::move(j.future));
    }
    m_jobs.clear();
    m_covers.clear();
    m_backgrounds.clear();
    m_logos.clear();
}

} // namespace cards
