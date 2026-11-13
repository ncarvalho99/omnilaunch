// CardsHomeActivity: OmniLaunch self-update. Same release feed, version rule,
// SHA-256 check and daemon staging as Classic (update/UpdateClient and
// update/UpdateStager); only the presentation is the Cards one. Same class and
// thread rules as the other CardsHomeActivity files.

#include "cards/CardsHomeActivity.hpp"
#include "core/DebugLog.hpp"
#include "home/HomeCore.hpp"
#include "update/UpdateStager.hpp"
#include "widgets/OverlayDialog.hpp"
#include "widgets/ProgressDialog.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <chrono>
#include <mutex>

#ifndef SWITCHU_VERSION
#define SWITCHU_VERSION "unknown"
#endif

namespace cards {

struct CardsHomeActivity::SelfUpdateJob {
    std::mutex mutex;
    std::uint64_t received = 0;
    std::uint64_t total = 0;
    bool applying = false;
    bool done = false;
    bool ok = false;
    std::string error;
};

namespace {
int todayNumber() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<int>(std::chrono::duration_cast<std::chrono::hours>(now).count() / 24);
}
} // namespace

void CardsHomeActivity::checkSelfUpdate(bool forced) {
    auto& i18n = nxui::I18n::instance();
    if (m_selfUpdateJob)
        return; // a download is running
    if (update::restartPending()) {
        m_selfUpdateAvailable = {};
        m_selfUpdateStatus = i18n.tr("themeshop.update_restart_pending", "Downloaded. Restart to apply it.");
        if (forced)
            showToast(m_selfUpdateStatus);
        return;
    }
    if (!forced) {
        // Once a day, shared with Classic: the menu restarts every time a game
        // closes, and the unauthenticated GitHub limit is small.
        auto& config = m_core.config();
        const int today = todayNumber();
        if (config.lastUpdateCheckDay == today)
            return;
        config.lastUpdateCheckDay = today;
        saveConfig("update check day");
    }
    if (!m_core.threadPool().acceptingTasks())
        return;
    m_selfUpdateForced = forced;
    m_selfUpdateStatus = i18n.tr("settings.about.check_updates_running", "Checking for updates...");
    if (forced)
        showToast(m_selfUpdateStatus);
    m_selfUpdate.check(m_core.threadPool(), SWITCHU_VERSION);
    DebugLog::log("[cards-update] check started (current %s, forced=%d)", SWITCHU_VERSION, forced ? 1 : 0);
}

