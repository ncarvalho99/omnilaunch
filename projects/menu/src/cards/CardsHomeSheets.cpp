// CardsHomeActivity: pages, move mode, sheets, quick settings, text entry,
// achievements and the welcome tour (HomeNX batch 1: E1, E2, E3, E4 safe part,
// E5 safe part, E6, E7, E9 safe part, E11). Split from CardsHomeActivity.cpp
// to keep that file navigable; same class, same thread (main) rules.

#include "cards/CardsAchievements.hpp"
#include "cards/CardsArtSources.hpp"
#include "cards/CardsHomeActivity.hpp"
#include "cards/CardsPages.hpp"
#include "core/DebugLog.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include "core/PlayTime.hpp"
#include "home/HomeCore.hpp"
#include "widgets/OverlayDialog.hpp"
#include "widgets/ProgressDialog.hpp"
#include "launcher/HekateReboot.hpp"
#include "widgets/LockPassword.hpp"
#include "net/NetService.hpp"
#include "themeshop/ThemeHttp.hpp"
#ifdef SWITCHU_MENU
#include "core/NsService.hpp"
#include <switchu/control_cache.hpp>
#include <switchu/sd_commit.hpp>
#include <switchu/title_footprint.hpp>
#endif
#include <nxui/Application.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <switch.h>

