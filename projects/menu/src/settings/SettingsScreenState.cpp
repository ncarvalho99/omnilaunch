#include "SettingsScreen.hpp"
#include "tabs/TabBuilders.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <chrono>

namespace {
constexpr int kTabCount = 10;
constexpr int kBluetoothTab = 7;
} // namespace

SettingsScreen::~SettingsScreen() {
    // A worker may still be building a tab that captures this screen.
    waitTabLoaders();
}

void SettingsScreen::waitTabLoaders() {
    for (auto& task : m_tabTasks)
        if (task.valid())
            task.wait();
}

// Registers the tabs by name only; their items (some need blocking service
// queries: storage sizes, network, Bluetooth) are built off the UI thread the
// first time a tab is shown, and the rest are prefetched one per frame.
void SettingsScreen::buildTabs() {
    waitTabLoaders();
    auto& i18n = nxui::I18n::instance();
    m_tabNames = {
        i18n.tr("settings.tabs.system", "System"),
        i18n.tr("settings.tabs.accessibility", "Accessibility"),
        i18n.tr("settings.tabs.storage", "Storage"),
        i18n.tr("settings.tabs.audio", "Audio"),
        i18n.tr("settings.tabs.display", "Display"),
        i18n.tr("settings.tabs.internet", "Internet"),
        i18n.tr("settings.tabs.controllers", "Controllers"),
        i18n.tr("settings.tabs.bluetooth", "Bluetooth"),
        i18n.tr("settings.tabs.sleep", "Sleep"),
        i18n.tr("settings.tabs.about", "About"),
    };
    m_tabs.clear();
    m_tabTasks.clear();
    m_tabTasks.resize(kTabCount);
    m_loadedTabs.assign(kTabCount, false);
    m_loadingTabs.assign(kTabCount, false);
    m_nextPrefetchTab = 0;
    for (int i = 0; i < kTabCount; ++i)
        m_tabs.push_back(makeLoadingTab(i));

    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());
    DebugLog::log("[settings] registered %d lazy tabs", kTabCount);

    if (m_tabBar) rebuildTabBar();
    if (m_tabContent) rebuildContentItems();
}

SettingsScreen::Tab SettingsScreen::buildTabNow(int tabIndex) {
    switch (tabIndex) {
    case 0: return settings::tabs::SystemTab::build(*this);
    case 1: return settings::tabs::AccessibilityTab::build(*this);
    case 2: return settings::tabs::StorageTab::build(*this);
    case 3: return settings::tabs::AudioTab::build(*this);
    case 4: return settings::tabs::DisplayTab::build(*this);
    case 5: return settings::tabs::InternetTab::build(*this);
    case 6: return settings::tabs::ControllersTab::build(*this);
    case 7: return settings::tabs::BluetoothTab::build(*this);
    case 8: return settings::tabs::SleepTab::build(*this);
    case 9: return settings::tabs::AboutTab::build(*this);
    default: return makeLoadingTab(tabIndex);
    }
}

SettingsScreen::Tab SettingsScreen::makeLoadingTab(int tabIndex) const {
    auto& i18n = nxui::I18n::instance();
    Tab tab;
    tab.name = tabIndex >= 0 && tabIndex < static_cast<int>(m_tabNames.size())
        ? m_tabNames[static_cast<std::size_t>(tabIndex)]
        : i18n.tr("common.loading", "Loading");
    SettingItem item;
    item.label = i18n.tr("common.loading", "Loading...");
    item.type = ItemType::Info;
    tab.items.push_back(std::move(item));
    return tab;
}

void SettingsScreen::ensureTabLoaded(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= static_cast<int>(m_loadedTabs.size()))
        return;
    const auto idx = static_cast<std::size_t>(tabIndex);
    if (m_loadedTabs[idx] || m_loadingTabs[idx])
        return;
    startAsyncTabLoad(tabIndex);
}

void SettingsScreen::startAsyncTabLoad(int tabIndex) {
    const auto idx = static_cast<std::size_t>(tabIndex);
    if (idx >= m_tabTasks.size())
        return;
    m_loadingTabs[idx] = true;
    m_tabTasks[idx] = std::async(std::launch::async, [this, tabIndex]() {
        return buildTabNow(tabIndex);
    });
}

void SettingsScreen::pollTabLoaders() {
    for (std::size_t i = 0; i < m_tabTasks.size(); ++i) {
        if (!m_loadingTabs[i] || !m_tabTasks[i].valid())
            continue;
        if (m_tabTasks[i].wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            continue;
        Tab loaded = m_tabTasks[i].get();
        if (i >= m_tabs.size())
            continue;
        m_tabs[i] = std::move(loaded);
        m_loadedTabs[i] = true;
        m_loadingTabs[i] = false;
        if (i < m_cachedTabContentWidgets.size())
            m_cachedTabContentWidgets[i].clear();
        if (static_cast<int>(i) == m_tabIndex) {
            clampContentIdx();
            m_contentIdx = std::clamp(m_contentIdx, 0, std::max(0, focusableCount() - 1));
            rebuildContentItems();
        }
    }
}

void SettingsScreen::prefetchOneTab() {
    for (std::size_t n = 0; n < m_loadedTabs.size(); ++n) {
        const auto idx = static_cast<std::size_t>(m_nextPrefetchTab);
        m_nextPrefetchTab = (m_nextPrefetchTab + 1) % static_cast<int>(m_loadedTabs.size());
        if (m_loadedTabs[idx] || m_loadingTabs[idx])
            continue;
        // Bluetooth queries btm only when the player opens that tab.
        if (static_cast<int>(idx) == kBluetoothTab && static_cast<int>(idx) != m_tabIndex)
            continue;
        startAsyncTabLoad(static_cast<int>(idx));
        return;
    }
}

void SettingsScreen::onContentUpdate(float dt) {
    pollTabLoaders();
    if (isActive())
        prefetchOneTab();
    TabbedOverlayScreen::onContentUpdate(dt);
}
