// WiiUMenuApp: the Classic launcher tiles (homenx_todo F4). Two widget types
// put the Cards-only shortcuts on the Classic grid:
//   - Homebrew: every .nro on the SD card (HomebrewStore), opened through the
//     same NRO host the Cards page uses (AppletLauncher::launchNro), with the
//     options Cards offers (open/resume, update, close, delete) and the
//     homebrew update sheet's check and "update all".
//   - System: the Nintendo page tiles (Album, Controllers, Mii Maker,
//     Internet, Users, Settings, News); eShop, Online and Friends stay
//     blocked, as in Cards. TikTok is fetched on first use like Cards Media.
// Lists are shown in the existing ListDetailScreen. Scans and update work run
// on the HOME pool; textures are uploaded here, on the main thread.

#include "WiiUMenuApp.hpp"
#include "DebugLog.hpp"
#include "cards/CardsPages.hpp"
#include "homebrew/HomebrewStore.hpp"
#include "homebrew/HomebrewUpdater.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/ThreadPool.hpp>
#ifdef SWITCHU_MENU
#include <switchu/sd_commit.hpp>
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>

namespace {
constexpr const char* kOwnNro = "sdmc:/switch/OmniLaunch/OmniLaunch.nro";

std::string replaceTag(std::string s, const std::string& tag, const std::string& value) {
    for (std::size_t p = s.find(tag); p != std::string::npos; p = s.find(tag, p + value.size()))
        s.replace(p, tag.size(), value);
    return s;
}
} // namespace

struct WiiUMenuApp::LauncherHbScan {
    std::atomic<bool> finished{false};
    std::vector<HomebrewApp> apps;   // icons included (JPEG bytes)
    hbupdate::State state;           // saved update offers (no network)
};

struct WiiUMenuApp::LauncherHbJob {
    bool install = false;
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
    hbupdate::CheckProgress check;
    hbupdate::InstallProgress progress;
    hbupdate::State state;
    hbupdate::InstallResult result;
};

// ── Which grid entry is the suspended software ──────────────────────────────
std::uint64_t WiiUMenuApp::suspendedGridId() const {
#ifdef SWITCHU_MENU
    const std::uint64_t suspended = m_launcher.suspendedTitleId();
    if (suspended == 0 || m_launcher.runningHomebrew().empty())
        return suspended;
    // A homebrew borrows a game (its host) to run. Marking that game's icon
    // showed the host as the open software after closing the homebrew, so the
    // Homebrew tile carries the mark instead, when the grid has one.
    for (const auto& w : m_widgetStore.all())
        if (w.type == switchu::widgets::WidgetType::HomebrewApps)
            return switchu::widgets::widgetTitleId(w.id);
    return 0;
#else
    return 0;
#endif
}

bool WiiUMenuApp::gridTitleSuspended(std::uint64_t titleId) const {
    return titleId != 0 && titleId == suspendedGridId();
}

// ── Homebrew list ───────────────────────────────────────────────────────────
void WiiUMenuApp::ensureLauncherScreen() {
    if (m_launcherScreen)
        return;
    m_launcherScreen = std::make_shared<ListDetailScreen>();
    m_launcherScreen->setInput(&app().input());
    m_launcherScreen->setFont(&m_fontNormal);
    m_launcherScreen->setSmallFont(&m_fontSmall);
    m_launcherScreen->setTheme(&m_theme);
    m_launcherScreen->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    if (m_overlayLayer) m_overlayLayer->addChild(m_launcherScreen);
}

void WiiUMenuApp::openHomebrewLauncher() {
    auto& i18n = nxui::I18n::instance();
    ensureLauncherScreen();
    m_launcherShowsHomebrew = true;
    m_launcherScreen->setEntries({}, i18n.tr("cards.page_homebrew", "Homebrew"),
                                 i18n.tr("launcher.reading", "Reading the Homebrew folder..."));
    raiseOverlay(m_launcherScreen);
    if (!m_launcherScreen->isActive())
        m_launcherScreen->show();
    focusManager().setFocus(m_launcherScreen.get());
    startLauncherHomebrewScan();
}

