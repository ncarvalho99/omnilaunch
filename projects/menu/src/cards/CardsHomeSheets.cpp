// CardsHomeActivity: pages, move mode, sheets, quick settings, text entry,
// achievements and the welcome tour (HomeNX batch 1: E1, E2, E3, E4 safe part,
// E5 safe part, E6, E7, E9 safe part, E11). Split from CardsHomeActivity.cpp
// to keep that file navigable; same class, same thread (main) rules.

#include "cards/CardsAchievements.hpp"
#include "cards/CardsArtSources.hpp"
#include "cards/CardsHomeActivity.hpp"
#include "cards/CardsPages.hpp"
#include "cards/CardsDotsNavigation.hpp"
#include "core/DebugLog.hpp"
#include "core/ButtonGlyph.hpp"
#include <nxui/core/GpuDevice.hpp>
#include "bluetooth/BluetoothManager.hpp"
#include "core/PlayTime.hpp"
#include "home/HomeCore.hpp"
#include "widgets/OverlayDialog.hpp"
#include "widgets/ProgressDialog.hpp"
#include "launcher/HekateReboot.hpp"
#include "settings/SystemPrefs.hpp"
#include "widgets/LockPassword.hpp"
#include "net/NetService.hpp"
#include "usb/Mtp.hpp"
#include "themeshop/ThemeHttp.hpp"
#ifdef SWITCHU_MENU
#include "core/NsService.hpp"
#include "smi_commands.hpp"
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
#include <chrono>
#include <ctime>
#include <sys/statvfs.h>
#include <cstring>
#include <filesystem>
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

bool CardsHomeActivity::overlayActive(bool ignoreFetch) const {
    return (m_userSelect && m_userSelect->isActive()) || (m_text && m_text->isActive()) ||
           (m_profiles && m_profiles->isActive()) ||
           (m_quick && m_quick->isActive()) || (m_deleteProgress && m_deleteProgress->isActive()) ||
           (m_selfUpdateProgress && m_selfUpdateProgress->isActive()) ||
           (!ignoreFetch && m_fetchProgress && m_fetchProgress->isActive()) ||
           (m_picker && m_picker->isActive()) || (m_padTest && m_padTest->isActive()) ||
           (m_discordPanel && m_discordPanel->isActive());
}

void CardsHomeActivity::saveConfig(const char* why) {
    auto& config = m_core.config();
    config.cards.normalize();
    // Success is logged too: without it a settings test on hardware cannot
    // tell which rows actually wrote the configuration.
    if (!config.save())
        DebugLog::log("[cards] config save failed (%s)", why);
    else
        DebugLog::log("[cards] config saved (%s)", why);
#ifdef SWITCHU_MENU
    switchu::commitSdCard(why);
#endif
}

void CardsHomeActivity::openPowerMenu() {
    m_powerOpen = true;
    m_powerIn = 0.f;
    m_popupBackdropValid = false;
    m_powerSel = 0;
    m_audio.playSfx(Sfx::ModalShow);
}

void CardsHomeActivity::closePowerMenu() {
    m_powerOpen = false;
    m_waitRelease = true;
    m_audio.playSfx(Sfx::ModalHide);
}

bool CardsHomeActivity::handlePowerInput(nxui::Input& in) {
    using B = nxui::Button;
    const nxui::Rect card{180.f, 190.f, 920.f, 340.f};
    if (in.touchDown() || in.touchUp()) {
        const float x = in.touchX(), y = in.touchY();
        if (!card.contains(x, y)) {
            closePowerMenu();
            return true;
        }
        const float tileW = 196.f, tileH = 210.f;
        const float startX = card.x + 40.f, startY = card.y + 88.f;
        const float gap = (card.width - 80.f - tileW * 4.f) / 3.f;
        for (int i = 0; i < 4; ++i) {
            const nxui::Rect r{startX + static_cast<float>(i) * (tileW + gap), startY, tileW, tileH};
            if (r.contains(x, y)) {
                m_powerSel = i;
                if (in.touchUp()) {
                    closePowerMenu();
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    if (i == 0) {
#ifdef SWITCHU_MENU
                        m_core.launcher().enterSleep();
#endif
                    } else if (i == 1) {
#ifdef SWITCHU_MENU
                        m_core.launcher().reboot();
#endif
                    } else if (i == 2) {
#ifdef SWITCHU_MENU
                        m_core.launcher().shutdown();
#endif
                    } else if (i == 3) {
                        m_hekateQueued = true;
                    }
                    return true;
                }
            }
        }
        return true;
    }
    if (in.isDown(B::B) || in.isDown(B::Plus)) {
        closePowerMenu();
        return true;
    }
    const bool left = in.isDown(B::DLeft) || in.isDown(B::LStickL);
    const bool right = in.isDown(B::DRight) || in.isDown(B::LStickR);
    if (left && m_powerSel > 0) {
        --m_powerSel;
        m_audio.playSfx(Sfx::Navigate);
        return true;
    }
    if (right && m_powerSel < 3) {
        ++m_powerSel;
        m_audio.playSfx(Sfx::Navigate);
        return true;
    }
    if (in.isDown(B::A)) {
        const int sel = m_powerSel;
        closePowerMenu();
        m_audio.playSfx(Sfx::ConfirmPositive);
        if (sel == 0) {
#ifdef SWITCHU_MENU
            m_core.launcher().enterSleep();
#endif
        } else if (sel == 1) {
#ifdef SWITCHU_MENU
            m_core.launcher().reboot();
#endif
        } else if (sel == 2) {
#ifdef SWITCHU_MENU
            m_core.launcher().shutdown();
#endif
        } else if (sel == 3) {
            m_hekateQueued = true;
        }
        return true;
    }
    return true;
}

void CardsHomeActivity::drawGlassCard(nxui::Renderer& ren, const nxui::Rect& card, float radius, float a) {
    bool glass = false;
#ifdef SWITCHU_MENU
    if (ren.gpu().offscreenReady()) {
        if (!m_popupBackdropValid) {
            ren.captureToOffscreenSharp();
            ren.applyBlur(2.5f, 2);
            ren.copyOffscreen(nxui::GpuDevice::OFF_SHARP_A, nxui::GpuDevice::OFF_DIALOG);
            m_popupBackdropValid = true;
        }
        ren.drawRect({0.f, 0.f, 1280.f, 720.f}, {0.f, 0.f, 0.f, 0.45f * a});
        ren.drawLiquidGlass(nxui::GpuDevice::OFF_DIALOG, card, radius, CardsChrome::glassTint(1.f), a, 0.35f);
        ren.drawRoundedRect(card, {0.04f, 0.05f, 0.08f, 0.45f * a}, radius);
        glass = true;
    }
#endif
    if (!glass) {
        ren.drawRect({0.f, 0.f, 1280.f, 720.f}, {0.f, 0.f, 0.f, 0.55f * a});
        ren.drawRoundedRect(card, {0.08f, 0.09f, 0.13f, 0.86f * a}, radius);
    }
    ren.drawRoundedRectOutline(card, {1.f, 1.f, 1.f, 0.16f * a}, radius, 1.5f);
    ren.drawGradientRect({card.x + 2.f, card.y + 2.f, card.width - 4.f, 90.f}, {1.f, 1.f, 1.f, 0.05f * a},
                         {1.f, 1.f, 1.f, 0.f});
}

void CardsHomeActivity::drawPower(nxui::Renderer& ren) {
    if (!m_powerOpen)
        return;
    m_powerIn = std::min(1.f, m_powerIn + 1.f / 10.f);
    const float a = 1.f - (1.f - m_powerIn) * (1.f - m_powerIn);

    const nxui::Rect card{180.f, 190.f, 920.f, 340.f};
    drawGlassCard(ren, card, 32.f, a);

    auto& tf = m_chrome.titleFont();
    auto& lf = m_chrome.labelFont();
    auto& sf = m_chrome.smallFont();
    auto& i18n = nxui::I18n::instance();

    m_chrome.icon(ren, "power", card.x + 44.f, card.y + 44.f, 26.f, CardsChrome::kWhite.withAlpha(a));
    m_chrome.textMid(ren, tf, i18n.tr("power.title", "Power"), card.x + 68.f, card.y + 44.f, CardsChrome::kWhite.withAlpha(a));

    {
        const std::vector<ui::glyph::ActionHint> hints = {
            {ui::glyph::button(nxui::Button::A), i18n.tr("hint.select", "Select")},
            {ui::glyph::button(nxui::Button::B), i18n.tr("hint.back", "Back")},
        };
        ui::glyph::drawHintBar(ren, m_fontIcons, sf, hints, card.right() - 32.f, card.y + 57.f, a);
    }

    struct PowerTileDef {
        std::string name;
        std::string about;
        const char* icon;
        nxui::Color color;
    };
    const PowerTileDef defs[4] = {
        {i18n.tr("power.sleep", "Suspender"), i18n.tr("cards.power_sleep_desc", "Continuar depois"), "moon", nxui::Color(0.38f, 0.56f, 1.f, 1.f)},
        {i18n.tr("power.reboot", "Reiniciar"), i18n.tr("cards.power_reboot_desc", "Desligar e ligar"), "restart", nxui::Color(0.25f, 0.82f, 0.68f, 1.f)},
        {i18n.tr("power.shutdown", "Desligar"), i18n.tr("cards.power_shutdown_desc", "Desligar o console"), "power", nxui::Color(1.f, 0.36f, 0.36f, 1.f)},
        {i18n.tr("power.hekate", "Hekate"), i18n.tr("cards.power_hekate_desc", "Menu do Hekate"), "hekate", nxui::Color(0.70f, 0.50f, 1.f, 1.f)},
    };

    const float tileW = 196.f;
    const float tileH = 210.f;
    const float startX = card.x + 40.f;
    const float startY = card.y + 88.f;
    const float gap = (card.width - 80.f - tileW * 4.f) / 3.f;

    for (int i = 0; i < 4; ++i) {
        const bool selected = (i == m_powerSel);
        const float dy = selected ? -6.f : 0.f;
        const nxui::Rect r{startX + static_cast<float>(i) * (tileW + gap), startY + dy, tileW, tileH};

        ren.drawRoundedRect({r.x, r.y + 6.f, r.width, r.height}, {0.f, 0.f, 0.f, (selected ? 0.25f : 0.12f) * a}, 22.f);
        ren.drawRoundedRect(r, {0.13f, 0.15f, 0.20f, (selected ? 0.80f : 0.55f) * a}, 20.f);

        if (selected) {
            ren.drawRoundedRectOutline(r, CardsChrome::accent(0.95f * a), 20.f, 2.5f);
        } else {
            ren.drawRoundedRectOutline(r, {1.f, 1.f, 1.f, 0.10f * a}, 20.f, 1.f);
        }

        const nxui::Vec2 ic{r.x + r.width * 0.5f, r.y + 62.f};
        ren.drawCircle(ic, selected ? 38.f : 34.f, defs[i].color.withAlpha((selected ? 0.35f : 0.20f) * a), 36);
        ren.drawCircle(ic, 28.f, defs[i].color.withAlpha(0.90f * a), 36);
        m_chrome.icon(ren, defs[i].icon, ic.x, ic.y, 24.f, CardsChrome::kWhite.withAlpha(a));

        const std::string name = defs[i].name;
        m_chrome.textMid(ren, lf, name, r.x + (r.width - lf.measure(name).x) * 0.5f, r.y + 128.f, CardsChrome::kWhite.withAlpha(a));

        const std::string about = defs[i].about;
        m_chrome.textMid(ren, sf, about, r.x + (r.width - sf.measure(about).x) * 0.5f, r.y + 162.f, {0.78f, 0.80f, 0.88f, 0.80f * a});
    }
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
        openPowerMenu();
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
        m_discordPanel->setIconFont(&m_fontIcons);
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
    // Songs downloaded or deleted since the list was built (YouTube store,
    // MTP) appear here; the playing song keeps playing.
    if (m_audioStarted)
        m_core.audioServices().reloadMusicTracks(m_core.config(), {});
    m_musicSheet.open(m_audio, m_core.config());
}

