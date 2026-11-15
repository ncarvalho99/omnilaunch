// The icon store half of the Theme Shop: the Icons tab's catalogue, the packs
// on the SD card, and installing, applying and removing them.
//
// The tab draws through the same catalogue grid as the Themes tab: its entries
// are ThemeCatalogClient::Entry values placed in m_communityEntries while the
// tab is open (see applySearchFilter). The network and SD card work runs on
// the pool and reports through m_iconShared; pollIconStore() picks the results
// up on the UI thread.
#include "ThemeShopScreen.hpp"
#include "IconPackStore.hpp"

#include "core/DebugLog.hpp"

#include <nxui/core/I18n.hpp>
#include <nxui/core/ThreadPool.hpp>

#include <algorithm>

namespace {
std::string formatSize(std::uint64_t bytes) {
    if (bytes == 0)
        return {};
    char buf[32];
    if (bytes >= 1024ull * 1024ull)
        std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
    else
        std::snprintf(buf, sizeof(buf), "%llu KB", (unsigned long long)((bytes + 1023) / 1024));
    return buf;
}
} // namespace

void ThemeShopScreen::refreshIconCatalog() {
    if (!m_threadPool)
        return;
    {
        std::lock_guard<std::mutex> lk(m_iconShared->mutex);
        if (m_iconCatalogState.isRunning())
            return;
        m_iconShared->catalogDone = false;
    }
    m_iconCatalogRequested = true;
    m_iconCatalogState.begin(nxui::I18n::instance().tr("iconstore.loading", "Loading icon packs..."));
    auto shared = m_iconShared;
    m_threadPool->submit([shared]() {
        auto catalog = iconstore::fetchCatalog();
        std::lock_guard<std::mutex> lk(shared->mutex);
        shared->catalogOk = catalog.ok;
        shared->catalogError = catalog.error;
        if (catalog.ok)
            shared->catalog = std::move(catalog.entries);
        shared->catalogDone = true;
    });
    rescanInstalledIconPacks();
}

void ThemeShopScreen::rescanInstalledIconPacks() {
    if (!m_threadPool)
        return;
    m_iconScanRequested = true;
    auto shared = m_iconShared;
    m_threadPool->submit([shared]() {
        auto packs = iconpack::scanInstalled();
        std::lock_guard<std::mutex> lk(shared->mutex);
        shared->installed = std::move(packs);
        shared->scanDone = true;
    });
}

const iconpack::Installed* ThemeShopScreen::installedIconPack(const std::string& id) const {
    for (const auto& pack : m_installedIconPacks)
        if (pack.id == id)
            return &pack;
    return nullptr;
}

bool ThemeShopScreen::iconPackActive(const ThemeCatalogClient::Entry& entry) const {
    const std::string active = iconpack::activeId();
    return entry.builtin ? active.empty() : active == entry.id;
}

// The tab's list: the built-in set first, then the catalogue, then packs that
// are on the card but no longer (or not yet) in the catalogue, so they can
// still be applied or removed.
void ThemeShopScreen::rebuildIconEntries() {
    auto& i18n = nxui::I18n::instance();
    m_allIconEntries.clear();
    bool hasBuiltin = false;
    for (const auto& e : m_iconCatalog)
        hasBuiltin = hasBuiltin || e.builtin;
    if (!hasBuiltin) {
        ThemeCatalogClient::Entry def;
        def.id = "default";
        def.name = i18n.tr("iconstore.default_name", "OmniLaunch (built-in)");
        def.author = "OmniLaunch";
        def.builtin = true;
        m_allIconEntries.push_back(std::move(def));
    }
    for (const auto& e : m_iconCatalog) {
        m_allIconEntries.push_back(e);
        if (e.builtin) // named in the player's language, not the catalogue's
            m_allIconEntries.back().name = i18n.tr("iconstore.default_name", "OmniLaunch (built-in)");
    }
    for (const auto& pack : m_installedIconPacks) {
        const bool listed = std::any_of(m_allIconEntries.begin(), m_allIconEntries.end(),
                                        [&](const ThemeCatalogClient::Entry& e) { return e.id == pack.id; });
        if (listed)
            continue;
        ThemeCatalogClient::Entry local;
        local.id = pack.id;
        local.name = pack.name;
        local.author = pack.author;
        local.version = pack.version;
        local.installedBytes = pack.bytes;
        local.files = pack.files;
        // No catalogue entry, so no preview URL: the card shows its placeholder.
        m_allIconEntries.push_back(std::move(local));
    }
    if (isIconTab())
        applySearchFilter();
}

void ThemeShopScreen::pollIconStore() {
    // The catalogue is fetched the first time the Icons tab opens.
    if (!m_iconCatalogRequested && m_threadPool && isIconTab())
        refreshIconCatalog();

    bool changed = false;
    bool opFinished = false, opOk = false, opApply = false, opRemove = false;
    std::string opId;
    {
        std::lock_guard<std::mutex> lk(m_iconShared->mutex);
        auto& s = *m_iconShared;
        if (s.catalogDone) {
            s.catalogDone = false;
            if (s.catalogOk) {
                m_iconCatalog = s.catalog;
                m_iconCatalogState.succeed();
            } else {
                m_iconCatalogState.fail(nxui::I18n::instance().tr(
                    "themeshop.community.offline", "No connection. Check the internet and try again."));
            }
            changed = true;
        }
        if (s.scanDone) {
            s.scanDone = false;
            m_installedIconPacks = s.installed;
            changed = true;
        }
        if (s.opRunning) {
            m_iconTransferState.update(s.opLabel, s.opProgress);
        } else if (s.opDone) {
            s.opDone = false;
            opFinished = true;
            opOk = s.opOk;
            opApply = s.opApply;
            opRemove = s.opRemove;
            opId = s.opId;
            if (s.opOk)
                m_iconTransferState.succeed(s.opLabel);
            else
                m_iconTransferState.fail(s.opLabel);
        }
    }

    if (opFinished) {
        if (opOk && opApply && m_iconPackApplyCb)
            m_iconPackApplyCb(opId);
        if (opOk && opRemove && iconpack::activeId() == opId && m_iconPackApplyCb)
            m_iconPackApplyCb(std::string()); // the pack in use went: back to built-in
        requestToast(m_iconTransferState.label(), 2.5f);
        rescanInstalledIconPacks();
    }
    if (changed)
        rebuildIconEntries();
}