void WiiUMenuApp::startLauncherHomebrewScan() {
    if (m_launcherScanFuture.valid() || !m_threadPool.acceptingTasks())
        return;
    auto scan = std::make_shared<LauncherHbScan>();
    m_launcherScan = scan;
    try {
        m_launcherScanFuture = m_threadPool.submit([scan]() {
            scan->apps = scanHomebrew(kOwnNro, true);
            scan->state = hbupdate::loadState();
            scan->finished.store(true, std::memory_order_release);
        });
    } catch (...) {
        m_launcherScan.reset();
    }
}

void WiiUMenuApp::launchHomebrewNro(const std::string& path, const std::string& title) {
#ifdef SWITCHU_MENU
    auto& i18n = nxui::I18n::instance();
    const std::string running = m_launcher.runningHomebrew();
    if (m_launcher.suspendedTitleId() != 0 && running == path) {
        if (m_launcherScreen) m_launcherScreen->hide();
        resumeSuspendedApplication(m_launcher.suspendedTitleId(), title);
        return;
    }
    if (m_launcher.suspendedTitleId() != 0) {
        m_audio.playSfx(Sfx::ModalShow);
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(i18n.tr("cards.suspended_title", "Software suspended"),
                       i18n.tr("browser.close_suspended", "Close the suspended software first."),
                       {{i18n.tr("button.ok", "OK"), []() {}, true}});
        raiseOverlay(m_dialog);
        focusManager().setFocus(m_dialog.get());
        return;
    }
    DebugLog::log("[launcher-tile] homebrew -> %s", path.c_str());
    if (m_launcherScreen) m_launcherScreen->hide();
    // Same path as the BrowseNX button: the NRO host borrows a game.
    m_launcher.launchNro(path, 0, {}, true);
#else
    (void)path;
    (void)title;
#endif
}

void WiiUMenuApp::showHomebrewEntryOptions(const std::string& path, const std::string& title,
                                           const std::string& version) {
#ifdef SWITCHU_MENU
    auto& i18n = nxui::I18n::instance();
    const bool running = m_launcher.suspendedTitleId() != 0 && m_launcher.runningHomebrew() == path;
    std::vector<OverlayDialog::ButtonDef> buttons;
    buttons.push_back({running ? i18n.tr("cards.resume", "Resume") : i18n.tr("cards.open", "Open"),
                       [this, path, title]() { launchHomebrewNro(path, title); }, true});
    const hbupdate::State* state = m_launcherScan && m_launcherScan->finished.load() ? &m_launcherScan->state : nullptr;
    if (state && !running && hbupdate::pendingOffer(*state, hbupdate::Installed{path, title, version})) {
        buttons.push_back({i18n.tr("cards.hb_update_one", "Update"),
                           [this, path]() { startLauncherHomebrewJob(true, {path}); }, true});
    }
    if (running) {
        buttons.push_back({i18n.tr("cards.close_software", "Close software"),
                           [this]() {
                               m_launcher.terminateApplication();
                               if (m_launcherScreen) m_launcherScreen->hide();
                           }, true});
    } else if (!hbupdate::managedPath(path)) {
        buttons.push_back({i18n.tr("cards.hb_delete", "Delete from the SD card"),
                           [this, path, title]() {
                               if (m_launcherHbFuture.valid()) {
                                   auto& t = nxui::I18n::instance();
                                   m_dialog->show(title, t.tr("cards.hb_delete_busy", "Wait for the homebrew update to finish"),
                                                  {{t.tr("button.ok", "OK"), []() {}, true}});
                                   return;
                               }
                               std::error_code ec;
                               const bool ok = std::filesystem::remove(path, ec) && !ec;
                               switchu::commitSdCard("classic homebrew delete");
                               DebugLog::log("[launcher-tile] delete %s: %s", path.c_str(), ok ? "ok" : "failed");
                               auto& t = nxui::I18n::instance();
                               m_dialog->show(title,
                                              ok ? t.tr("cards.hb_deleted", "Deleted") + ": " + title
                                                 : t.tr("cards.hb_delete_failed", "The homebrew could not be deleted"),
                                              {{t.tr("button.ok", "OK"), []() {}, true}});
                               if (ok) startLauncherHomebrewScan();
                           }, false});
    }
    buttons.push_back({i18n.tr("button.cancel", "Cancel"), []() {}, true});
    m_audio.playSfx(Sfx::ModalShow);
    m_dialogReturnFocus = m_launcherScreen.get();
    raiseOverlay(m_dialog);
    m_dialog->show(title, path + (version.empty() ? std::string() : "\n" + version), std::move(buttons), 0, {});
    focusManager().setFocus(m_dialog.get());
#else
    (void)path;
    (void)title;
    (void)version;
#endif
}