namespace cards {

namespace {
constexpr float kMoveRepeatDelay = 0.35f;
constexpr float kMoveRepeatRate = 0.14f; // C-IN-2: slower while carrying a card

const char* pageKey(int page) {
    switch (page) {
    case AppConfig::CardsPrefs::kHomebrew: return "cards.page_homebrew";
    case AppConfig::CardsPrefs::kNintendo: return "cards.page_nintendo";
    case AppConfig::CardsPrefs::kMedia: return "cards.page_media";
    default: return "cards.page_games";
    }
}
const char* pageFallback(int page) {
    switch (page) {
    case AppConfig::CardsPrefs::kHomebrew: return "Homebrew";
    case AppConfig::CardsPrefs::kNintendo: return "Nintendo";
    case AppConfig::CardsPrefs::kMedia: return "Media";
    default: return "Games";
    }
}
std::string onOff(bool v) {
    auto& t = nxui::I18n::instance();
    return v ? t.tr("common.active", "Active") : t.tr("common.disabled", "Disabled");
}
} // namespace

bool CardsHomeActivity::overlayActive() const {
    return (m_userSelect && m_userSelect->isActive()) || (m_text && m_text->isActive()) ||
           (m_quick && m_quick->isActive()) || (m_deleteProgress && m_deleteProgress->isActive()) ||
           (m_picker && m_picker->isActive()) || (m_discordPanel && m_discordPanel->isActive());
}

void CardsHomeActivity::saveConfig(const char* why) {
    auto& config = m_core.config();
    config.cards.normalize();
    if (!config.save())
        DebugLog::log("[cards] config save failed (%s)", why);
#ifdef SWITCHU_MENU
    switchu::commitSdCard(why);
#endif
}

// ── Top bar (E3/E5) ─────────────────────────────────────────────────────────
void CardsHomeActivity::activateTop() {
    auto& i18n = nxui::I18n::instance();
    switch (static_cast<TopButton>(m_topIndex)) {
    case TopButton::Menu: openDotsMenu(); return;
    case TopButton::Sort: openSortSheet(); return;
    case TopButton::User: openUsersSheet(); return;
    case TopButton::Nextendo: {
        const int count = m_core.nextendo().totalOnline();
        if (count < 0) {
            showToast(i18n.tr("cards.nextendo_counting", "Counting Nextendo players..."));
        } else {
            std::string msg = i18n.tr("cards.nextendo_players_count", "{n} players online in Nextendo.");
            const auto pos = msg.find("{n}");
            if (pos != std::string::npos) msg.replace(pos, 3, std::to_string(count));
            showToast(msg);
        }
        return;
    }
    case TopButton::Bell: openNotificationsSheet(); return;
    case TopButton::Settings: openSettingsSheet(); return;
    case TopButton::Power:
        if (!m_userSelect)
            return;
        // Classic's power actions plus Restart to Hekate (E5; see
        // launcher/HekateReboot.hpp for the authorized exception).
        m_audio.playSfx(Sfx::ModalShow);
        m_userSelect->show(
            i18n.tr("power.title", "Power"),
            i18n.tr("power.choose_action", "Choose a power action."),
            {
                {i18n.tr("button.cancel", "Cancel"), []() {}, true},
                {i18n.tr("power.sleep", "Sleep"), [this]() {
#ifdef SWITCHU_MENU
                    m_core.launcher().enterSleep();
#endif
                }, true},
                {i18n.tr("power.reboot", "Restart"), [this]() {
#ifdef SWITCHU_MENU
                    m_core.launcher().reboot();
#endif
                }, true},
                {i18n.tr("power.shutdown", "Turn Off"), [this]() {
#ifdef SWITCHU_MENU
                    m_core.launcher().shutdown();
#endif
                }, true},
                {i18n.tr("power.hekate", "Hekate"), [this]() { m_hekateQueued = true; }, true},
            },
            0, {});
        focusManager().setFocus(m_userSelect.get());
        return;
    case TopButton::Music:
        openMusicSheet();
        return;
    case TopButton::Discord:
        openDiscord();
        return;
    default:
        break;
    }
}

void CardsHomeActivity::ensureDiscordPanel() {
    if (!m_discordPanel) {
        m_discordPanel = std::make_shared<discord::DiscordPanel>();
        m_discordPanel->setFont(&m_chrome.labelFont());
        m_discordPanel->setSmallFont(&m_chrome.smallFont());
        m_discordPanel->setService(&m_core.discord(), &m_core);
        discord::DiscordPanel::Callbacks cb;
        cb.onClosed = [this]() {
            focusManager().setFocus(nullptr);
        };
        cb.onNavigate = [this]() { m_audio.playSfx(Sfx::Navigate); };
        cb.onActivate = [this]() { m_audio.playSfx(Sfx::Activate); };
        cb.onReply = [this](discord::Snowflake chanId, const std::string& name) {
            requestText("Discord: " + name, "Type your message", "", [this, chanId](const std::string& text) {
                m_core.discord().sendMessage(m_core, chanId, text);
            });
        };
        cb.onUserTokenLogin = [this]() {
            requestText("Discord Token", "Enter Discord account token", "", [this](const std::string& tok) {
                m_core.discord().signInWithToken(m_core, tok);
            });
        };
        m_discordPanel->setCallbacks(std::move(cb));
    }
}

void CardsHomeActivity::openDiscord() {
    m_audio.playSfx(Sfx::ModalShow);
    ensureDiscordPanel();
    m_discordPanel->show();
    focusManager().setFocus(m_discordPanel.get());
}

void CardsHomeActivity::openMusicSheet() {
    m_audio.playSfx(Sfx::ModalShow);
    m_musicSheet.open(m_audio, m_core.config());
}

void CardsHomeActivity::openNewsSheet() {
    m_audio.playSfx(Sfx::ModalShow);
    m_newsSheet.open(m_core, m_chrome, m_time);
}

// ── "..." menu (E3, C-UI-2) ─────────────────────────────────────────────────
void CardsHomeActivity::openDotsMenu() {
    auto& i18n = nxui::I18n::instance();
    m_audio.playSfx(Sfx::ModalShow);
    std::vector<CardsSheet::Row> rows;
    for (int page : pages::shown(m_core.config().cards)) {
        CardsSheet::Row r;
        r.label = i18n.tr(pageKey(page), pageFallback(page));
        r.value = [this, page]() {
            return page == m_page ? nxui::I18n::instance().tr("cards.page_current", "Current page") : std::string();
        };
        r.onA = [this, page]() {
            m_sheet.close();
            goToPage(page, 0);
        };
        rows.push_back(std::move(r));
    }
    auto add = [&rows](std::string label, std::function<void()> fn) {
        CardsSheet::Row r;
        r.label = std::move(label);
        r.onA = std::move(fn);
        rows.push_back(std::move(r));
    };
    add(i18n.tr("cards.menu_news", "News"), [this]() {
        m_sheet.close();
        openNewsSheet();
    });
    add(i18n.tr("cards.menu_users", "Users"), [this]() { openUsersSheet(); });
    add(i18n.tr("cards.menu_quick", "Quick settings"), [this]() {
        m_sheet.close();
        openQuickSettings();
    });
    add(i18n.tr("cards.menu_notifications", "Notifications"), [this]() { openNotificationsSheet(); });
    add(homebrewUpdateCount() > 0
            ? i18n.tr("cards.menu_updates", "Updates") + " (" + std::to_string(homebrewUpdateCount()) + ")"
            : i18n.tr("cards.menu_updates", "Updates"),
        [this]() { openHomebrewUpdates(); });
    add(i18n.tr("cards.menu_achievements", "Achievements"), [this]() {
        const bool game = m_selected >= 0 && m_selected < m_catalog.count() && !m_catalog.at(m_selected).system;
        openAchievementsSheet(game ? m_catalog.at(m_selected).app.titleId : 0);
    });
    add(i18n.tr("settings.title", "Settings"), [this]() { openSettingsSheet(); });
    add(i18n.tr("power.title", "Power"), [this]() {
        m_sheet.close();
        m_topIndex = static_cast<int>(TopButton::Power);
        activateTop();
    });
#ifdef SWITCHU_MENU
    if (m_core.launcher().suspendedTitleId() != 0) {
        add(i18n.tr("cards.close_software", "Close software"), [this]() {
            m_sheet.close();
            // The suspended title may live on another page: go there first.
            const std::uint64_t id = runningCardId();
            const int home = pages::isHomebrewId(id) ? AppConfig::CardsPrefs::kHomebrew
                           : m_core.config().cards.isMedia(id) ? AppConfig::CardsPrefs::kMedia
                                                               : AppConfig::CardsPrefs::kGames;
            if (m_catalog.indexOf(id) < 0)
                goToPage(home, 0);
            const int idx = m_catalog.indexOf(id);
            if (idx >= 0) {
                m_selected = idx;
                m_scroll = static_cast<float>(idx);
                m_closeConfirmQueued = true;
            }
        });
    }
#endif
    m_sheet.open(i18n.tr("cards.menu_title", "Menu"), {{std::string(), std::move(rows)}});
}

// ── Pages (E1, C-PG-1/3/4/5) ────────────────────────────────────────────────
void CardsHomeActivity::goToPage(int page, int dir) {
    if (page == m_page)
        return;
    if (m_moving)
        finishMove(true);
    if (m_selected >= 0 && m_selected < m_catalog.count())
        m_pageFocus[m_page] = m_catalog.at(m_selected).app.titleId;
    if (dir == 0) {
        const auto list = pages::shown(m_core.config().cards);
        const auto a = std::find(list.begin(), list.end(), m_page);
        const auto b = std::find(list.begin(), list.end(), page);
        dir = b > a ? 1 : -1;
    }
    m_page = page;
    m_catalog.setPage(page, m_core.config());
    const auto it = m_pageFocus.find(page);
    const int idx = it != m_pageFocus.end() ? m_catalog.indexOf(it->second) : -1;
    m_selected = std::max(0, idx);
    m_scroll = static_cast<float>(m_selected);
    m_pageDir = dir > 0 ? 1 : -1;
    m_pageIn = 0.f;
    m_panelZone = false;
    m_bgPrevId = m_bgId;
    m_bgId = 0;
    m_detailChangedAt = m_time;
    m_audio.playSfx(Sfx::PageChange);
    DebugLog::log("[cards] page %d (%d cards)", page, m_catalog.count());
}

void CardsHomeActivity::drawPageDots(nxui::Renderer& ren, const nxui::Rect& p) {
    const auto list = pages::shown(m_core.config().cards);
    const float gap = 14.f;
    float x = p.right() - 40.f - gap * static_cast<float>(list.size() - 1);
    for (int page : list) {
        const bool cur = page == m_page;
        ren.drawCircle({x, p.bottom() - 16.f}, cur ? 4.5f : 3.5f, {1.f, 1.f, 1.f, cur ? 0.95f : 0.35f}, 12);
        x += gap;
    }
}

void CardsHomeActivity::openSystemTile(std::uint64_t id) {
    using T = pages::SystemTile;
    const T tile = pages::systemTileOf(id);
    if (tile == T::News) {
        openNewsSheet();
        return;
    }
    if (tile == T::Users) {
        openUsersSheet();
        return;
    }
    if (tile == T::HomeSettings) {
        openSettingsSheet();
        return;
    }
    if (tile == T::Eshop || tile == T::SwitchOnline || tile == T::Friends) {
        auto& i18n = nxui::I18n::instance();
        m_audio.playSfx(Sfx::ModalShow);
        m_userSelect->show(
            i18n.tr("cards.safe_blocked_title", "Online service unavailable"),
            i18n.tr("cards.safe_blocked_desc",
                    "This online service is blocked by default to protect your console from bans."),
            {{i18n.tr("button.ok", "OK"), []() {}, true}}, 0, {});
        focusManager().setFocus(m_userSelect.get());
        return;
    }
#ifdef SWITCHU_MENU
    if (m_core.launcher().suspendedTitleId() != 0) {
        // An applet over suspended software needs the daemon's applet queue
        // (todo F1); refuse, as the launch path does.
        m_audio.playSfx(Sfx::ModalShow);
        showToast(nxui::I18n::instance().tr("cards.suspended_body",
                                            "Close the suspended software first (X on its card)."));
        return;
    }
    if (m_launching || m_leaving)
        return;
    m_audio.playSfx(Sfx::Activate);
    DebugLog::log("[cards] system tile %d", static_cast<int>(tile));
    // Save the leave frame first so the applet returns to this page and tile
    // (console run 2026-11-09: it came back to the Games page).
    m_launching = true;
    m_launchElapsed = 0.f;
    scheduleLeaveCapture([this, tile]() {
        auto& launcher = m_core.launcher();
        switch (tile) {
        case T::Album: launcher.launchAlbum(); break;
        case T::Controllers: launcher.launchControllerPairing(); break;
        case T::MiiEditor: launcher.launchMiiEditor(); break;
        case T::Internet: launcher.launchNetConnect(); break;
        default: m_launching = false; break;
        }
    });
#endif
}

// ── Sort and Move mode (E2, C-IN-5) ─────────────────────────────────────────
void CardsHomeActivity::openSortSheet() {
    auto& i18n = nxui::I18n::instance();
    if (m_page == AppConfig::CardsPrefs::kNintendo)
        return;
    if (m_page == AppConfig::CardsPrefs::kMedia) {
        showToast(i18n.tr("cards.media_order", "Media keeps your order: + > Move"));
        return;
    }
    if (!m_userSelect)
        return;
    m_audio.playSfx(Sfx::ModalShow);
    m_userSelect->show(
        i18n.tr("cards.sort", "Sort"),
        i18n.tr("cards.sort_desc", "Choose the order of your software."),
        {
            {i18n.tr("cards.sort_recent", "Last played"), [this]() { applySort(0); }, true},
            {i18n.tr("cards.sort_title", "Title"), [this]() { applySort(1); }, true},
            {i18n.tr("cards.sort_played", "Most played"), [this]() { applySort(2); }, true},
            {i18n.tr("cards.sort_mine", "My order"), [this]() { applySort(3); }, true},
        },
        m_core.config().cardsSort, {});
    focusManager().setFocus(m_userSelect.get());
}

void CardsHomeActivity::applySort(int mode) {
    auto& config = m_core.config();
    if (config.cardsSort == mode)
        return;
    // "My order" chosen for the first time starts from what is shown now.
    if (mode == 3 && config.cards.gamesOrder.empty() && m_page == AppConfig::CardsPrefs::kGames)
        config.cards.gamesOrder = m_catalog.pageIds();
    config.cardsSort = mode;
    saveConfig("cards sort");
    if (m_catalog.count() == 0)
        return;
    const std::uint64_t keep = m_catalog.at(m_selected).app.titleId;
    m_catalog.sort(config);
    const int index = m_catalog.indexOf(keep);
    m_selected = index >= 0 ? index : 0;
    m_scroll = static_cast<float>(m_selected);
    DebugLog::log("[cards] sort %d", mode);
}

void CardsHomeActivity::startMove() {
    if (m_page == AppConfig::CardsPrefs::kNintendo || m_catalog.count() < 2)
        return;
    m_moveBackup = m_catalog.pageIds();
    m_moving = true;
    m_moveFrom = m_selected;
    m_panelZone = false;
    m_topZone = false;
    m_repeatDir = 0;
    m_audio.playSfx(Sfx::Activate);
}

void CardsHomeActivity::finishMove(bool keep) {
    if (!m_moving)
        return;
    m_moving = false;
    m_repeatDir = 0;
    auto& config = m_core.config();
    if (!keep) {
        const std::uint64_t id = m_catalog.count() > 0 ? m_catalog.at(m_selected).app.titleId : 0;
        // Put every card back where it was (selection sort by backup order).
        for (int i = 0; i < m_catalog.count() && i < static_cast<int>(m_moveBackup.size()); ++i) {
            const int at = m_catalog.indexOf(m_moveBackup[static_cast<std::size_t>(i)]);
            if (at >= 0 && at != i)
                m_catalog.swapEntries(i, at);
        }
        if (m_playtimeSortDeferred && config.cardsSort == 2)
            m_catalog.sort(config);
        m_playtimeSortDeferred = false;
        m_selected = std::max(0, m_catalog.indexOf(id));
        m_scroll = static_cast<float>(m_selected);
        m_audio.playSfx(Sfx::ModalHide);
        return;
    }
    const auto ids = m_catalog.pageIds();
    if (ids == m_moveBackup) {
        if (m_playtimeSortDeferred && config.cardsSort == 2) {
            const auto id = m_catalog.at(m_selected).app.titleId;
            m_catalog.sort(config);
            m_selected = std::max(0, m_catalog.indexOf(id));
        }
        m_playtimeSortDeferred = false;
        return;
    }
    m_playtimeSortDeferred = false;
    if (m_page == AppConfig::CardsPrefs::kMedia) {
        config.cards.mediaOrder = ids;
    } else {
        config.cards.gamesOrder = ids;
        config.cardsSort = 3; // C-PG-1: moving a card makes "my order" the sort
    }
    saveConfig("cards move");
    m_audio.playSfx(Sfx::ConfirmPositive);
    DebugLog::log("[cards] move saved (page %d)", m_page);
}

void CardsHomeActivity::handleMoveInput() {
    auto& in = app().input();
    using B = nxui::Button;
    if (in.isDown(B::B)) {
        finishMove(false);
        return;
    }
    if (in.isDown(B::A) || in.isDown(B::Plus)) {
        finishMove(true);
        return;
    }
    const bool left = in.isHeld(B::DLeft) || in.isHeld(B::LStickL);
    const bool right = in.isHeld(B::DRight) || in.isHeld(B::LStickR);
    const int dir = left == right ? 0 : (left ? -1 : 1);
    auto carry = [this](int d) {
        const int next = m_selected + d;
        if (next < 0 || next >= m_catalog.count())
            return;
        m_catalog.swapEntries(m_selected, next);
        m_selected = next;
        m_audio.playSfx(Sfx::Navigate);
    };
    if (dir != 0 && dir != m_repeatDir) {
        m_repeatDir = dir;
        m_repeatTimer = kMoveRepeatDelay;
        carry(dir);
    } else if (dir != 0) {
        m_repeatTimer -= 1.f / 60.f;
        if (m_repeatTimer <= 0.f) {
            m_repeatTimer = kMoveRepeatRate;
            carry(dir);
        }
    } else {
        m_repeatDir = 0;
    }
}

void CardsHomeActivity::drawMoveHint(nxui::Renderer& ren) {
    if (!m_moving)
        return;
    auto& i18n = nxui::I18n::instance();
    const std::string hint = i18n.tr("cards.move_hint", "Left/Right  Move     A  Drop     B  Cancel");
    auto& f = m_chrome.labelFont();
    const float w = f.measure(hint).x + 48.f;
    const nxui::Rect box{640.f - w * 0.5f, 104.f, w, 44.f};
    ren.drawRoundedRect(box, CardsChrome::accent(0.85f), 22.f);
    m_chrome.textMid(ren, f, hint, box.x + 24.f, box.y + 22.f, CardsChrome::kWhite);
}

// ── Card options (E4 safe part, C-UI-3) ─────────────────────────────────────
// Delete with confirmation is E4 (batch 2); downloads are G4.
void CardsHomeActivity::openCardOptions() {
    if (m_selected < 0 || m_selected >= m_catalog.count())
        return;
    auto& i18n = nxui::I18n::instance();
    const CardEntry& card = m_catalog.at(m_selected);
    if (card.system) {
        openSystemTile(card.app.titleId);
        return;
    }
    const std::uint64_t titleId = card.app.titleId;
    const std::string title = card.app.title;
    m_audio.playSfx(Sfx::ModalShow);
    std::vector<CardsSheet::Row> rows;
    auto add = [&rows](std::string label, std::function<void()> fn, std::string detail = {}) {
        CardsSheet::Row r;
        r.label = std::move(label);
        r.onA = std::move(fn);
        r.detail = std::move(detail);
        rows.push_back(std::move(r));
    };
#ifdef SWITCHU_MENU
    const bool suspended = isRunningCard(titleId);
#else
    const bool suspended = false;
#endif
    add(suspended ? i18n.tr("cards.resume", "Resume") : i18n.tr("cards.play", "Play"), [this]() {
        m_sheet.close();
        activateSelected();
    });
    add(i18n.tr("cards.opt_move", "Move"), [this]() {
        m_sheet.close();
        startMove();
    });
    add(i18n.tr("dialog.details_rename", "Rename"), [this]() {
        m_sheet.close();
        renameSelected();
    });
    const bool media = m_page == AppConfig::CardsPrefs::kMedia;
    add(media ? i18n.tr("cards.opt_to_games", "Move to Games") : i18n.tr("cards.opt_to_media", "Move to Media"),
        [this, titleId, media]() {
            m_sheet.close();
            auto& config = m_core.config();
            if (!pages::setMedia(config.cards, titleId, !media))
                return;
            if (!media && config.cards.pageHidden(AppConfig::CardsPrefs::kMedia))
                pages::togglePage(config.cards, AppConfig::CardsPrefs::kMedia);
            saveConfig("cards media");
            m_catalog.rebuild(config);
            m_selected = std::clamp(m_selected, 0, std::max(0, m_catalog.count() - 1));
            m_scroll = static_cast<float>(m_selected);
            auto& t = nxui::I18n::instance();
            showToast(media ? t.tr("cards.moved_games", "Moved to Games") : t.tr("cards.moved_media", "Moved to Media"));
        });
    const bool isPinned = (m_core.config().cards.panelTitleId == titleId);
    if (!isPinned) {
        add(i18n.tr("cards.panel_pin", "Put on panel button"), [this, titleId]() {
            m_core.config().cards.panelTitleId = titleId;
            m_core.config().cards.panelButtonEnabled = true;
            saveConfig("cards panel shortcut");
            m_sheet.close();
            showToast(nxui::I18n::instance().tr("cards.panel_pinned_toast", "Placed on panel button"));
        });
    } else {
        add(i18n.tr("cards.panel_unpin", "Remove from panel button"), [this]() {
            m_core.config().cards.panelTitleId = 0;
            saveConfig("cards panel shortcut");
            m_sheet.close();
            showToast(nxui::I18n::instance().tr("cards.panel_unpinned_toast", "Removed from panel button (News restored)"));
        });
    }
    add(i18n.tr("cards.art_choices", "Images"), [this, titleId]() { openArtSheet(titleId); });
    add(i18n.tr("cards.menu_achievements", "Achievements"), [this, titleId]() { openAchievementsSheet(titleId); });
    char id[24];
    std::snprintf(id, sizeof(id), "%016llX", static_cast<unsigned long long>(titleId));
    add(i18n.tr("game.title_id", "Title ID"), {}, id);
#ifdef SWITCHU_MENU
    if (suspended) {
        add(i18n.tr("cards.close_software", "Close software"), [this]() {
            m_sheet.close();
            m_closeConfirmQueued = true;
        });
    }
    // E4. A game card title cannot be deleted from the console, and the
    // running title has to be closed first (ns refuses it while it runs).
    if (card.app.isGameCard()) {
        add(i18n.tr("game.delete_software", "Delete Software"), {},
            i18n.tr("game.delete_gamecard_unavailable",
                    "Game card software cannot be deleted from the console."));
        rows.back().enabled = false;
    } else if (suspended) {
        add(i18n.tr("game.delete_software", "Delete Software"), {},
            i18n.tr("cards.delete_close_first", "Close the software before deleting it."));
        rows.back().enabled = false;
    } else {
        add(i18n.tr("game.delete_software", "Delete Software"), [this, titleId]() {
                m_sheet.close();
                m_deleteConfirmId = titleId;
            },
            i18n.tr("game.delete_software_desc", "Deletes the software from the console. Save data is not deleted."));
    }
#endif
    m_sheet.open(title, {{std::string(), std::move(rows)}});
}

#ifdef SWITCHU_MENU
// ── Delete software (E4, C-UI-3) ────────────────────────────────────────────
// Same contract as Classic's confirmDeleteSoftware/startSoftwareDeletion: ns
// removes what it installed, then the SD footprint (ports, LayeredFS) is swept;
// both run on the Core pool behind a progress dialog so the frame never blocks.
bool CardsHomeActivity::deleteRunning() const {
    return m_deleteFuture.valid() &&
           m_deleteFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

void CardsHomeActivity::confirmDeleteSoftware() {
    const std::uint64_t titleId = m_deleteConfirmId;
    m_deleteConfirmId = 0;
    const int index = m_catalog.indexOf(titleId);
    if (!m_userSelect || index < 0 || deleteRunning())
        return;
    const AppEntry& app = m_catalog.at(index).app;
    if (app.isGameCard() || m_core.launcher().isAppSuspended(titleId) ||
        switchu::titles::isProtectedTitleId(titleId))
        return;
    auto& i18n = nxui::I18n::instance();
    const std::string title = app.title;
    std::string body = title + "\n\n" +
        i18n.tr("dialog.delete_software_body",
                "This removes the software from the console. It cannot be undone "
                "here -- the software has to be downloaded or installed again.");
    const auto footprint = switchu::titles::sdFootprint(titleId);
    if (!footprint.empty()) {
        body += "\n\n";
        body += i18n.tr("dialog.delete_software_files", "These folders on the SD card will be removed too:");
        for (const std::string& path : footprint)
            body += "\n" + path;
    }
    m_audio.playSfx(Sfx::ModalShow);
    // Cancel is the default button: an accidental A does not delete.
    m_userSelect->show(
        i18n.tr("dialog.delete_software", "Delete software?"), body,
        {
            {i18n.tr("button.cancel", "Cancel"), []() {}, true},
            {i18n.tr("button.delete", "Delete"), [this, titleId, title]() {
                 startDeleteSoftware(titleId, title);
             }, true},
        },
        0, {});
    focusManager().setFocus(m_userSelect.get());
}

void CardsHomeActivity::startDeleteSoftware(std::uint64_t titleId, const std::string& title) {
    if (deleteRunning())
        return;
    auto& i18n = nxui::I18n::instance();
    if (!m_deleteProgress) {
        m_deleteProgress = std::make_shared<ProgressDialog>();
        m_deleteProgress->setFont(&m_font);
        m_deleteProgress->setSmallFont(&m_fontSmall);
        m_deleteProgress->setTheme(&m_overlayTheme);
        m_deleteProgress->setRect({0, 0, 1280, 720});
    }
    m_deleteTitleId = titleId;
    m_deleteTitle = title;
    m_deleteResult = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    m_deleteSdRemoved = 0;
    m_deleteSdFailure.clear();
    m_deleteDone.store(0);
    m_deleteTotal.store(0);
    m_deleteProgress->show(i18n.tr("game.uninstall_progress_title", "Deleting software"),
                           i18n.tr("game.uninstall_progress", "Please wait. Do not turn off the console.") +
                               std::string("\n") + title,
                           -1.f);
    DebugLog::log("[cards] delete queued title=%016llX", static_cast<unsigned long long>(titleId));
    m_deleteFuture = m_core.threadPool().submit([this, titleId]() {
        m_deleteTotal.store(switchu::titles::countSdFootprint(titleId));
        const Result nsRc = switchu::menu::ensureNsService("cards-delete");
        m_deleteResult = R_SUCCEEDED(nsRc) ? nsDeleteApplicationCompletely(titleId) : nsRc;
        const auto report = switchu::titles::removeSdFootprint(titleId, &m_deleteDone);
        m_deleteSdRemoved = report.removed;
        m_deleteSdFailure = report.firstFailure;
    });
}

void CardsHomeActivity::pollDeleteSoftware() {
    if (!m_deleteFuture.valid())
        return;
    auto& i18n = nxui::I18n::instance();
    if (deleteRunning()) {
        const std::uint64_t total = m_deleteTotal.load();
        const std::uint64_t done = m_deleteDone.load();
        std::string message = i18n.tr("game.uninstall_progress", "Please wait. Do not turn off the console.") +
                              std::string("\n") + m_deleteTitle;
        if (total > 0) {
            char count[48];
            std::snprintf(count, sizeof(count), "\n%llu/%llu", static_cast<unsigned long long>(done),
                          static_cast<unsigned long long>(total));
            message += count;
            m_deleteProgress->updateState(message, std::min(1.f, static_cast<float>(done) / static_cast<float>(total)));
        } else {
            m_deleteProgress->updateState(message, -1.f);
        }
        return;
    }
    Result rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    try {
        m_deleteFuture.get();
        rc = m_deleteResult;
    } catch (...) {
        DebugLog::log("[cards] delete worker exception");
    }
    m_deleteProgress->hide();
    m_overlayLatch = true;
    const bool sweepFailed = !m_deleteSdFailure.empty();
    DebugLog::log("[cards] delete ns rc=0x%08X sd-removed=%d sd-failure=%s", rc, m_deleteSdRemoved,
                  sweepFailed ? m_deleteSdFailure.c_str() : "none");
    // A port copied to the card has nothing registered with ns: the ns call
    // fails while the sweep succeeds (same rule as Classic).
    if (!sweepFailed && (R_SUCCEEDED(rc) || m_deleteSdRemoved > 0)) {
        const std::uint64_t id = m_deleteTitleId;
        auto& config = m_core.config();
        switchu::control_cache::forget(id);
        bool dirty = pages::forgetTitle(config.cards, id);
        if (config.hasCustomTitle(id)) {
            config.setCustomTitle(id, "");
            dirty = true;
        }
        if (dirty)
            saveConfig("cards delete");
        // Keep the focus where the card was; the refresh lands on a neighbour.
        const int at = m_catalog.indexOf(id);
        if (at >= 0 && m_catalog.count() > 1) {
            const int next = at + 1 < m_catalog.count() ? at + 1 : at - 1;
            m_selected = next;
            m_scroll = static_cast<float>(m_selected);
        }
        m_audio.playSfx(Sfx::ConfirmPositive);
        showToast(i18n.tr("dialog.delete_software_done", "The software was deleted."));
        m_refreshQueued = true;
        m_refreshDelayFrames = 3;
        return;
    }
    char code[96];
    std::snprintf(code, sizeof(code), "\nResult: 0x%08X (module %u, description %u)", rc, R_MODULE(rc),
                  R_DESCRIPTION(rc));
    const std::string details = sweepFailed
        ? i18n.tr("settings.storage.uninstall_failed_files", "Some files could not be removed from the SD card.") +
              std::string("\n") + m_deleteSdFailure
        : i18n.tr("settings.storage.uninstall_failed", "Failed to uninstall the selected title.") + std::string(code);
    m_audio.playSfx(Sfx::ModalShow);
    m_userSelect->show(i18n.tr("game.uninstall_failed", "Uninstall failed"), m_deleteTitle + "\n\n" + details,
                       {{i18n.tr("button.ok", "OK"), []() {}, true}}, 0, {});
    focusManager().setFocus(m_userSelect.get());
    // ns may have removed part of it: reload so the card reflects the console.
    m_refreshQueued = true;
    m_refreshDelayFrames = 3;
}
#endif

void CardsHomeActivity::requestText(const std::string& title, const std::string& guide, const std::string& initial,
                                    std::function<void(const std::string&)> onAccept) {
    if (!m_text)
        return;
    TextEntryScreen::Request req;
    req.title = title;
    req.guide = guide;
    req.initial = initial;
    req.maxLength = 128;
    m_text->onAccept([onAccept](const std::string& v) {
        if (onAccept)
            onAccept(v);
    });
    m_text->onCancel([]() {});
    m_text->show(req);
    m_overlayLatch = true;
    focusManager().setFocus(m_text.get());
}

void CardsHomeActivity::renameSelected() {
    if (m_selected < 0 || m_selected >= m_catalog.count() || m_catalog.at(m_selected).system)
        return;
    auto& i18n = nxui::I18n::instance();
    const std::uint64_t titleId = m_catalog.at(m_selected).app.titleId;
    requestText(i18n.tr("dialog.details_rename", "Rename"),
                i18n.tr("dialog.details_rename_guide", "Enter a name. Leave it empty to use the original."),
                m_catalog.at(m_selected).app.title, [this, titleId](const std::string& value) {
                    // Classic's store (customTitles): both frontends show the same name.
                    std::size_t a = value.find_first_not_of(" \t");
                    std::size_t b = value.find_last_not_of(" \t");
                    const std::string v = a == std::string::npos ? std::string() : value.substr(a, b - a + 1);
                    m_core.config().setCustomTitle(titleId, v);
                    saveConfig("cards rename");
                    m_catalog.rebuild(m_core.config());
                    m_selected = std::max(0, m_catalog.indexOf(titleId));
                    m_scroll = static_cast<float>(m_selected);
                });
}

// ── Users (E5 safe part, C-UI-10) and notifications (C-UI-11) ───────────────
void CardsHomeActivity::openUsersSheet() {
    m_sheet.close();
#ifdef SWITCHU_MENU
    if (!m_userSelect)
        return;
    auto& i18n = nxui::I18n::instance();
    m_audio.playSfx(Sfx::ModalShow);
    m_userSelect->loadUsers(app().gpu(), app().renderer());
    m_userSelect->showUserSwitcher(
        [this](AccountUid picked) { m_core.launcher().launchUserPage(picked); },
        [this]() { m_core.launcher().launchUserCreator(); },
        i18n.tr("userselect.add_user", "Add user"));
    focusManager().setFocus(m_userSelect.get());
#endif
}

void CardsHomeActivity::openNotificationsSheet() {
    auto& i18n = nxui::I18n::instance();
    m_audio.playSfx(Sfx::ModalShow);
    std::vector<CardsSheet::Row> rows;
#ifdef SWITCHU_MENU
    // Local sources first (Discord is Part H): low controller batteries.
    auto lowBattery = [&rows, &i18n](const std::string& who, const HidPowerInfo& info) {
        if (info.battery_level > 1 || info.is_charging)
            return;
        CardsSheet::Row r;
        r.label = i18n.tr("cards.notify_battery", "Controller battery low") + " (" + who + ")";
        r.detail = std::to_string(static_cast<int>(info.battery_level) * 25) + "%";
        rows.push_back(std::move(r));
    };
    if (hidGetNpadStyleSet(HidNpadIdType_Handheld) != 0) {
        HidPowerInfo l{}, r{};
        hidGetNpadPowerInfoSplit(HidNpadIdType_Handheld, &l, &r);
        lowBattery("L", l);
        lowBattery("R", r);
    }
    for (int player = 0; player < 8; ++player) {
        const auto id = static_cast<HidNpadIdType>(HidNpadIdType_No1 + player);
        const u32 style = hidGetNpadStyleSet(id);
        if (style == 0 || (style & HidNpadStyleTag_NpadHandheld))
            continue;
        const std::string who = "P" + std::to_string(player + 1);
        if (style & HidNpadStyleTag_NpadJoyDual) {
            HidPowerInfo l{}, r{};
            hidGetNpadPowerInfoSplit(id, &l, &r);
            lowBattery(who + " L", l);
            lowBattery(who + " R", r);
        } else {
            HidPowerInfo info{};
            hidGetNpadPowerInfoSingle(id, &info);
            lowBattery(who, info);
        }
    }
#endif
    if (m_core.discord().state() == discord::DiscordService::State::Ready) {
        for (const auto& d : m_core.discord().dms()) {
            CardsSheet::Row r;
            r.label = d.name.empty() ? "Discord DM" : "Discord: " + d.name;
            r.detail = d.topic;
            r.onA = [this, chanId = d.id, name = d.name]() {
                m_sheet.close();
                openDiscord();
                if (m_discordPanel) m_discordPanel->openChat(chanId, 0, name);
            };
            rows.push_back(std::move(r));
        }
    }
    if (rows.empty()) {
        CardsSheet::Row r;
        r.label = i18n.tr("cards.notify_empty", "All caught up");
        r.enabled = false;
        rows.push_back(std::move(r));
    }
    m_sheet.open(i18n.tr("cards.menu_notifications", "Notifications"), {{std::string(), std::move(rows)}});
}

// ── Achievements (E11, C-UI-5) ──────────────────────────────────────────────
void CardsHomeActivity::openAchievementsSheet(std::uint64_t titleId) {
    auto& i18n = nxui::I18n::instance();
    if (titleId == 0) {
        m_sheet.close();
        showToast(i18n.tr("cards.ach_pick", "Choose a game first"));
        return;
    }
    namespace ach = achievements;
    m_audio.playSfx(Sfx::ModalShow);
    // Play time: the cached pdm figure (ns). Launch count: one pdm query on
    // the pool; the rows show "..." until it lands.
    struct State {
        ach::Play play;               // written by the worker before `ready`
        std::atomic<bool> ready{false};
        std::atomic<bool> saveFailed{false};
        std::atomic<bool> queryFailed{false};
    };
    auto st = std::make_shared<State>();
    st->play.seconds = m_core.config().playtimeOf(titleId) / 1000000000ULL;
    try {
        // Launch count, then load -> record -> save as one serialized
        // transaction (CardsAchievements recordAndSave), all off the UI thread.
        m_core.threadPool().submit([st, titleId]() {
#ifdef SWITCHU_MENU
            if (auto n = switchu::menu::playtime::queryLaunches(titleId))
                st->play.launches = *n;
            else st->queryFailed.store(true);
#endif
            bool ok = true;
            std::vector<const ach::Milestone*> fresh;
            try { fresh = ach::recordAndSave(ach::kPath, titleId, st->play,
                                                  static_cast<std::int64_t>(std::time(nullptr)), ok); }
            catch (...) { ok = false; }
            if (!ok) {
                st->saveFailed.store(true);
                DebugLog::log("[cards] achievements save failed (%zu new kept in memory)", fresh.size());
            }
            st->ready.store(true);
        });
    } catch (...) {
        st->queryFailed.store(true);
        st->ready.store(true);
    }
    std::vector<CardsSheet::Row> rows;
    for (const auto& m : ach::kPerGame) {
        const ach::Milestone* mp = &m;
        CardsSheet::Row r;
        r.label = i18n.tr(m.key, m.name);
        r.detail = m.about;
        r.progress = [st, mp]() { return st->ready.load() ? ach::progress(*mp, st->play) : 0.f; };
        r.value = [st, mp]() -> std::string {
            if (!st->ready.load())
                return "...";
            if (st->saveFailed.load()) return nxui::I18n::instance().tr("cards.save_failed", "Save failed");
            if (!mp->hours && st->queryFailed.load()) return nxui::I18n::instance().tr("cards.query_failed", "Unavailable");
            const float f = ach::progress(*mp, st->play);
            if (f >= 1.f)
                return nxui::I18n::instance().tr("cards.ach_done", "Earned");
            return std::to_string(static_cast<int>(f * 100.f)) + "%";
        };
        rows.push_back(std::move(r));
    }    const int idx = m_catalog.indexOf(titleId);
    const std::string title = idx >= 0 ? m_catalog.at(idx).app.title : std::string();
    m_sheet.open(i18n.tr("cards.menu_achievements", "Achievements") + (title.empty() ? "" : " - " + title),
                 {{std::string(), std::move(rows)}});
}

// ── Quick settings (E6, C-UI-8) ─────────────────────────────────────────────
void CardsHomeActivity::openQuickSettings() {
    if (!m_quick) {
        m_quick = std::make_shared<QuickSettingsOverlay>();
        m_quick->setFont(&m_font);
        m_quick->setSmallFont(&m_fontSmall);
        m_quick->setIconFont(&m_fontIcons);
        m_quick->setTheme(&m_overlayTheme);
        m_quick->setInput(&app().input());
        QuickSettingsOverlay::Callbacks cbs;
        cbs.onBgmVolumeChanged = [this](float v) {
            m_audio.setVolume(v);
            m_core.config().musicVolume = v;
        };
        cbs.onSfxVolumeChanged = [this](float v) {
            m_audio.setSfxVolume(v);
            m_core.config().sfxVolume = v;
        };
        cbs.onSleepRequested = [this]() {
            closeQuickSettings();
#ifdef SWITCHU_MENU
            m_core.launcher().enterSleep();
#endif
        };
        // Restart / turn off go through the confirmation dialog.
        auto power = [this]() {
            closeQuickSettings();
            m_topIndex = static_cast<int>(TopButton::Power);
            activateTop();
        };
        cbs.onRebootRequested = power;
        cbs.onShutdownRequested = power;
        cbs.onActivityLogRequested = [this]() {
            closeQuickSettings();
            showToast(nxui::I18n::instance().tr("cards.coming_soon", "Coming in a later update"));
        };
        cbs.onClose = [this]() { closeQuickSettings(); };
        cbs.onNavigateSfx = [this]() { m_audio.playSfx(Sfx::Navigate); };
        cbs.onActivateSfx = [this]() { m_audio.playSfx(Sfx::Activate); };
        cbs.onToggleOffSfx = [this]() { m_audio.playSfx(Sfx::ToggleOff); };
        m_quick->setCallbacks(cbs);
    }
    if (m_quick->isActive())
        return;
    m_quick->setInitialValues(0.5f, m_audio.volume(), m_audio.sfxVolume(), false, true);
    m_quick->setBatteryStatus(m_status.batteryPercent, m_status.charging);
    m_quick->show();
    m_overlayLatch = true;
    focusManager().setFocus(m_quick.get());
    m_audio.playSfx(Sfx::ModalShow);
}

void CardsHomeActivity::closeQuickSettings() {
    if (!m_quick || !m_quick->isActive())
        return;
    m_quick->hide();
    saveConfig("cards quick settings");
}

// ── Settings sheet (E7, C-ST / C-UI-14) ─────────────────────────────────────
// Ten-section framework. Unimplemented controls are explicitly marked with their owning step.
std::vector<CardsSheet::Section> CardsHomeActivity::settingsSections() {
    auto& i18n = nxui::I18n::instance();
    auto reopen = [this]() {
        const int s = m_sheet.section(), r = m_sheet.row();
        m_sheet.open(m_sheet.title(), settingsSections(), s, r);
        m_sheet.onReorderSection([this](int d) { reorderSettingsSection(d); });
    };
    using Row = CardsSheet::Row;
    std::map<std::string, CardsSheet::Section> all;

    { // Home: pages (C-PG-5), sort, ask user, HOME style.
        CardsSheet::Section s{i18n.tr("cards.set_home", "Home"), {}};
        for (int page : m_core.config().cards.pageOrder) {
            Row r;
            r.label = i18n.tr(pageKey(page), pageFallback(page));
            r.detail = i18n.tr("cards.set_page_hint", "A: show or hide   Left/Right: order");
            r.value = [this, page]() { return onOff(!m_core.config().cards.pageHidden(page)); };
            r.onA = [this, page, reopen]() {
                if (!pages::togglePage(m_core.config().cards, page)) {
                    showToast(nxui::I18n::instance().tr("cards.page_last", "One page always shows"));
                    return;
                }
                saveConfig("cards pages");
                if (m_core.config().cards.pageHidden(m_page))
                    goToPage(pages::first(m_core.config().cards), 0);
                reopen();
            };
            r.onChange = [this, page, reopen](int d) {
                if (pages::movePage(m_core.config().cards, page, d)) {
                    saveConfig("cards page order");
                    reopen();
                }
            };
            s.rows.push_back(std::move(r));
        }
        Row sort;
        sort.label = i18n.tr("cards.sort", "Sort");
        sort.value = [this]() {
            auto& t = nxui::I18n::instance();
            switch (m_core.config().cardsSort) {
            case 1: return t.tr("cards.sort_title", "Title");
            case 2: return t.tr("cards.sort_played", "Most played");
            case 3: return t.tr("cards.sort_mine", "My order");
            default: return t.tr("cards.sort_recent", "Last played");
            }
        };
        sort.onChange = [this](int d) {
            const int n = AppConfig::kCardsSortCount;
            const int next = (m_core.config().cardsSort + d + n) % n;
            if (m_page == AppConfig::CardsPrefs::kGames) {
                applySort(next);
            } else {
                m_core.config().cardsSort = next;
                saveConfig("cards sort");
            }
        };
        s.rows.push_back(std::move(sort));
        Row ask;
        ask.label = i18n.tr("cards.ask_user", "Ask who's playing");
        ask.value = [this]() { return onOff(m_core.config().cardsAskUser); };
        ask.onChange = [this](int) {
            m_core.config().cardsAskUser = !m_core.config().cardsAskUser;
            saveConfig("cards ask user");
        };
        s.rows.push_back(std::move(ask));
        Row panel;
        panel.label = i18n.tr("cards.panel_button", "Panel button");
        panel.value = [this]() {
            auto& t = nxui::I18n::instance();
            if (!m_core.config().cards.panelButtonEnabled)
                return t.tr("cards.panel_button_none", "None");
            const auto pin = m_core.config().cards.panelTitleId;
            if (pin == 0)
                return t.tr("cards.panel_button_news", "News");
            for (const auto& a : m_catalog.all())
                if (a.titleId == pin) return a.title;
            return t.tr("cards.panel_missing", "Software unavailable");
        };
        panel.onChange = [this, reopen](int) {
            auto& c = m_core.config().cards;
            if (c.panelTitleId != 0) {
                c.panelTitleId = 0;
                c.panelButtonEnabled = true;
            } else if (c.panelButtonEnabled) {
                c.panelButtonEnabled = false;
            } else {
                c.panelButtonEnabled = true;
            }
            saveConfig("cards panel button");
            reopen();
        };
        s.rows.push_back(std::move(panel));
        Row style;
        style.label = i18n.tr("cards.home_style", "HOME style");
        style.value = []() { return nxui::I18n::instance().tr("cards.home_style_cards", "Cards"); };
        style.detail = i18n.tr("cards.home_style_switch_hint", "A: switch to Classic");
        style.onA = [this]() {
            m_sheet.close();
            m_switchQueued = true;
            m_switchTarget = home::HomeStyle::Classic;
        };
        s.rows.push_back(std::move(style));
        Row nextendo;
        nextendo.label = i18n.tr("cards.nextendo_players", "Nextendo players");
        nextendo.value = [this]() {
            return m_core.config().cards.showNextendo
                ? nxui::I18n::instance().tr("setting.on", "On")
                : nxui::I18n::instance().tr("setting.off", "Off");
        };
        nextendo.onChange = [this, reopen](int) {
            auto& c = m_core.config().cards;
            c.showNextendo = !c.showNextendo;
            saveConfig("cards nextendo players");
            reopen();
        };
        s.rows.push_back(std::move(nextendo));
        all["home"] = std::move(s);
    }
    { // Appearance (HomeNX E8, C-SK-6/8: accent, glass, haze, motion).
        CardsSheet::Section s{i18n.tr("cards.set_look", "Appearance"), {}};
        Row acc;
        acc.label = i18n.tr("cards.set_accent", "Highlight colour");
        acc.value = [this]() {
            auto& t = nxui::I18n::instance();
            const int idx = std::clamp(m_core.config().cards.accent, 0, CardsChrome::kAccentCount - 1);
            return t.tr(std::string("cards.accent_") + std::to_string(idx), CardsChrome::kAccents[idx].name);
        };
        acc.detail = i18n.tr("cards.set_accent_desc", "The ring around what is chosen");
        acc.onChange = [this](int d) {
            auto& c = m_core.config();
            c.cards.accent = (c.cards.accent + d + CardsChrome::kAccentCount) % CardsChrome::kAccentCount;
            CardsChrome::setStyle(c.cards.accent, c.cards.glass);
            saveConfig("cards accent");
        };
        s.rows.push_back(std::move(acc));

        Row gl;
        gl.label = i18n.tr("cards.set_glass", "Glass style");
        gl.value = [this]() {
            auto& t = nxui::I18n::instance();
            const int idx = std::clamp(m_core.config().cards.glass, 0, 2);
            return t.tr(std::string("cards.glass_") + std::to_string(idx), CardsChrome::kGlassNames[idx]);
        };
        gl.detail = i18n.tr("cards.set_glass_desc", "Buttons, the bar and the panel");
        gl.onChange = [this](int d) {
            auto& c = m_core.config();
            c.cards.glass = (c.cards.glass + d + 3) % 3;
            CardsChrome::setStyle(c.cards.accent, c.cards.glass);
            saveConfig("cards glass");
        };
        s.rows.push_back(std::move(gl));

        Row hz;
        hz.label = i18n.tr("cards.set_haze", "Background softness");
        hz.value = [this]() { return std::to_string(m_core.config().cards.haze) + "%"; };
        hz.detail = i18n.tr("cards.set_haze_desc", "Haze over the game's art");
        hz.onChange = [this](int d) {
            auto& c = m_core.config();
            c.cards.haze = std::clamp(c.cards.haze + d * 5, 0, 80);
            saveConfig("cards haze");
        };
        s.rows.push_back(std::move(hz));

        Row bgm;
        bgm.label = i18n.tr("cards.set_bg_motion", "Background drift");
        bgm.value = [this]() { return onOff(m_core.config().cards.bgMotion); };
        bgm.detail = i18n.tr("cards.set_bg_motion_desc", "The background art slowly zooms and pans");
        bgm.onChange = [this](int) {
            auto& c = m_core.config();
            c.cards.bgMotion = !c.cards.bgMotion;
            saveConfig("cards bg motion");
        };
        s.rows.push_back(std::move(bgm));

        Row mot;
        mot.label = i18n.tr("cards.set_motion", "Motion");
        mot.value = [this]() { return onOff(m_core.config().cards.motion); };
        mot.detail = i18n.tr("cards.set_motion_desc", "Smooth sliding animations");
        mot.onChange = [this](int) {
            auto& c = m_core.config();
            c.cards.motion = !c.cards.motion;
            saveConfig("cards motion");
        };
        s.rows.push_back(std::move(mot));

        Row r;
        r.label = i18n.tr("cards.launch_anim", "Launch animation");
        r.value = [this]() {
            auto& t = nxui::I18n::instance();
            return m_core.config().cardsLaunchStyle == 1 ? t.tr("cards.launch_zoom", "Card zoom")
                                                         : t.tr("cards.launch_fade", "Simple fade");
        };
        r.onChange = [this](int) {
            auto& c = m_core.config();
            c.cardsLaunchStyle = (c.cardsLaunchStyle + 1) % AppConfig::kCardsLaunchStyleCount;
            saveConfig("cards launch style");
        };
        s.rows.push_back(std::move(r));
        Row clock;
        clock.label = i18n.tr("cards.set_clock24", "24-hour clock");
        clock.value = [this]() { return onOff(!m_core.config().clockUse12Hour); };
        clock.onChange = [this](int) {
            auto& c = m_core.config();
            c.clockUse12Hour = !c.clockUse12Hour;
            m_lock.setUse12HourClock(c.clockUse12Hour);
            m_statusTimer = 0.f;
            saveConfig("cards clock");
        };
        s.rows.push_back(std::move(clock));
        all["look"] = std::move(s);
    }
    { // Sound: shared with Classic, stored once at the top level.
        CardsSheet::Section s{i18n.tr("cards.set_sound", "Sound"), {}};
        Row music;
        music.label = i18n.tr("cards.set_music", "Music");
        music.value = [this]() { return onOff(m_core.config().musicEnabled); };
        music.onChange = [this](int) {
            auto& c = m_core.config();
            c.musicEnabled = !c.musicEnabled;
            if (c.musicEnabled && m_audioStarted)
                m_audio.play();
            else if (!c.musicEnabled)
                m_audio.stop();
            saveConfig("cards music");
        };
        s.rows.push_back(std::move(music));
        for (const bool sfx : {false, true}) {
            Row r;
            r.label = sfx ? i18n.tr("cards.set_sfx_volume", "Sound volume")
                          : i18n.tr("cards.set_music_volume", "Music volume");
            r.value = [this, sfx]() {
                const float v = sfx ? m_core.config().sfxVolume : m_core.config().musicVolume;
                return std::to_string(static_cast<int>(std::lround(v * 100.f))) + "%";
            };
            r.onChange = [this, sfx](int d) {
                auto& c = m_core.config();
                float& v = sfx ? c.sfxVolume : c.musicVolume;
                v = std::clamp(v + 0.05f * static_cast<float>(d), 0.f, 1.f);
                if (sfx)
                    m_audio.setSfxVolume(v);
                else
                    m_audio.setVolume(v);
                saveConfig("cards volume");
            };
            s.rows.push_back(std::move(r));
        }
        all["sound"] = std::move(s);
    }
    { // Console: the system pages that own network and controllers.
        CardsSheet::Section s{i18n.tr("cards.set_console", "Console"), {}};
        Row net;
        net.label = i18n.tr("cards.tile_internet", "Internet");
        net.onA = [this]() {
            m_sheet.close();
            openSystemTile(pages::systemId(pages::SystemTile::Internet));
        };
        s.rows.push_back(std::move(net));
        Row pads;
        pads.label = i18n.tr("cards.tile_controllers", "Controllers");
        pads.onA = [this]() {
            m_sheet.close();
            openSystemTile(pages::systemId(pages::SystemTile::Controllers));
        };
        s.rows.push_back(std::move(pads));
        Row tut;
        tut.label = i18n.tr("cards.set_tutorial", "Show the welcome tour again");
        tut.onA = [this]() {
            m_sheet.close();
            m_tutorialCard = 1;
            m_tutorialIn = 0.f;
        };
        s.rows.push_back(std::move(tut));
        all["console"] = std::move(s);
    }
    { // About, with the section order (C-UI-14).
        CardsSheet::Section s{i18n.tr("cards.set_about", "About"), {}};
        Row ver;
        ver.label = "OmniLaunch";
        ver.value = []() { return std::string(SWITCHU_VERSION); };
        ver.enabled = false;
        s.rows.push_back(std::move(ver));
        Row games;
        games.label = i18n.tr("cards.set_installed", "Installed software");
        games.value = [this]() { return std::to_string(m_catalog.installedCount()); };
        games.enabled = false;
        s.rows.push_back(std::move(games));
        all["about"] = std::move(s);
    }

    {
        CardsSheet::Section s{i18n.tr("cards.set_lock", "Lock screen"), {}};
        Row r;
        r.label = i18n.tr("cards.lock_now", "Lock now");
        r.onA = [this]() { m_sheet.close(); ensureClockFont(); m_lock.lockNow(); };
        s.rows.push_back(std::move(r));
        // E9 password (C-UI-16), shared with Classic's lock screen.
        Row password;
        password.label = i18n.tr("cards.lock_password", "Password");
        password.value = [this]() {
            auto& t = nxui::I18n::instance();
            return m_lock.passwordActive() ? t.tr("cards.lock_password_on", "On") : t.tr("cards.lock_password_off", "Off");
        };
        password.detail = i18n.tr("cards.lock_password_desc",
                                  "4 to 8 D-pad presses. Forgot it: HOME, then hold Minus + Plus for 3 s.");
        password.onA = [this]() {
            m_sheet.close();
            ensureClockFont();
            m_lock.beginPasswordSetup([this](const std::string& presses) {
#ifdef SWITCHU_MENU
                const bool ok = lockpw::save(lockpw::sha256Hex, presses);
                switchu::commitSdCard("lock password");
                lockpw::attach(m_lock);
                showToast(ok ? nxui::I18n::instance().tr("cards.lock_password_saved", "Password saved")
                             : nxui::I18n::instance().tr("cards.lock_password_failed", "The password could not be saved"));
#else
                (void)presses;
#endif
            });
        };
        s.rows.push_back(std::move(password));
        Row removePw;
        removePw.label = i18n.tr("cards.lock_password_remove", "Remove password");
        removePw.enabled = m_lock.passwordActive();
        removePw.onA = [this]() {
#ifdef SWITCHU_MENU
            lockpw::save(lockpw::sha256Hex, "");
            switchu::commitSdCard("lock password removed");
            lockpw::attach(m_lock);
#endif
            m_sheet.close();
            showToast(nxui::I18n::instance().tr("cards.lock_password_removed", "Password removed"));
        };
        s.rows.push_back(std::move(removePw));
        all["lock"] = std::move(s);
    }
    {
        CardsSheet::Section s{i18n.tr("cards.tile_internet", "Internet"), {}};
        Row r;
        r.label = i18n.tr("cards.internet_settings", "Open console network settings");
        r.onA = [this]() { m_sheet.close(); openSystemTile(pages::systemId(pages::SystemTile::Internet)); };
        s.rows.push_back(std::move(r));
        Row check;
        check.label = i18n.tr("cards.net_check", "Check connection");
        check.detail = i18n.tr("cards.net_check_desc", "Network state and a secure test request");
        check.onA = [this]() { startNetCheck(); };
        s.rows.push_back(std::move(check));
        Row speed;
        speed.label = i18n.tr("cards.speed_test", "Connection test");
        speed.detail = i18n.tr("cards.speed_test_desc", "Ping, download and upload speed");
        speed.onA = [this]() { openSpeedTest(); };
        s.rows.push_back(std::move(speed));
        all["internet"] = std::move(s);
    }
    {
        CardsSheet::Section s{"Bluetooth", {}};
        Row r;
        r.label = "Bluetooth";
        r.value = []() { return bluetooth::IsAvailable() ? onOff(bluetooth::IsRadioEnabled()) : std::string("..."); };
        r.onChange = [this](int) {
            m_core.startBluetooth();
            const Result rc = bluetooth::SetRadioEnabled(!bluetooth::IsRadioEnabled());
            if (R_FAILED(rc)) showToast(nxui::I18n::instance().tr("cards.bluetooth_failed", "Bluetooth change failed"));
        };
        s.rows.push_back(std::move(r));
        Row controllers;
        controllers.label = i18n.tr("cards.tile_controllers", "Controllers");
        controllers.onA = [this]() { m_sheet.close(); openSystemTile(pages::systemId(pages::SystemTile::Controllers)); };
        s.rows.push_back(std::move(controllers));
        all["bluetooth"] = std::move(s);
    }
    {
        CardsSheet::Section s{i18n.tr("cards.art_choices", "Images"), {}};
        Row r;
        r.label = i18n.tr("cards.art_selected", "Images for the selected card");
        r.onA = [this]() {
            if (m_selected >= 0 && m_selected < m_catalog.count() && !m_catalog.at(m_selected).system)
                openArtSheet(m_catalog.at(m_selected).app.titleId);
            else showToast(nxui::I18n::instance().tr("cards.ach_pick", "Choose a game first"));
        };
        s.rows.push_back(std::move(r));

        Row cardPics;
        cardPics.label = i18n.tr("cards.art_card_pictures", "Card pictures");
        cardPics.value = [this]() {
            auto& t = nxui::I18n::instance();
            return m_core.config().cards.boxArt
                ? t.tr("cards.art_box_covers", "Box covers")
                : t.tr("cards.art_eshop_banners", "eShop banners");
        };
        cardPics.onChange = [this](int) {
            auto& c = m_core.config().cards;
            c.boxArt = !c.boxArt;
            saveConfig("cards boxArt");
            m_art.releaseAll();
        };
        s.rows.push_back(std::move(cardPics));

        Row titlePics;
        titlePics.label = i18n.tr("cards.art_title_pictures", "Title logos");
        titlePics.value = [this]() {
            return onOff(m_core.config().cards.titlePics);
        };
        titlePics.onChange = [this](int) {
            auto& c = m_core.config().cards;
            c.titlePics = !c.titlePics;
            saveConfig("cards titlePics");
            m_art.releaseAll();
        };
        titlePics.detail = i18n.tr("cards.art_title_pictures_desc", "Show game logo on background instead of text");
        s.rows.push_back(std::move(titlePics));

        Row saveAll;
        saveAll.label = i18n.tr("cards.art_save_all", "Save all pictures now");
        saveAll.value = [this]() {
            if (saveAllRunning()) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%d / %d", m_saveAllDone.load(), m_saveAllTotal.load());
                return std::string(buf);
            }
            return m_saveAllTotal.load() > 0 ? nxui::I18n::instance().tr("cards.art_save_all_done", "All saved") : std::string();
        };
        saveAll.onA = [this]() {
            startSaveAll(false);
        };
        saveAll.detail = i18n.tr("cards.art_save_all_desc", "Download covers and backgrounds for all games");
        s.rows.push_back(std::move(saveAll));

        Row dlAgain;
        dlAgain.label = i18n.tr("cards.art_download_again", "Download all pictures again");
        dlAgain.onA = [this]() {
            startSaveAll(true);
        };
        dlAgain.detail = i18n.tr("cards.art_download_again_desc", "Refresh artwork from online sources");
        s.rows.push_back(std::move(dlAgain));

        Row sgdbKey;
        sgdbKey.label = i18n.tr("cards.art_sgdb_key", "SteamGridDB API key");
        sgdbKey.value = [this]() {
            auto& t = nxui::I18n::instance();
            return m_core.config().steamGridDbApiKey.empty()
                ? t.tr("cards.art_sgdb_none", "None")
                : t.tr("cards.art_sgdb_set", "Set");
        };
        sgdbKey.onA = [this]() {
            auto& t = nxui::I18n::instance();
            requestText(t.tr("cards.art_sgdb_key", "SteamGridDB API key"),
                        t.tr("cards.art_sgdb_key_desc", "Free at steamgriddb.com (Preferences > API)"),
                        m_core.config().steamGridDbApiKey,
                        [this](const std::string& entered) {
                            std::string key;
                            for (char c : entered)
                                if (!std::isspace(static_cast<unsigned char>(c))) key += c;
                            m_core.config().steamGridDbApiKey = key;
                            saveConfig("cards sgdb key");
                            auto& tr = nxui::I18n::instance();
                            showToast(key.empty()
                                ? tr.tr("cards.art_sgdb_key_removed", "SteamGridDB key removed")
                                : tr.tr("cards.art_sgdb_key_saved", "SteamGridDB key saved"));
                        });
        };
        sgdbKey.detail = i18n.tr("cards.art_sgdb_key_desc", "Free at steamgriddb.com (Preferences > API)");
        s.rows.push_back(std::move(sgdbKey));

        all["images"] = std::move(s);
    }
    {
        CardsSheet::Section s{i18n.tr("cards.set_updates", "Updates"), {}};
        Row version;
        version.label = "OmniLaunch";
        version.value = []() { return std::string(SWITCHU_VERSION); };
        version.enabled = false;
        s.rows.push_back(std::move(version));
        Row hb;
        hb.label = i18n.tr("cards.hb_title", "Homebrew updates");
        hb.value = [this]() {
            const int n = homebrewUpdateCount();
            return n > 0 ? std::to_string(n) : std::string();
        };
        hb.detail = i18n.tr("cards.hb_sources", "Homebrew App Store and GitHub releases");
        hb.onA = [this]() { openHomebrewUpdates(); };
        s.rows.push_back(std::move(hb));
        all["updates"] = std::move(s);
    }
    // Saved order: unknown keys dropped, new keys appended.
    static const char* kKeys[] = {"home", "look", "lock", "sound", "internet", "bluetooth", "console", "images", "updates", "about"};
    auto& order = m_core.config().cards.settingsOrder;
    std::vector<std::string> fixed;
    for (const auto& k : order)
        if (all.count(k) && std::find(fixed.begin(), fixed.end(), k) == fixed.end())
            fixed.push_back(k);
    for (const char* k : kKeys)
        if (std::find(fixed.begin(), fixed.end(), std::string(k)) == fixed.end())
            fixed.push_back(k);
    order = fixed;
    std::vector<CardsSheet::Section> out;
    for (const auto& k : fixed)
        out.push_back(std::move(all[k]));
    return out;
}

void CardsHomeActivity::openSettingsSheet() {
    m_audio.playSfx(Sfx::ModalShow);
    m_sheet.open(nxui::I18n::instance().tr("settings.title", "Settings"), settingsSections());
    m_sheet.onReorderSection([this](int d) { reorderSettingsSection(d); });
}

// ── Welcome tour (E9 safe part, C-UI-1) ─────────────────────────────────────
void CardsHomeActivity::maybeShowTutorial() {
    if (m_core.config().cards.tutorialDone || m_tutorialCard > 0 || m_tutorialBlocked)
        return;
    m_tutorialCard = 1;
    m_tutorialIn = 0.f;
}

void CardsHomeActivity::drawTutorial(nxui::Renderer& ren) {
    if (m_tutorialCard <= 0)
        return;
    auto& i18n = nxui::I18n::instance();
    struct Card { const char* tk; const char* tf; const char* bk; const char* bf; };
    static const Card kCards[] = {
        {"cards.tut1_title", "Welcome to Cards", "cards.tut1_body",
         "Your software as cards. Left/Right to browse, A to play."},
        {"cards.tut2_title", "Card options", "cards.tut2_body",
         "Press + on a card to move it, rename it or send it to Media."},
        {"cards.tut3_title", "Pages", "cards.tut3_body", "ZL and ZR change page: Games, Nintendo and Media."},
        {"cards.tut4_title", "Quick settings", "cards.tut4_body", "R opens quick settings: volume and power."},
    };
    const Card& c = kCards[std::clamp(m_tutorialCard, 1, 4) - 1];
    const float a = m_tutorialIn;
    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, {0.f, 0.f, 0.f, 0.55f * a});
    const nxui::Rect p{290.f, 190.f, 700.f, 320.f};
    ren.drawRoundedRect(p, {0.08f, 0.09f, 0.13f, 0.94f * a}, 30.f);
    ren.drawRoundedRectOutline(p, {1.f, 1.f, 1.f, 0.16f * a}, 30.f, 1.5f);
    m_chrome.textMid(ren, m_chrome.titleFont(), i18n.tr(c.tk, c.tf), p.x + 40.f, p.y + 60.f,
                     CardsChrome::kWhite.withAlpha(a));
    m_chrome.textMid(ren, m_chrome.labelFont(),
                     m_chrome.fit(m_chrome.labelFont(), i18n.tr(c.bk, c.bf), p.width - 80.f), p.x + 40.f,
                     p.y + 140.f, {0.85f, 0.87f, 0.93f, a});
    for (int i = 1; i <= 4; ++i)
        ren.drawCircle({p.x + p.width * 0.5f - 30.f + 20.f * static_cast<float>(i - 1), p.bottom() - 70.f},
                       i == m_tutorialCard ? 5.f : 4.f, {1.f, 1.f, 1.f, (i == m_tutorialCard ? 0.95f : 0.35f) * a},
                       12);
    const std::string hint = i18n.tr("cards.tut_hint", "A  Next     B  Skip");
    m_chrome.textMid(ren, m_chrome.smallFont(), hint,
                     p.x + (p.width - m_chrome.smallFont().measure(hint).x) * 0.5f, p.bottom() - 34.f,
                     {0.78f, 0.80f, 0.88f, a});
}


