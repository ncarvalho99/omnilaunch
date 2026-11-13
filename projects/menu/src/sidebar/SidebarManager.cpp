#include "SidebarManager.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Input.hpp>
#include <filesystem>
#include <algorithm>
#include <system_error>

namespace {

constexpr int kSidebarIconCount = 10;
constexpr float kSidebarButtonSize = 70.f;
constexpr float kSidebarColumnGap = 16.f;
constexpr float kSidebarMarginX = 14.f;
constexpr float kDynamicStripButtonSize = 56.f;
constexpr float kDynamicStripGap = 10.f;
constexpr float kDynamicStripY = 17.f;
constexpr float kDynamicLeftGroupX = 190.f;
constexpr float kDynamicRightGroupX = 902.f;

bool pathExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty())
        return name;
    if (base.back() == '/')
        return base + name;
    return base + "/" + name;
}

} // namespace


void SidebarManager::build(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                           const std::string& assetsBase,
                           const Actions& actions,
                           const std::vector<int>& order) {
    const float btnSize = kSidebarButtonSize * m_columnScale;
    constexpr float gap     = kSidebarColumnGap;
    constexpr float marginX = kSidebarMarginX;

    float leftX  = marginX;
    float rightX = 1280.f - marginX - btnSize;
    float totalH = 5 * btnSize + 4.f * gap;
    float startY = 360.f - totalH * 0.5f;

    m_leftButtons.clear();
    m_rightButtons.clear();
    m_settingsButton = nullptr;
    m_themeShopButton = nullptr;
    m_albumButton    = nullptr;
    m_slotIds.assign(10, -1);
    m_hasDiscord     = (bool)actions.onDiscord;
    m_anims.clear();
    m_icons.clear();
    m_icons.resize(kSidebarIconCount);
    invalidateAssetsCache();

    auto makeBtn = [](nxui::Texture* tex, const std::string& labelKey,
                      const std::string& fallback, std::function<void()> action) {
        auto btn = std::make_shared<AppletButton>();
        btn->setIcon(tex);
        btn->setLabelKey(labelKey, fallback);
        btn->setAccessibilityLabel(fallback);
        btn->setAccessibilityRole(nxui::I18n::instance().tr("accessibility.roles.button", "button"));
        btn->setAccessibilityHint(nxui::I18n::instance().tr("accessibility.hints.open", "A to open."));
        btn->setOnActivate(std::move(action));
        btn->setFocusable(true);
        return btn;
    };

    struct Def {
        int id;
        const char* key;
        const char* fallback;
        std::function<void()> action;
    };
    const Def defs[kSidebarIconCount] = {
        {0, "sidebar.sphaira", "Sphaira", actions.onAlbum},
        {1, "sidebar.discord", "Discord", actions.onDiscord ? actions.onDiscord : actions.onMiiEditor},
        {2, "sidebar.news", "News", actions.onNews},
        {3, "sidebar.nextendo", "Nextendo", actions.onNextendo},
        {4, "sidebar.settings", "Settings", actions.onSettings},
        {5, "sidebar.toggle", "Cards", actions.onToggle},
        {6, "sidebar.controllers", "Controllers", actions.onControllers},
        {7, "sidebar.social", "Social", actions.onSocial},
        {8, "sidebar.theme_shop", "OmniLaunch", actions.onMiiverse},
        {9, "sidebar.power", "Power Options", actions.onSleep},
    };

    for (int slot = 0; slot < 10; ++slot) {
        const int id = (order.size() == 10 && order[slot] >= 0 && order[slot] < kSidebarIconCount)
                     ? order[slot] : slot;
        const auto& d = defs[id];
        m_slotIds[static_cast<std::size_t>(slot)] = d.id;
        auto btn = makeBtn(&m_icons[static_cast<std::size_t>(d.id)], d.key, d.fallback, d.action);
        if (d.id == 0) m_albumButton = btn.get();
        if (d.id == 4) m_settingsButton = btn.get();
        if (d.id == 8) m_themeShopButton = btn.get();

        if (slot < 5) {
            btn->setRect({leftX, startY + static_cast<float>(slot) * (btnSize + gap), btnSize, btnSize});
            m_leftButtons.push_back(std::move(btn));
        } else {
            btn->setRect({rightX, startY + static_cast<float>(slot - 5) * (btnSize + gap), btnSize, btnSize});
            m_rightButtons.push_back(std::move(btn));
        }
    }

    for (std::size_t i = 0; i < m_leftButtons.size(); ++i) {
        auto* btn = m_leftButtons[i].get();
        btn->setCustomNavigation(nxui::FocusDirection::UP,
                                 i > 0 ? m_leftButtons[i - 1].get() : btn);
        btn->setCustomNavigation(nxui::FocusDirection::DOWN,
                                 i + 1 < m_leftButtons.size() ? m_leftButtons[i + 1].get() : btn);
    }
    for (std::size_t i = 0; i < m_rightButtons.size(); ++i) {
        auto* btn = m_rightButtons[i].get();
        btn->setCustomNavigation(nxui::FocusDirection::UP,
                                 i > 0 ? m_rightButtons[i - 1].get() : btn);
        btn->setCustomNavigation(nxui::FocusDirection::DOWN,
                                 i + 1 < m_rightButtons.size() ? m_rightButtons[i + 1].get() : btn);
    }

    (void)gpu;
    (void)ren;
    (void)assetsBase;
}