void WiiUMenuApp::startLauncherHomebrewJob(bool install, std::vector<std::string> paths) {
    auto& i18n = nxui::I18n::instance();
    if (m_launcherHbFuture.valid()) {
        m_dialog->show(i18n.tr("cards.hb_title", "Homebrew updates"),
                       i18n.tr("cards.hb_busy", "Homebrew updates are still working..."),
                       {{i18n.tr("button.ok", "OK"), []() {}, true}});
        return;
    }
    if (!m_threadPool.acceptingTasks() || !m_progressDialog)
        return;
    if (!install && !m_homeCore.net().online()) {
        m_dialog->show(i18n.tr("cards.hb_title", "Homebrew updates"),
                       i18n.tr("cards.hb_offline", "Connect to the internet to check."),
                       {{i18n.tr("button.ok", "OK"), []() {}, true}});
        return;
    }
    auto job = std::make_shared<LauncherHbJob>();
    job->install = install;
    if (install) {
        if (!m_launcherScan || !m_launcherScan->finished.load())
            return;
        job->state = m_launcherScan->state;
        job->progress.count = static_cast<int>(paths.size());
    }
    net::NetService* service = &m_homeCore.net();
    std::string skip;
#ifdef SWITCHU_MENU
    skip = m_launcher.runningHomebrew(); // never replace the .nro that is open
#endif
    try {
        m_launcherHbFuture = m_threadPool.submit([job, service, paths, skip]() {
            try {
                if (job->install)
                    job->result = hbupdate::install(*service, job->state, paths, skip, job->progress, job->stop);
                else
                    job->state = hbupdate::check(*service, true, job->check, job->stop, true);
            } catch (const std::exception& e) {
                job->result.failed.push_back(e.what());
            } catch (...) {
                job->result.failed.push_back("error");
            }
            job->finished.store(true, std::memory_order_release);
        });
    } catch (...) {
        return;
    }
    m_launcherHbJob = job;
    m_progressDialog->setTheme(&m_theme);
    raiseOverlay(m_progressDialog);
    m_progressDialog->show(i18n.tr("cards.hb_title", "Homebrew updates"),
                           install ? i18n.tr("cards.hb_installing", "Updating...")
                                   : i18n.tr("cards.hb_checking", "Checking for homebrew updates..."),
                           -1.f);
    m_progressDialog->setCancelAction(i18n.tr("button.cancel", "Cancel"), [this]() {
        if (m_launcherHbJob) m_launcherHbJob->stop = true;
        if (m_progressDialog)
            m_progressDialog->markCancelling(nxui::I18n::instance().tr("cards.fetch_cancelling", "Cancelling..."));
    });
    m_dialogReturnFocus = m_launcherScreen && m_launcherScreen->isActive() ? m_launcherScreen.get()
                                                                         : focusManager().current();
    focusManager().setFocus(m_progressDialog.get());
}

