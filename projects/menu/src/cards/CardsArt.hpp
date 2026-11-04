#pragma once
// Card and background art for the Cards HOME (HomeNX D1b). Reads only what
// OmniLaunch already keeps on the card -- portrait covers (covers/, game_art/,
// sLaunch covers) and SteamGridDB heroes -- decoded on the pool and uploaded
// on the main thread. GPU textures belong to the frontend: releaseAll() runs
// in onDestroy. Nothing here touches the network.

#include <nxui/core/Texture.hpp>
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nxui { class GpuDevice; class Renderer; class ThreadPool; }

namespace cards {

class CardsArt {
public:
    /// Portrait cover for the card face, or nullptr while loading / absent.
    const nxui::Texture* cover(std::uint64_t titleId) const;
    /// Wide background (hero, else the cover), or nullptr.
    const nxui::Texture* background(std::uint64_t titleId) const;
    /// Seconds since the title's cover/background became visible (fade-in).
    float coverAge(std::uint64_t titleId, float now) const;
    float backgroundAge(std::uint64_t titleId, float now) const;

    /// Requests covers for `wanted` and the background for `focused`; uploads
    /// finished decodes (main thread, at most `uploadBudget`) and drops covers
    /// not in `keep`. `now` timestamps new textures for fades.
    void pump(nxui::GpuDevice& gpu, nxui::Renderer& ren, nxui::ThreadPool& pool,
              const std::vector<std::uint64_t>& wanted, const std::vector<std::uint64_t>& keep,
              std::uint64_t focused, float now, int uploadBudget);

    void releaseAll();

    /// Path helpers (also used for "has a cover" decisions). Empty if none.
    static std::string coverPath(std::uint64_t titleId);
    static std::string heroPath(std::uint64_t titleId);

private:
    struct Slot {
        std::unique_ptr<nxui::Texture> tex;
        float shownAt = 0.f;
        bool requested = false;
        bool missing = false;
    };
    struct Job {
        std::uint64_t titleId = 0;
        bool background = false;
        std::shared_ptr<nxui::DecodedImage> image;
        std::shared_ptr<std::atomic<bool>> cancelled;
        std::future<void> future;
    };

    void request(nxui::ThreadPool& pool, std::uint64_t titleId, bool background);

    std::unordered_map<std::uint64_t, Slot> m_covers;
    std::unordered_map<std::uint64_t, Slot> m_backgrounds;
    std::vector<Job> m_jobs;
};

} // namespace cards
