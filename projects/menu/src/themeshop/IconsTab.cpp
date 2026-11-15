#include "ThemeShopTabBuilders.hpp"

#include <nxui/core/I18n.hpp>

// The icon store. Drawn by the shared catalogue grid; its list, downloads and
// apply live in ThemeShopIcons.cpp.
ThemeShopScreen::Tab themeshop::tabs::IconsTab::build(ThemeShopScreen& screen) {
    using Tab = ThemeShopScreen::Tab;
    auto& i18n = nxui::I18n::instance();

    Tab t;
    t.name = i18n.tr("themeshop.tabs.icons", "Icons");
    t.onUpdate = [&screen](Tab&, TabbedOverlayScreen&) {
        screen.pollIconStore();
    };
    return t;
}