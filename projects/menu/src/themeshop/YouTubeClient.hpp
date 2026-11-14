#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>

#include <nxui/core/Texture.hpp>

namespace nxui {
class ThreadPool;
class GpuDevice;
class Renderer;
}

class YouTubeClient {
public:
    struct TrackItem {
        std::string id;
        std::string title;
        std::string author;
        std::string duration;
        std::string thumbnailUrl;
        std::string streamUrl;
        bool isDownloaded = false;
        bool isDownloading = false;
        float downloadProgress = 0.0f;
        std::string localFilePath;
        std::string statusText;
    };

    enum class PreviewPhase {
        Idle,
        Loading,
        Downloaded,
        Ready,
        Failed
    };

    struct PreviewState {
        mutable std::mutex mutex;
        PreviewPhase phase = PreviewPhase::Idle;
        std::vector<std::uint8_t> bytes;
        nxui::Texture texture;
        int width = 0;
        int height = 0;
        std::uint64_t lastAccessed = 0;
    };

    using SearchCallback = std::function<void(bool success, const std::string& error)>;
    using StatusProgressCallback = std::function<void(const std::string& statusMessage, float progress01)>;
    using CompleteCallback = std::function<void(bool success, const std::string& path, const std::string& error)>;

    YouTubeClient();
    ~YouTubeClient();

    void setThreadPool(nxui::ThreadPool* pool) { m_pool = pool; }
    void setRenderContext(nxui::GpuDevice* gpu, nxui::Renderer* renderer) {
        m_gpu = gpu;
        m_renderer = renderer;
    }
    static constexpr const char* kDefaultBackendUrl = "https://ytdl.nclabs.dev";
    // An empty override (the config default) keeps the project backend.
    void setBackendUrl(const std::string& url) { m_backendUrl = url.empty() ? std::string(kDefaultBackendUrl) : url; }
    const std::string& backendUrl() const { return m_backendUrl; }

    bool isSearching() const { return m_isSearching.load(std::memory_order_relaxed); }
    const std::string& lastQuery() const { return m_lastQuery; }
    std::vector<TrackItem> tracksSnapshot() const;
    size_t trackCount() const;
    bool getTrack(size_t index, TrackItem& outTrack) const;
    const TrackItem* trackAt(size_t index) const;

    // Search YouTube
    void search(const std::string& query, SearchCallback cb = nullptr);
    void cancelSearch();

    // Thumbnail textures
    PreviewPhase thumbnailPhase(const std::string& videoId) const;
    const nxui::Texture* thumbnailTexture(const std::string& videoId) const;
    void primeThumbnail(const std::string& videoId, const std::string& thumbnailUrl);
    void updateThumbnailTextures(); // upload to GPU on render thread
    void trimThumbnailCache(size_t maxItems = 24);

    // Audio download
    bool downloadTrack(size_t trackIndex, StatusProgressCallback onProgress = nullptr, CompleteCallback onComplete = nullptr);

    // Check SD card for existing files & load installed library
    void loadInstalledTracks();
    void refreshDownloadedStatus();
    bool deleteTrack(size_t trackIndex);

    static std::string sanitizeFilename(const std::string& name);
    static std::string musicDirectory();

private:
    std::vector<TrackItem> searchBackend(const std::string& query);
    std::vector<TrackItem> searchInnerTube(const std::string& query);
    std::string resolveStreamUrl(const std::string& videoId, const std::string& title, StatusProgressCallback onProgress = nullptr);

    nxui::ThreadPool* m_pool = nullptr;
    nxui::GpuDevice* m_gpu = nullptr;
    nxui::Renderer* m_renderer = nullptr;
    std::string m_backendUrl = kDefaultBackendUrl;
    std::string m_lastQuery;
    std::atomic<bool> m_isSearching{false};
    std::atomic<bool> m_cancelRequested{false};

    mutable std::mutex m_tracksMutex;
    std::vector<TrackItem> m_tracks;

    mutable std::mutex m_thumbMutex;
    std::unordered_map<std::string, std::shared_ptr<PreviewState>> m_thumbnailCache;
    std::uint64_t m_accessCounter = 0;

    std::atomic<bool> m_isDownloading{false};
};
