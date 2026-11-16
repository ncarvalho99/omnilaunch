#include "ThemeShopTabBuilders.hpp"
#include "settings/SystemPrefs.hpp"

#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

ThemeShopScreen::Tab themeshop::tabs::OptionsTab::build(ThemeShopScreen& screen) {
    using Tab = ThemeShopScreen::Tab;
    using SettingItem = ThemeShopScreen::SettingItem;
    using ItemType = ThemeShopScreen::ItemType;
    auto& i18n = nxui::I18n::instance();

    Tab t;
    t.name = i18n.tr("themeshop.tabs.options", "Options");
    t.key = "options";

    {
        SettingItem it;
        it.label = i18n.tr("settings.music.menu_music", "Menu Music");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_musicEnabled;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_musicEnabled = self.boolVal;
            if (screen.m_musicEnabledCb) screen.m_musicEnabledCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.music.theme_video_audio", "Theme Video Audio");
        it.description = i18n.tr("settings.music.theme_video_audio_desc",
                                 "Play soundtrack accompanying animated video themes.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_themeMusicEnabled;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_themeMusicEnabled = self.boolVal;
            if (screen.m_themeMusicEnabledCb) screen.m_themeMusicEnabledCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.music.music_volume", "Music Volume");
        it.type = ItemType::Slider;
        it.floatVal = std::clamp(screen.m_musicVolume, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.onChange = [&screen](SettingItem& self) {
            self.anim01 = self.floatVal;
            screen.m_musicVolume = std::clamp(self.floatVal, 0.f, 1.f);
            if (screen.m_musicVolumeCb) screen.m_musicVolumeCb(screen.m_musicVolume);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.music.sfx_volume", "SFX Volume");
        it.type = ItemType::Slider;
        it.floatVal = std::clamp(screen.m_sfxVolume, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.onChange = [&screen](SettingItem& self) {
            self.anim01 = self.floatVal;
            screen.m_sfxVolume = std::clamp(self.floatVal, 0.f, 1.f);
            if (screen.m_sfxVolumeCb) screen.m_sfxVolumeCb(screen.m_sfxVolume);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.background_speed", "Background animation speed");
        it.description = i18n.tr("settings.display.background_speed_desc",
                                 "How fast the shapes drift. Centre is the theme's own pace.");
        it.type = ItemType::Slider;
        it.floatVal = std::clamp(screen.m_backgroundSpeed, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.onChange = [&screen](SettingItem& self) {
            self.anim01 = self.floatVal;
            screen.m_backgroundSpeed = std::clamp(self.floatVal, 0.f, 1.f);
            if (screen.m_backgroundSpeedCb) screen.m_backgroundSpeedCb(screen.m_backgroundSpeed);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.background_blur", "Background blur");
        it.description = i18n.tr("settings.display.background_blur_desc",
                                 "Soften the wallpaper and the shapes drifting over it.");
        it.type = ItemType::Slider;
        it.floatVal = std::clamp(screen.m_backgroundBlur, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.onChange = [&screen](SettingItem& self) {
            self.anim01 = self.floatVal;
            screen.m_backgroundBlur = std::clamp(self.floatVal, 0.f, 1.f);
            if (screen.m_backgroundBlurCb) screen.m_backgroundBlurCb(screen.m_backgroundBlur);
        };
        t.items.push_back(std::move(it));
    }

    {
        // Switch between Classic and Cards HOME frontends
        SettingItem it;
        it.label = i18n.tr("cards.home_style", "HOME style");
        it.description = i18n.tr("settings.system.home_style_desc",
                                 "Switch to the Cards HOME. Cards can switch back from its settings button.");
        it.type = ItemType::Action;
        it.infoText = i18n.tr("cards.home_style_classic", "Classic");
        it.buttonLabel = i18n.tr("settings.system.home_style_switch", "Switch to Cards");
        it.onChange = [&screen](SettingItem& /* self */) {
            if (screen.m_switchToCardsCb) screen.m_switchToCardsCb();
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.launcher_view", "Launcher View");
        it.description = i18n.tr("settings.display.launcher_view_desc",
                                 "Choose between standard Grid, Dynamic Line carousel, 3D Flow, 3D Shelf, 3D Deck, Fullscreen Cover, XMB, List (Niagara), or Metro (Live Tiles).");
        it.type = ItemType::Selector;
        it.options = {
            i18n.tr("settings.layout.grid", "Grid"),
            i18n.tr("settings.layout.dynamic_line", "Dynamic Line"),
            i18n.tr("settings.layout.flow", "Flow (3D)"),
            i18n.tr("settings.layout.shelf", "Shelf (3D)"),
            i18n.tr("settings.layout.deck", "Deck (3D)"),
            i18n.tr("settings.layout.cover", "Cover"),
            i18n.tr("settings.layout.xmb", "XMB"),
            i18n.tr("settings.layout.list", "List (Niagara)"),
            i18n.tr("settings.layout.metro", "Metro (Live Tiles)"),
        };
        it.intVal = screen.m_layoutMode == AppLayoutMode::Metro ? 8
                  : screen.m_layoutMode == AppLayoutMode::List  ? 7
                  : screen.m_layoutMode == AppLayoutMode::Xmb   ? 6
                  : screen.m_layoutMode == AppLayoutMode::Cover ? 5
                  : screen.m_layoutMode == AppLayoutMode::Deck  ? 4
                  : screen.m_layoutMode == AppLayoutMode::Shelf ? 3
                  : screen.m_layoutMode == AppLayoutMode::Flow  ? 2
                  : screen.m_layoutMode == AppLayoutMode::DynamicLine ? 1 : 0;
        it.onChange = [&screen](SettingItem& self) {
            AppLayoutMode mode = self.intVal == 8 ? AppLayoutMode::Metro
                               : self.intVal == 7 ? AppLayoutMode::List
                               : self.intVal == 6 ? AppLayoutMode::Xmb
                               : self.intVal == 5 ? AppLayoutMode::Cover
                               : self.intVal == 4 ? AppLayoutMode::Deck
                               : self.intVal == 3 ? AppLayoutMode::Shelf
                               : self.intVal == 2 ? AppLayoutMode::Flow
                               : self.intVal == 1 ? AppLayoutMode::DynamicLine
                                                  : AppLayoutMode::Grid;
            screen.m_layoutMode = mode;
            if (screen.m_layoutModeCb) screen.m_layoutModeCb(mode);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.view_switch_button", "View Switch Button");
        it.description = i18n.tr("settings.display.view_switch_button_desc",
                                 "Choose the button used to cycle views. Sort button swaps automatically.");
        it.type = ItemType::Selector;
        it.options = {
            "R",
            "- (Minus)",
        };
        it.intVal = (screen.m_viewSwitchButton == "Minus" || screen.m_viewSwitchButton == "-") ? 1 : 0;
        it.onChange = [&screen](SettingItem& self) {
            std::string choice = (self.intVal == 1) ? "Minus" : "R";
            screen.m_viewSwitchButton = choice;
            if (screen.m_viewSwitchButtonCb) screen.m_viewSwitchButtonCb(choice);
        };
        t.items.push_back(std::move(it));
    }


    {
        SettingItem it;
        it.label = i18n.tr("settings.display.grid_columns", "Home Grid Columns");
        it.type = ItemType::Slider;
        it.sliderSteps = 5;
        int cols = std::clamp(screen.m_gridColumns, 3, 8);
        it.floatVal = (float)(cols - 3) / 5.f;
        it.anim01 = it.floatVal;
        it.infoText = std::to_string(cols);
        it.onChange = [&screen](SettingItem& self) {
            int cols = std::clamp(3 + (int)std::round(std::clamp(self.floatVal, 0.f, 1.f) * 5.f), 3, 8);
            self.floatVal = (float)(cols - 3) / 5.f;
            self.anim01 = self.floatVal;
            self.infoText = std::to_string(cols);
            screen.m_gridColumns = cols;
            if (screen.m_gridColumnsCb) screen.m_gridColumnsCb(cols);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.grid_rows", "Home Grid Rows");
        it.type = ItemType::Slider;
        it.sliderSteps = 3;
        int rows = std::clamp(screen.m_gridRows, 2, 5);
        it.floatVal = (float)(rows - 2) / 3.f;
        it.anim01 = it.floatVal;
        it.infoText = std::to_string(rows);
        it.onChange = [&screen](SettingItem& self) {
            int rows = std::clamp(2 + (int)std::round(std::clamp(self.floatVal, 0.f, 1.f) * 3.f), 2, 5);
            self.floatVal = (float)(rows - 2) / 3.f;
            self.anim01 = self.floatVal;
            self.infoText = std::to_string(rows);
            screen.m_gridRows = rows;
            if (screen.m_gridRowsCb) screen.m_gridRowsCb(rows);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.dynamic_pages", "Dynamic Pages");
        it.description = i18n.tr("settings.display.dynamic_pages_desc",
                                 "Adjust home pages to fit installed games and homebrew instead of keeping 8 fixed pages.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_dynamicPages;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_dynamicPages = self.boolVal;
            if (screen.m_dynamicPagesCb) screen.m_dynamicPagesCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    // --- Automatic day/night theme -----------------------------------------
    // A single entry opens a dedicated window with all the auto-theme settings.
    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.auto_theme", "Automatic Theme");
        it.description = screen.m_autoThemeSummary.empty()
            ? i18n.tr("themeshop.options.auto_theme_desc",
                      "Switch between a day theme and a night theme automatically.")
            : screen.m_autoThemeSummary;
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_autoThemeOpenCb)
                screen.m_autoThemeOpenCb();
        };
        t.items.push_back(std::move(it));
    }

    // Asked for by a player who had just made a shortcut and could only get the
    // grid to show it by rebooting the console.
    {
        SettingItem it;
        it.label = i18n.tr("settings.display.reload_grid", "Reload games and shortcuts");
        it.description = i18n.tr("settings.display.reload_grid_desc",
                                 "Reads the installed titles again. Use it after creating or "
                                 "deleting a shortcut.");
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_reloadCatalogCb) screen.m_reloadCatalogCb();
        };
        t.items.push_back(std::move(it));
    }

    // The expensive half of what "reload" used to do, on its own item. Reading
    // a title's control data costs about a second, so a hundred-title console
    // spends minutes on this -- worth it when a name or an icon is actually
    // wrong, and pure waste when a shortcut was merely added.
    {
        SettingItem it;
        it.label = i18n.tr("settings.display.rebuild_names",
                           "Rebuild names and icons");
        it.description = i18n.tr("settings.display.rebuild_names_desc",
                                 "Discards every cached name and icon and reads them from the "
                                 "titles again. Takes a few minutes on a full console.");
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_rebuildControlCacheCb) screen.m_rebuildControlCacheCb();
        };
        t.items.push_back(std::move(it));
    }

    // Moved from the OmniLaunch tab, which now holds only the theme look.
    // Sidebar order, icon sizes, system switches and the lock screen.
    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.side_section", "Side buttons and icons");
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }
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
    // "Look for new software" ran the same reload as "Reload games and
    // shortcuts" above; with both on one tab only that one is kept.
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