void CardsHomeActivity::reorderSettingsSection(int delta) {
    const int at = m_sheet.section();
    auto& order = m_core.config().cards.settingsOrder;
    const int next = at + delta;
    if (next < 0 || next >= static_cast<int>(order.size())) return;
    std::swap(order[static_cast<std::size_t>(at)], order[static_cast<std::size_t>(next)]);
    saveConfig("cards settings order");
    m_sheet.open(m_sheet.title(), settingsSections(), next, m_sheet.row());
    m_sheet.onReorderSection([this](int d) { reorderSettingsSection(d); });
}

void CardsHomeActivity::activatePanelShortcut() {
    const auto id = m_core.config().cards.panelTitleId;
    if (!id) return;
    const int page = m_core.config().cards.isMedia(id) ? AppConfig::CardsPrefs::kMedia : AppConfig::CardsPrefs::kGames;
    goToPage(page, 0);
    const int index = m_catalog.indexOf(id);
    if (index < 0) {
        showToast(nxui::I18n::instance().tr("cards.panel_missing", "Software unavailable"));
        return;
    }
    m_selected = index;
    m_scroll = static_cast<float>(index);
    activateSelected();
}

void CardsHomeActivity::openArtSheet(std::uint64_t titleId) {
    using Row = CardsSheet::Row;
    auto& tr = nxui::I18n::instance();
    auto contains = [](const std::vector<std::uint64_t>& v, std::uint64_t id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    };
    auto set = [](std::vector<std::uint64_t>& v, std::uint64_t id, bool enabled) {
        v.erase(std::remove(v.begin(), v.end(), id), v.end());
        if (enabled) v.push_back(id);
    };

    std::string titleName;
    const int idx = m_catalog.indexOf(titleId);
    if (idx >= 0 && idx < m_catalog.count())
        titleName = m_catalog.at(idx).app.title;

    Row card;
    card.label = tr.tr("cards.art_card", "Card image");
    card.value = [this, titleId, contains]() {
        auto& t = nxui::I18n::instance();
        return contains(m_core.config().cards.iconTitles, titleId) ? t.tr("cards.art_icon", "Icon")
                                                                 : t.tr("cards.art_cover", "Cover");
    };
    card.onChange = [this, titleId, contains, set](int) {
        auto& v = m_core.config().cards.iconTitles;
        set(v, titleId, !contains(v, titleId));
        m_art.releaseAll();
        saveConfig("cards card image");
    };
    Row background;
    background.label = tr.tr("cards.art_background", "Background image");
    background.value = [this, titleId, contains]() {
        auto& t = nxui::I18n::instance();
        const auto& c = m_core.config().cards;
        return contains(c.plainBackgroundTitles, titleId) ? t.tr("cards.art_plain", "Plain")
             : contains(c.coverBackgroundTitles, titleId) ? t.tr("cards.art_cover", "Cover")
                                                         : t.tr("cards.art_hero", "Game art");
    };
    background.onChange = [this, titleId, contains, set](int d) {
        auto& c = m_core.config().cards;
        const int current = contains(c.plainBackgroundTitles, titleId) ? 2 : contains(c.coverBackgroundTitles, titleId) ? 1 : 0;
        const int next = (current + d + 3) % 3;
        set(c.plainBackgroundTitles, titleId, next == 2);
        set(c.coverBackgroundTitles, titleId, next == 1);
        m_art.releaseAll();
        saveConfig("cards background image");
    };
    Row title;
    title.label = tr.tr("cards.art_title", "Title image");
    title.value = [this, titleId, contains]() {
        auto& t = nxui::I18n::instance();
        return contains(m_core.config().cards.logoTitles, titleId) ? t.tr("cards.art_logo", "Local logo")
                                                                 : t.tr("cards.art_text", "Text");
    };
    title.onChange = [this, titleId, contains, set](int) {
        auto& v = m_core.config().cards.logoTitles;
        set(v, titleId, !contains(v, titleId));
        m_art.releaseAll();
        saveConfig("cards title image");
    };

    Row pickCover;
    pickCover.label = tr.tr("cards.art_pick_cover", "Choose card cover");
    pickCover.onA = [this, titleId, titleName]() {
        m_sheet.close();
        openTitlePicker(titleId, titleName, SteamGridDbManager::ArtworkKind::Cover);
    };

    Row pickHero;
    pickHero.label = tr.tr("cards.art_pick_hero", "Choose background image");
    pickHero.onA = [this, titleId, titleName]() {
        m_sheet.close();
        openTitlePicker(titleId, titleName, SteamGridDbManager::ArtworkKind::Hero);
    };

    Row pickLogo;
    pickLogo.label = tr.tr("cards.art_pick_logo", "Choose title logo");
    pickLogo.onA = [this, titleId, titleName]() {
        m_sheet.close();
        openTitlePicker(titleId, titleName, SteamGridDbManager::ArtworkKind::Logo);
    };

    Row dlNow;
    dlNow.label = tr.tr("cards.art_download_now", "Download artwork for this title");
    dlNow.onA = [this, titleId, titleName]() {
        m_sheet.close();
        downloadSingleTitleArt(titleId, titleName);
    };

    m_sheet.open(tr.tr("cards.art_choices", "Images"),
                 {{std::string(), {card, background, title, pickCover, pickHero, pickLogo, dlNow}}});
}

