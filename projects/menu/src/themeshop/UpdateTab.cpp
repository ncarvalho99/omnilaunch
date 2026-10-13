#include "ThemeShopTabBuilders.hpp"

#include <nxui/core/I18n.hpp>

#ifndef SWITCHU_VERSION
#define SWITCHU_VERSION "unknown"
#endif

// The launcher's own updates, kept beside the theme catalogue because both are
// the same errand: something published elsewhere that the console can fetch.
//
// This tab is a view. It never talks to GitHub itself -- the app owns the check
// and pushes results in through setUpdateState, so the daily check and the one
// asked for here cannot disagree about what is installed.
ThemeShopScreen::Tab themeshop::tabs::UpdateTab::build(ThemeShopScreen& screen) {
    using Tab = ThemeShopScreen::Tab;
    using SettingItem = ThemeShopScreen::SettingItem;
    using ItemType = ThemeShopScreen::ItemType;
    auto& i18n = nxui::I18n::instance();

    Tab t;
    t.name = i18n.tr("themeshop.tabs.update", "Update");

    {
        SettingItem it;
        it.label = i18n.tr("settings.about.version", "Version");
        it.type = ItemType::Info;
        it.infoText = screen.m_updateInstalledVersion.empty()
            ? std::string("OmniLaunch " SWITCHU_VERSION)
            : screen.m_updateInstalledVersion;
        t.items.push_back(std::move(it));
    }



    // One line says where things stand: what the last check found, or that no
    // check has happened yet. Two rows here read as a duplicate, which is what
    // they were.
    //
    // A pending restart outranks whatever the last check said: the package is
    // already on the card and the only thing left is the reboot that applies it.
    {
        SettingItem it;
        it.label = i18n.tr("themeshop.update_status", "Status");
        it.type = ItemType::Info;
        it.infoText = screen.m_updateRestartPending
            ? i18n.tr("themeshop.update_restart_pending",
                      "Downloaded. Restart to apply it.")
            : (!screen.m_updateStatusText.empty()
                   ? screen.m_updateStatusText
                   : i18n.tr("themeshop.update_unknown", "Not checked yet"));
        t.items.push_back(std::move(it));
    }

    // Always available once a release has been read, so the notes for the
    // installed version can be revisited without an update being pending.
    if (!screen.m_updateLatestNotes.empty()) {
        SettingItem it;
        it.label = i18n.tr("themeshop.update_notes", "What changed");
        it.description = screen.m_updateLatestVersion.empty()
            ? std::string()
            : std::string("OmniLaunch ") + screen.m_updateLatestVersion;
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_releaseNotesCb) screen.m_releaseNotesCb();
        };
        t.items.push_back(std::move(it));
    }

    // With a package waiting to be applied there is nothing to ask GitHub and
    // nothing to install: both rows would only offer to fetch again what is
    // already on the card, which is the loop that was reported.
    if (screen.m_updateRestartPending) {
        SettingItem it;
        it.label = i18n.tr("themeshop.update_restart", "Restart now");
        it.description = i18n.tr("themeshop.update_restart_desc",
                                 "The update is applied while the console starts. "
                                 "This first boot takes about half a minute longer.");
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_updateRestartCb) screen.m_updateRestartCb();
        };
        t.items.push_back(std::move(it));
    } else {
        SettingItem it;
        it.label = i18n.tr("settings.about.check_updates", "Check for updates");
        it.description = i18n.tr("settings.about.check_updates_desc",
                                 "Asks GitHub whether a newer version has been released.");
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (!screen.m_updateBusy && screen.m_updateCheckCb) screen.m_updateCheckCb();
        };
        t.items.push_back(std::move(it));
    }

    // Only once a release is known to be newer, so the row cannot start a
    // download of something that does not exist. Reading the notes is the row
    // above; this one does the one thing its label says.
    if (!screen.m_updateAvailableVersion.empty()) {
        SettingItem it;
        it.label = i18n.tr("themeshop.update_install", "Install update");
        it.description = screen.m_updateAvailableVersion;
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (!screen.m_updateBusy && screen.m_updateInstallCb) screen.m_updateInstallCb();
        };
        t.items.push_back(std::move(it));
    }

    return t;
}