void ThemeShopScreen::startIconInstall(const std::string& id, bool apply) {
    if (!m_threadPool)
        return;
    const ThemeCatalogClient::Entry* entry = nullptr;
    for (const auto& e : m_allIconEntries)
        if (e.id == id)
            entry = &e;
    if (!entry || entry->package.empty())
        return;
    auto& i18n = nxui::I18n::instance();
    {
        std::lock_guard<std::mutex> lk(m_iconShared->mutex);
        if (m_iconShared->opRunning) {
            requestToast(i18n.tr("themeshop.community.transfer_busy",
                                 "Another theme transfer is already running."), 2.5f);
            return;
        }
        m_iconShared->opRunning = true;
        m_iconShared->opDone = false;
        m_iconShared->opId = id;
        m_iconShared->opApply = apply;
        m_iconShared->opRemove = false;
        m_iconShared->opLabel = i18n.tr("iconstore.downloading", "Downloading icons");
        m_iconShared->opProgress = 0.f;
        m_iconShared->cancel = false;
    }
    m_iconTransferId = id;
    m_iconTransferState.begin(i18n.tr("iconstore.downloading", "Downloading icons"));
    DebugLog::log("[icon-store] install %s apply=%d", id.c_str(), apply ? 1 : 0);
    auto shared = m_iconShared;
    ThemeCatalogClient::Entry copy = *entry;
    m_threadPool->submit([shared, copy]() {
        auto result = iconstore::install(copy, [&](const std::string& label, float p) {
            std::lock_guard<std::mutex> lk(shared->mutex);
            shared->opLabel = label;
            shared->opProgress = p;
        }, &shared->cancel);
        std::lock_guard<std::mutex> lk(shared->mutex);
        shared->opOk = result.ok;
        shared->opLabel = result.message;
        shared->opRunning = false;
        shared->opDone = true;
    });
}

void ThemeShopScreen::startIconRemove(const std::string& id) {
    if (!m_threadPool || !iconpack::safeId(id))
        return;
    auto& i18n = nxui::I18n::instance();
    {
        std::lock_guard<std::mutex> lk(m_iconShared->mutex);
        if (m_iconShared->opRunning)
            return;
        m_iconShared->opRunning = true;
        m_iconShared->opDone = false;
        m_iconShared->opId = id;
        m_iconShared->opApply = false;
        m_iconShared->opRemove = true;
        m_iconShared->opLabel = i18n.tr("iconstore.removing", "Removing icons");
        m_iconShared->opProgress = -1.f;
    }
    // Drop the pack before its files go, so nothing loads from a folder being
    // deleted; the app reloads every icon from the built-in set.
    if (iconpack::activeId() == id && m_iconPackApplyCb)
        m_iconPackApplyCb(std::string());
    m_iconTransferId = id;
    m_iconTransferState.begin(i18n.tr("iconstore.removing", "Removing icons"));
    auto shared = m_iconShared;
    m_threadPool->submit([shared, id]() {
        std::string error;
        const bool ok = iconstore::remove(id, error);
        auto& tr = nxui::I18n::instance();
        std::lock_guard<std::mutex> lk(shared->mutex);
        shared->opOk = ok;
        shared->opLabel = ok ? tr.tr("iconstore.removed", "Icon pack removed.")
                             : tr.tr("iconstore.remove_failed", "The icon pack could not be removed.");
        shared->opRunning = false;
        shared->opDone = true;
    });
}

// "3 packs - 4.1 MB to download - 2 installed - 2.6 MB on the SD card".
std::string ThemeShopScreen::iconCatalogueTotals() const {
    auto& i18n = nxui::I18n::instance();
    int packs = 0;
    std::uint64_t download = 0;
    for (const auto& e : m_allIconEntries) {
        if (e.builtin)
            continue;
        ++packs;
        download += e.packageBytes;
    }
    std::uint64_t onCard = 0;
    for (const auto& p : m_installedIconPacks)
        onCard += p.bytes;
    if (packs == 0)
        return {};
    std::string line = std::to_string(packs) + " "
        + (packs == 1 ? i18n.tr("iconstore.pack_one", "pack") : i18n.tr("iconstore.pack_many", "packs"));
    if (download > 0)
        line += " - " + formatSize(download) + " " + i18n.tr("themeshop.catalog.to_download", "to download");
    if (!m_installedIconPacks.empty()) {
        line += " - " + std::to_string(m_installedIconPacks.size()) + " "
            + i18n.tr("iconstore.installed_count", "installed");
        if (onCard > 0)
            line += " (" + formatSize(onCard) + " " + i18n.tr("themeshop.catalog.on_card", "on the SD card") + ")";
    }
    return line;
}
