#pragma once
// Card and background art for the Cards HOME (HomeNX D1b). Reads only what
// OmniLaunch already keeps on the card -- portrait covers (covers/, game_art/,
// sLaunch covers) and SteamGridDB heroes -- decoded on the pool and uploaded
// on the main thread. GPU textures belong to the frontend: releaseAll() runs
// in onDestroy. Nothing here touches the network.

#include "cards/CardsSnapshot.hpp"
#include <nxui/core/Texture.hpp>
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct AppConfig;

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
    /// outside `keep` only when the 48-cover session limit is exceeded.
    /// `now` timestamps new textures for fades and least-recent use.
    void pump(nxui::GpuDevice& gpu, nxui::Renderer& ren, nxui::ThreadPool& pool,
              const std::vector<std::uint64_t>& wanted, const std::vector<std::uint64_t>& keep,
              std::uint64_t focused, float now, int uploadBudget);

    /// Page snapshot (CardsSnapshot): shows a saved cover at once, without
    /// fade, until the fresh decode replaces it. Ignored when the title has a
    /// cover already, has none, or is set to show its icon.
    /// Returns true when a texture was uploaded.
    bool adoptSnapshotCover(nxui::GpuDevice& gpu, nxui::Renderer& ren, const SnapItem& item, float now);
    enum class SnapState { None, Pending, Ready };
    /// Whether the title's cover can go into a snapshot: Ready (fresh decode,
    /// `out` set), Pending (not decoded from its source yet) or None.
    SnapState coverSnapshot(std::uint64_t titleId, std::shared_ptr<const SnapItem>& out) const;

    void setConfig(const AppConfig* config) { m_config = config; }
    const nxui::Texture* logo(std::uint64_t titleId) const;
    /// Teardown: cancels and waits for every decode, then drops all textures.
    void releaseAll();
    /// Live refresh (art downloaded, art settings changed): drops all textures
    /// and cancels the decodes without waiting. A decode already running keeps
    /// its own buffers and is collected by pump() once it ends; its result is
    /// discarded.
    void invalidate();

    /// Path helpers (also used for "has a cover" decisions). Empty if none.
    static std::string coverPath(std::uint64_t titleId);
    static std::string heroPath(std::uint64_t titleId);

private:
    struct Slot {
        std::unique_ptr<nxui::Texture> tex;
        float shownAt = 0.f;
        float usedAt = 0.f;
        bool requested = false;
        bool missing = false;
        bool fresh = false; // tex decoded from its source (not the snapshot)
        std::shared_ptr<const SnapItem> snap; // covers: pixels for the next snapshot
    };
    struct Job {
        std::uint64_t titleId = 0;
        int kind = 0; // 0 cover, 1 background, 2 logo
        std::shared_ptr<nxui::DecodedImage> image;
        std::shared_ptr<SnapItem> snap;
        std::shared_ptr<std::atomic<bool>> cancelled;
        std::future<void> future;
    };

    void request(nxui::ThreadPool& pool, std::uint64_t titleId, int kind);

    std::unordered_map<std::uint64_t, Slot> m_covers;
    std::unordered_map<std::uint64_t, Slot> m_backgrounds;
    std::unordered_map<std::uint64_t, Slot> m_logos;
    const AppConfig* m_config = nullptr;
    std::vector<Job> m_jobs;
    std::vector<std::future<void>> m_retired; // cancelled by invalidate(), not yet finished
};

} // namespace cards