void CardsHomeActivity::pollSelfUpdate() {
    // First automatic check of this activation, once the HOME screen is settled.
    if (!m_selfUpdateChecked && !m_catalog.loading() && m_time > 8.f && !m_launching && !m_leaving) {
        m_selfUpdateChecked = true;
        checkSelfUpdate(false);
    }

    auto& i18n = nxui::I18n::instance();
    const auto snap = m_selfUpdate.snapshot();
    if (snap.revision != m_selfUpdateSeen && snap.phase != update::UpdateClient::Phase::Checking &&
        snap.phase != update::UpdateClient::Phase::Idle) {
        m_selfUpdateSeen = snap.revision;
        if (snap.phase == update::UpdateClient::Phase::Failed) {
            DebugLog::log("[cards-update] check failed: %s", snap.error.c_str());
            m_selfUpdateStatus = i18n.tr("dialog.update_check_failed", "Could not reach the update server.");
            if (m_selfUpdateForced)
                showToast(m_selfUpdateStatus);
        } else if (snap.phase != update::UpdateClient::Phase::Available) {
            m_selfUpdateAvailable = {};
            m_selfUpdateStatus = i18n.tr("dialog.update_up_to_date", "OmniLaunch is up to date.");
            if (m_selfUpdateForced)
                showToast(m_selfUpdateStatus);
        } else {
            DebugLog::log("[cards-update] %s available (%llu bytes)", snap.release.version.c_str(),
                          static_cast<unsigned long long>(snap.release.sizeBytes));
            m_selfUpdateAvailable = snap.release;
            m_selfUpdateStatus = i18n.tr("dialog.update_available_short", "Update available") + " (" +
                                 snap.release.version + ")";
            if (m_selfUpdateForced) {
                m_selfUpdateOffered = true;
                offerSelfUpdate(snap.release);
            }
        }
        m_selfUpdateForced = false;
    }

    // The automatic offer waits for a quiet screen instead of covering a sheet.
    if (!m_selfUpdateOffered && !m_selfUpdateAvailable.version.empty() && !m_selfUpdateJob &&
        !overlayActive() && !m_sheet.isOpen() && !m_musicSheet.isOpen() && !m_newsSheet.isOpen() &&
        !m_dotsOpen && !m_powerOpen && !m_cardOptOpen && !m_moving && m_tutorialCard == 0 &&
        !m_launching && !m_leaving && !m_lock.isLocked()) {
        m_selfUpdateOffered = true;
        offerSelfUpdate(m_selfUpdateAvailable);
    }

    // Download progress and outcome.
    auto job = m_selfUpdateJob;
    if (!job)
        return;
    std::uint64_t received = 0, total = 0;
    bool applying = false, done = false, ok = false;
    std::string error;
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        received = job->received;
        total = job->total;
        applying = job->applying;
        done = job->done;
        ok = job->ok;
        error = job->error;
    }
    if (!done) {
        if (m_selfUpdateProgress && m_selfUpdateProgress->isActive()) {
            if (applying)
                m_selfUpdateProgress->updateState(i18n.tr("dialog.update_applying", "Applying update..."), -1.f);
            else if (total > 0)
                m_selfUpdateProgress->updateState(i18n.tr("dialog.update_downloading", "Downloading update..."),
                                                  static_cast<float>(received) / static_cast<float>(total));
        }
        return;
    }
    m_selfUpdateJob.reset();
    if (m_selfUpdateFuture.valid()) {
        try { m_selfUpdateFuture.get(); } catch (...) {}
    }
    if (m_selfUpdateProgress)
        m_selfUpdateProgress->hide();
    DebugLog::log("[cards-update] %s%s", ok ? "staged for the next boot" : "failed: ", ok ? "" : error.c_str());
    if (ok) {
        m_selfUpdateAvailable = {};
        m_selfUpdateStatus = i18n.tr("themeshop.update_restart_pending", "Downloaded. Restart to apply it.");
    } else {
        m_selfUpdateStatus = i18n.tr("dialog.update_failed", "The update could not be installed.");
    }
    if (!m_userSelect)
        return;
    m_audio.playSfx(Sfx::ModalShow);
    if (ok) {
        m_userSelect->show(i18n.tr("dialog.update_title", "Update available"),
                           i18n.tr("dialog.update_done",
                                   "Update downloaded. Restart the console to apply it. The first boot may take "
                                   "around half a minute."),
                           {{i18n.tr("dialog.update_later", "Later"), []() {}, true},
                            {i18n.tr("themeshop.update_restart", "Restart now"),
                             [this]() {
#ifdef SWITCHU_MENU
                                 m_core.launcher().reboot();
#endif
                             },
                             true}},
                           1, {});
    } else {
        m_userSelect->show(i18n.tr("dialog.update_title", "Update available"), m_selfUpdateStatus,
                           {{i18n.tr("button.ok", "OK"), []() {}, true}}, 0, {});
    }
    focusManager().setFocus(m_userSelect.get());
}

void CardsHomeActivity::offerSelfUpdate(const update::UpdateClient::Release& release) {
    if (!m_userSelect || update::restartPending())
        return;
    auto& i18n = nxui::I18n::instance();
    const std::string notes = update::UpdateClient::condenseNotes(release.notes, i18n.activeLanguageTag());
    std::string body = i18n.tr("dialog.update_body", "A new version of OmniLaunch is available.");
    body += "\n\n" + i18n.tr("dialog.update_installed", "Installed") + ": " + SWITCHU_VERSION;
    body += "\n" + i18n.tr("dialog.update_new", "New") + ": " + release.version;
    if (!notes.empty())
        body += "\n\n" + notes;
    m_audio.playSfx(Sfx::ModalShow);
    m_userSelect->show(i18n.tr("dialog.update_title", "Update available"), body,
                       {{i18n.tr("dialog.update_later", "Later"), []() {}, true},
                        {i18n.tr("dialog.update_install", "Update"),
                         [this, release]() { startSelfUpdateDownload(release); }, true}},
                       0, {});
    focusManager().setFocus(m_userSelect.get());
}