// ── Internet: connection check (HomeNX G1, C-NET-1) ─────────────────────────
// Network state plus one verified HTTPS request through NetService, on the
// Core pool. Cloudflare's 204 endpoint, as HomeNX uses for its test; the full
// speed test (C-NET-7) is G4.
void CardsHomeActivity::startNetCheck() {
    auto& tr = nxui::I18n::instance();
    if (m_netCheck.valid()) {
        showToast(tr.tr("cards.net_checking", "Checking the connection..."));
        return;
    }
    // The pool is shut down only on the main thread, after this frontend.
    if (!m_core.threadPool().acceptingTasks())
        return;
    m_sheet.close();
    showToast(tr.tr("cards.net_checking", "Checking the connection..."));
    net::NetService* service = &m_core.net();
    auto promise = std::make_shared<std::promise<std::string>>();
    m_netCheck = promise->get_future();
    // Texts are looked up here, on the main thread; the worker only picks one.
    struct Texts { std::string connecting, offline, ok, failed; };
    Texts texts{tr.tr("cards.net_connecting", "Not connected yet: the console is still connecting"),
                tr.tr("cards.net_offline", "No internet connection"),
                tr.tr("cards.net_ok", "Connected: secure request OK"),
                tr.tr("cards.net_failed", "Connected, but the test request failed: ")};
    m_core.threadPool().submit([service, promise, texts]() {
        std::string text;
        const net::NetState state = service->state();
        if (state != net::NetState::Online) {
            text = state == net::NetState::Connecting ? texts.connecting : texts.offline;
        } else {
            net::Request req;
            req.method = "HEAD";
            req.url = "https://cp.cloudflare.com/generate_204";
            req.timeoutMs = 8000;
            req.attempts = 1;
            const net::Response res = service->request(req);
            char ms[32];
            std::snprintf(ms, sizeof(ms), " (%u ms)", res.elapsedMs);
            if (res.cancelled)
                text.clear();
            else if (res.status == 204 || res.ok())
                text = texts.ok + ms;
            else
                text = texts.failed +
                       (res.error.empty() ? "HTTP " + std::to_string(res.status) : res.error);
        }
        DebugLog::log("[net] connection check: %s", text.empty() ? "cancelled" : text.c_str());
        promise->set_value(std::move(text));
    });
}

