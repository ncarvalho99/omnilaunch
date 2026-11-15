#pragma once
// Classic's music player: the Cards music sheet, hosted in Classic's overlay
// layer, so both frontends show one player with the same song, lists and
// controls. Replaces the old Classic "Multimedia Center" panel, which kept its
// own copy of the playlist and could show another song than the one playing.

#include "cards/CardsChrome.hpp"
#include "cards/CardsMusicSheet.hpp"
#include <nxui/core/Input.hpp>
#include <nxui/widgets/Widget.hpp>
#include <functional>

struct AppConfig;
namespace home { class HomeAudio; }
namespace nxui { class GpuDevice; class Renderer; class ThreadPool; }

namespace media {

class MusicPlayerOverlay final : public nxui::Widget {
public:
    MusicPlayerOverlay(nxui::GpuDevice& gpu, nxui::Renderer& ren, home::HomeAudio& service,
                       AppConfig& config, nxui::ThreadPool& pool, std::string assetsRoot);

    void show();
    void hide();
    bool isActive() const { return m_sheet.isOpen(); }
    // Full-screen and always in the overlay layer: closed, it must not take
    // part in focus search.
    bool isFocusable() const override { return isActive() && nxui::Widget::isFocusable(); }

    void handleInput(const nxui::Input& input, float dt);
    void handleTouch(const nxui::Input&) {} // handled inside handleInput, as in Cards
    bool isFocusedOnPlaylist() const { return false; }

    cards::CardsMusicSheet& sheet() { return m_sheet; }
    void onClose(std::function<void()> cb) { m_onClose = std::move(cb); }

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    nxui::GpuDevice& m_gpu;
    nxui::Renderer& m_ren;
    home::HomeAudio& m_service;
    AppConfig& m_config;
    nxui::ThreadPool& m_pool;
    std::string m_assets;
    cards::CardsChrome m_chrome;
    bool m_chromeLoaded = false;
    bool m_waitRelease = false; // the press that opened it is still held
    cards::CardsMusicSheet m_sheet;
    std::function<void()> m_onClose;
};

} // namespace media
