#include "ThemeShopTabBuilders.hpp"

#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>

ThemeShopScreen::Tab themeshop::tabs::SteamGridDbTab::build(ThemeShopScreen& screen) {
    using Tab = ThemeShopScreen::Tab;
    using SettingItem = ThemeShopScreen::SettingItem;
    using ItemType = ThemeShopScreen::ItemType;
    auto& i18n = nxui::I18n::instance();

    Tab t;
    t.name = i18n.tr("themeshop.tabs.steamgriddb", "SteamGridDB");

    {
        SettingItem section;
        section.type = ItemType::Section;
        section.label = i18n.tr("settings.steamgriddb.section", "Game artwork");
        t.items.push_back(std::move(section));
    }

    {
        SettingItem it;
        it.type = ItemType::Toggle;
        it.label = i18n.tr("settings.steamgriddb.enabled", "Display SteamGridDB artwork");
        it.description = i18n.tr("settings.steamgriddb.enabled_desc",
            "Show heroes and logos behind the application menu.");
        it.boolVal = screen.m_steamGridDbEnabled;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& item) {
            screen.m_steamGridDbEnabled = item.boolVal;
            if (screen.m_steamGridDbEnabledCb)
                screen.m_steamGridDbEnabledCb(item.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    // Where the artwork shows (HomeNX parity), nested under the master switch.
    auto addViewToggle = [&](const char* labelKey, const char* label, const char* descKey, const char* desc,
                             bool ThemeShopScreen::*flag) {
        SettingItem item;
        item.type = ItemType::Toggle;
        item.label = i18n.tr(labelKey, label);
        item.description = i18n.tr(descKey, desc);
        item.boolVal = screen.*flag;
        item.anim01 = item.boolVal ? 1.f : 0.f;
        item.onChange = [&screen, flag](SettingItem& self) {
            screen.*flag = self.boolVal;
            if (screen.m_steamGridDbViewFlagsCb)
                screen.m_steamGridDbViewFlagsCb(screen.m_steamGridDbShowInGrid,
                                                screen.m_steamGridDbShowInDynamicLine,
                                                screen.m_steamGridDbShowInFolders);
        };
        t.items.push_back(std::move(item));
    };
    addViewToggle("settings.steamgriddb.show_grid", "Show on grid menu", "settings.steamgriddb.show_grid_desc",
                  "Hero backdrop behind the standard HOME grid.", &ThemeShopScreen::m_steamGridDbShowInGrid);
    addViewToggle("settings.steamgriddb.show_line", "Show on single-row menu", "settings.steamgriddb.show_line_desc",
                  "Hero and logo behind the single-row carousel.", &ThemeShopScreen::m_steamGridDbShowInDynamicLine);
    addViewToggle("settings.steamgriddb.show_folders", "Show inside folders", "settings.steamgriddb.show_folders_desc",
                  "Artwork while browsing games inside an open folder.", &ThemeShopScreen::m_steamGridDbShowInFolders);
    {
        SettingItem it;
        it.type = ItemType::Slider;
        it.label = i18n.tr("settings.display.steamgriddb_opacity", "SteamGridDB artwork opacity");
        it.description = i18n.tr("settings.display.steamgriddb_opacity_desc",
                                 "Adjust the opacity of downloaded game background artwork.");
        it.floatVal = std::clamp(screen.m_steamGridDbOpacity, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.infoText = std::to_string(static_cast<int>(std::round(it.floatVal * 100.f))) + "%";
        it.onChange = [&screen](SettingItem& self) {
            self.anim01 = self.floatVal;
            self.infoText = std::to_string(static_cast<int>(std::round(self.floatVal * 100.f))) + "%";
            screen.m_steamGridDbOpacity = std::clamp(self.floatVal, 0.f, 1.f);
            if (screen.m_steamGridDbOpacityCb)
                screen.m_steamGridDbOpacityCb(screen.m_steamGridDbOpacity);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.type = ItemType::Action;
        it.label = i18n.tr("settings.steamgriddb.api_key", "API key");
        it.buttonLabel = i18n.tr("button.configure", "Configure");
        it.description = screen.m_steamGridDbHasApiKey
            ? i18n.tr("settings.steamgriddb.api_key_set", "Configured (hidden)")
            : i18n.tr("settings.steamgriddb.api_key_optional",
                      "Optional. Heroes and icons work without it; logos need one.");
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_steamGridDbApiKeyCb)
                screen.m_steamGridDbApiKeyCb();
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.type = ItemType::Action;
        it.label = i18n.tr("settings.steamgriddb.scan", "Search artwork for missing applications");
        it.buttonLabel = i18n.tr("button.search", "Search");
        it.description = i18n.tr("settings.steamgriddb.scan_desc",
            "Downloads a hero and logo only for applications that do not already have artwork.");
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_steamGridDbScrapeCb)
                screen.m_steamGridDbScrapeCb();
        };
        t.items.push_back(std::move(it));
    }

    t.onUpdate = [](ThemeShopScreen::Tab& current, TabbedOverlayScreen& base) {
        auto& owner = static_cast<ThemeShopScreen&>(base);
        if (current.items.size() < 8) return;

        // The tab may be built before the owner pushes its state: mirror it.
        const bool flags[4] = {owner.m_steamGridDbEnabled, owner.m_steamGridDbShowInGrid,
                               owner.m_steamGridDbShowInDynamicLine, owner.m_steamGridDbShowInFolders};
        for (int i = 0; i < 4; ++i) {
            auto& row = current.items[static_cast<std::size_t>(1 + i)];
            if (row.boolVal != flags[i]) {
                row.boolVal = flags[i];
                row.anim01 = flags[i] ? 1.f : 0.f;
            }
        }
        auto& key = current.items[6]; // section, enable, 3 view toggles, opacity, key
        key.description = owner.m_steamGridDbHasApiKey
            ? nxui::I18n::instance().tr("settings.steamgriddb.api_key_set", "Configured (hidden)")
            : nxui::I18n::instance().tr("settings.steamgriddb.api_key_optional",
                  "Optional. Heroes and icons work without it; logos need one.");
    };

    return t;
}
