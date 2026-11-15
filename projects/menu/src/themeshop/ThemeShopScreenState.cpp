#include "ThemeShopScreen.hpp"
#include "ThemeShopTabBuilders.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <algorithm>

void ThemeShopScreen::buildTabs() {
    DebugLog::log("[themeshop] buildTabs() start");
    m_tabs.clear();
    DebugLog::log("[themeshop]   InstalledTab...");
    m_tabs.push_back(themeshop::tabs::InstalledTab::build(*this));
    // Animados primeiro: sao os deste projeto e os que a maioria vem buscar.
    // Os estaticos do PoloNX ficam logo depois.
    DebugLog::log("[themeshop]   AnimatedTab...");
    m_tabs.push_back(themeshop::tabs::AnimatedTab::build(*this));
    // Static themes live inside the Themes tab now (its Animated / Static
    // switch); the Icons store takes the slot the static tab had.
    DebugLog::log("[themeshop]   IconsTab...");
    m_tabs.push_back(themeshop::tabs::IconsTab::build(*this));
    DebugLog::log("[themeshop]   MusicTab...");
    m_tabs.push_back(themeshop::tabs::MusicTab::build(*this));
    DebugLog::log("[themeshop]   SteamGridDbTab...");
    m_tabs.push_back(themeshop::tabs::SteamGridDbTab::build(*this));
    DebugLog::log("[themeshop]   OptionsTab...");
    m_tabs.push_back(themeshop::tabs::OptionsTab::build(*this));
    DebugLog::log("[themeshop]   OmniLaunchTab...");
    m_tabs.push_back(themeshop::tabs::OmniLaunchTab::build(*this));
    DebugLog::log("[themeshop]   UpdateTab...");
    m_tabs.push_back(themeshop::tabs::UpdateTab::build(*this));
    DebugLog::log("[themeshop]   UninstallTab...");
    m_tabs.push_back(themeshop::tabs::UninstallTab::build(*this));
    DebugLog::log("[themeshop] buildTabs() done (%d tabs)", (int)m_tabs.size());
    // Quanto do orcamento de imagem sobra para as previas. Sem este numero,
    // uma previa que nao aparece nao se distingue de uma previa que nao coube.
    if (m_gpu)
        DebugLog::log("[themeshop] imagem em %.1f de %.1f MB ao abrir",
                      m_gpu->imageMemoryUsed() / 1048576.0,
                      nxui::GpuDevice::imageBudget() / 1048576.0);

    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());

    // TabbedOverlayScreen owns the presentation refresh after every build:
    // warmup(), refreshTranslations(), and rebuildCurrentTab() all restore the
    // selected tab before rebuilding its widgets.  Rebuilding here used the
    // old Update-tab selection and then rebuildCurrentTab() immediately did
    // the same work again.  Keep this method to data/cache construction only.
}

// The sidebar order and names arrive after the tabs were first built (the
// shop is created once, warmed up, then fed state on every open). Without
// this the Options tab kept its empty first build: no sidebar rows.
void ThemeShopScreen::rebuildOptionsTab() {
    for (std::size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].key != "options")
            continue;
        m_tabs[i] = themeshop::tabs::OptionsTab::build(*this);
        if (i < m_cachedTabContentWidgets.size())
            m_cachedTabContentWidgets[i].clear();
        if (static_cast<int>(i) == m_tabIndex)
            refreshState();
        return;
    }
}

void ThemeShopScreen::setAppearanceChoice(const AppearanceChoice& c) {
    m_appearance = c;
    m_appearance.cornerRoundness = std::clamp(c.cornerRoundness, 0.f, 1.f);
    m_panelHsva = colorhex::fromColor(c.panelColor, m_panelHsva.h);
    m_textHsva = colorhex::fromColor(c.textColor, m_textHsva.h);
    for (std::size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].name != "OmniLaunch")
            continue;
        m_tabs[i] = themeshop::tabs::OmniLaunchTab::build(*this);
        if (i < m_cachedTabContentWidgets.size())
            m_cachedTabContentWidgets[i].clear();
        if (static_cast<int>(i) == m_tabIndex)
            refreshState();
        return;
    }
}

void ThemeShopScreen::publishAppearance() {
    if (m_appearanceCb)
        m_appearanceCb(m_appearance);
}

void ThemeShopScreen::refreshAppearanceHexRows() {
    const std::string panelHex = colorhex::format(m_appearance.panelColor);
    const std::string textHex = colorhex::format(m_appearance.textColor, false);
    for (auto& tab : m_tabs) {
        if (tab.name != "OmniLaunch")
            continue;
        for (auto& row : tab.items) {
            if (row.key == "appearance.panel_hex")
                row.buttonLabel = panelHex;
            else if (row.key == "appearance.text_hex")
                row.buttonLabel = textHex;
        }
    }
}
