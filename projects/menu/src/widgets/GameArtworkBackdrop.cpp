#include "GameArtworkBackdrop.hpp"

#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/ThreadPool.hpp>

#include <algorithm>
#include <utility>

namespace {
// The backdrop covers the screen, so there is nothing to gain above this.
constexpr int kBackdropMaxSide = 1280;
} // namespace

void GameArtworkBackdrop::requestArtwork(nxui::ThreadPool& pool, const std::string& path) {
    if (path == m_requestedPath)
        return;
    if (path.empty()) {
        clearArtwork();
        return;
    }

    m_requestedPath = path;

    // The previous request is abandoned rather than waited for: moving the
    // cursor quickly across the grid asks for several backgrounds in a row and
    // only the last one is ever shown. The worker keeps writing into its own
    // shared state, which nobody reads again, and it dies with the shared_ptr.
    m_pending = std::make_shared<PendingDecode>();
    m_pending->path = path;
    auto pending = m_pending;
    m_pendingJob = pool.submit([pending, path]() {
        nxui::DecodedImage image = nxui::Texture::decodeFile(path, kBackdropMaxSide);
        std::lock_guard<std::mutex> lk(pending->mutex);
        pending->image = std::move(image);
        pending->finished = true;
    });
}

bool GameArtworkBackdrop::pollPendingArtwork(nxui::GpuDevice& gpu, nxui::Renderer& ren) {
    if (!m_pending)
        return false;

    nxui::DecodedImage image;
    std::string path;
    {
        std::lock_guard<std::mutex> lk(m_pending->mutex);
        if (!m_pending->finished)
            return false;
        image = std::move(m_pending->image);
        path  = m_pending->path;
    }
    m_pending.reset();

    // Overtaken while decoding: the cursor moved on and a newer request is
    // already running. Dropping this one is the whole point of not waiting.
    if (path != m_requestedPath)
        return false;

    if (!image.valid()) {
        clearArtwork(&gpu);
        return false;
    }

    nxui::Texture loaded;
    if (!loaded.loadFromDecoded(gpu, ren, image)) {
        clearArtwork(&gpu);
        return false;
    }
    // Texture's move assignment waits for the queue before it retires what it
    // replaces, so the frame still sampling the outgoing image is finished with
    // it by the time its memory goes back.
    m_texture = std::move(loaded);
    m_path = path;
    return true;
}

void GameArtworkBackdrop::clearArtwork(nxui::GpuDevice* gpu) {
    m_pending.reset();
    if (gpu && m_texture.valid())
        gpu->waitIdle();
    m_texture = nxui::Texture{};
    m_path.clear();
    m_requestedPath.clear();
}

void GameArtworkBackdrop::onRender(nxui::Renderer& ren) {
    if (!m_texture.valid() || opacity() <= 0.f)
        return;
    const nxui::Rect dest = rect();
    const float sourceAspect = static_cast<float>(m_texture.width()) / m_texture.height();
    const float destAspect = dest.width / dest.height;
    nxui::Rect source{0.f, 0.f, static_cast<float>(m_texture.width()), static_cast<float>(m_texture.height())};
    if (sourceAspect > destAspect) {
        source.width = source.height * destAspect;
        source.x = (m_texture.width() - source.width) * 0.5f;
    } else {
        source.height = source.width / destAspect;
        source.y = (m_texture.height() - source.height) * 0.5f;
    }
    ren.drawTextureSub(&m_texture, source, dest, nxui::Color::white().withAlpha(m_artworkOpacityScale * opacity()));
    ren.drawGradientRect(dest, nxui::Color(0.01f, 0.02f, 0.04f, 0.20f * opacity()),
                         nxui::Color(0.01f, 0.02f, 0.04f, 0.62f * opacity()));
}