void CardsHomeActivity::startSelfUpdateDownload(const update::UpdateClient::Release& release) {
    if (m_selfUpdateJob || release.downloadUrl.empty() || !m_core.threadPool().acceptingTasks())
        return;
    auto& i18n = nxui::I18n::instance();
    if (!m_core.net().online()) {
        showToast(i18n.tr("cards.net_offline", "No internet connection"));
        return;
    }
    auto job = std::make_shared<SelfUpdateJob>();
    job->total = release.sizeBytes;
    if (!m_selfUpdateProgress) {
        m_selfUpdateProgress = std::make_shared<ProgressDialog>();
        m_selfUpdateProgress->setFont(&m_font);
        m_selfUpdateProgress->setSmallFont(&m_fontSmall);
        m_selfUpdateProgress->setTheme(&m_overlayTheme);
        m_selfUpdateProgress->setRect({0, 0, 1280, 720});
    }
    m_sheet.close();
    m_selfUpdateProgress->show(i18n.tr("dialog.update_title", "Update available"),
                               i18n.tr("dialog.update_downloading", "Downloading update..."), -1.f);
    // The worker owns only `job` and a copy of the release: it never touches
    // this activity, so a frontend switch cannot leave it dangling.
    try {
        m_selfUpdateFuture = m_core.threadPool().submit([job, release]() {
            try {
                update::stageRelease(
                    release,
                    [job](std::uint64_t received, std::uint64_t total) {
                        std::lock_guard<std::mutex> lock(job->mutex);
                        job->received = received;
                        if (total) job->total = total;
                    },
                    [job]() {
                        std::lock_guard<std::mutex> lock(job->mutex);
                        job->applying = true;
                    });
                std::lock_guard<std::mutex> lock(job->mutex);
                job->ok = true;
                job->done = true;
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->error = e.what();
                job->done = true;
            } catch (...) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->error = "unknown update failure";
                job->done = true;
            }
        });
    } catch (...) {
        m_selfUpdateProgress->hide();
        return;
    }
    m_selfUpdateJob = job;
    DebugLog::log("[cards-update] download %s started", release.version.c_str());
}

// Joins the worker only. The job keeps its outcome, so an activity that
// resumes after a sleep or a failed power action still shows the result (and
// releases its progress dialog) on the next pollSelfUpdate().
void CardsHomeActivity::waitSelfUpdate() {
    if (m_selfUpdateFuture.valid())
        m_selfUpdateFuture.wait();
}

CardsSheet::Row CardsHomeActivity::selfUpdateRow() {
    auto& i18n = nxui::I18n::instance();
    CardsSheet::Row row;
    row.label = std::string("OmniLaunch ") + SWITCHU_VERSION;
    row.value = [this]() -> std::string {
        if (m_selfUpdateJob)
            return nxui::I18n::instance().tr("dialog.update_downloading", "Downloading update...");
        if (update::restartPending())
            return nxui::I18n::instance().tr("themeshop.update_restart", "Restart now");
        if (!m_selfUpdateAvailable.version.empty())
            return m_selfUpdateAvailable.version;
        return std::string();
    };
    row.detail = i18n.tr("settings.about.check_updates", "Check for updates");
    row.onA = [this]() {
        auto& t = nxui::I18n::instance();
        if (m_selfUpdateJob)
            return;
        if (update::restartPending()) {
            if (!m_userSelect)
                return;
            m_audio.playSfx(Sfx::ModalShow);
            m_userSelect->show(t.tr("themeshop.update_restart", "Restart now"),
                               t.tr("themeshop.update_restart_confirm",
                                    "The console restarts and applies the update while it starts. This first boot "
                                    "takes about half a minute longer than usual."),
                               {{t.tr("dialog.update_later", "Later"), []() {}, true},
                                {t.tr("themeshop.update_restart", "Restart now"),
                                 [this]() {
#ifdef SWITCHU_MENU
                                     m_core.launcher().reboot();
#endif
                                 },
                                 true}},
                               1, {});
            focusManager().setFocus(m_userSelect.get());
            return;
        }
        if (!m_selfUpdateAvailable.version.empty()) {
            offerSelfUpdate(m_selfUpdateAvailable);
            return;
        }
        if (!m_core.net().online()) {
            showToast(t.tr("cards.net_offline", "No internet connection"));
            return;
        }
        checkSelfUpdate(true);
    };
    return row;
}

} // namespace cards