void WiiUMenuApp::stopLauncherHomebrewJob() {
    if (m_launcherHbJob)
        m_launcherHbJob->stop = true;
    if (m_launcherHbFuture.valid()) {
        m_launcherHbFuture.wait();
        try { m_launcherHbFuture.get(); } catch (...) {}
    }
    m_launcherHbJob.reset();
    if (m_launcherScanFuture.valid()) {
        m_launcherScanFuture.wait();
        try { m_launcherScanFuture.get(); } catch (...) {}
    }
}

// ── System applets ──────────────────────────────────────────────────────────
void WiiUMenuApp::openSystemLauncher() {
    using T = cards::pages::SystemTile;
    auto& i18n = nxui::I18n::instance();
    ensureLauncherScreen();
    m_launcherShowsHomebrew = false;
    struct Tile { T id; const char* key; const char* fallback; const char* icon; const char* descKey; const char* desc; };
    static const Tile kTiles[] = {
        {T::News, "cards.tile_news", "News", "cards/news.png", "launcher.news_desc", "Nintendo news on the console"},
        {T::Album, "cards.tile_album", "Album", "album.png", "launcher.album_desc", "Screenshots and videos"},
        {T::Controllers, "cards.tile_controllers", "Controllers", "controller.png", "launcher.controllers_desc",
         "Pair controllers and change grip or order"},
        {T::MiiEditor, "cards.tile_mii", "Mii Maker", "mii_editor.png", "launcher.mii_desc", "Create and edit Miis"},
        {T::Internet, "cards.tile_internet", "Internet", "network.png", "launcher.internet_desc",
         "Wi-Fi and the console network settings"},
        {T::Users, "cards.tile_users", "Users", "switch_logo_white.png", "launcher.users_desc", "Choose or add a user"},
        {T::HomeSettings, "cards.tile_settings", "HOME settings", "settings.png", "launcher.settings_desc",
         "OmniLaunch settings"},
        {T::Eshop, "cards.tile_eshop", "Nintendo eShop", "cards/download.png", "cards.safe_blocked_desc",
         "This online service is blocked by default to protect your console from bans."},
        {T::SwitchOnline, "cards.tile_online", "Nintendo Switch Online", "cards/wifi.png", "cards.safe_blocked_desc",
         "This online service is blocked by default to protect your console from bans."},
        {T::Friends, "cards.tile_friends", "Friends", "cards/friends.png", "cards.safe_blocked_desc",
         "This online service is blocked by default to protect your console from bans."},
    };
    std::vector<ListDetailScreen::Entry> entries;
    const std::string iconBase = std::string(SD_ASSETS) + "/icons/";
    for (const auto& t : kTiles) {
        ListDetailScreen::Entry e;
        e.title = i18n.tr(t.key, t.fallback);
        e.listName = e.title;
        e.kicker = i18n.tr("widget.system_applets", "System");
        e.body = i18n.tr(t.descKey, t.desc);
        const bool blocked = t.id == T::Eshop || t.id == T::SwitchOnline || t.id == T::Friends;
        if (!blocked) {
            const int tile = static_cast<int>(t.id);
            e.onActivate = [this, tile]() { openSystemApplet(tile); };
        }
        const std::string iconPath = iconBase + t.icon;
        e.lazyImage = [this, iconPath]() -> std::shared_ptr<nxui::Texture> {
            auto tex = std::make_shared<nxui::Texture>();
            if (tex->loadFromFile(app().gpu(), app().renderer(), iconPath, 256))
                return tex;
            return nullptr;
        };
        e.roundImage = false;
        entries.push_back(std::move(e));
    }
    // TikTok, fetched on first use (Cards Media).
    {
        std::error_code ec;
        const bool installed = std::filesystem::exists(cards::pages::kTikTokNro, ec);
        ListDetailScreen::Entry e;
        e.title = "TikTok";
        e.listName = e.title;
        e.kicker = i18n.tr("cards.page_media", "Media");
        e.body = installed ? std::string(cards::pages::kTikTokNro)
                           : i18n.tr("launcher.fetch_on_first_use", "Downloaded from GitHub the first time you open it.");
        e.onActivate = [this, installed]() {
            if (installed)
                launchHomebrewNro(cards::pages::kTikTokNro, "TikTok");
            else
                fetchLauncherApp(cards::pages::kTikTokUrl, cards::pages::kTikTokNro, "TikTok");
        };
        entries.push_back(std::move(e));
    }
    m_launcherScreen->setEntries(std::move(entries), i18n.tr("widget.system_applets", "System"), std::string());
    raiseOverlay(m_launcherScreen);
    if (!m_launcherScreen->isActive())
        m_launcherScreen->show();
    focusManager().setFocus(m_launcherScreen.get());
}

