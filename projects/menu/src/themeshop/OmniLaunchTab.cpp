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
        // The ten slot rows take a whole screen: a switch shows or hides them.
        // The tab is rebuilt on the next frame (ThemeShopScreen::updateCustomContent),
        // never inside this onChange, which a rebuild would destroy while it runs.
        SettingItem it;
        it.label = i18n.tr("settings.omnilaunch.order", "Botões da barra lateral");
        it.description = i18n.tr("settings.omnilaunch.order_show_desc",
            "Show the ten side-column positions to choose which button goes where.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_sidebarOrderExpanded;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_sidebarOrderExpanded = self.boolVal;
            screen.m_omniTabRebuildPending = true;
        };
        t.items.push_back(std::move(it));
    }

    auto slotLabel = [&i18n](int slot) {
        const bool left = slot < 5;
        return (left ? i18n.tr("settings.omnilaunch.left", "Esquerda") : i18n.tr("settings.omnilaunch.right", "Direita"))
             + " " + std::to_string(left ? slot + 1 : slot - 4);
    };

    if (screen.m_sidebarOrderExpanded && screen.m_sidebarOrder.size() == 10 && !screen.m_sidebarNames.empty()) {
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
    // Shared with Cards: the same Config fields Cards uses (they used to be
    // flag files here that nothing read).
    auto toggleRow = [&t](std::string label, std::string desc, bool value, ThemeShopScreen::BoolCb* cb, bool* state) {
        SettingItem it;
        it.label = std::move(label);
        it.description = std::move(desc);
        it.type = ItemType::Toggle;
        it.boolVal = value;
        it.onChange = [cb, state](SettingItem& self) {
            *state = self.boolVal;
            if (*cb) (*cb)(self.boolVal);
        };
        t.items.push_back(std::move(it));
    };
    auto actionRow = [&t](std::string label, std::string desc, ThemeShopScreen::VoidCb* cb) {
        SettingItem it;
        it.label = std::move(label);
        it.description = std::move(desc);
        it.type = ItemType::Action;
        it.onChange = [cb](SettingItem&) { if (*cb) (*cb)(); };
        t.items.push_back(std::move(it));
    };
    auto& sp = screen.m_sharedPrefs;
    toggleRow(i18n.tr("settings.omnilaunch.ask_user", "Ask who's playing"),
              i18n.tr("settings.omnilaunch.ask_user_desc",
                      "Starting a game shows the user picker first, like Nintendo's own HOME."),
              sp.askUser, &screen.m_askUserCb, &sp.askUser);
    toggleRow(i18n.tr("cards.set_repeat", "Repeat song"), std::string(),
              sp.repeat, &screen.m_repeatCb, &sp.repeat);
    toggleRow(i18n.tr("cards.set_mtp", "Connect to a computer"),
              i18n.tr("cards.set_mtp_desc", "Plug in a USB cable: the SD card shows up as a drive (MTP)"),
              sp.mtp, &screen.m_mtpCb, &sp.mtp);
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
    {
        SettingItem it;
        it.label = i18n.tr("cards.set_lock", "Lock screen");
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }
    if (sp.passwordActive) {
        // A password always locks, so the switch stays on and says why.
        SettingItem it;
        it.label = i18n.tr("settings.omnilaunch.lock", "Lock screen");
        it.description = i18n.tr("settings.omnilaunch.lock_pw_forced",
            "On while a password is set. Remove the password to turn it off.");
        it.type = ItemType::Toggle;
        it.boolVal = true;
        it.onChange = [&screen](SettingItem& self) {
            self.boolVal = true;
            screen.requestToast(nxui::I18n::instance().tr("settings.omnilaunch.lock_pw_forced_toast",
                "Remove the password first to turn the lock screen off."));
        };
        t.items.push_back(std::move(it));
    } else {
        toggleRow(i18n.tr("settings.omnilaunch.lock", "Lock screen"),
                  i18n.tr("cards.set_lock_desc", "After sleep. A password always locks."),
                  sp.lockScreen, &screen.m_lockScreenCb, &sp.lockScreen);
    }
    toggleRow(i18n.tr("cards.set_lock_music", "Music on the lock screen"), std::string(),
              sp.lockMusic, &screen.m_lockMusicCb, &sp.lockMusic);
    actionRow(i18n.tr("cards.lock_now", "Lock now"), std::string(), &screen.m_lockNowCb);
    actionRow(i18n.tr("cards.lock_password", "Password") + ": "
                  + (sp.passwordActive ? i18n.tr("cards.lock_password_on", "On") : i18n.tr("cards.lock_password_off", "Off")),
              i18n.tr("cards.lock_password_desc", "4 to 8 D-pad presses. Forgot it: HOME, then hold Minus + Plus for 3 s."),
              &screen.m_lockPasswordSetCb);
    if (sp.passwordActive)
        actionRow(i18n.tr("cards.lock_password_remove", "Remove password"), std::string(), &screen.m_lockPasswordRemoveCb);
#endif

    return t;
}

} // namespace themeshop::tabs