void CardsHomeActivity::openNewsSheet() {
    m_audio.playSfx(Sfx::ModalShow);
    m_newsSheet.open(m_core, m_chrome, m_time);
}

// ── "..." menu (E3, C-UI-2) ─────────────────────────────────────────────────
void CardsHomeActivity::openDotsMenu() {
    // CardsNX build 185 layout: a popover under the "..." button with a
    // "Pages" chip row and coloured shortcut tiles, four per row.
    auto& i18n = nxui::I18n::instance();
    m_dotsTiles.clear();
    auto tile = [this](std::string label, const char* icon, nxui::Color color, std::function<void()> run) {
        m_dotsTiles.push_back({std::move(label), icon, color, std::move(run)});
    };
    tile(i18n.tr("cards.menu_news", "News"), "news", nxui::Color(0.95f, 0.25f, 0.25f, 1.f), [this]() { openNewsSheet(); });
    tile("Discord", "discord", nxui::Color(0.35f, 0.40f, 0.95f, 1.f), [this]() { openDiscord(); });
    tile(i18n.tr("cards.menu_users", "Users"), "friends", nxui::Color(1.f, 0.6f, 0.2f, 1.f), [this]() { openUsersSheet(); });
    tile(i18n.tr("cards.menu_music", "Music"), "music", nxui::Color(0.95f, 0.35f, 0.6f, 1.f), [this]() { openMusicSheet(); });
    tile(i18n.tr("cards.menu_quick", "Quick settings"), "sun", nxui::Color(1.f, 0.78f, 0.25f, 1.f), [this]() { openQuickSettings(); });
    tile(homebrewUpdateCount() > 0
             ? i18n.tr("cards.menu_updates", "Updates") + " (" + std::to_string(homebrewUpdateCount()) + ")"
             : i18n.tr("cards.menu_updates", "Updates"),
         "arrows", nxui::Color(0.25f, 0.8f, 0.5f, 1.f), [this]() { openHomebrewUpdates(); });
    tile(i18n.tr("sidebar.settings", "Settings"), "gear", nxui::Color(0.55f, 0.6f, 0.68f, 1.f), [this]() { openSettingsSheet(); });
    tile(i18n.tr("power.title", "Power"), "power", nxui::Color(1.f, 0.36f, 0.36f, 1.f), [this]() {
        openPowerMenu();
    });
    tile(i18n.tr("cards.menu_achievements", "Achievements"), "tag", nxui::Color(0.85f, 0.65f, 0.15f, 1.f), [this]() {
        const bool game = m_selected >= 0 && m_selected < m_catalog.count() && !m_catalog.at(m_selected).system;
        openAchievementsSheet(game ? m_catalog.at(m_selected).app.titleId : 0);
    });
    tile(i18n.tr("cards.menu_notifications", "Notifications"), "bell", nxui::Color(0.45f, 0.55f, 0.95f, 1.f),
         [this]() { openNotificationsSheet(); });
#ifdef SWITCHU_MENU
    if (m_core.launcher().suspendedTitleId() != 0) {
        tile(i18n.tr("cards.close_software", "Close software"), "trash", nxui::Color(0.85f, 0.2f, 0.2f, 1.f), [this]() {
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
    m_audio.playSfx(Sfx::ModalShow);
    m_dotsOpen = true;
    m_dotsIn = 0.f;
    m_dotsRow = 1;
    m_dotsCol = 0;
}

namespace {
constexpr int kDotsCols = 4;
constexpr float kDotsX = 97.f, kDotsY = 93.f, kDotsW = 468.f;
constexpr float kDotsTile = 102.f, kDotsGap = 9.f;

nxui::Rect dotsPanelRect(std::size_t tiles) {
    const int rows = static_cast<int>((tiles + kDotsCols - 1) / kDotsCols);
    const float h = 112.f + rows * (kDotsTile + kDotsGap) + 50.f;
    return {kDotsX, kDotsY, kDotsW, h};
}
} // namespace

bool CardsHomeActivity::handleDotsInput(nxui::Input& in) {
    if (!m_dotsOpen)
        return false;
    using B = nxui::Button;
    const auto shown = pages::shown(m_core.config().cards);
    const int tiles = static_cast<int>(m_dotsTiles.size());
    const nxui::Rect p = dotsPanelRect(m_dotsTiles.size());
    if (in.touchDown() || in.touchUp()) {
        const float x = in.touchX(), y = in.touchY();
        if (!p.contains(x, y)) {
            m_dotsOpen = false;
            m_waitRelease = true;
            m_audio.playSfx(Sfx::ModalHide);
            return true;
        }
        if (in.touchUp()) {
            const float chipW = (p.width - 24.f - 9.f * 3.f) / 4.f;
            for (std::size_t c = 0; c < shown.size() && c < 4; ++c) {
                const nxui::Rect chip{p.x + 17.f + static_cast<float>(c) * (chipW + 9.f), p.y + 47.f, chipW, 52.f};
                if (chip.contains(x, y)) {
                    m_dotsOpen = false;
                    m_waitRelease = true;
                    goToPage(shown[c], 0);
                    m_audio.playSfx(Sfx::Activate);
                    return true;
                }
            }
            for (std::size_t i = 0; i < m_dotsTiles.size(); ++i) {
                const int row = static_cast<int>(i) / kDotsCols, col = static_cast<int>(i) % kDotsCols;
                const nxui::Rect t{p.x + 17.f + col * (kDotsTile + kDotsGap), p.y + 113.f + row * (kDotsTile + kDotsGap),
                                   kDotsTile, kDotsTile};
                if (t.contains(x, y)) {
                    m_dotsOpen = false;
                    m_waitRelease = true;
                    m_audio.playSfx(Sfx::Activate);
                    if (m_dotsTiles[i].run) m_dotsTiles[i].run();
                    return true;
                }
            }
        }
        return true;
    }
    if (in.isDown(B::B) || in.isDown(B::Plus)) {
        m_dotsOpen = false;
        m_waitRelease = true;
        m_audio.playSfx(Sfx::ModalHide);
        return true;
    }
    if (navigateDots(in, static_cast<int>(shown.size()), tiles, m_dotsRow, m_dotsCol)) {
        m_audio.playSfx(Sfx::Navigate);
        return true;
    }
    if (!in.isDown(B::A))
        return true;
    m_dotsOpen = false;
    m_waitRelease = true;
    m_audio.playSfx(Sfx::Activate);
    if (m_dotsRow == 0) {
        if (m_dotsCol >= 0 && m_dotsCol < static_cast<int>(shown.size()))
            goToPage(shown[static_cast<std::size_t>(m_dotsCol)], 0);
        return true;
    }
    const int i = (m_dotsRow - 1) * kDotsCols + m_dotsCol;
    if (i >= 0 && i < tiles && m_dotsTiles[static_cast<std::size_t>(i)].run) {
        auto run = m_dotsTiles[static_cast<std::size_t>(i)].run;
        run();
    }
    return true;
}

void CardsHomeActivity::drawDots(nxui::Renderer& ren) {
    if (!m_dotsOpen)
        return;
    m_dotsIn = std::min(1.f, m_dotsIn + 1.f / 12.f);
    const float a = 1.f - (1.f - m_dotsIn) * (1.f - m_dotsIn);
    auto& i18n = nxui::I18n::instance();
    const nxui::Color white = CardsChrome::kWhite.withAlpha(a);
    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, {0.f, 0.f, 0.f, 0.30f * a});
    nxui::Rect p = dotsPanelRect(m_dotsTiles.size());
    p.y -= 10.f * (1.f - a);
    ren.drawRoundedRect({p.x - 4.f, p.y + 10.f, p.width + 8.f, p.height + 4.f}, {0.f, 0.f, 0.f, 0.28f * a}, 24.f);
    ren.drawRoundedRect(p, {0.10f, 0.11f, 0.15f, 0.90f * a}, 20.f);
    ren.drawRoundedRectOutline(p, {1.f, 1.f, 1.f, 0.14f * a}, 20.f, 1.5f);
    // Pointer notch under the "..." button.
    ren.drawTriangle({134.f - 10.f, p.y + 1.f}, {134.f + 10.f, p.y + 1.f}, {134.f, p.y - 9.f},
                     {0.10f, 0.11f, 0.15f, 0.90f * a});

    auto& sf = m_chrome.smallFont();
    auto& lf = m_chrome.labelFont();
    m_chrome.textMid(ren, sf, i18n.tr("cards.pages", "Pages"), p.x + 21.f, p.y + 29.f, white);

    const auto shown = pages::shown(m_core.config().cards);
    const float chipW = (p.width - 24.f - 9.f * 3.f) / 4.f;
    for (std::size_t c = 0; c < shown.size() && c < 4; ++c) {
        const int page = shown[c];
        const nxui::Rect chip{p.x + 17.f + static_cast<float>(c) * (chipW + 9.f), p.y + 47.f, chipW, 52.f};
        const bool current = page == m_page;
        const bool focus = m_dotsRow == 0 && m_dotsCol == static_cast<int>(c);
        ren.drawRoundedRect(chip, current ? CardsChrome::kWhite.withAlpha(0.95f * a) : nxui::Color{1.f, 1.f, 1.f, 0.10f * a},
                            26.f);
        if (focus)
            ren.drawRoundedRectOutline({chip.x - 3.f, chip.y - 3.f, chip.width + 6.f, chip.height + 6.f},
                                       CardsChrome::accent(a), 29.f, 2.5f);
        const std::string name = m_chrome.fit(sf, i18n.tr(pageKey(page), pageFallback(page)), chip.width - 12.f);
        const nxui::Color ink = current ? nxui::Color{0.08f, 0.09f, 0.12f, a} : white;
        m_chrome.textMid(ren, sf, name, chip.x + (chip.width - sf.measure(name).x) * 0.5f, chip.y + 26.f, ink);
    }

    for (std::size_t i = 0; i < m_dotsTiles.size(); ++i) {
        const int row = static_cast<int>(i) / kDotsCols, col = static_cast<int>(i) % kDotsCols;
        const nxui::Rect t{p.x + 17.f + col * (kDotsTile + kDotsGap), p.y + 113.f + row * (kDotsTile + kDotsGap),
                           kDotsTile, kDotsTile};
        const bool focus = m_dotsRow == row + 1 && m_dotsCol == col;
        ren.drawRoundedRect(t, {1.f, 1.f, 1.f, (focus ? 0.16f : 0.07f) * a}, 14.f);
        if (focus)
            ren.drawRoundedRectOutline(t, CardsChrome::kWhite.withAlpha(0.9f * a), 14.f, 2.f);
        const auto& d = m_dotsTiles[i];
        ren.drawCircle({t.x + t.width * 0.5f, t.y + 38.f}, 25.f, d.color.withAlpha(a), 32);
        m_chrome.icon(ren, d.icon, t.x + t.width * 0.5f, t.y + 38.f, 26.f, white);
        const std::string lbl = m_chrome.fit(sf, d.label, t.width - 8.f);
        m_chrome.textMid(ren, sf, lbl, t.x + (t.width - sf.measure(lbl).x) * 0.5f, t.y + 82.f, white);
    }

    std::vector<ui::glyph::ActionHint> hints{
        {ui::glyph::button(nxui::Button::A), i18n.tr("hint.select", "Select")},
        {ui::glyph::button(nxui::Button::B), i18n.tr("hint.back", "Back")},
    };
    ui::glyph::drawHintBar(ren, m_fontIcons, sf, hints, p.right() - 16.f, p.bottom() - 12.f, a);
    (void)lf;
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

// Media app fetch (TikTok, BrowseNX): a progress dialog says what is happening
// (downloading with size and percentage, then installing) and offers Cancel.
// When the app arrives it opens at once, as CardsNX, if nothing else has taken
// the screen meanwhile.
void CardsHomeActivity::stopBrowserDownload() {
    m_fetch.stop();
}

void CardsHomeActivity::startAppFetch(const CardEntry& card) {
    auto& i18n = nxui::I18n::instance();
    m_audio.playSfx(Sfx::ModalShow);
    if (m_fetch.running()) {
        showToast(i18n.tr("cards.downloading_app", "Downloading app..."));
        return;
    }
    if (!m_core.net().online()) {
        showToast(i18n.tr("cards.net_offline", "No internet connection"));
        return;
    }
    if (!m_fetch.start(m_core.net(), m_core.threadPool(), card.downloadUrl, card.homebrewPath, card.app.title))
        return;
    m_fetchCardId = card.app.titleId;
    if (!m_fetchProgress) {
        m_fetchProgress = std::make_shared<ProgressDialog>();
        m_fetchProgress->setFont(&m_font);
        m_fetchProgress->setSmallFont(&m_fontSmall);
        m_fetchProgress->setTheme(&m_overlayTheme);
        m_fetchProgress->setRect({0, 0, 1280, 720});
    }
    std::string title = i18n.tr("cards.fetch_title", "Getting {app}");
    if (const auto at = title.find("{app}"); at != std::string::npos)
        title.replace(at, 5, card.app.title);
    m_fetchProgress->show(title, i18n.tr("cards.fetch_connecting", "Connecting to GitHub..."), -1.f);
    m_fetchProgress->setCancelAction(i18n.tr("button.cancel", "Cancel"), [this]() {
        m_fetch.cancel();
        if (m_fetchProgress)
            m_fetchProgress->markCancelling(nxui::I18n::instance().tr("cards.fetch_cancelling", "Cancelling..."));
    });
    focusManager().setFocus(m_fetchProgress.get());
    DebugLog::log("[cards] fetch %s -> %s", card.downloadUrl.c_str(), card.homebrewPath.c_str());
}

void CardsHomeActivity::pollBrowserDownload() {
    auto& i18n = nxui::I18n::instance();
    const auto state = m_fetch.poll();
    if (state == home::AppFetch::Running) {
        if (m_fetchProgress && m_fetchProgress->isActive() && !m_fetchProgress->cancelRequested()) {
            std::string message;
            if (m_fetch.installing()) {
                message = i18n.tr("cards.fetch_installing", "Installing...");
            } else if (m_fetch.received() > 0) {
                char size[48];
                const double mb = static_cast<double>(m_fetch.received()) / (1024.0 * 1024.0);
                if (m_fetch.total() > 0)
                    std::snprintf(size, sizeof(size), " %.1f / %.1f MB", mb,
                                  static_cast<double>(m_fetch.total()) / (1024.0 * 1024.0));
                else
                    std::snprintf(size, sizeof(size), " %.1f MB", mb);
                message = i18n.tr("cards.fetch_downloading", "Downloading...") + size;
            } else {
                message = i18n.tr("cards.fetch_connecting", "Connecting to GitHub...");
            }
            m_fetchProgress->updateState(message, m_fetch.progress());
        }
        return;
    }
    if (state == home::AppFetch::Idle)
        return;
    if (m_fetchProgress)
        m_fetchProgress->hide();
    const std::uint64_t id = m_fetchCardId;
    m_fetchCardId = 0;
    if (state == home::AppFetch::Cancelled) {
        showToast(i18n.tr("cards.fetch_cancelled", "Download cancelled."));
        return;
    }
    if (state != home::AppFetch::Done) {
        showToast(i18n.tr("cards.download_failed", "Download failed."));
        return;
    }
    // The card now points at the installed .nro (same stable id).
    m_catalog.rebuild(m_core.config());
    const bool busy = overlayActive(true) || m_sheet.isOpen() || m_musicSheet.isOpen() || m_newsSheet.isOpen() ||
                      m_dotsOpen || m_powerOpen || m_cardOptOpen || m_moving || m_launching || m_leaving ||
                      m_lock.isLocked();
    const int index = m_catalog.indexOf(id);
    if (busy || index < 0 || index != m_selected) {
        showToast(i18n.tr("cards.download_done", "Download complete!"));
        return;
    }
    launchTitle(m_catalog.at(index));
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
    if (m_catalog.count() < 2)
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
        // Games, Homebrew and Nintendo share one hand order: replace only this
        // page's ids and keep the other pages' entries after them.
        std::vector<std::uint64_t> merged = ids;
        for (std::uint64_t id : config.cards.gamesOrder)
            if (std::find(ids.begin(), ids.end(), id) == ids.end())
                merged.push_back(id);
        config.cards.gamesOrder = std::move(merged);
        if (m_page == AppConfig::CardsPrefs::kGames)
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

// ── Card options (HomeNX build 185 parity modal, C-UI-3) ───────────────────
namespace {
const nxui::Rect kOptCard{176.f, 84.f, 928.f, 552.f};
constexpr float kOptRowsY = 136.f;

nxui::Rect cardOptionRow(std::size_t n, std::size_t i, float dy) {
    const float tx = kOptCard.x + 32.f + 260.f + 36.f;
    const float maxW = kOptCard.x + kOptCard.width - 32.f - tx;
    const float top = kOptCard.y + kOptRowsY + dy;
    if (i == 0) return {tx, top, maxW, 52.f};
    const std::size_t k = i - 1;
    const std::size_t rows = std::max<std::size_t>(1, (n - 1 + 1) / 2);
    const float pitch = std::min(54.f, (kOptCard.height - kOptRowsY - 40.f - 60.f) / static_cast<float>(rows));
    const float cw = (maxW - 12.f) * 0.5f;
    return {tx + (cw + 12.f) * static_cast<float>(k % 2), top + 60.f + pitch * static_cast<float>(k / 2), cw, pitch - 8.f};
}
} // namespace

void CardsHomeActivity::closeCardOptions() {
    m_cardOptOpen = false;
    m_waitRelease = true;
    m_audio.playSfx(Sfx::ModalHide);
}

void CardsHomeActivity::openHomebrewOptions() {
    openCardOptions();
}

void CardsHomeActivity::openCardOptions() {
    if (m_selected < 0 || m_selected >= m_catalog.count())
        return;
    auto& i18n = nxui::I18n::instance();
    const CardEntry& card = m_catalog.at(m_selected);
    const std::uint64_t titleId = card.app.titleId;
    const std::string title = card.app.title;
    const std::string path = card.homebrewPath;
    m_cardOptIndex = m_selected;
    m_cardOptTitleId = titleId;
    m_cardOptTitle = title;
    m_cardOptVersion = card.version;
    m_cardOptAuthor = card.author;
    m_cardOptIsHomebrew = card.isHomebrew;
    m_cardOptSystem = card.system;
    m_cardOptItems.clear();

    auto add = [this](std::string label, const char* icon, std::function<void()> fn, bool danger = false, bool primary = false) {
        CardOptionItem item;
        item.label = std::move(label);
        item.icon = icon;
        item.run = std::move(fn);
        item.danger = danger;
        item.primary = primary;
        m_cardOptItems.push_back(std::move(item));
    };

#ifdef SWITCHU_MENU
    const bool suspended = isRunningCard(titleId);
#else
    const bool suspended = false;
#endif

    // Primary: Play / Resume / Open
    const std::string playLabel = suspended ? i18n.tr("cards.resume", "Resume")
                                : ((card.isHomebrew || card.system) ? i18n.tr("cards.open", "Open")
                                                                     : i18n.tr("cards.play", "Play"));
    add(playLabel, "play", [this]() {
        closeCardOptions();
        activateSelected();
    }, false, true);

    // HomeNX: each kind of card has its own menu. An applet (Nintendo page
    // tile) only moves, renames and goes on the panel button.
    auto panelPin = [&]() {
        const bool isPinned = (m_core.config().cards.panelTitleId == titleId);
        if (!isPinned) {
            add(i18n.tr("cards.panel_pin", "Put on panel button"), "copy", [this, titleId]() {
                m_core.config().cards.panelTitleId = titleId;
                m_core.config().cards.panelButtonEnabled = true;
                saveConfig("cards panel shortcut");
                closeCardOptions();
                showToast(nxui::I18n::instance().tr("cards.panel_pinned_toast", "Placed on panel button"));
            });
        } else {
            add(i18n.tr("cards.panel_unpin", "Remove from panel button"), "copy", [this]() {
                m_core.config().cards.panelTitleId = 0;
                saveConfig("cards panel shortcut");
                closeCardOptions();
                showToast(nxui::I18n::instance().tr("cards.panel_unpinned_toast", "Removed from panel button (News restored)"));
            });
        }
    };
    if (card.system) {
        add(i18n.tr("cards.opt_move", "Move"), "arrows", [this]() {
            closeCardOptions();
            startMove();
        });
        add(i18n.tr("dialog.details_rename", "Rename"), "tag", [this]() {
            closeCardOptions();
            renameSelected();
        });
        panelPin();
        m_cardOptSel = 0;
        m_cardOptIn = 0.f;
        m_popupBackdropValid = false;
        m_cardOptOpen = true;
        m_audio.playSfx(Sfx::ModalShow);
        return;
    }

    // Update (homebrew with known update)
    if (card.isHomebrew && homebrewHasUpdate(path, card.version)) {
        add(i18n.tr("cards.hb_update_one", "Update"), "restart", [this, path]() {
            closeCardOptions();
            installHomebrewUpdates({path});
        });
    }

#ifdef SWITCHU_MENU
    if (suspended) {
        add(i18n.tr("cards.close_software", "Close software"), "power", [this]() {
            closeCardOptions();
            m_closeConfirmQueued = true;
        }, true);
    }
#endif

    // HomeNX order. Homebrew: Move, Rename, Panel button, Move to Games,
    // Move to Media, New pictures, Card image, Background image, Show as
    // card image, Delete. Games: Move, Rename, Move to Games/Homebrew (or
    // Media), New pictures, Card image, Title image, Background, Achievements.
    add(i18n.tr("cards.opt_move", "Move"), "arrows", [this]() {
        closeCardOptions();
        startMove();
    });
    add(i18n.tr("dialog.details_rename", "Rename"), "tag", [this]() {
        closeCardOptions();
        renameSelected();
    });
    if (card.isHomebrew)
        panelPin();

    // Moving between pages: Media holds games and homebrew put there by hand.
    const bool media = m_page == AppConfig::CardsPrefs::kMedia;
    auto movePage = [this, titleId](bool toMedia) {
        closeCardOptions();
        auto& config = m_core.config();
        if (!pages::setMedia(config.cards, titleId, toMedia))
            return;
        if (toMedia && config.cards.pageHidden(AppConfig::CardsPrefs::kMedia))
            pages::togglePage(config.cards, AppConfig::CardsPrefs::kMedia);
        saveConfig("cards media");
        m_catalog.rebuild(config);
        m_selected = std::clamp(m_selected, 0, std::max(0, m_catalog.count() - 1));
        m_scroll = static_cast<float>(m_selected);
        auto& t = nxui::I18n::instance();
        showToast(toMedia ? t.tr("cards.moved_media", "Moved to Media")
                          : t.tr("cards.moved_back", "Moved back to its page"));
    };
    if (media) {
        add(card.isHomebrew ? i18n.tr("cards.opt_to_homebrew", "Move to Homebrew")
                            : i18n.tr("cards.opt_to_games", "Move to Games"),
            card.isHomebrew ? "dots" : "play", [movePage]() { movePage(false); });
    } else {
        add(i18n.tr("cards.opt_to_media", "Move to Media"), "music", [movePage]() { movePage(true); });
    }

    add(i18n.tr("cards.opt_new_pictures", "Download new pictures"), "restart", [this, titleId, title]() {
        closeCardOptions();
        downloadSingleTitleArt(titleId, title);
    });
    add(i18n.tr("cards.opt_card_image", "Card image"), "copy", [this, titleId, title]() {
        closeCardOptions();
        openTitlePicker(titleId, title, SteamGridDbManager::ArtworkKind::Cover);
    });
    if (!card.isHomebrew) {
        add(i18n.tr("cards.opt_title_image", "Title image"), "tag", [this, titleId, title]() {
            closeCardOptions();
            openTitlePicker(titleId, title, SteamGridDbManager::ArtworkKind::Logo);
        });
    }
    add(i18n.tr("cards.bg_image", "Background picture"), "sun", [this, titleId, title]() {
        closeCardOptions();
        openTitlePicker(titleId, title, SteamGridDbManager::ArtworkKind::Hero);
    });
    if (card.isHomebrew) {
        // HomeNX "Show as card image": the homebrew icon fills the card
        // instead of a cover (toggle of the per-title icon choice).
        const bool iconCard = std::find(m_core.config().cards.iconTitles.begin(), m_core.config().cards.iconTitles.end(),
                                        titleId) != m_core.config().cards.iconTitles.end();
        add(iconCard ? i18n.tr("cards.opt_show_cover", "Show the cover on the card")
                     : i18n.tr("cards.opt_show_icon", "Show icon as card image"),
            "copy", [this, titleId, iconCard]() {
                closeCardOptions();
                auto& v = m_core.config().cards.iconTitles;
                if (iconCard)
                    v.erase(std::remove(v.begin(), v.end(), titleId), v.end());
                else
                    v.push_back(titleId);
                m_art.releaseAll();
                saveConfig("cards icon image");
            });
    } else {
        add(i18n.tr("cards.art_choices", "Images"), "copy", [this, titleId]() {
            closeCardOptions();
            openArtSheet(titleId);
        });
        add(i18n.tr("cards.menu_achievements", "Achievements"), "sun", [this, titleId]() {
            closeCardOptions();
            openAchievementsSheet(titleId);
        });
    }

    // Delete
#ifdef SWITCHU_MENU
    if (card.isHomebrew) {
        if (!suspended) {
            add(i18n.tr("cards.hb_delete", "Delete"), "trash", [this, path, title, titleId]() {
                closeCardOptions();
                if (!m_userSelect) return;
                auto& t = nxui::I18n::instance();
                m_audio.playSfx(Sfx::ModalShow);
                m_userSelect->show(
                    t.tr("cards.hb_delete_title", "Delete homebrew?"),
                    title + "\n" + path,
                    {{t.tr("button.cancel", "Cancel"), []() {}, true},
                     {t.tr("cards.hb_delete_confirm", "Delete"), [this, path, title, titleId]() {
                          if (m_hbFuture.valid() &&
                              m_hbFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                              showToast(nxui::I18n::instance().tr("cards.hb_delete_busy",
                                                                  "Wait for the homebrew update to finish"));
                              return;
                          }
                          std::error_code ec;
                          const bool ok = std::filesystem::remove(path, ec) && !ec;
                          switchu::commitSdCard("cards homebrew delete");
                          DebugLog::log("[cards] homebrew delete %s: %s", path.c_str(), ok ? "ok" : "failed");
                          auto& tt = nxui::I18n::instance();
                          if (!ok) {
                              showToast(tt.tr("cards.hb_delete_failed", "The homebrew could not be deleted"));
                              return;
                          }
                          if (m_core.config().cards.panelTitleId == titleId)
                              m_core.config().cards.panelTitleId = 0;
                          m_catalog.setPage(m_page, m_core.config());
                          m_selected = std::clamp(m_selected, 0, std::max(0, m_catalog.count() - 1));
                          m_scroll = static_cast<float>(m_selected);
                          showToast(tt.tr("cards.hb_deleted", "Deleted") + ": " + title);
                     }, true}},
                    0, {});
                focusManager().setFocus(m_userSelect.get());
            }, true);
        }
    } else if (!card.app.isGameCard()) {
        if (!suspended) {
            add(i18n.tr("game.delete_software", "Delete"), "trash", [this, titleId]() {
                closeCardOptions();
                m_deleteConfirmId = titleId;
            }, true);
        }
    }
#endif

    m_cardOptSel = 0;
    m_cardOptIn = 0.f;
    m_popupBackdropValid = false;
    m_cardOptOpen = true;
    m_audio.playSfx(Sfx::ModalShow);
}

bool CardsHomeActivity::handleCardOptionsInput(nxui::Input& in) {
    if (!m_cardOptOpen) return false;
    using B = nxui::Button;
    const int n = static_cast<int>(m_cardOptItems.size());
    if (in.touchDown() || in.touchUp()) {
        const float x = in.touchX(), y = in.touchY();
        if (!kOptCard.contains(x, y)) {
            closeCardOptions();
            return true;
        }
        if (in.touchUp()) {
            for (std::size_t i = 0; i < m_cardOptItems.size(); ++i) {
                const nxui::Rect r = cardOptionRow(m_cardOptItems.size(), i, 0.f);
                if (r.contains(x, y)) {
                    m_cardOptSel = static_cast<int>(i);
                    auto run = m_cardOptItems[i].run;
                    closeCardOptions();
                    if (run) run();
                    return true;
                }
            }
        }
        return true;
    }
    const bool up = in.isDown(B::DUp) || in.isDown(B::LStickU);
    const bool down = in.isDown(B::DDown) || in.isDown(B::LStickD);
    const bool left = in.isDown(B::DLeft) || in.isDown(B::LStickL);
    const bool right = in.isDown(B::DRight) || in.isDown(B::LStickR);
    int s = m_cardOptSel;
    if (s == 0) {
        if (down && n > 1) s = 1;
    } else {
        const int k = s - 1;
        if (left && k % 2 == 1) s -= 1;
        else if (right && k % 2 == 0 && s + 1 < n) s += 1;
        else if (up) s = (k < 2) ? 0 : s - 2;
        else if (down && s + 2 < n) s += 2;
        else if (down && k % 2 == 1 && s + 1 < n) s += 1;
    }
    if (s != m_cardOptSel) {
        m_cardOptSel = s;
        m_audio.playSfx(Sfx::Navigate);
    }
    if (in.isDown(B::B) || in.isDown(B::Plus)) {
        closeCardOptions();
        return true;
    }
    if (in.isDown(B::A) && m_cardOptSel >= 0 && m_cardOptSel < n) {
        auto run = m_cardOptItems[static_cast<std::size_t>(m_cardOptSel)].run;
        closeCardOptions();
        if (run) run();
        return true;
    }
    return true;
}

void CardsHomeActivity::drawCardOptions(nxui::Renderer& ren) {
    if (!m_cardOptOpen) return;
    m_cardOptIn = std::min(1.f, m_cardOptIn + 1.f / 10.f);
    const float a = 1.f - (1.f - m_cardOptIn) * (1.f - m_cardOptIn);

    const nxui::Rect card = kOptCard;
    drawGlassCard(ren, card, 32.f, a);

    const float dy = 0.f;
    const std::uint64_t id = m_cardOptTitleId;

    const nxui::Rect art{card.x + 32.f, card.y + 50.f, 260.f, 368.f};
    ren.drawRoundedRect({art.x - 3.f, art.y + 8.f, art.width + 6.f, art.height + 6.f},
                        {0.f, 0.f, 0.f, 0.32f * a}, 27.f);
    const nxui::Texture* cover = (m_cardOptSystem) ? nullptr : m_art.cover(id);
    if (cover) {
        CardsChrome::drawCover(ren, cover, art, 24.f, CardsChrome::kWhite.withAlpha(a));
    } else {
        ren.drawRoundedRect(art, {0.28f, 0.31f, 0.40f, 0.88f * a}, 24.f);
        if (m_cardOptIndex >= 0 && m_cardOptIndex < m_catalog.count()) {
            const auto& c = m_catalog.at(m_cardOptIndex);
            if (c.icon && c.icon->valid()) {
                const float side = 170.f;
                const nxui::Rect ic{art.x + (art.width - side) * 0.5f, art.y + (art.height - side) * 0.42f, side, side};
                ren.drawTextureRounded(c.icon.get(), ic, 24.f, CardsChrome::kWhite.withAlpha(a));
            }
        }
    }

    auto& tf = m_chrome.titleFont();
    auto& lf = m_chrome.labelFont();
    auto& sf = m_chrome.smallFont();
    auto& i18n = nxui::I18n::instance();

    const float tx = art.x + art.width + 36.f;
    const float maxW = card.x + card.width - 32.f - tx;
    const std::string nameFit = m_chrome.fit(tf, m_cardOptTitle, maxW);
    m_chrome.textMid(ren, tf, nameFit, tx, card.y + 60.f, CardsChrome::kWhite.withAlpha(a));

    std::string about;
    if (m_cardOptSystem) {
        about = i18n.tr("cards.system_app", "System Application");
    } else if (m_cardOptIsHomebrew) {
        about = m_cardOptAuthor.empty() ? i18n.tr("cards.homebrew", "Homebrew") : m_cardOptAuthor;
    } else {
        const auto optPlayed = switchu::menu::playtime::query(id);
        const std::uint64_t played = (optPlayed && *optPlayed > 0) ? *optPlayed : 0;
        if (played > 0)
            about = switchu::menu::playtime::format(played);
        else
            about = i18n.tr("cards.not_played", "Not played yet");
        if (m_details.titleId == id && m_details.lastPlayed && *m_details.lastPlayed > 0) {
            const std::string ago = lastPlayedText(*m_details.lastPlayed);
            if (!ago.empty())
                about += "    " + ago;
        }
    }
    if (!m_cardOptVersion.empty()) {
        about += (about.empty() ? "" : "   ") + m_cardOptVersion;
    }
    m_chrome.textMid(ren, sf, about, tx, card.y + 96.f, CardsChrome::kWhite.withAlpha(0.72f * a));

    {
        const std::vector<ui::glyph::ActionHint> hints = {
            {ui::glyph::button(nxui::Button::A), i18n.tr("hint.select", "Select")},
            {ui::glyph::button(nxui::Button::B), i18n.tr("hint.back", "Back")},
        };
        ui::glyph::drawHintBar(ren, m_fontIcons, sf, hints, card.right() - 24.f, card.bottom() - 12.f, a);
    }

    for (std::size_t i = 0; i < m_cardOptItems.size(); ++i) {
        const auto& item = m_cardOptItems[i];
        const nxui::Rect row = cardOptionRow(m_cardOptItems.size(), i, dy);
        const bool chosen = (static_cast<int>(i) == m_cardOptSel);
        const bool primary = item.primary;

        if (primary) {
            ren.drawRoundedRect(row, CardsChrome::kWhite.withAlpha((chosen ? 1.f : 0.88f) * a), row.height * 0.5f);
            if (chosen)
                ren.drawRoundedRectOutline({row.x - 3.f, row.y - 3.f, row.width + 6.f, row.height + 6.f},
                                           CardsChrome::accent(a), row.height * 0.5f + 3.f, 2.5f);
        } else {
            ren.drawRoundedRect(row, {1.f, 1.f, 1.f, (chosen ? 0.18f : 0.08f) * a}, row.height * 0.5f);
            if (chosen)
                ren.drawRoundedRectOutline({row.x - 2.f, row.y - 2.f, row.width + 4.f, row.height + 4.f},
                                           CardsChrome::accent(a), row.height * 0.5f + 2.f, 2.f);
        }

        const float cy = row.y + row.height * 0.5f;
        const float d = row.height - 12.f;
        const nxui::Color disc = primary ? nxui::Color(0.f, 0.f, 0.f, 0.85f * a)
                               : item.danger ? nxui::Color(0.95f, 0.28f, 0.25f, 0.9f * a)
                               : nxui::Color(0.25f, 0.28f, 0.35f, 0.75f * a);
        ren.drawCircle({row.x + 10.f + d * 0.5f, cy}, d * 0.5f, disc, 32);
        if (item.icon) {
            m_chrome.icon(ren, item.icon, row.x + 10.f + d * 0.5f, cy, d * 0.55f, CardsChrome::kWhite.withAlpha(a));
        }

        const nxui::Color ink = primary ? nxui::Color(0.f, 0.f, 0.f, a)
                              : item.danger ? nxui::Color(1.f, 0.45f, 0.42f, a)
                              : CardsChrome::kWhite.withAlpha(a);
        const float room = row.width - 24.f - d - 14.f;
        const std::string lblFit = m_chrome.fit(lf, item.label, room);
        m_chrome.textMid(ren, lf, lblFit, row.x + 18.f + d, cy, ink);
    }
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
    if (m_selected < 0 || m_selected >= m_catalog.count())
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
    // HomeNX's account picker (the one Classic uses): a row of avatar tiles,
    // a name bubble, the details panel and the "+" tile.
    if (!m_profiles) {
        m_profiles = std::make_shared<ProfileCarouselScreen>();
        m_profiles->setFont(&m_font);
        m_profiles->setSmallFont(&m_fontSmall);
        m_profiles->setTheme(&m_overlayTheme);
        m_profiles->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
        m_profiles->onLockSfx([this]() { m_audio.playSfx(Sfx::ConfirmPositive); });
        m_profiles->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
        m_profiles->onLockProfile([](std::optional<AccountUid>) {});
        m_profiles->onOpenProfile([this](AccountUid uid) {
            m_profiles->hide();
            m_core.launcher().launchUserPage(uid);
        });
        m_profiles->onAddUser([this]() {
            m_profiles->hide();
            m_core.launcher().launchUserCreator();
        });
        m_profiles->onClosed([this]() { m_overlayLatch = true; });
    }
    std::vector<HomeProfile> list;
    AccountUid uids[8] = {};
    s32 count = 0;
    if (R_SUCCEEDED(accountListAllUsers(uids, 8, &count))) {
        for (int i = 0; i < count; ++i) {
            AccountProfile profile{};
            if (R_FAILED(accountGetProfile(&profile, uids[i]))) continue;
            AccountProfileBase base{};
            AccountUserData userData{};
            if (R_SUCCEEDED(accountProfileGet(&profile, &userData, &base))) {
                HomeProfile p;
                p.uid = uids[i];
                p.nickname = base.nickname;
                u32 imgSize = 0;
                if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &imgSize)) && imgSize > 0) {
                    std::vector<std::uint8_t> buf(imgSize);
                    u32 real = 0;
                    if (R_SUCCEEDED(accountProfileLoadImage(&profile, buf.data(), imgSize, &real)) && real > 0) {
                        auto tex = std::make_shared<nxui::Texture>();
                        if (tex->loadFromMemory(app().gpu(), app().renderer(), buf.data(), real))
                            p.avatar = std::move(tex);
                    }
                }
                list.push_back(std::move(p));
            }
            accountProfileClose(&profile);
        }
    }
    const bool canAdd = list.size() < 8;
    m_profiles->setProfiles(std::move(list), canAdd);
    m_audio.playSfx(Sfx::ModalShow);
    m_profiles->show();
    focusManager().setFocus(m_profiles.get());
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
    // Discord: only messages that arrived while the menu was open (newest
    // first), not the whole DM list -- that is the Discord panel's job.
    for (auto it = m_discordNotices.rbegin(); it != m_discordNotices.rend(); ++it) {
        CardsSheet::Row r;
        r.label = "Discord: " + it->title;
        r.detail = it->text;
        r.onA = [this, chanId = it->channelId, guildId = it->guildId, name = it->title]() {
            m_sheet.close();
            m_discordNotices.clear();
            openDiscord();
            if (m_discordPanel) m_discordPanel->openChat(chanId, guildId, name);
        };
        rows.push_back(std::move(r));
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
    // No game chosen (menu on the Nintendo page): only the overall set.
    if (titleId != 0) try {
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
    if (titleId != 0) for (const auto& m : ach::kPerGame) {
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
            // As build 185: "0.4 / 10 h", "6 / 10".
            char buf[48];
            if (mp->hours)
                std::snprintf(buf, sizeof(buf), "%.1f / %u h", static_cast<double>(st->play.seconds) / 3600.0, mp->goal);
            else
                std::snprintf(buf, sizeof(buf), "%u / %u", st->play.launches, mp->goal);
            return buf;
        };
        rows.push_back(std::move(r));
    }    const int idx = m_catalog.indexOf(titleId);
    const std::string title = idx >= 0 ? m_catalog.at(idx).app.title : std::string();

    // Overall milestones (build 185): different games played and all play
    // time together, from the play time cache; recorded on the pool.
    auto overall = std::make_shared<ach::Play>();
    for (const auto& [id, ns] : m_core.config().playtime) {
        if (ns == 0 || pages::isSystemId(id) || pages::isHomebrewId(id))
            continue;
        overall->launches += 1;
        overall->seconds += ns / 1000000000ULL;
    }
    try {
        m_core.threadPool().submit([overall]() {
            bool ok = true;
            try { ach::recordOverallAndSave(ach::kPath, *overall, static_cast<std::int64_t>(std::time(nullptr)), ok); }
            catch (...) { ok = false; }
            if (!ok) DebugLog::log("[cards] overall achievements save failed");
        });
    } catch (...) {
    }
    std::vector<CardsSheet::Row> allRows;
    for (const auto& m : ach::kOverall) {
        const ach::Milestone* mp = &m;
        CardsSheet::Row r;
        r.label = i18n.tr(m.key, m.name);
        r.detail = m.about;
        r.progress = [overall, mp]() { return ach::progress(*mp, *overall); };
        r.value = [overall, mp]() -> std::string {
            const float f = ach::progress(*mp, *overall);
            if (f >= 1.f) return nxui::I18n::instance().tr("cards.ach_done", "Earned");
            char buf[48];
            if (mp->hours)
                std::snprintf(buf, sizeof(buf), "%.1f / %u h", static_cast<double>(overall->seconds) / 3600.0, mp->goal);
            else
                std::snprintf(buf, sizeof(buf), "%u / %u", overall->launches, mp->goal);
            return buf;
        };
        allRows.push_back(std::move(r));
    }
    std::vector<CardsSheet::Section> sections;
    if (titleId != 0)
        sections.push_back({title.empty() ? i18n.tr("cards.ach_this_game", "This game") : title, std::move(rows)});
    sections.push_back({i18n.tr("cards.ach_overall", "All games"), std::move(allRows)});
    m_sheet.open(i18n.tr("cards.menu_achievements", "Achievements"), std::move(sections));
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
            m_core.config().sfxVolume = v;
            m_audio.setSfxVolume(m_core.config().cards.sounds ? v : 0.f);
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
        // E6 (C-UI-8): mini player, Bluetooth toggle and the Hekate tile.
        cbs.bluetoothEnabled = []() { return bluetooth::IsAvailable() && bluetooth::IsRadioEnabled(); };
        cbs.onBluetoothToggled = [this](bool on) {
            m_core.startBluetooth();
            if (R_FAILED(bluetooth::SetRadioEnabled(on)))
                showToast(nxui::I18n::instance().tr("cards.bluetooth_failed", "Bluetooth change failed"));
        };
        cbs.musicTitle = [this]() {
            return m_audio.isPlaying() ? m_audio.currentTrackTitle() : std::string();
        };
        cbs.musicPlaying = [this]() { return m_audio.isPlaying() && !m_audio.isPaused(); };
        cbs.onMusicToggle = [this]() {
            if (!m_audio.isPlaying()) {
                m_core.config().musicEnabled = true;
                m_audio.play();
            } else {
                m_audio.togglePlayPause();
            }
        };
        cbs.onMusicSkip = [this](int dir) {
            if (dir < 0) m_audio.previousTrack();
            else m_audio.nextTrack();
        };
        cbs.onHekateRequested = [this]() {
            closeQuickSettings();
            m_hekateQueued = true;
        };
        cbs.onNavigateSfx = [this]() { m_audio.playSfx(Sfx::Navigate); };
        cbs.onActivateSfx = [this]() { m_audio.playSfx(Sfx::Activate); };
        cbs.onToggleOffSfx = [this]() { m_audio.playSfx(Sfx::ToggleOff); };
        m_quick->setCallbacks(cbs);
        m_quick->setExtended(true);
    }
    if (m_quick->isActive())
        return;
    m_quick->setInitialValues(0.5f, m_audio.volume(), m_core.config().sfxVolume, false, true);
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
        CardsSheet::Section s{i18n.tr("cards.set_home", "Home"), {}, "home", nxui::Color(0.23f, 0.51f, 0.96f)};
        for (int page : m_core.config().cards.pageOrder) {
            Row r;
            r.label = i18n.tr(pageKey(page), pageFallback(page));
            r.detail = i18n.tr("cards.set_page_hint", "A: show or hide   Left/Right: order");
            r.value = [this, page]() { return onOff(!m_core.config().cards.pageHidden(page)); };
            r.toggle = [this, page]() { return !m_core.config().cards.pageHidden(page); };
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
        ask.toggle = [this]() { return m_core.config().cardsAskUser; };
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
            return onOff(m_core.config().cards.showNextendo);
        };
        nextendo.onChange = [this, reopen](int) {
            auto& c = m_core.config().cards;
            c.showNextendo = !c.showNextendo;
            saveConfig("cards nextendo players");
            reopen();
        };
        s.rows.push_back(std::move(nextendo));
        Row scan;
        scan.label = i18n.tr("cards.set_scan", "Look for new software");
        scan.detail = i18n.tr("cards.set_scan_desc", "Reads the installed list and the Homebrew folder again");
        scan.onA = [this]() {
            m_sheet.close();
            refreshCatalogue();
            m_catalog.setPage(m_page, m_core.config());
            showToast(nxui::I18n::instance().tr("cards.set_scan_done", "Looking for new software..."));
        };
        s.rows.push_back(std::move(scan));
        Row reset;
        reset.label = i18n.tr("cards.set_reset_order", "Reset my order");
        reset.enabled = !m_core.config().cards.gamesOrder.empty();
        reset.onA = [this, reopen]() {
            auto& c = m_core.config();
            c.cards.gamesOrder.clear();
            if (c.cardsSort == 3) c.cardsSort = 0;
            saveConfig("cards reset order");
            if (m_page == AppConfig::CardsPrefs::kGames) applySort(c.cardsSort);
            showToast(nxui::I18n::instance().tr("cards.set_reset_order_done", "Order reset"));
            reopen();
        };
        s.rows.push_back(std::move(reset));
        all["home"] = std::move(s);
    }
    { // Appearance (HomeNX E8, C-SK-6/8: accent, glass, haze, motion).
        CardsSheet::Section s{i18n.tr("cards.set_look", "Appearance"), {}, "sun", nxui::Color(0.66f, 0.33f, 0.97f)};
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
        bgm.toggle = [this]() { return m_core.config().cards.bgMotion; };
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
        mot.toggle = [this]() { return m_core.config().cards.motion; };
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
        clock.toggle = [this]() { return !m_core.config().clockUse12Hour; };
        clock.onChange = [this](int) {
            auto& c = m_core.config();
            c.clockUse12Hour = !c.clockUse12Hour;
            m_lock.setUse12HourClock(c.clockUse12Hour);
            m_statusTimer = 0.f;
            saveConfig("cards clock");
        };
        s.rows.push_back(std::move(clock));
        Row freeRow;
        freeRow.label = i18n.tr("cards.set_show_free", "Free space in the bar");
        freeRow.value = [this]() { return onOff(m_core.config().cards.showFree); };
        freeRow.toggle = [this]() { return m_core.config().cards.showFree; };
        freeRow.onChange = [this](int) {
            auto& c = m_core.config().cards;
            c.showFree = !c.showFree;
            m_statusTimer = 0.f;
            saveConfig("cards show free");
        };
        s.rows.push_back(std::move(freeRow));
        Row tempRow;
        tempRow.label = i18n.tr("cards.set_show_temp", "Temperature in the bar");
        tempRow.value = [this]() { return onOff(m_core.config().cards.showTemp); };
        tempRow.toggle = [this]() { return m_core.config().cards.showTemp; };
        tempRow.onChange = [this](int) {
            auto& c = m_core.config().cards;
            c.showTemp = !c.showTemp;
            m_statusTimer = 0.f;
            saveConfig("cards show temp");
        };
        s.rows.push_back(std::move(tempRow));
        Row wallRow; // HomeNX "Background"
        wallRow.label = i18n.tr("cards.set_wallpaper", "Background picture");
        wallRow.detail = i18n.tr("cards.set_wallpaper_desc", "One of your pictures behind everything");
        wallRow.value = [this]() {
            const auto& c = m_core.config().cards;
            if (c.shuffleWall)
                return nxui::I18n::instance().tr("cards.set_shuffle_wall", "Shuffle background");
            if (c.wallpaper.empty())
                return nxui::I18n::instance().tr("cards.wall_game_art", "Each game's art");
            return std::filesystem::path(c.wallpaper).filename().string();
        };
        wallRow.onA = [this]() { openWallpaperSheet(); };
        s.rows.push_back(std::move(wallRow));
        Row shuffleWall; // HomeNX "Shuffle background"
        shuffleWall.label = i18n.tr("cards.set_shuffle_wall", "Shuffle background");
        shuffleWall.detail = i18n.tr("cards.set_shuffle_wall_desc", "Another of your pictures at every start");
        shuffleWall.toggle = [this]() { return m_core.config().cards.shuffleWall; };
        shuffleWall.onChange = [this](int) {
            auto& c = m_core.config().cards;
            c.shuffleWall = !c.shuffleWall;
            saveConfig("cards shuffle background");
            startWallpaper(c.shuffleWall);
        };
        s.rows.push_back(std::move(shuffleWall));
        all["look"] = std::move(s);
    }
    { // Sound: shared with Classic, stored once at the top level.
        CardsSheet::Section s{i18n.tr("cards.set_sound", "Sound"), {}, "speaker", nxui::Color(0.94f, 0.27f, 0.27f)};
        Row music;
        music.label = i18n.tr("cards.set_music", "Music");
        music.value = [this]() { return onOff(m_core.config().musicEnabled); };
        music.toggle = [this]() { return m_core.config().musicEnabled; };
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
                    m_audio.setSfxVolume(c.cards.sounds ? v : 0.f);
                else
                    m_audio.setVolume(v);
                saveConfig("cards volume");
            };
            s.rows.push_back(std::move(r));
        }
        Row shuffle;
        shuffle.label = i18n.tr("cards.set_shuffle", "Shuffle");
        // Shared with Classic's Media Center: one shuffle setting, so a
        // frontend switch no longer reshuffles the songs.
        shuffle.value = [this]() { return onOff(m_core.config().customBgmShuffle); };
        shuffle.toggle = [this]() { return m_core.config().customBgmShuffle; };
        shuffle.onChange = [this](int) {
            auto& c = m_core.config();
            c.customBgmShuffle = !c.customBgmShuffle;
            m_audio.setShuffle(c.customBgmShuffle);
            saveConfig("shuffle");
        };
        s.rows.push_back(std::move(shuffle));
        Row repeat; // HomeNX "Repeat song"
        repeat.label = i18n.tr("cards.set_repeat", "Repeat song");
        repeat.detail = i18n.tr("cards.set_repeat_desc", "Play the same song again");
        repeat.toggle = [this]() { return m_audio.isRepeat(); };
        repeat.onChange = [this](int) {
            auto& c = m_core.config();
            c.bgmRepeat = !m_audio.isRepeat();
            m_audio.setRepeat(c.bgmRepeat);
            saveConfig("cards repeat");
        };
        s.rows.push_back(std::move(repeat));
        Row sounds;
        sounds.label = i18n.tr("cards.set_sounds", "Sounds");
        sounds.detail = i18n.tr("cards.set_sounds_desc", "Interface sound effects");
        sounds.value = [this]() { return onOff(m_core.config().cards.sounds); };
        sounds.toggle = [this]() { return m_core.config().cards.sounds; };
        sounds.onChange = [this](int) {
            auto& c = m_core.config();
            c.cards.sounds = !c.cards.sounds;
            m_audio.setSfxVolume(c.cards.sounds ? c.sfxVolume : 0.f);
            saveConfig("cards sounds");
        };
        s.rows.push_back(std::move(sounds));
        all["sound"] = std::move(s);
    }
    { // Console: the system pages that own network and controllers.
        CardsSheet::Section s{i18n.tr("cards.set_console", "Console"), {}, "power", nxui::Color(0.39f, 0.45f, 0.55f)};
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
        Row padTest;
        padTest.label = i18n.tr("settings.controllers.test", "Test Input Devices");
        padTest.detail = i18n.tr("settings.controllers.test_desc", "Test joysticks, buttons, and the touch screen.");
        padTest.onA = [this]() { openPadTest(); };
        s.rows.push_back(std::move(padTest));
#ifdef SWITCHU_MENU
        Row vibration;
        vibration.label = i18n.tr("settings.controllers.vibration", "Vibration");
        vibration.value = []() {
            return std::to_string(static_cast<int>(std::lround(settings::system_prefs::vibrationVolume() * 100.f))) + "%";
        };
        vibration.progress = []() { return settings::system_prefs::vibrationVolume(); };
        vibration.onChange = [](int d) {
            const bool ok = settings::system_prefs::setVibrationVolume(settings::system_prefs::vibrationVolume() + 0.1f * static_cast<float>(d));
            DebugLog::log("[settings] vibration -> %.2f ok=%d", settings::system_prefs::vibrationVolume(), ok ? 1 : 0);
        };
        s.rows.push_back(std::move(vibration));
        Row tvRes;
        tvRes.label = i18n.tr("settings.audio.tv_resolution", "TV Resolution");
        tvRes.detail = i18n.tr("settings.audio.tv_resolution_desc", "Adjust HDMI resolution. Auto uses recommended value.");
        tvRes.value = []() {
            auto& t = nxui::I18n::instance();
            switch (settings::system_prefs::tvResolution()) {
            case 1: return t.tr("settings.audio.res_1080p", "1080p");
            case 2: return t.tr("settings.audio.res_720p", "720p");
            case 3: return t.tr("settings.audio.res_480p", "480p");
            default: return t.tr("common.auto", "Auto");
            }
        };
        tvRes.onChange = [](int d) {
            const int n = settings::system_prefs::kTvResolutionCount;
            const bool ok = settings::system_prefs::setTvResolution((settings::system_prefs::tvResolution() + d + n) % n);
            DebugLog::log("[settings] tv resolution -> %d ok=%d", settings::system_prefs::tvResolution(), ok ? 1 : 0);
        };
        s.rows.push_back(std::move(tvRes));
        Row native;
        native.label = i18n.tr("settings.omnilaunch.docked_1080p", "1080p on the TV");
        native.detail = i18n.tr("settings.omnilaunch.docked_1080p_desc",
            "Docked, HOME is drawn at 1920x1080. Sharper, uses more memory. Applies the next time HOME starts.");
        native.toggle = []() { return settings::system_prefs::dockedNativeRender(); };
        native.onChange = [](int) {
            settings::system_prefs::setDockedNativeRender(!settings::system_prefs::dockedNativeRender());
        };
        s.rows.push_back(std::move(native));
        Row ingame;
        ingame.label = i18n.tr("settings.omnilaunch.ingame_discord", "Discord messages during games (experimental)");
        ingame.detail = settings::system_prefs::ultrahandNotificationsPresent()
            ? i18n.tr("settings.omnilaunch.ingame_discord_desc",
                      "New direct messages appear as Ultrahand notifications, checked every 2 to 4 minutes. Uses your account token; Discord may flag automated use.")
            : i18n.tr("settings.omnilaunch.ingame_discord_needs",
                      "Needs Ultrahand with notifications turned on.");
        ingame.toggle = []() { return settings::system_prefs::inGameDiscord(); };
        ingame.onChange = [](int) {
            settings::system_prefs::setInGameDiscord(!settings::system_prefs::inGameDiscord());
        };
        s.rows.push_back(std::move(ingame));
#endif
        Row tut;
        tut.label = i18n.tr("cards.set_tutorial", "Show the welcome tour again");
        tut.onA = [this]() {
            m_sheet.close();
            m_tutorialCard = 1;
            m_tutorialIn = 0.f;
            m_popupBackdropValid = false;
        };
        s.rows.push_back(std::move(tut));
        Row mtp;
        mtp.label = i18n.tr("cards.set_mtp", "Connect to a computer");
        mtp.detail = i18n.tr("cards.set_mtp_desc", "Plug in a USB cable: the SD card shows up as a drive (MTP)");
        mtp.value = [this]() {
            auto& t = nxui::I18n::instance();
            if (!m_core.config().mtpEnabled)
                return onOff(false);
#ifdef SWITCHU_MENU
            return switchu::usb::mtpConnected() ? t.tr("cards.set_mtp_on", "Connected")
                                                : t.tr("cards.set_mtp_ready", "Ready");
#else
            return std::string();
#endif
        };
        mtp.toggle = [this]() { return m_core.config().mtpEnabled; };
        mtp.onChange = [this](int) {
            auto& c = m_core.config();
            c.mtpEnabled = !c.mtpEnabled;
#ifdef SWITCHU_MENU
            // Start/stop the responder now; the setting also decides it at
            // every HOME start. Stopping joins the thread (short).
            if (c.mtpEnabled)
                switchu::usb::mtpStart();
            else
                switchu::usb::mtpStop();
#endif
            saveConfig("cards mtp");
        };
        s.rows.push_back(std::move(mtp));
        Row lang;
        lang.label = i18n.tr("cards.set_language", "Language");
        lang.detail = i18n.tr("cards.set_language_desc2", "Left/Right to change the OmniLaunch language");
        lang.value = [this]() {
            auto& t = nxui::I18n::instance();
            const std::string& tag = m_core.config().uiLanguageOverride;
            if (tag.empty() || tag == "auto")
                return t.tr("common.auto", "Auto") + " (" + t.activeLanguageTag() + ")";
            return t.tr("languages." + tag, tag);
        };
        lang.onChange = [this, reopen](int dir) {
            const std::vector<std::string> tags = nxui::I18n::supportedLanguageTags();
            if (tags.empty())
                return;
            auto& c = m_core.config();
            auto it = std::find(tags.begin(), tags.end(), c.uiLanguageOverride.empty() ? "auto" : c.uiLanguageOverride);
            int idx = it == tags.end() ? 0 : static_cast<int>(it - tags.begin());
            const int n = static_cast<int>(tags.size());
            idx = ((idx + (dir < 0 ? -1 : 1)) % n + n) % n;
            c.uiLanguageOverride = tags[static_cast<std::size_t>(idx)];
            auto& i18n = nxui::I18n::instance();
            if (c.uiLanguageOverride == "auto")
                i18n.setLanguageAuto();
            else
                i18n.setLanguage(c.uiLanguageOverride);
            m_core.audioServices().configureAccessibility(c, i18n.activeLanguageTag());
            saveConfig("cards language");
            reopen(); // every label of the sheet in the new language
        };
        s.rows.push_back(std::move(lang));
#ifdef SWITCHU_MENU
        // HomeNX: clock, sleep timers and auto-brightness live in Console too.
        Row autoTime;
        autoTime.label = i18n.tr("cards.set_auto_time", "Set the time automatically");
        autoTime.detail = i18n.tr("cards.set_auto_time_desc", "From the internet");
        autoTime.toggle = []() {
            bool on = false;
            return R_SUCCEEDED(setsysIsUserSystemClockAutomaticCorrectionEnabled(&on)) && on;
        };
        autoTime.onChange = [this](int) {
            bool on = false;
            setsysIsUserSystemClockAutomaticCorrectionEnabled(&on);
            const Result rc = switchu::menu::smi_cmd::setInternetTimeSync(!on);
            DebugLog::log("[settings] auto time -> %d rc=0x%X", on ? 0 : 1, rc);
            if (R_FAILED(rc))
                showToast(nxui::I18n::instance().tr("settings.system.time_change_failed",
                                                    "The date and time setting could not be changed."));
        };
        s.rows.push_back(std::move(autoTime));
        Row dateTime;
        dateTime.label = i18n.tr("settings.system.manual_time", "Set Date and Time");
        dateTime.detail = i18n.tr("settings.system.manual_time_desc",
                                  "Available when Internet clock synchronization is disabled.");
        dateTime.onA = [this]() { openDateTimeEditor(); };
        s.rows.push_back(std::move(dateTime));
        Row zone;
        zone.label = i18n.tr("settings.system.timezone", "Timezone");
        const std::string zoneName = settings::system_prefs::currentTimezone();
        zone.value = [zoneName]() { return zoneName; };
        zone.onA = [this]() { openTimezonePicker(std::string()); };
        s.rows.push_back(std::move(zone));
        for (const bool docked : {false, true}) {
            Row r;
            r.label = docked ? i18n.tr("cards.set_sleep_tv", "Sleep on TV")
                             : i18n.tr("cards.set_sleep_handheld", "Sleep in handheld mode");
            r.detail = i18n.tr("cards.set_sleep_desc", "After this long without a press");
            r.value = [docked]() {
                auto& t = nxui::I18n::instance();
                static const char* kHand[][2] = {{"settings.sleep.1min", "1 min"}, {"settings.sleep.3min", "3 min"},
                                                 {"settings.sleep.5min", "5 min"}, {"settings.sleep.10min", "10 min"},
                                                 {"settings.sleep.30min", "30 min"}, {"settings.sleep.never", "Never"}};
                static const char* kDock[][2] = {{"settings.sleep.1h", "1 hour"}, {"settings.sleep.2h", "2 hours"},
                                                 {"settings.sleep.3h", "3 hours"}, {"settings.sleep.6h", "6 hours"},
                                                 {"settings.sleep.12h", "12 hours"}, {"settings.sleep.never", "Never"}};
                SetSysSleepSettings st{};
                if (R_FAILED(setsysGetSleepSettings(&st))) return std::string("-");
                const int v = std::clamp(static_cast<int>(docked ? st.console_sleep_plan : st.handheld_sleep_plan), 0, 5);
                const auto& e = docked ? kDock[v] : kHand[v];
                return t.tr(e[0], e[1]);
            };
            r.onChange = [docked](int d) {
                SetSysSleepSettings st{};
                if (R_FAILED(setsysGetSleepSettings(&st))) return;
                auto& plan = docked ? st.console_sleep_plan : st.handheld_sleep_plan;
                plan = static_cast<decltype(st.console_sleep_plan)>((static_cast<int>(plan) + d + 6) % 6);
                const Result rc = setsysSetSleepSettings(&st);
                DebugLog::log("[settings] sleep %s -> %d rc=0x%X", docked ? "tv" : "handheld", static_cast<int>(plan), rc);
            };
            s.rows.push_back(std::move(r));
        }
        Row bright;
        bright.label = i18n.tr("cards.set_auto_brightness", "Automatic brightness");
        bright.detail = i18n.tr("cards.set_auto_brightness_desc", "In handheld mode it follows the room light");
        bright.toggle = []() {
            bool on = false;
            return R_SUCCEEDED(lblIsAutoBrightnessControlEnabled(&on)) && on;
        };
        bright.onChange = [](int) {
            bool on = false;
            lblIsAutoBrightnessControlEnabled(&on);
            const Result rc = on ? lblDisableAutoBrightnessControl() : lblEnableAutoBrightnessControl();
            const Result saveRc = lblSaveCurrentSetting();
            bool now = false;
            lblIsAutoBrightnessControlEnabled(&now);
            DebugLog::log("[settings] auto brightness -> %d rc=0x%X save=0x%X", now ? 1 : 0, rc, saveRc);
        };
        s.rows.push_back(std::move(bright));
#endif
        all["console"] = std::move(s);
    }
    { // About, with the section order (C-UI-14).
        CardsSheet::Section s{i18n.tr("cards.set_about", "About"), {}, "dots", nxui::Color(0.58f, 0.64f, 0.72f)};
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
#ifdef SWITCHU_MENU
        std::string fwText = "-", amsText = "-";
        {
            SetSysFirmwareVersion fw{};
            if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
                fwText = fw.display_version;
            u64 cfg = 0;
            if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(65000), &cfg))) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%u.%u.%u", static_cast<unsigned>((cfg >> 56) & 0xFF),
                              static_cast<unsigned>((cfg >> 48) & 0xFF), static_cast<unsigned>((cfg >> 40) & 0xFF));
                amsText = buf;
            }
        }
        Row fwRow;
        fwRow.label = i18n.tr("cards.set_system_version", "System version");
        fwRow.value = [fwText]() { return fwText; };
        fwRow.enabled = false;
        s.rows.push_back(std::move(fwRow));
        Row amsRow;
        amsRow.label = "Atmosphere";
        amsRow.value = [amsText]() { return amsText; };
        amsRow.enabled = false;
        s.rows.push_back(std::move(amsRow));