void WiiUMenuApp::openSystemApplet(int tile) {
    using T = cards::pages::SystemTile;
    const T t = static_cast<T>(tile);
    if (m_launcherScreen) m_launcherScreen->hide();
    switch (t) {
    case T::News: openNews(); return;
    case T::Users: openProfileCarousel(); return;
    case T::HomeSettings:
        createSettings();
        if (m_settings) {
            m_settingsReturnFocus = focusManager().current();
            m_navigator.navigate(switchu::navigation::Route::Settings);
            m_settings->show();
            focusManager().setFocus(m_settings.get());
        }
        return;
    default: break;
    }
#ifdef SWITCHU_MENU
    auto& i18n = nxui::I18n::instance();
    if (m_launcher.suspendedTitleId() != 0) {
        // An applet over suspended software needs the daemon's applet queue.
        m_audio.playSfx(Sfx::ModalShow);
        m_dialogReturnFocus = focusManager().current();
        raiseOverlay(m_dialog);
        m_dialog->show(i18n.tr("cards.suspended_title", "Software suspended"),
                       i18n.tr("browser.close_suspended", "Close the suspended software first."),
                       {{i18n.tr("button.ok", "OK"), []() {}, true}});
        focusManager().setFocus(m_dialog.get());
        return;
    }
    DebugLog::log("[launcher-tile] system applet %d", tile);
    switch (t) {
    case T::Album: m_launcher.launchAlbum(); break;
    case T::Controllers: m_launcher.launchControllerPairing(); break;
    case T::MiiEditor: m_launcher.launchMiiEditor(); break;
    case T::Internet: m_launcher.launchNetConnect(); break;
    default: break;
    }
#endif
}

// ── First-use download (TikTok) ─────────────────────────────────────────────
void WiiUMenuApp::fetchLauncherApp(const std::string& url, const std::string& dest, const std::string& name) {
    auto& i18n = nxui::I18n::instance();
    if (m_launcherFetch.running() || !m_progressDialog || m_progressDialog->isActive())
        return;
    if (!m_homeCore.net().online()) {
        m_dialog->show(name, i18n.tr("cards.net_offline", "No internet connection"),
                       {{i18n.tr("button.ok", "OK"), []() {}, true}});
        raiseOverlay(m_dialog);
        focusManager().setFocus(m_dialog.get());
        return;
    }
    if (!m_launcherFetch.start(m_homeCore.net(), m_threadPool, url, dest, name))
        return;
    m_progressDialog->setTheme(&m_theme);
    raiseOverlay(m_progressDialog);
    m_progressDialog->show(replaceTag(i18n.tr("cards.fetch_title", "Getting {app}"), "{app}", name),
                           i18n.tr("cards.fetch_connecting", "Connecting to GitHub..."), -1.f);
    m_progressDialog->setCancelAction(i18n.tr("button.cancel", "Cancel"), [this]() {
        m_launcherFetch.cancel();
        if (m_progressDialog)
            m_progressDialog->markCancelling(nxui::I18n::instance().tr("cards.fetch_cancelling", "Cancelling..."));
    });
    m_dialogReturnFocus = m_launcherScreen.get();
    focusManager().setFocus(m_progressDialog.get());
}

