#pragma once
#include "SidebarAnimation.hpp"
#include "widgets/AppletButton.hpp"
#include <nxui/core/Texture.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <nxui/Theme.hpp>
#include <nxui/widgets/Widget.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class SidebarManager {
public:
    struct Actions {
        std::function<void()> onAlbum;
        std::function<void()> onMiiEditor;
        std::function<void()> onControllers;
        std::function<void()> onSleep;
        std::function<void()> onSettings;
        std::function<void()> onMiiverse;
        std::function<void()> onDiscord;
        std::function<void()> onNews;
        std::function<void()> onNextendo;
        std::function<void()> onToggle;
        std::function<void()> onSocial;
    };

    void build(nxui::GpuDevice& gpu, nxui::Renderer& ren,
               const std::string& assetsBase, const Actions& actions,
               const std::vector<int>& order = {});

    void reloadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                      const std::string& assetsBase,
                      const std::string& customIconsBase = std::string());
    void invalidateAssetsCache();

    void update(float dt, nxui::Widget* focusedWidget);

    // With a pool, the static icons are read and decoded on a worker and
    // uploaded by pumpAssets() on the render thread; without one, inline.
    void setThreadPool(nxui::ThreadPool* pool) { m_pool = pool; }
    // Render thread, once per frame: uploads icons a worker has finished.
    void pumpAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren);

    // The dynamic-line HOME layout turns the two side columns into one
    // horizontal control strip between the profile bar and the applications.
    void setDynamicLineLayout(bool enabled);
    // Player size of the side columns (0.8..1.3), used by the next
    // setDynamicLineLayout(false). The one-row strip sizes itself to fit.
    void setColumnScale(float scale) { m_columnScale = scale < 0.8f ? 0.8f : (scale > 1.3f ? 1.3f : scale); }
    // Where the left group of that strip ends and the right one begins: the
    // span the top HUD has for the profiles and whatever sits beside them.
    static float dynamicLineGapLeft();
    static float dynamicLineGapRight();
    // Places the strip computed by the top HUD: the left group starts at
    // `leftX`, the right group ends at `rightEndX`, buttons `size` wide with
    // `gap` between them, centred on `centerY`. Navigation is unchanged.
    void placeDynamicLine(float leftX, float rightEndX, float size, float gap, float centerY);
    // Where the placed groups end/start (valid after placeDynamicLine).
    float placedLeftEnd() const { return m_placedLeftEnd; }
    float placedRightStart() const { return m_placedRightStart; }
    void setDynamicLineDownAction(std::function<void()> action);
    void setDynamicLineUpTarget(nxui::Widget* target);
    void setDynamicLineProfileTargets(nxui::Widget* firstProfile,
                                      nxui::Widget* lastProfile);

    void applyTheme(const nxui::Theme& theme);

    std::vector<std::shared_ptr<AppletButton>>& leftButtons()  { return m_leftButtons; }
    std::vector<std::shared_ptr<AppletButton>>& rightButtons() { return m_rightButtons; }
    const std::vector<std::shared_ptr<AppletButton>>& leftButtons() const  { return m_leftButtons; }
    const std::vector<std::shared_ptr<AppletButton>>& rightButtons() const { return m_rightButtons; }

    AppletButton*  albumButton()    const { return m_albumButton; }
    nxui::Widget*  settingsButton() const { return m_settingsButton; }
    nxui::Widget*  themeShopButton() const { return m_themeShopButton; }
    // Button showing sidebar entry `id` (0 Sphaira .. 9 Power) wherever the
    // user's order placed it; nullptr before build().
    AppletButton*  buttonForId(int id) const;

private:
    void loadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                    const std::string& assetsBase,
                    const std::string& customIconsBase);
    void tryLoadAnimation(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                          const std::string& webpPath, AppletButton* button,
                          nxui::Texture* staticIcon);

    // One worker decode of the static icons. Shared with the task, so the
    // task never touches this object; a newer request bumps m_iconGeneration
    // and the older result is dropped.
    struct IconDecode {
        std::vector<nxui::DecodedImage> images;
        std::atomic<bool> done{false};
        std::uint32_t generation = 0;
        std::uint64_t startTick = 0;
    };
    std::shared_ptr<IconDecode> m_iconDecode;
    std::uint32_t m_iconGeneration = 0;
    nxui::ThreadPool* m_pool = nullptr;

    std::vector<nxui::Texture> m_icons;
    std::vector<std::shared_ptr<AppletButton>> m_leftButtons;
    std::vector<std::shared_ptr<AppletButton>> m_rightButtons;
    AppletButton*  m_albumButton    = nullptr;
    nxui::Widget*  m_settingsButton = nullptr;
    nxui::Widget*  m_themeShopButton = nullptr;

    struct AnimEntry {
        SidebarAnimation anim;
        AppletButton*    button    = nullptr;
        nxui::Texture*   staticTex = nullptr;
    };
    std::vector<AnimEntry> m_anims;
    std::vector<int> m_slotIds; // slot -> entry id, filled by build()
    std::string m_loadedAssetsBase;
    std::string m_loadedCustomIconsBase;
    bool m_assetsLoaded = false;
    bool m_hasDiscord = false;
    float m_placedLeftEnd = 0.f;
    float m_placedRightStart = 1280.f;
    float m_columnScale = 1.f;
};