#endif
        Row freeSd;
        freeSd.label = i18n.tr("cards.set_free_sd", "Free on the SD card");
        std::string freeText = "-";
        {
            // Read once per sheet build: the value lambda runs every frame.
            struct statvfs st{};
            if (statvfs("sdmc:/", &st) == 0) {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "%.1f GB",
                              static_cast<double>(st.f_bavail) * static_cast<double>(st.f_frsize) / 1e9);
                freeText = buf;
            }
        }
        freeSd.value = [freeText]() { return freeText; };
        freeSd.enabled = false;
        s.rows.push_back(std::move(freeSd));
        Row order;
        order.label = i18n.tr("cards.set_tab_order", "Settings tab order");
        order.detail = i18n.tr("cards.set_tab_order_desc", "X / Y move the section you are on");
        order.enabled = false;
        s.rows.push_back(std::move(order));
        Row perf;
        perf.label = i18n.tr("cards.set_perf_log", "Performance log");
        perf.detail = i18n.tr("cards.set_perf_log_desc", "Start-up times and slow frames in config/OmniLaunch/perf.log");
        perf.value = [this]() { return onOff(m_core.config().cards.perfLog); };
        perf.toggle = [this]() { return m_core.config().cards.perfLog; };
        perf.onChange = [this](int) {
            auto& c = m_core.config().cards;
            c.perfLog = !c.perfLog;
            saveConfig("cards perf log");
        };
        s.rows.push_back(std::move(perf));
        all["about"] = std::move(s);
    }

    {
        CardsSheet::Section s{i18n.tr("cards.set_lock", "Lock screen"), {}, "moon", nxui::Color(0.39f, 0.40f, 0.95f)};
        Row enabled;
        enabled.label = i18n.tr("cards.set_lock", "Lock screen");
        enabled.detail = i18n.tr("cards.set_lock_desc", "After sleep. A password always locks.");
        enabled.value = [this]() { return onOff(m_core.config().cards.lockScreen); };
        enabled.toggle = [this]() { return m_core.config().cards.lockScreen; };
        enabled.onChange = [this](int) {
            auto& c = m_core.config().cards;
            c.lockScreen = !c.lockScreen;
            saveConfig("cards lock screen");
        };
        s.rows.push_back(std::move(enabled));
        Row lockMusic;
        lockMusic.label = i18n.tr("cards.set_lock_music", "Music on the lock screen");
        lockMusic.value = [this]() { return onOff(m_core.config().cards.lockMusic); };
        lockMusic.toggle = [this]() { return m_core.config().cards.lockMusic; };
        lockMusic.onChange = [this](int) {
            auto& c = m_core.config().cards;
            c.lockMusic = !c.lockMusic;
            saveConfig("cards lock music");
        };
        s.rows.push_back(std::move(lockMusic));
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
        CardsSheet::Section s{i18n.tr("cards.tile_internet", "Internet"), {}, "wifi", nxui::Color(0.08f, 0.72f, 0.65f)};
#ifdef SWITCHU_MENU
        std::string ssid, ipText;
        if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
            u32 ip = 0;
            if (R_SUCCEEDED(nifmGetCurrentIpAddress(&ip)) && ip != 0) {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF,
                              (ip >> 24) & 0xFF);
                ipText = buf;
            }
            NifmNetworkProfileData prof{};
            if (R_SUCCEEDED(nifmGetCurrentNetworkProfile(&prof))) {
                char s32[33]{};
                std::memcpy(s32, prof.wireless_setting_data.ssid, 32);
                ssid = s32;
            }
            nifmExit();
        }
        Row network;
        network.label = i18n.tr("cards.net_network", "Network");
        network.value = [ssid]() {
            return ssid.empty() ? nxui::I18n::instance().tr("cards.net_none", "None") : ssid;
        };
        network.detail = i18n.tr("cards.net_network_desc", "Change it in the console network settings");
        network.onA = [this]() { m_sheet.close(); openSystemTile(pages::systemId(pages::SystemTile::Internet)); };
        s.rows.push_back(std::move(network));
        Row ipRow;
        ipRow.label = i18n.tr("cards.net_ip", "IP address");
        ipRow.value = [ipText]() {
            return ipText.empty() ? nxui::I18n::instance().tr("cards.net_none", "None") : ipText;
        };
        ipRow.enabled = false;
        s.rows.push_back(std::move(ipRow));
