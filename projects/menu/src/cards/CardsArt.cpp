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
// The background is drawn full screen under a dark wash; 640 wide is enough.
constexpr int kBackgroundMaxSide = 640;

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
    auto& slot = (kind == 2 ? m_logos : kind == 1 ? m_backgrounds : m_covers)[titleId];
    if (slot.tex || slot.requested || slot.missing)
        return;
    slot.requested = true;
    Job job;
    job.titleId = titleId;
    job.kind = kind;
    job.image = std::make_shared<nxui::DecodedImage>();
    job.cancelled = std::make_shared<std::atomic<bool>>(false);
    auto image = job.image;
    auto cancelled = job.cancelled;
    const auto contains = [titleId](const std::vector<std::uint64_t>& list) {
        return std::find(list.begin(), list.end(), titleId) != list.end();
    };
    const bool coverBg = m_config && contains(m_config->cards.coverBackgroundTitles);
    try {
        job.future = pool.submit([image, cancelled, titleId, kind, coverBg]() {
            if (cancelled->load())
                return;
            std::string path = kind == 2 ? SteamGridDbManager::logoPath(titleId)
                             : kind == 1 && !coverBg ? heroPath(titleId) : std::string();
            if (path.empty() && kind != 2)
                path = coverPath(titleId);
            if (path.empty() || cancelled->load())
                return;
            *image = nxui::Texture::decodeFile(path, kind == 1 ? kBackgroundMaxSide : kCoverMaxSide);
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
                slot.tex = std::move(tex);
                slot.shownAt = now;
            } else {
                slot.missing = true;
            }
            --uploadBudget;
        } else {
            slot.missing = true;
        }
        it = m_jobs.erase(it);
    }

    // Drop covers no longer near the focus; keep one background (the focused
    // title's) plus whatever is still on screen while it fades.
    for (auto it = m_covers.begin(); it != m_covers.end();) {
        const bool kept = std::find(keep.begin(), keep.end(), it->first) != keep.end();
        if (!kept && it->second.tex) {
            it = m_covers.erase(it);
            continue;
        }
        ++it;
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
    m_jobs.clear();
    m_covers.clear();
    m_backgrounds.clear();
    m_logos.clear();
}

} // namespace cards
