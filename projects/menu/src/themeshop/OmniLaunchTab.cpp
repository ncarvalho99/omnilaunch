#include "ThemeShopTabBuilders.hpp"
#include "settings/SystemPrefs.hpp"
#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace themeshop::tabs {

ThemeShopScreen::Tab OmniLaunchTab::build(ThemeShopScreen& screen) {
    using SettingItem = ThemeShopScreen::SettingItem;
    using ItemType = ThemeShopScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    ThemeShopScreen::Tab t;
    t.name = "OmniLaunch";

    {
        SettingItem it;
        it.label = i18n.tr("settings.omnilaunch.order", "Botões da barra lateral");
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }

    auto slotLabel = [&i18n](int slot) {
        const bool left = slot < 5;
        return (left ? i18n.tr("settings.omnilaunch.left", "Esquerda") : i18n.tr("settings.omnilaunch.right", "Direita"))
             + " " + std::to_string(left ? slot + 1 : slot - 4);
    };

    if (screen.m_sidebarOrder.size() == 10 && !screen.m_sidebarNames.empty()) {
        for (int slot = 0; slot < 10; ++slot) {
            SettingItem it;
            it.label = slotLabel(slot);
            it.description = i18n.tr("settings.omnilaunch.order_desc",
                "Escolha o botão para esta posição; ele troca com o botão que estava nela.");
            it.type = ItemType::Selector;
            it.options = screen.m_sidebarNames;
            it.intVal = screen.m_sidebarOrder[static_cast<std::size_t>(slot)];
            it.onChange = [&screen, slot, slotLabel](SettingItem& self) {
                auto& order = screen.m_sidebarOrder;
                if (order.size() != 10) return;
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
                // Refresh only the row widgets. rebuildCurrentTab() rebuilt
                // every tab, destroying this std::function while it ran and
                // leaving the other rows bound to freed items, so a swap could
                // show a stale or wrong value on screen.
                screen.refreshCurrentTabWidgets();
            };
            t.items.push_back(std::move(it));
        }
    }

    {
        // Classic icon size: 80..130 % in 5 % steps, applied live.
        SettingItem it;
        it.label = i18n.tr("settings.omnilaunch.icon_size", "Icon size");
        it.description = i18n.tr("settings.omnilaunch.icon_size_desc",
            "Size of the HOME icons in the Classic views (grid, line and Flow).");
        it.type = ItemType::Slider;
        it.sliderSteps = 10;
        const int pct = std::clamp(screen.m_iconScalePercent, 80, 130);
        it.floatVal = static_cast<float>(pct - 80) / 50.f;
        it.anim01 = it.floatVal;
        it.infoText = std::to_string(pct) + "%";
        it.onChange = [&screen](SettingItem& self) {
            const int pct = std::clamp(80 + static_cast<int>(std::round(std::clamp(self.floatVal, 0.f, 1.f) * 10.f)) * 5,
                                       80, 130);
            self.floatVal = static_cast<float>(pct - 80) / 50.f;
            self.anim01 = self.floatVal;
            self.infoText = std::to_string(pct) + "%";
            screen.m_iconScalePercent = pct;
            if (screen.m_iconScaleCb) screen.m_iconScaleCb(pct);
        };
        t.items.push_back(std::move(it));
    }

    {
        // Classic side buttons (the two columns beside the grid): 80..130 %.
        SettingItem it;
        it.label = i18n.tr("settings.omnilaunch.sidebar_size", "Side button size");
        it.description = i18n.tr("settings.omnilaunch.sidebar_size_desc",
            "Size of the buttons in the Classic side columns.");
        it.type = ItemType::Slider;
        it.sliderSteps = 10;
        const int pct = std::clamp(screen.m_sidebarScalePercent, 80, 130);
        it.floatVal = static_cast<float>(pct - 80) / 50.f;
        it.anim01 = it.floatVal;
        it.infoText = std::to_string(pct) + "%";
        it.onChange = [&screen](SettingItem& self) {
            const int pct = std::clamp(80 + static_cast<int>(std::round(std::clamp(self.floatVal, 0.f, 1.f) * 10.f)) * 5,
                                       80, 130);
            self.floatVal = static_cast<float>(pct - 80) / 50.f;
            self.anim01 = self.floatVal;
            self.infoText = std::to_string(pct) + "%";
            screen.m_sidebarScalePercent = pct;
            if (screen.m_sidebarScaleCb) screen.m_sidebarScaleCb(pct);
        };
        t.items.push_back(std::move(it));
    }
#ifdef SWITCHU_MENU
    {
        SettingItem it;
        it.label = "OmniLaunch";
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }
    {
        static constexpr const char* kAppletFlag = "sdmc:/switch/OmniLaunch/homebrew_applet_mode";
        std::error_code ec;
        SettingItem it;
        it.label = i18n.tr("settings.omnilaunch.hb_app", "Sphaira como aplicativo");
        it.description = i18n.tr("settings.omnilaunch.hb_app_desc",
            "Ligado: Sphaira e homebrew têm memória total pegando emprestado um jogo. Desligado: abrem sobre o Álbum com memória reduzida.");
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
        SettingItem it;
        it.label = i18n.tr("settings.omnilaunch.docked_1080p", "1080p on the TV");
        it.description = i18n.tr("settings.omnilaunch.docked_1080p_desc",
            "Docked, HOME is drawn at 1920x1080. Sharper, uses more memory. Applies the next time HOME starts.");
        it.type = ItemType::Toggle;
        it.boolVal = settings::system_prefs::dockedNativeRender();
        it.onChange = [](SettingItem& self) {
            if (settings::system_prefs::setDockedNativeRender(self.boolVal) != self.boolVal)
                self.boolVal = !self.boolVal;
        };
        t.items.push_back(std::move(it));
    }
    {
        SettingItem it;
        it.label = i18n.tr("settings.omnilaunch.ingame_discord", "Discord messages during games (experimental)");
        it.description = settings::system_prefs::ultrahandNotificationsPresent()
            ? i18n.tr("settings.omnilaunch.ingame_discord_desc",
                      "New direct messages appear as Ultrahand notifications, checked every 2 to 4 minutes. Uses your account token; Discord may flag automated use.")
            : i18n.tr("settings.omnilaunch.ingame_discord_needs",
                      "Needs Ultrahand with notifications turned on.");
        it.type = ItemType::Toggle;
        it.boolVal = settings::system_prefs::inGameDiscord();
        it.onChange = [](SettingItem& self) {
            if (settings::system_prefs::setInGameDiscord(self.boolVal) != self.boolVal)
                self.boolVal = !self.boolVal;
        };
        t.items.push_back(std::move(it));
    }
    {
        static constexpr const char* kAskFlag = "sdmc:/config/OmniLaunch/ask_user";
        std::error_code ec;
        SettingItem it;
        it.label = i18n.tr("settings.omnilaunch.ask_user", "Perguntar quem está jogando");
        it.description = i18n.tr("settings.omnilaunch.ask_user_desc",
            "Iniciar um jogo exibe a seleção de usuários antes, como o HOME original da Nintendo.");
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
        it.label = i18n.tr("settings.omnilaunch.lock", "Tela de bloqueio");
        it.description = i18n.tr("settings.omnilaunch.lock_desc",
            "Exibe hora e data ao ligar ou despertar o console. A abre o HOME.");
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
    {
        SettingItem it;
        it.label = i18n.tr("settings.omnilaunch.look_software", "Procurar novos softwares");
        it.description = i18n.tr("settings.omnilaunch.look_software_desc",
            "Lê novamente o que está instalado para títulos ou atalhos que ainda não aparecem no menu.");
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_reloadCatalogCb) screen.m_reloadCatalogCb();
        };
        t.items.push_back(std::move(it));
    }
#endif

    return t;
}

} // namespace themeshop::tabs