#endif
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
        CardsSheet::Section s{"Bluetooth", {}, "bluetooth", nxui::Color(0.05f, 0.65f, 0.91f)};
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
#ifdef SWITCHU_MENU
        Row grip;
        grip.label = i18n.tr("cards.bt_grip", "Change grip/order");
        grip.onA = [this]() { m_sheet.close(); m_core.launcher().launchControllerPairing(); };
        s.rows.push_back(std::move(grip));
        Row remap;
        remap.label = i18n.tr("cards.bt_remap", "Button mapping");
        remap.onA = [this]() { m_sheet.close(); m_core.launcher().launchControllerRemapping(); };
        s.rows.push_back(std::move(remap));
        Row btAudio;
        btAudio.label = i18n.tr("cards.bt_audio", "Audio devices");
        btAudio.detail = i18n.tr("cards.bt_audio_desc", "Pair and connect Bluetooth headphones and speakers");
        btAudio.onA = [this]() { openBluetoothAudioSheet(); };
        s.rows.push_back(std::move(btAudio));
#endif
        all["bluetooth"] = std::move(s);
    }
    {
        CardsSheet::Section s{i18n.tr("cards.art_choices", "Images"), {}, "tag", nxui::Color(0.13f, 0.77f, 0.37f)};
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
        CardsSheet::Section s{i18n.tr("cards.set_updates", "Updates"), {}, "arrows", nxui::Color(0.98f, 0.45f, 0.09f)};
        s.rows.push_back(selfUpdateRow());
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
    openSettingsAt(0, 0);
}

