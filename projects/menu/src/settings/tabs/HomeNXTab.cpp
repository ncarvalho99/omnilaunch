#include "TabBuilders.hpp"
#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

// OmniLaunch HomeNX parity tab (HomeNX build 185, C-UI-13 / C-UI-14).
// Reordering/swapping of the 10 sidebar buttons, Sphaira as application mode,
// ask user, and lock screen settings.
SettingsScreen::Tab settings::tabs::HomeNXTab::build(SettingsScreen& screen) {
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    SettingsScreen::Tab t;
    t.name = "HomeNX";

    {
        SettingItem it;
        it.label = i18n.tr("settings.homenx.order", "Sidebar buttons");
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }

    auto slotLabel = [&i18n](int slot) {
        const bool left = slot < 5;
        return (left ? i18n.tr("settings.homenx.left", "Left") : i18n.tr("settings.homenx.right", "Right"))
             + " " + std::to_string(left ? slot + 1 : slot - 4);
    };

    if (screen.m_sidebarOrder.size() == 10 && !screen.m_sidebarNames.empty()) {
        for (int slot = 0; slot < 10; ++slot) {
            SettingItem it;
            it.label = slotLabel(slot);
            it.description = i18n.tr("settings.homenx.order_desc",
                "Choose the button for this spot; it swaps with the spot it was in.");
            it.type = ItemType::Selector;
            it.options = screen.m_sidebarNames;
            it.intVal = screen.m_sidebarOrder[static_cast<std::size_t>(slot)];
            it.onChange = [&screen, slot, slotLabel](SettingItem& self) {
                auto& order = screen.m_sidebarOrder;
                const int previous = order[static_cast<std::size_t>(slot)];
                const auto other = std::find(order.begin(), order.end(), self.intVal);
                if (other != order.end() && other - order.begin() == slot)
                    return;
                if (other != order.end()) {
                    const int otherSlot = static_cast<int>(other - order.begin());
                    *other = previous;
                    const std::string otherLabel = slotLabel(otherSlot);
                    for (auto& tab : screen.m_tabs) {
                        for (auto& row : tab.items) {
                            if (row.type == ItemType::Selector && row.label == otherLabel)
                                row.intVal = previous;
                        }
                    }
                }
                order[static_cast<std::size_t>(slot)] = self.intVal;
                if (screen.m_sidebarOrderCb)
                    screen.m_sidebarOrderCb(order);
            };
            t.items.push_back(std::move(it));
        }
    }

#ifdef SWITCHU_MENU
    {
        SettingItem it;
        it.label = "HomeNX";
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }
    {
        static constexpr const char* kAppletFlag = "sdmc:/switch/OmniLaunch/homebrew_applet_mode";
        std::error_code ec;
        SettingItem it;
        it.label = i18n.tr("settings.homenx.hb_app", "Sphaira as an application");
        it.description = i18n.tr("settings.homenx.hb_app_desc",
            "On: Sphaira and homebrew get full memory by borrowing one of your games. Off: they open over Album with less memory.");
        it.type = ItemType::Toggle;
        it.boolVal = !std::filesystem::exists(kAppletFlag, ec);
        it.onChange = [](SettingItem& self) {
            std::error_code err;
            if (self.boolVal)
                std::filesystem::remove(kAppletFlag, err);
            else
                std::ofstream(kAppletFlag) << "settings";
        };
        t.items.push_back(std::move(it));
    }
    {
        static constexpr const char* kAskFlag = "sdmc:/config/OmniLaunch/ask_user";
        std::error_code ec;
        SettingItem it;
        it.label = i18n.tr("settings.homenx.ask_user", "Ask who's playing");
        it.description = i18n.tr("settings.homenx.ask_user_desc",
            "Starting a game shows the users first, each time, as Nintendo's HOME does.");
        it.type = ItemType::Toggle;
        it.boolVal = std::filesystem::exists(kAskFlag, ec);
        it.onChange = [](SettingItem& self) {
            std::error_code err;
            if (self.boolVal)
                std::ofstream(kAskFlag) << "1";
            else
                std::filesystem::remove(kAskFlag, err);
        };
        t.items.push_back(std::move(it));
    }
    {
        static constexpr const char* kLockFlag = "sdmc:/config/OmniLaunch/lock_screen";
        std::error_code ec;
        SettingItem it;
        it.label = i18n.tr("settings.homenx.lock", "Lock screen");
        it.description = i18n.tr("settings.homenx.lock_desc",
            "The time and date when the console starts or wakes. A opens HOME.");
        it.type = ItemType::Toggle;
        it.boolVal = std::filesystem::exists(kLockFlag, ec);
        it.onChange = [](SettingItem& self) {
            std::error_code err;
            if (self.boolVal)
                std::ofstream(kLockFlag) << "1";
            else
                std::filesystem::remove(kLockFlag, err);
        };
        t.items.push_back(std::move(it));
    }
#endif

    return t;
}