void SidebarManager::setDynamicLineLayout(bool enabled) {
    if (enabled) {
        std::vector<AppletButton*> ordered;
        ordered.reserve(m_leftButtons.size() + m_rightButtons.size());
        for (auto& button : m_leftButtons) ordered.push_back(button.get());
        for (auto& button : m_rightButtons) ordered.push_back(button.get());
        if (ordered.empty()) return;

        for (auto* button : ordered) {
            button->setCustomNavigation(nxui::FocusDirection::LEFT, nullptr);
            button->setCustomNavigation(nxui::FocusDirection::RIGHT, nullptr);
            button->setCustomNavigation(nxui::FocusDirection::UP, nullptr);
            button->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
        }

        for (std::size_t i = 0; i < ordered.size(); ++i) {
            auto* button = ordered[i];
            const bool rightGroup = i >= m_leftButtons.size();
            const std::size_t groupIndex = rightGroup ? i - m_leftButtons.size() : i;
            const float groupX = rightGroup ? kDynamicRightGroupX : kDynamicLeftGroupX;
            button->setRect({groupX + static_cast<float>(groupIndex) *
                                      (kDynamicStripButtonSize + kDynamicStripGap),
                             kDynamicStripY,
                             kDynamicStripButtonSize,
                             kDynamicStripButtonSize});
            button->setCustomNavigation(nxui::FocusDirection::LEFT,
                                        i > 0 ? ordered[i - 1] : button);
            button->setCustomNavigation(nxui::FocusDirection::RIGHT,
                                        i + 1 < ordered.size() ? ordered[i + 1] : button);
        }
        return;
    }

    const float count = static_cast<float>(std::max(m_leftButtons.size(), m_rightButtons.size()));
    const float size = kSidebarButtonSize * m_columnScale;
    const float totalH = count * size + std::max(0.f, count - 1.f) * kSidebarColumnGap;
    const float startY = 360.f - totalH * 0.5f;
    const float rightX = 1280.f - kSidebarMarginX - size;
    for (std::size_t i = 0; i < m_leftButtons.size(); ++i) {
        m_leftButtons[i]->setRect({kSidebarMarginX,
                                   startY + static_cast<float>(i) * (size + kSidebarColumnGap),
                                   size, size});
    }
    for (std::size_t i = 0; i < m_rightButtons.size(); ++i) {
        m_rightButtons[i]->setRect({rightX,
                                    startY + static_cast<float>(i) * (size + kSidebarColumnGap),
                                    size, size});
    }
    for (std::size_t i = 0; i < m_leftButtons.size(); ++i) {
        auto* btn = m_leftButtons[i].get();
        btn->setCustomNavigation(nxui::FocusDirection::UP,
                                 i > 0 ? m_leftButtons[i - 1].get() : btn);
        btn->setCustomNavigation(nxui::FocusDirection::DOWN,
                                 i + 1 < m_leftButtons.size() ? m_leftButtons[i + 1].get() : btn);
        btn->setCustomNavigation(nxui::FocusDirection::LEFT, nullptr);
        btn->setCustomNavigation(nxui::FocusDirection::RIGHT, nullptr);
    }
    for (std::size_t i = 0; i < m_rightButtons.size(); ++i) {
        auto* btn = m_rightButtons[i].get();
        btn->setCustomNavigation(nxui::FocusDirection::UP,
                                 i > 0 ? m_rightButtons[i - 1].get() : btn);
        btn->setCustomNavigation(nxui::FocusDirection::DOWN,
                                 i + 1 < m_rightButtons.size() ? m_rightButtons[i + 1].get() : btn);
        btn->setCustomNavigation(nxui::FocusDirection::LEFT, nullptr);
        btn->setCustomNavigation(nxui::FocusDirection::RIGHT, nullptr);
    }
}

