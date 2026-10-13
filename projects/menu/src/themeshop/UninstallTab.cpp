#include "ThemeShopTabBuilders.hpp"
#include <nxui/core/I18n.hpp>

namespace themeshop::tabs {

ThemeShopScreen::Tab UninstallTab::build(ThemeShopScreen& screen) {
    using ItemType = TabbedOverlayScreen::ItemType;
    using SettingItem = TabbedOverlayScreen::SettingItem;

    auto& i18n = nxui::I18n::instance();

    ThemeShopScreen::Tab t;
    t.name = i18n.tr("themeshop.tabs.uninstall", "Uninstall");

    SettingItem it;
    it.label = i18n.tr("themeshop.uninstall", "Uninstall OmniLaunch");
    it.description = i18n.tr("themeshop.uninstall_desc",
                             "Permanently delete OmniLaunch and return to the Nintendo HOME Menu.");
    it.type = ItemType::Action;
    it.onChange = [&screen](SettingItem&) {
        if (screen.m_selfUninstallCb) screen.m_selfUninstallCb();
    };
    t.items.push_back(std::move(it));

    return t;
}

} // namespace themeshop::tabs