// ── Per-frame: scans, update jobs, downloads ────────────────────────────────
void WiiUMenuApp::syncLauncherWork() {
    auto& i18n = nxui::I18n::instance();
    // Homebrew scan finished: build the list (icons uploaded lazily).
    if (m_launcherScanFuture.valid() &&
        m_launcherScanFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try { m_launcherScanFuture.get(); } catch (...) {}
        auto scan = m_launcherScan;
        if (scan && scan->finished.load(std::memory_order_acquire) && m_launcherScreen &&
            m_launcherScreen->isActive() && m_launcherShowsHomebrew) {
            std::vector<ListDetailScreen::Entry> entries;
            std::string running;
#ifdef SWITCHU_MENU
            if (m_launcher.suspendedTitleId() != 0)
                running = m_launcher.runningHomebrew();
#endif
            int pending = 0;
            for (const auto& hb : scan->apps) {
                ListDetailScreen::Entry e;
                e.title = hb.name.empty() ? std::filesystem::path(hb.path).stem().string() : hb.name;
                e.listName = e.title;
                e.kicker = hb.author.empty() ? i18n.tr("cards.hb_kind", "Homebrew")
                                             : replaceTag(i18n.tr("cards.hb_by", "Homebrew by {author}"), "{author}", hb.author);
                e.body = hb.version.empty() ? hb.path : hb.version + "\n" + hb.path;
                const hbupdate::Offer* offer =
                    hbupdate::pendingOffer(scan->state, hbupdate::Installed{hb.path, hb.name, hb.version});
                if (offer) {
                    ++pending;
                    e.footer = replaceTag(i18n.tr("cards.hb_update_to", "Update to {version} (+)"), "{version}",
                                          offer->latest) + "\n" + offer->source;
                }
                // Button hints are glyph capsules in the bottom bar now; the
                // running one says so in its kicker instead.
                if (!running.empty() && running == hb.path)
                    e.kicker += "  \u2022  " + i18n.tr("cards.running", "Running");
                e.unread = offer != nullptr;
                const std::string path = hb.path, title = e.title, version = hb.version;
                e.onActivate = [this, path, title]() { launchHomebrewNro(path, title); };
                e.onSecondary = [this, path, title, version]() { showHomebrewEntryOptions(path, title, version); };
                if (!hb.icon.empty()) {
                    auto bytes = std::make_shared<std::vector<std::uint8_t>>(hb.icon);
                    e.lazyImage = [this, bytes]() -> std::shared_ptr<nxui::Texture> {
                        auto tex = std::make_shared<nxui::Texture>();
                        if (tex->loadFromMemory(app().gpu(), app().renderer(), bytes->data(), bytes->size(), 256))
                            return tex;
                        return nullptr;
                    };
                }
                entries.push_back(std::move(e));
            }
            // Updates: check the sources, or install every pending offer.
            {
                ListDetailScreen::Entry e;
                e.title = i18n.tr("cards.hb_title", "Homebrew updates");
                e.listName = pending > 0
                    ? replaceTag(i18n.tr("cards.hb_update_all", "Update all ({n})"), "{n}", std::to_string(pending))
                    : e.title;
                e.kicker = i18n.tr("cards.hb_sources", "Homebrew App Store and GitHub releases");
                e.body = pending > 0 ? replaceTag(i18n.tr("cards.hb_available",
                                                          "{n} homebrew apps have an update (Menu > Updates)."),
                                                  "{n}", std::to_string(pending))
                                     : i18n.tr("cards.hb_none", "Everything is up to date");
                e.footer = pending > 0 ? std::string() : i18n.tr("cards.hb_check_again", "Check again");
                std::vector<std::string> paths;
                for (const auto& hb : scan->apps)
                    if (hbupdate::pendingOffer(scan->state, hbupdate::Installed{hb.path, hb.name, hb.version}) &&
                        hb.path != running)
                        paths.push_back(hb.path);
                e.onActivate = [this, paths]() {
                    if (paths.empty())
                        startLauncherHomebrewJob(false, {});
                    else
                        startLauncherHomebrewJob(true, paths);
                };
                entries.push_back(std::move(e));
            }
            for (auto& e : scan->apps)
                std::vector<std::uint8_t>().swap(e.icon); // the entries hold their own copy
            m_launcherScreen->setEntries(std::move(entries), i18n.tr("cards.page_homebrew", "Homebrew"),
                                         i18n.tr("launcher.no_homebrew", "No .nro files in the switch folder."));
        }
    }

    // Update check / install finished.
    if (m_launcherHbFuture.valid()) {
        auto job = m_launcherHbJob;
        if (job && m_progressDialog && m_progressDialog->isActive() && !m_progressDialog->cancelRequested() &&
            job->install) {
            const int n = std::max(1, job->progress.count.load());
            const float f = (static_cast<float>(job->progress.index.load()) + job->progress.fraction.load()) /
                            static_cast<float>(n);
            m_progressDialog->updateState(i18n.tr("cards.hb_installing", "Updating..."), std::clamp(f, 0.f, 1.f));
        }
        if (m_launcherHbFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try { m_launcherHbFuture.get(); } catch (...) {}
            m_launcherHbJob.reset();
            if (m_progressDialog && m_progressDialog->isActive())
                m_progressDialog->hide();
            if (isCurrentFocusableWidget(m_dialogReturnFocus))
                focusManager().setFocus(m_dialogReturnFocus);
            if (job && job->finished.load(std::memory_order_acquire) && !job->stop.load()) {
                std::string text;
                if (job->install) {
                    if (!job->result.updated.empty())
                        text = replaceTag(i18n.tr("cards.hb_updated", "{n} homebrew updated"), "{n}",
                                          std::to_string(job->result.updated.size()));
                    if (!job->result.failed.empty())
                        text += (text.empty() ? "" : "\n") + i18n.tr("cards.hb_failed", "Update failed: ") +
                                job->result.failed.front();
                } else {
                    text = job->result.failed.empty() ? i18n.tr("launcher.check_done", "Update check finished.")
                                                      : i18n.tr("cards.hb_failed", "Update failed: ") +
                                                            job->result.failed.front();
                }
                if (!text.empty()) {
                    m_dialog->show(i18n.tr("cards.hb_title", "Homebrew updates"), text,
                                   {{i18n.tr("button.ok", "OK"), []() {}, true}});
                    raiseOverlay(m_dialog);
                    focusManager().setFocus(m_dialog.get());
                }
                if (m_launcherScreen && m_launcherScreen->isActive() && m_launcherShowsHomebrew)
                    startLauncherHomebrewScan(); // versions and offers changed
            }
        }
    }

    // First-use download.
    const auto state = m_launcherFetch.poll();
    if (state == home::AppFetch::Running) {
        if (m_progressDialog && m_progressDialog->isActive() && !m_progressDialog->cancelRequested())
            m_progressDialog->updateState(m_launcherFetch.installing()
                                              ? i18n.tr("cards.fetch_installing", "Installing...")
                                              : i18n.tr("cards.fetch_downloading", "Downloading..."),
                                          m_launcherFetch.progress());
    } else if (state != home::AppFetch::Idle) {
        if (m_progressDialog && m_progressDialog->isActive())
            m_progressDialog->hide();
        if (isCurrentFocusableWidget(m_dialogReturnFocus))
            focusManager().setFocus(m_dialogReturnFocus);
        if (state == home::AppFetch::Done) {
            if (m_launcherScreen && m_launcherScreen->isActive() && !m_launcherShowsHomebrew)
                openSystemLauncher(); // the entry now says installed
            launchHomebrewNro(m_launcherFetch.dest(), m_launcherFetch.name());
        } else if (state == home::AppFetch::Failed) {
            m_dialog->show(m_launcherFetch.name(), i18n.tr("cards.download_failed", "Download failed."),
                           {{i18n.tr("button.ok", "OK"), []() {}, true}});
            raiseOverlay(m_dialog);
            focusManager().setFocus(m_dialog.get());
        }
    }
}
