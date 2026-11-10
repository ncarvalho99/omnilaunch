// CardsHomeActivity: homebrew updates (HomeNX G5, requirements C-NET-6,
// C-UI-13): the check, the badge on homebrew cards, the "Homebrew updates"
// sheet and the installer. Same class and thread rules as the other
// CardsHomeActivity files; the work itself runs on the HomeCore pool.
// Behaviour follows upstream/homenx @ 0448cd4 projects/cards/src/
// CardsActivity.cpp (checkHomebrewUpdates, updateNro, openHomebrewUpdates).

#include "cards/CardsHomeActivity.hpp"
#include "cards/CardsPages.hpp"
#include "core/DebugLog.hpp"
#include "home/HomeCore.hpp"
#include "homebrew/HomebrewUpdater.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <algorithm>
#include <chrono>

namespace cards {

struct CardsHomeActivity::HbJob {
    enum class Kind { Check, Install } kind = Kind::Check;
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
    hbupdate::CheckProgress check;
    hbupdate::InstallProgress install;
    // Written by the worker before `finished`, read on the main thread after.
    hbupdate::State state;
    hbupdate::InstallResult result;
    std::vector<HbPending> pending; ///< what the sheet lists, from the SD at the end
};

namespace {
// The installed apps that still have an offer (worker: reads .nro headers).
std::vector<CardsHomeActivity::HbPending> pendingFor(const hbupdate::State& state) {
    std::vector<CardsHomeActivity::HbPending> out;
    for (const hbupdate::Installed& app : hbupdate::installedApps())
        if (const hbupdate::Offer* o = hbupdate::pendingOffer(state, app))
            out.push_back({app, *o});
    return out;
}
} // namespace

namespace {
std::string replaceAll(std::string s, const std::string& tag, const std::string& value) {
    for (std::size_t p = s.find(tag); p != std::string::npos; p = s.find(tag, p + value.size()))
        s.replace(p, tag.size(), value);
    return s;
}
} // namespace

bool CardsHomeActivity::homebrewHasUpdate(const std::string& path, const std::string& version) const {
    if (!m_hbState || path.empty() || m_hbJob)
        return false;
    return hbupdate::pendingOffer(*m_hbState, hbupdate::Installed{path, std::string(), version}) != nullptr;
}

int CardsHomeActivity::homebrewUpdateCount() const {
    return static_cast<int>(m_hbPending.size());
}

void CardsHomeActivity::checkHomebrewUpdates(bool force, bool openSheet) {
    // Back from a game the saved list is enough; the sources are asked on a
    // console start, a frontend switch or "Check again".
    const bool mayFetch = force || openSheet || !m_hbReturnActivation;
    auto& tr = nxui::I18n::instance();
    if (m_hbFuture.valid()) {
        if (openSheet)
            showToast(tr.tr("cards.hb_busy", "Homebrew updates are still working..."));
        return;
    }
    if (!m_core.threadPool().acceptingTasks())
        return;
    auto job = std::make_shared<HbJob>();
    job->kind = HbJob::Kind::Check;
    m_hbJob = job;
    m_hbSheetWanted = openSheet;
    net::NetService* service = &m_core.net();
    try {
        m_hbFuture = m_core.threadPool().submit([job, service, force, mayFetch]() {
            try {
                job->state = hbupdate::check(*service, force, job->check, job->stop, mayFetch);
                if (!job->stop.load())
                    job->pending = pendingFor(job->state);
            } catch (const std::exception& e) {
                DebugLog::log("[hbupdate] check failed: %s", e.what());
            } catch (...) {
                DebugLog::log("[hbupdate] check failed");
            }
            job->finished.store(true, std::memory_order_release);
        });
    } catch (...) {
        m_hbJob.reset();
        return;
    }
    if (openSheet)
        showToast(force ? tr.tr("cards.hb_checking", "Checking for homebrew updates...")
                        : tr.tr("cards.hb_loading", "Loading homebrew updates..."));
}

void CardsHomeActivity::installHomebrewUpdates(std::vector<std::string> paths) {
    auto& tr = nxui::I18n::instance();
    if (paths.empty() || !m_hbState)
        return;
    if (m_hbFuture.valid()) {
        showToast(tr.tr("cards.hb_busy", "Homebrew updates are still working..."));
        return;
    }
    if (!m_core.threadPool().acceptingTasks())
        return;
    auto job = std::make_shared<HbJob>();
    job->kind = HbJob::Kind::Install;
    job->state = *m_hbState;
    job->install.count = static_cast<int>(paths.size());
    m_hbJob = job;
    m_hbSheetWanted = true;
    net::NetService* service = &m_core.net();
    std::string skip;
#ifdef SWITCHU_MENU
    skip = m_core.launcher().runningHomebrew(); // never replace the .nro that is open
#endif
    try {
        m_hbFuture = m_core.threadPool().submit([job, service, paths, skip]() {
            try {
                job->result = hbupdate::install(*service, job->state, paths, skip, job->install, job->stop);
                if (!job->stop.load())
                    job->pending = pendingFor(job->state);
            } catch (const std::exception& e) {
                job->result.failed.push_back(e.what());
            } catch (...) {
                job->result.failed.push_back("error");
            }
            job->finished.store(true, std::memory_order_release);
        });
    } catch (...) {
        m_hbJob.reset();
        return;
    }
    openHomebrewUpdates(); // shows the progress row
}

void CardsHomeActivity::pollHomebrewUpdates() {
    // First visit of a run: load the saved list (and ask the sources when it
    // is old), so the badges and the toast appear without opening anything.
    if (!m_hbCheckedThisRun && !m_hbFuture.valid() && !m_catalog.loading() && m_time > 6.f) {
        m_hbCheckedThisRun = true;
        checkHomebrewUpdates(false, false);
    }
    if (!m_hbFuture.valid() || m_hbFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try { m_hbFuture.get(); } catch (...) {}
    if (!m_hbJob || !m_hbJob->finished.load(std::memory_order_acquire)) {
        m_hbJob.reset(); // the worker never ran its body (pool stopped): allow a retry
        return;
    }
    std::shared_ptr<HbJob> job = std::move(m_hbJob);
    auto& tr = nxui::I18n::instance();
    const bool sheetWasOurs = m_sheet.isOpen() && m_sheet.title() == tr.tr("cards.hb_title", "Homebrew updates");
    if (job->stop.load())
        return; // cancelled (frontend leaving): nothing to show
    const int before = static_cast<int>(m_hbPending.size());
    m_hbState = std::make_shared<const hbupdate::State>(std::move(job->state));
    m_hbPending = std::move(job->pending);

    if (job->kind == HbJob::Kind::Install) {
        const auto& r = job->result;
        std::string text;
        if (!r.updated.empty())
            text = replaceAll(tr.tr("cards.hb_updated", "{n} homebrew updated"), "{n}", std::to_string(r.updated.size()));
        if (!r.failed.empty())
            text += (text.empty() ? "" : ". ") + tr.tr("cards.hb_failed", "Update failed: ") + r.failed.front();
        if (!text.empty())
            showToast(text);
        if (!r.updated.empty() && m_page == AppConfig::CardsPrefs::kHomebrew)
            m_catalog.rebuild(m_core.config()); // new versions on the cards
    } else if (!m_hbSheetWanted && !m_hbPending.empty() && static_cast<int>(m_hbPending.size()) != before) {
        // HomeNX's toast: once per change, not on every visit.
        showToast(replaceAll(tr.tr("cards.hb_available", "{n} homebrew apps have an update (Menu > Updates)."),
                             "{n}", std::to_string(m_hbPending.size())));
    }
    if (m_hbSheetWanted && (sheetWasOurs || !m_sheet.isOpen()))
        openHomebrewUpdates();
    m_hbSheetWanted = false;
}

void CardsHomeActivity::stopHomebrewUpdates() {
    if (m_hbJob)
        m_hbJob->stop = true;
    if (m_hbFuture.valid()) {
        m_hbFuture.wait();
        try { m_hbFuture.get(); } catch (...) {}
    }
    m_hbJob.reset();
}

void CardsHomeActivity::openHomebrewUpdates() {
    auto& tr = nxui::I18n::instance();
    if (!m_hbState && !m_hbJob) {
        checkHomebrewUpdates(false, true); // first open: load (or fetch) and come back here
        if (!m_hbJob)
            return;
    }
    using Row = CardsSheet::Row;
    std::vector<Row> rows;
    if (m_hbJob && m_hbJob->kind == HbJob::Kind::Install) {
        auto job = m_hbJob;
        Row r;
        r.label = tr.tr("cards.hb_installing", "Updating...");
        r.enabled = false;
        r.value = [job]() {
            return std::to_string(std::min(job->install.index.load() + 1, job->install.count.load())) + "/" +
                   std::to_string(job->install.count.load());
        };
        r.progress = [job]() {
            const int n = std::max(1, job->install.count.load());
            return (static_cast<float>(job->install.index.load()) + job->install.fraction.load()) / static_cast<float>(n);
        };
        rows.push_back(std::move(r));
    } else if (m_hbJob) {
        auto job = m_hbJob;
        Row r;
        r.label = tr.tr("cards.hb_checking", "Checking for homebrew updates...");
        r.enabled = false;
        r.value = [job]() {
            return std::to_string(job->check.done.load()) + "/" + std::to_string(job->check.total.load());
        };
        rows.push_back(std::move(r));
    }

    std::vector<std::string> paths;
    if (m_hbState && !m_hbJob) {
        for (const HbPending& p : m_hbPending) {
            const hbupdate::Installed& app = p.app;
            const hbupdate::Offer* o = &p.offer;
            paths.push_back(app.path);
            Row r;
            r.label = app.name.empty() ? app.path : app.name;
            const std::string from = app.version.empty() ? "?" : app.version;
            const std::string to = o->latest;
            r.value = [from, to]() { return from + "  >  " + to; };
            r.detail = o->source;
            const std::string path = app.path;
            r.onA = [this, path]() { installHomebrewUpdates({path}); };
            rows.push_back(std::move(r));
        }
        if (paths.empty()) {
            Row r;
            r.label = tr.tr("cards.hb_none", "Everything is up to date");
            r.enabled = false;
            rows.push_back(std::move(r));
        } else {
            Row all;
            all.label = replaceAll(tr.tr("cards.hb_update_all", "Update all ({n})"), "{n}", std::to_string(paths.size()));
            all.onA = [this, paths]() { installHomebrewUpdates(paths); };
            rows.insert(rows.begin(), std::move(all));
        }
    }
    if (!m_hbJob) {
        Row again;
        again.label = tr.tr("cards.hb_check_again", "Check again");
        again.detail = m_hbState && m_hbState->checkedAt > 0
                           ? tr.tr("cards.hb_sources", "Homebrew App Store and GitHub releases")
                           : tr.tr("cards.hb_never", "Not checked yet");
        again.onA = [this]() {
            if (!m_core.net().online()) {
                showToast(nxui::I18n::instance().tr("cards.hb_offline", "Connect to the internet to check."));
                return;
            }
            checkHomebrewUpdates(true, true);
            openHomebrewUpdates();
        };
        rows.push_back(std::move(again));
    }
    m_sheet.open(tr.tr("cards.hb_title", "Homebrew updates"), {{std::string(), std::move(rows)}});
}

} // namespace cards