// ── Welcome tour (E9 safe part, C-UI-1) ─────────────────────────────────────
void CardsHomeActivity::maybeShowTutorial() {
    if (m_core.config().cards.tutorialDone || m_tutorialCard > 0 || m_tutorialBlocked)
        return;
    m_tutorialCard = 1;
    m_tutorialIn = 0.f;
    m_popupBackdropValid = false;
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
        {"cards.tut3_title", "Pages", "cards.tut3_body", "ZL and ZR change page: Games, Homebrew, Nintendo and Media."},
        {"cards.tut4_title", "Quick settings", "cards.tut4_body", "Click the left stick for quick settings: volume and power."},
    };
    const Card& c = kCards[std::clamp(m_tutorialCard, 1, 4) - 1];
    const float a = m_tutorialIn;
    // Same frosted glass as the power menu and the sheets (blurred screen copy
    // under a translucent tint), not an opaque coloured box.
    const nxui::Rect p{290.f, 190.f, 700.f, 320.f};
    drawGlassCard(ren, p, 30.f, a);
    m_chrome.textMid(ren, m_chrome.titleFont(), i18n.tr(c.tk, c.tf), p.x + 40.f, p.y + 60.f,
                     CardsChrome::kWhite.withAlpha(a));
    // The body wraps (it was cut to one line with "..."); four lines fit
    // between the title and the page dots.
    const auto body = m_chrome.wrap(m_chrome.labelFont(), i18n.tr(c.bk, c.bf), p.width - 80.f, 4);
    for (std::size_t l = 0; l < body.size(); ++l)
        m_chrome.textMid(ren, m_chrome.labelFont(), body[l], p.x + 40.f,
                         p.y + 120.f + 32.f * static_cast<float>(l), {0.85f, 0.87f, 0.93f, a});
    for (int i = 1; i <= 4; ++i)
        ren.drawCircle({p.x + p.width * 0.5f - 30.f + 20.f * static_cast<float>(i - 1), p.bottom() - 70.f},
                       i == m_tutorialCard ? 5.f : 4.f, {1.f, 1.f, 1.f, (i == m_tutorialCard ? 0.95f : 0.35f) * a},
                       12);
    // Button glyph capsules, like every other OmniLaunch hint bar.
    const std::vector<ui::glyph::ActionHint> hints{
        {ui::glyph::button(nxui::Button::A), i18n.tr("cards.tut_next", "Next")},
        {ui::glyph::button(nxui::Button::B), i18n.tr("cards.tut_skip", "Skip")},
    };
    float hintsW = 0.f;
    for (const auto& h : hints)
        hintsW += ui::glyph::capsuleWidth(m_chrome.iconFont(), m_chrome.smallFont(), h.icon, h.label) + 10.f;
    ui::glyph::drawHintBar(ren, m_chrome.iconFont(), m_chrome.smallFont(), hints,
                           p.x + (p.width + hintsW - 10.f) * 0.5f, p.bottom() - 22.f, a);
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
    auto id = m_core.config().cards.panelTitleId;
    if (!id) return;
    // The retired Nintendo-page Browser tile is the BrowseNX Media card now.
    if (id == pages::systemId(pages::SystemTile::Browser))
        id = pages::homebrewId(home::kBrowserNro);
    const bool builtInMedia = id == pages::homebrewId(home::kBrowserNro) || id == pages::homebrewId(pages::kTikTokNro);
    const int page = (builtInMedia || m_core.config().cards.isMedia(id)) ? AppConfig::CardsPrefs::kMedia
                                                                         : AppConfig::CardsPrefs::kGames;
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
