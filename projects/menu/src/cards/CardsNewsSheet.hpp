#pragma once
// News sheet for the Cards HOME (HomeNX G3, requirements C-NET-5, C-UI-6).
// Left: article list (title, topic and date, green unread dot). Right: the
// article (banner, title, source/date, text, scrollbar). Up/Down choose,
// ZL/ZR, L/R or the right stick scroll, B / Minus / HOME / touch outside close.
// Articles are read and banners decoded on the HomeCore pool (C-PF-2); only
// texture uploads happen on the main thread. Read state is kept in
// sdmc:/config/OmniLaunch/news_read.txt.
// Adapted from upstream/homenx @ 0448cd4, projects/cards/src/CardsActivity.cpp
// 6220-6433; see integration/provenance/homenx-intake.md.

#include "cards/CardsChrome.hpp"
#include "news/NewsStore.hpp"
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Types.hpp>
#include <atomic>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace home { class HomeCore; }
namespace nxui { class ThreadPool; }
class AudioManager;

namespace cards {

class CardsNewsSheet {
public:
    CardsNewsSheet() = default;
    ~CardsNewsSheet();

    /// `now` is the frontend clock in seconds; a list older than kCacheSec is read again.
    void open(home::HomeCore& core, CardsChrome& chrome, float now);
    void close();
    bool isOpen() const { return m_open; }

    bool handleInput(nxui::Input& in, float dt, CardsChrome& chrome, AudioManager& audio);
    void update(float dt, nxui::GpuDevice& gpu, nxui::Renderer& ren, nxui::ThreadPool& pool, CardsChrome& chrome,
                float now);
    void render(nxui::Renderer& ren, CardsChrome& chrome, float time) const;

    /// Drops textures and abandons pending decodes (onDestroy, before GPU teardown).
    void clearTextures();

private:
    static constexpr float kCacheSec = 120.f;
    static constexpr int kMaxPictures = 4;

    struct NewsItem {
        std::string id;
        std::string title;
        std::string topic; // channel name + date
        std::string body;
        std::string footer;
        bool unread = false;
        std::shared_ptr<const std::vector<std::uint8_t>> picture; // encoded JPEG
        bool pictureFailed = false;
        std::vector<std::string> titleLines;
        std::vector<std::string> bodyLines;
    };
    struct LoadState {
        std::mutex mu;
        bool ready = false;
        std::vector<NewsItem> items;
        std::string errorCode; // empty on success
    };
    struct Decode {
        int index = -1;
        std::shared_ptr<nxui::DecodedImage> image;
        std::shared_ptr<std::atomic<bool>> cancelled;
        std::future<void> future;
    };

    void finishLoad(CardsChrome& chrome, float now);
    void showItem(int index, CardsChrome& chrome);
    void pumpPictures(nxui::GpuDevice& gpu, nxui::Renderer& ren, nxui::ThreadPool& pool);
    void cancelDecodes();
    void layout(); // list smoothing and article scroll clamp, from update()

    bool m_open = false;
    float m_in = 0.f;
    bool m_loading = false;
    float m_loadedAt = -1e9f;
    std::string m_emptyMsg;
    bool m_shownSinceLoad = false; // selection/read marking deferred until the sheet is visible

    std::vector<NewsItem> m_items;
    int m_selected = 0;
    float m_listScroll = 0.f;
    float m_articleScroll = 0.f;
    float m_articleContent = 0.f; // measured in render, clamped in update

    bool m_dragging = false;
    float m_touchY = 0.f;
    float m_touchScroll = 0.f;

    // G3a: a text line is rasterized (SDL_ttf + one GPU upload) the first time
    // it is drawn. Opening the sheet or an article drew ~40 new lines in one
    // frame (41-65 ms on console), so at most this many new lines are made per
    // frame; lines already in the font cache are always drawn.
    static constexpr int kNewLinesPerFrame = 8;
    mutable int m_newLines = 0;

    std::shared_ptr<LoadState> m_loadState;
    std::future<void> m_loadFuture;
    std::vector<Decode> m_decodes;
    std::map<int, std::unique_ptr<nxui::Texture>> m_pictures;
};

} // namespace cards