void CardsHomeActivity::pollNetCheck() {
    if (m_speedJob.valid() && m_speedJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try { m_speedJob.get(); } catch (...) {}
    }
    if (!m_netCheck.valid() ||
        m_netCheck.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    const std::string text = m_netCheck.get();
    if (!text.empty())
        showToast(text);
}

// ── Internet: connection test (HomeNX G4, C-NET-7) ──────────────────────────
// Ping, download and upload against the project server's keyed /speedtest/
// endpoints (themeshop::http::speedProbe, the only code that sends the key).
// HomeNX used Cloudflare/Tele2 and a STUN NAT probe; OmniLaunch measures
// against its own server only.
namespace {
std::string speedText(int kbps) {
    char buf[32];
    if (kbps >= 1000)
        std::snprintf(buf, sizeof(buf), "%.1f Mbps", static_cast<double>(kbps) / 1000.0);
    else
        std::snprintf(buf, sizeof(buf), "%d kbps", kbps);
    return buf;
}
} // namespace

void CardsHomeActivity::openSpeedTest() {
    auto& i18n = nxui::I18n::instance();
    if (!m_speed)
        m_speed = std::make_shared<SpeedTest>();
    auto st = m_speed;
    using Row = CardsSheet::Row;
    auto value = [st](int which) {
        return [st, which]() -> std::string {
            auto& tr = nxui::I18n::instance();
            const int stage = st->stage.load();
            if (stage == 0) return "-";
            const int v = which == 1 ? st->pingMs.load() : which == 2 ? st->downKbps.load() : st->upKbps.load();
            if (v >= 0) return which == 1 ? std::to_string(v) + " ms" : speedText(v);
            if (stage == which) return "...";
            if (stage > which && st->online.load()) return tr.tr("cards.speed_failed", "Couldn't test");
            return "-";
        };
    };
    Row status;
    status.label = i18n.tr("cards.speed_status", "Connection");
    status.enabled = false;
    status.value = [st]() -> std::string {
        auto& tr = nxui::I18n::instance();
        const int stage = st->stage.load();
        if (stage == 0) return tr.tr("cards.speed_ready", "Press A on Start test");
        if (stage < 4) return tr.tr("cards.speed_running", "Testing...");
        if (!st->online.load()) return tr.tr("cards.net_offline", "No internet connection");
        return st->failed.load() ? tr.tr("cards.speed_partial", "Connected, some tests failed")
                                 : tr.tr("cards.speed_done", "Connected");
    };
    Row signal;
    signal.label = i18n.tr("cards.speed_signal", "Signal");
    signal.enabled = false;
    signal.value = [st]() -> std::string {
        const int s = st->signal.load();
        if (s < 0) return "-";
        if (s >= 4) return nxui::I18n::instance().tr("cards.speed_wired", "Wired");
        return std::string(static_cast<std::size_t>(s), '|') + std::string(static_cast<std::size_t>(3 - s), '.');
    };
    Row ping;
    ping.label = "Ping";
    ping.enabled = false;
    ping.value = value(1);
    Row down;
    down.label = i18n.tr("cards.speed_down", "Download");
    down.enabled = false;
    down.value = value(2);
    Row up;
    up.label = i18n.tr("cards.speed_up", "Upload");
    up.enabled = false;
    up.value = value(3);
    Row run;
    run.label = i18n.tr("cards.speed_start", "Start test");
    run.detail = i18n.tr("cards.speed_note", "About 20 seconds, with the OmniLaunch server");
    run.onA = [this]() { startSpeedTest(); };
    m_sheet.open(i18n.tr("cards.speed_test", "Connection test"),
                 {{std::string(), {run, status, signal, ping, down, up}}});
}

void CardsHomeActivity::startSpeedTest() {
    if (m_speedJob.valid() || !m_speed)
        return; // one run at a time; the sheet shows its progress
    if (!m_core.threadPool().acceptingTasks())
        return;
    // A fresh state per run, so a finished worker never writes into the next.
    auto st = std::make_shared<SpeedTest>();
    st->stage = 1;
    m_speed = st;
    openSpeedTest(); // rebind the rows to the new state, focus stays on Start
    net::NetService* service = &m_core.net();
    try {
        m_speedJob = m_core.threadPool().submit([st, service]() {
#ifdef SWITCHU_MENU
            NifmInternetConnectionType type{};
            u32 strength = 0;
            NifmInternetConnectionStatus status{};
            if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)))
                st->signal = type == NifmInternetConnectionType_WiFi ? static_cast<int>(strength) : 4;
#endif
            st->online = service->state() == net::NetState::Online;
            if (!st->online || st->stop) {
                st->stage = 4;
                return;
            }
            using themeshop::http::SpeedProbeKind;
            auto p = themeshop::http::speedProbe(SpeedProbeKind::Ping, 8000, &st->stop);
            if (p.ok) st->pingMs = std::max(1, static_cast<int>(p.rttMs + 0.5));
            else st->failed = true;
            if (st->stop || p.cancelled) { st->stage = 4; return; }
            st->stage = 2;
            auto d = themeshop::http::speedProbe(SpeedProbeKind::Download, 8000, &st->stop);
            if (d.kbps() > 0) st->downKbps = d.kbps();
            else st->failed = true;
            if (st->stop || d.cancelled) { st->stage = 4; return; }
            st->stage = 3;
            auto u = themeshop::http::speedProbe(SpeedProbeKind::Upload, 8000, &st->stop);
            if (u.kbps() > 0) st->upKbps = u.kbps();
            else st->failed = true;
            DebugLog::log("[speedtest] result ping=%d down=%d up=%d kbps signal=%d", st->pingMs.load(),
                          st->downKbps.load(), st->upKbps.load(), st->signal.load());
            st->stage = 4;
        });
    } catch (...) {
        st->stage = 0; // pool shutting down
    }
}