float SidebarManager::dynamicLineGapLeft() {
    constexpr float kGroupButtons = 3.f;
    return kDynamicLeftGroupX + kGroupButtons * kDynamicStripButtonSize +
           (kGroupButtons - 1.f) * kDynamicStripGap;
}

float SidebarManager::dynamicLineGapRight() {
    return kDynamicRightGroupX;
}

void SidebarManager::placeDynamicLine(float leftX, float rightEndX, float size, float gap, float centerY) {
    const float y = centerY - size * 0.5f;
    float x = leftX;
    for (auto& button : m_leftButtons) {
        button->setRect({x, y, size, size});
        x += size + gap;
    }
    m_placedLeftEnd = m_leftButtons.empty() ? leftX : x - gap;
    const float n = static_cast<float>(m_rightButtons.size());
    x = rightEndX - n * size - std::max(0.f, n - 1.f) * gap;
    m_placedRightStart = m_rightButtons.empty() ? rightEndX : x;
    for (auto& button : m_rightButtons) {
        button->setRect({x, y, size, size});
        x += size + gap;
    }
}

void SidebarManager::setDynamicLineDownAction(std::function<void()> action) {
    auto configure = [&action](std::shared_ptr<AppletButton>& button) {
        button->removeAction(static_cast<uint64_t>(nxui::Button::DDown));
        button->removeAction(static_cast<uint64_t>(nxui::Button::LStickD));
        button->removeAction(static_cast<uint64_t>(nxui::Button::RStickD));
        if (action) {
            button->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
            button->addDirectionAction(nxui::FocusDirection::DOWN, action);
        }
    };
    for (auto& button : m_leftButtons) configure(button);
    for (auto& button : m_rightButtons) configure(button);
}

void SidebarManager::setDynamicLineUpTarget(nxui::Widget* target) {
    for (auto& button : m_leftButtons)
        button->setCustomNavigation(nxui::FocusDirection::UP, target);
    for (auto& button : m_rightButtons)
        button->setCustomNavigation(nxui::FocusDirection::UP, target);
}

void SidebarManager::setDynamicLineProfileTargets(nxui::Widget* firstProfile,
                                                   nxui::Widget* lastProfile) {
    if (!m_leftButtons.empty())
        m_leftButtons.back()->setCustomNavigation(nxui::FocusDirection::RIGHT,
                                                  firstProfile);
    if (!m_rightButtons.empty())
        m_rightButtons.front()->setCustomNavigation(nxui::FocusDirection::LEFT,
                                                    lastProfile);
}

void SidebarManager::reloadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                  const std::string& assetsBase,
                                  const std::string& customIconsBase) {
    if (m_leftButtons.empty() || m_rightButtons.empty())
        return;

    if (m_assetsLoaded
        && m_loadedAssetsBase == assetsBase
        && m_loadedCustomIconsBase == customIconsBase) {
        DebugLog::log("[sidebar-anim] reload skipped: assets unchanged (custom=%s)",
                      customIconsBase.empty() ? "<empty>" : customIconsBase.c_str());
        return;
    }

    loadAssets(gpu, ren, assetsBase, customIconsBase);
}

void SidebarManager::invalidateAssetsCache() {
    m_loadedAssetsBase.clear();
    m_loadedCustomIconsBase.clear();
    m_assetsLoaded = false;
}

