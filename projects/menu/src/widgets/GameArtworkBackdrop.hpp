#pragma once

#include <nxui/core/Texture.hpp>
#include <nxui/widgets/Widget.hpp>

#include <future>
#include <memory>
#include <mutex>
#include <string>

namespace nxui {
class GpuDevice;
class Renderer;
class ThreadPool;
}

// A selected game's hero image. It stays below all home controls, so a custom
// background feels like artwork for that software rather than a new theme.
//
// The image is read and decoded on a worker, never on the render thread. Doing
// it inline cost about half a second every time the cursor landed on a game
// with custom art -- a cover is a megabyte or more off the card, and turning
// JPEG into pixels is tens of milliseconds on top of that. Only the upload is
// left in the frame, which is microseconds.
class GameArtworkBackdrop final : public nxui::Widget {
public:
    // Asks for a background. Returns immediately; the image appears a frame or
    // several later, once pollPendingArtwork has picked the decode up.
    void requestArtwork(nxui::ThreadPool& pool, const std::string& path);

    // Uploads a finished decode, if one is waiting. Call once per frame from
    // the render thread. Returns true when a new image was put on screen.
    bool pollPendingArtwork(nxui::GpuDevice& gpu, nxui::Renderer& ren);

    // Takes the device because releasing the outgoing texture has to wait for
    // the frame still sampling it. Focus moves call this on every step across
    // the grid, so the release cannot be left to chance.
    void clearArtwork(nxui::GpuDevice* gpu = nullptr);

    void setArtworkOpacityScale(float opacityScale) {
        m_artworkOpacityScale = std::clamp(opacityScale, 0.f, 1.f);
    }
    float artworkOpacityScale() const { return m_artworkOpacityScale; }

    // What is on screen, or what is on its way there. Compared against by the
    // caller to avoid asking for the same image twice, so a request already in
    // flight has to count.
    const std::string& artworkPath() const { return m_requestedPath; }

protected:
    void onRender(nxui::Renderer& ren) override;

private:
    // Shared with the worker, which may outlive a cancelled request.
    struct PendingDecode {
        std::mutex mutex;
        std::string path;
        nxui::DecodedImage image;
        bool finished = false;
    };

    nxui::Texture m_texture;
    float m_artworkOpacityScale = 0.50f;
    // The image currently uploaded. Empty until a decode lands.
    std::string m_path;
    // What was last asked for. Differs from m_path while a decode is running.
    std::string m_requestedPath;
    std::shared_ptr<PendingDecode> m_pending;
    std::future<void> m_pendingJob;
};