void CardsHomeActivity::stopSpeedTest() {
    if (m_speed)
        m_speed->stop = true;
    if (m_speedJob.valid()) {
        m_speedJob.wait();
        try { m_speedJob.get(); } catch (...) {}
    }
}

// ── Art sources: pickers, single title download, save-all (HomeNX G2, C-NET-4, C-UI-4) ───
void CardsHomeActivity::openTitlePicker(std::uint64_t titleId, const std::string& title,
                                        SteamGridDbManager::ArtworkKind kind, const std::string& query) {
    if (!m_picker)
        return;
    m_sheet.close();
    const std::string q = query.empty() ? title : query;
    m_picker->showLoading(titleId, title, q, kind);
    m_overlayLatch = true;
    focusManager().setFocus(m_picker.get());

    if (m_browseFuture.valid() &&
        m_browseFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        // A browse task is already running; the newest generation in picker discards old responses.
        return;
    }

    const std::string apiKey = m_core.config().steamGridDbApiKey;
    auto promise = std::make_shared<std::promise<SteamGridDbManager::BrowseResult>>();
    m_browseFuture = promise->get_future();
    m_core.threadPool().submit([this, titleId, title, q, kind, apiKey, promise]() {
        try {
            promise->set_value(CardsArtSources::browseAll(m_core.net(), apiKey,
                                                          titleId, title, q, kind));
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });
}

void CardsHomeActivity::applyPickerCandidate(const SteamGridDbManager::BrowseResult& browse,
                                             const SteamGridDbManager::Candidate& candidate) {
    if (!m_picker)
        return;
    m_picker->hide();
    m_overlayLatch = true;
    auto& tr = nxui::I18n::instance();

    if (m_applyFuture.valid() &&
        m_applyFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        showToast(tr.tr("cards.art_downloading", "Downloading artwork..."));
        return;
    }

    showToast(tr.tr("cards.art_downloading", "Downloading artwork..."));
    auto promise = std::make_shared<std::promise<bool>>();
    m_applyFuture = promise->get_future();
    m_core.threadPool().submit([this, browse, candidate, promise]() {
        try {
            promise->set_value(CardsArtSources::applyCandidate(m_core.net(), browse, candidate));
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });
}

void CardsHomeActivity::downloadSingleTitleArt(std::uint64_t titleId, const std::string& title) {
    if (titleId == 0)
        return;
    auto& tr = nxui::I18n::instance();
    if (!m_core.net().online()) {
        showToast(tr.tr("cards.net_offline", "No internet connection"));
        return;
    }
    if (m_singleArtFuture.valid() &&
        m_singleArtFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        showToast(tr.tr("cards.art_downloading", "Downloading artwork..."));
        return;
    }

    showToast(tr.tr("cards.art_downloading", "Downloading artwork..."));
    const std::string apiKey = m_core.config().steamGridDbApiKey;
    const bool boxArt = m_core.config().cards.boxArt;
    const bool titlePics = m_core.config().cards.titlePics;

    auto promise = std::make_shared<std::promise<CardsArtSources::SingleTitleResult>>();
    m_singleArtFuture = promise->get_future();
    m_core.threadPool().submit([this, titleId, title, apiKey, boxArt, titlePics, promise]() {
        try {
            promise->set_value(CardsArtSources::downloadTitleArt(m_core.net(), apiKey,
                                                                 titleId, title, boxArt,
                                                                 titlePics));
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });
}

void CardsHomeActivity::startSaveAll(bool forceAll) {
    auto& tr = nxui::I18n::instance();
    if (saveAllRunning()) {
        showToast(tr.tr("cards.art_save_all_saving", "Saving pictures in progress..."));
        return;
    }
    if (!m_core.net().online()) {
        showToast(tr.tr("cards.net_offline", "No internet connection"));
        return;
    }
    m_saveAllStop.store(false);
    std::vector<AppEntry> apps = m_catalog.all();
    showToast(tr.tr("cards.art_save_all_desc", "Download covers and backgrounds for all games"));

    const std::string apiKey = m_core.config().steamGridDbApiKey;
    const bool boxArt = m_core.config().cards.boxArt;
    const bool titlePics = m_core.config().cards.titlePics;

    auto promise = std::make_shared<std::promise<int>>();
    m_saveAllFuture = promise->get_future();
    m_core.threadPool().submit([this, apps, forceAll, apiKey, boxArt, titlePics, promise]() {
        try {
            CardsArtSources::runSaveAll(m_core.net(), apiKey,
                                        apps, boxArt, titlePics, forceAll,
                                        m_saveAllDone, m_saveAllTotal, m_saveAllSaved,
                                        m_saveAllStop);
            promise->set_value(m_saveAllSaved.load(std::memory_order_relaxed));
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });
}

bool CardsHomeActivity::saveAllRunning() const {
    return m_saveAllFuture.valid() &&
           m_saveAllFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

void CardsHomeActivity::pollArtJobs() {
    auto& tr = nxui::I18n::instance();
    if (m_browseFuture.valid() &&
        m_browseFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto res = m_browseFuture.get();
            if (m_picker && m_picker->isActive())
                m_picker->setResult(std::move(res));
        } catch (const std::exception& e) {
            if (m_picker && m_picker->isActive())
                m_picker->setMessage(e.what(), false);
        } catch (...) {
            if (m_picker && m_picker->isActive())
                m_picker->setMessage("Browse failed", false);
        }
    }
    if (m_applyFuture.valid() &&
        m_applyFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        bool ok = false;
        try {
            ok = m_applyFuture.get();
        } catch (...) {
        }
        m_art.releaseAll();
        showToast(ok ? tr.tr("cards.art_downloaded", "Artwork updated")
                     : tr.tr("cards.art_download_failed", "Could not download artwork"));
    }
    if (m_singleArtFuture.valid() &&
        m_singleArtFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        CardsArtSources::SingleTitleResult res{};
        try {
            res = m_singleArtFuture.get();
        } catch (...) {
        }
        m_art.releaseAll();
        if (res.cover || res.hero || res.logo) {
            showToast(tr.tr("cards.art_downloaded", "Artwork updated"));
        } else {
            showToast(tr.tr("cards.art_download_failed", "Could not download artwork"));
        }
    }
    if (m_saveAllFuture.valid() &&
        m_saveAllFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        int savedCount = 0;
        try {
            savedCount = m_saveAllFuture.get();
        } catch (...) {
        }
        m_art.releaseAll();
        if (savedCount > 0) {
            showToast(tr.tr("cards.art_save_all_done", "All pictures saved"));
        } else {
            showToast(tr.tr("cards.art_download_failed", "Could not download artwork"));
        }
    }
}

} // namespace cards