void SidebarManager::loadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                const std::string& assetsBase,
                                const std::string& customIconsBase) {
    gpu.waitIdle();

    std::string defaultIconsBase = joinPath(assetsBase, "icons");
    const bool useCustomStaticIcons = !customIconsBase.empty();
    auto defaultAssetPath = [&](const char* fileName) {
        return joinPath(defaultIconsBase, fileName);
    };
    auto resolveAsset = [&](const char* fileName) {
        if (!customIconsBase.empty()) {
            std::string customPath = joinPath(customIconsBase, fileName);
            if (pathExists(customPath))
                return customPath;
        }
        return defaultAssetPath(fileName);
    };
    auto loadIconTexture = [&](int iconIdx, const char* fileName) {
        if (useCustomStaticIcons) {
            std::string customPath = joinPath(customIconsBase, fileName);
            if (pathExists(customPath)) {
                if (m_icons[iconIdx].loadFromFile(gpu, ren, customPath))
                    return;
                DebugLog::log("[sidebar-assets] custom icon load failed, falling back: %s",
                              customPath.c_str());
            }
        }

        const std::string fallbackPath = defaultAssetPath(fileName);
        if (!m_icons[iconIdx].loadFromFile(gpu, ren, fallbackPath)) {
            DebugLog::log("[sidebar-assets] fallback icon load failed: %s",
                          fallbackPath.c_str());
        }
    };

    const char* iconFiles[] = {
        "sphaira.png", m_hasDiscord ? "discord.png" : "mii_editor.png", "news.png", "nextendo.png", "settings.png",
        "toggle.png", "controller.png", "friends.png", "themes.png", "power.png",
    };
    if ((int)m_icons.size() != kSidebarIconCount)
        m_icons.resize(kSidebarIconCount);
    for (int i = 0; i < kSidebarIconCount; ++i)
        loadIconTexture(i, iconFiles[i]);

    static const struct { int iconIdx; const char* webpFile; bool useFirstFrame; } animDefs[] = {
        { 0, "sphaira.webp",    false },
        { 1, "discord.webp",    false },
        { 4, "settings.webp",   false },
        { 6, "controller.webp", true  },
        { 8, "themes.webp",     false },
        { 9, "power.webp",      false },
    };

    m_anims.clear();
    if (useCustomStaticIcons) {
        DebugLog::log("[sidebar-anim] custom theme icons use PNG only; skipping WebP animations (%s)",
                      customIconsBase.c_str());
    }

    for (const auto& def : animDefs) {
        // The animation belongs to the entry, not to the slot it occupies:
        // with a custom order slot 1 may hold News while Discord sits in 2.
        AppletButton* btn = buttonForId(def.iconIdx);
        if (!btn)
            continue;

        nxui::Texture* staticTex = &m_icons[def.iconIdx];
        if (useCustomStaticIcons) {
            btn->setIcon(staticTex);
            continue;
        }

        nxui::Texture* idleTex = def.useFirstFrame ? nullptr : staticTex;
        tryLoadAnimation(gpu, ren, resolveAsset(def.webpFile), btn, idleTex);
        btn->setIcon(idleTex ? idleTex : staticTex);
    }

    m_loadedAssetsBase = assetsBase;
    m_loadedCustomIconsBase = customIconsBase;
    m_assetsLoaded = true;
}

AppletButton* SidebarManager::buttonForId(int id) const {
    for (std::size_t slot = 0; slot < m_slotIds.size(); ++slot) {
        if (m_slotIds[slot] != id)
            continue;
        if (slot < 5)
            return slot < m_leftButtons.size() ? m_leftButtons[slot].get() : nullptr;
        const std::size_t right = slot - 5;
        return right < m_rightButtons.size() ? m_rightButtons[right].get() : nullptr;
    }
    return nullptr;
}

void SidebarManager::tryLoadAnimation(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                      const std::string& webpPath,
                                      AppletButton* button,
                                      nxui::Texture* staticIcon) {
    AnimEntry entry;
    entry.button    = button;
    entry.staticTex = staticIcon;
    if (entry.anim.load(gpu, ren, webpPath)) {
        m_anims.push_back(std::move(entry));
    }
}

void SidebarManager::update(float dt, nxui::Widget* focusedWidget) {
    for (auto& e : m_anims) {
        if (!e.button) continue;
        bool focused = (focusedWidget == e.button);
        e.anim.update(dt, focused);
        if (focused && e.anim.hasFrames()) {
            e.button->setIcon(e.anim.currentFrame());
        } else {
            // staticTex == nullptr: use frame 0 of the animation as idle
            nxui::Texture* idle = e.staticTex ? e.staticTex
                                              : (e.anim.hasFrames() ? e.anim.currentFrame() : nullptr);
            e.button->setIcon(idle);
        }
    }
}


void SidebarManager::applyTheme(const nxui::Theme& theme) {
    auto apply = [&](std::shared_ptr<AppletButton>& btn) {
        btn->setBaseColor(theme.iconDefault);
        btn->setBorderColor(theme.panelBorder);
        btn->setHighlightColor(theme.panelHighlight);
        btn->setLiquidGlassEnabled(true);
        btn->setForceLiquidGlass(true);
        btn->setBlurEnabled(false);
        btn->setBorderWidth(2.2f);
    };
    for (auto& btn : m_leftButtons)  apply(btn);
    for (auto& btn : m_rightButtons) apply(btn);
}
