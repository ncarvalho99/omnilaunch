#include "AppletLauncher.hpp"
#include "core/DebugLog.hpp"
#include "homebrew/HomebrewStore.hpp"
#include "usb/Mtp.hpp"
#include <switchu/file_log.hpp>
#include <switchu/sd_commit.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <strings.h>
#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
#include <switchu/smi_protocol.hpp>
#endif
#include <switch.h>

void AppletLauncher::init(Callbacks cbs) {
    m_cb = std::move(cbs);
}

#ifdef SWITCHU_MENU
namespace {
// Which .nro runs inside the borrowed host game. The menu process is replaced
// on every HOME, so the answer lives on the card until no application is
// suspended any more.
constexpr const char* kRunningNro = "sdmc:/config/OmniLaunch/hbl_running.txt";
} // namespace

bool AppletLauncher::isAppRunning() const  { return m_appRunning; }
bool AppletLauncher::isAppSuspended(uint64_t titleId) const {
    return m_suspendedTitleId != 0 && m_suspendedTitleId == titleId;
}
uint64_t AppletLauncher::suspendedTitleId() const { return m_suspendedTitleId; }
std::string AppletLauncher::runningHomebrew() const {
    return m_suspendedTitleId != 0 ? m_runningHomebrew : std::string();
}

void AppletLauncher::forgetRunningHomebrew() {
    m_runningHomebrew.clear();
    std::error_code ec;
    std::filesystem::remove(kRunningNro, ec);
}

void AppletLauncher::setAppRunning(bool v)          { m_appRunning = v; }
void AppletLauncher::setAppHasForeground(bool v)    { m_appHasForeground = v; }
void AppletLauncher::setSuspendedTitleId(uint64_t v) {
    m_suspendedTitleId = v;
    if (v == 0 && !m_runningHomebrew.empty())
        forgetRunningHomebrew();
}

void AppletLauncher::setStartupStatus(uint64_t suspendedTitleId, bool appRunning) {
    m_suspendedTitleId = suspendedTitleId;
    m_appRunning       = appRunning;
    m_appHasForeground = false;
    m_runningHomebrew.clear();
    if (suspendedTitleId == 0) {
        forgetRunningHomebrew();
        if (!appRunning)
            restoreUserHbmenu();
    } else if (FILE* f = std::fopen(kRunningNro, "r")) {
        char buf[512] = {};
        if (std::fgets(buf, sizeof(buf), f)) {
            std::string line(buf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            m_runningHomebrew = std::move(line);
        }
        std::fclose(f);
    }
    DebugLog::log("[launcher] startup status: suspended=0x%016lX running=%d homebrew=%s",
                  suspendedTitleId, appRunning,
                  m_runningHomebrew.empty() ? "-" : m_runningHomebrew.c_str());
}

void AppletLauncher::launchAlbum() {
    DebugLog::log("[launcher] requesting Album launch via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::LaunchAlbum);
    DebugLog::log("[launcher] Album rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

void AppletLauncher::launchMiiEditor() {
    DebugLog::log("[launcher] requesting Mii Editor launch via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::LaunchMiiEditor);
    DebugLog::log("[launcher] Mii Editor rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

void AppletLauncher::launchControllerPairing() {
    DebugLog::log("[launcher] requesting Controller pairing via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::LaunchControllers);
    DebugLog::log("[launcher] Controller pairing rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

void AppletLauncher::launchControllerRemapping() {
    DebugLog::log("[launcher] requesting Controller remapping via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(
        switchu::smi::SystemMessage::LaunchControllerRemapping);
    DebugLog::log("[launcher] controller remapping rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

void AppletLauncher::launchNetConnect() {
    DebugLog::log("[launcher] requesting NetConnect launch via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::LaunchNetConnect);
    DebugLog::log("[launcher] NetConnect rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

void AppletLauncher::launchUserCreator() {
    DebugLog::log("[launcher] requesting user creator via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::LaunchUserCreator);
    DebugLog::log("[launcher] user creator rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

void AppletLauncher::launchUserPage(AccountUid uid) {
    DebugLog::log("[launcher] requesting User Page launch via daemon");
    Result rc = switchu::menu::smi_cmd::launchUserPage(uid);
    DebugLog::log("[launcher] User Page rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

// Wait for our own writes before the daemon is told to cut power. The menu
// does not exit here: asking it to made the daemon see no menu running and
// relaunch it into the middle of the reboot, leaving the console black.
void AppletLauncher::quiesceForPower(const char* what) {
    if (m_cb.quiesceWriters)
        m_cb.quiesceWriters();
    DebugLog::log("[launcher] requesting %s, writers quiesced", what);
}

void AppletLauncher::enterSleep() {
    quiesceForPower("sleep");
    switchu::menu::smi_cmd::enterSleep();
}

void AppletLauncher::shutdown() {
    quiesceForPower("shutdown");
    switchu::menu::smi_cmd::shutdown();
}

void AppletLauncher::reboot() {
    quiesceForPower("reboot");
#ifdef __SWITCH__
    // On Horizon 15.0.0+, AppletType_LibraryApplet can call appletStartRebootSequence()
    // directly via IGlobalStateController (command 4). This triggers the exact same
    // clean AM-mediated reboot sequence that the official Nintendo power menu uses,
    // coordinating PSC peripheral teardown (including SDMMC host controller).
    Result rc = appletStartRebootSequence();
    DebugLog::log("[launcher] appletStartRebootSequence rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        while (true)
            svcSleepThread(100'000'000ULL);
    }
#endif
    switchu::menu::smi_cmd::reboot();
}

Result AppletLauncher::leaveSafeMode() {
    quiesceForPower("leave-safe-mode");
    DebugLog::log("[launcher] asking the daemon to leave safe mode");
    return switchu::menu::smi_cmd::leaveSafeMode();
}

Result AppletLauncher::requestSelfUninstall() {
    quiesceForPower("self-uninstall");
    DebugLog::log("[launcher] requesting staged SwitchU removal");
    return switchu::menu::smi_cmd::requestSelfUninstall();
}

Result AppletLauncher::refreshCatalog() {
    DebugLog::log("[launcher] requesting catalog refresh");
    return switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::RefreshCatalog);
}

Result AppletLauncher::rotateLogs() {
    DebugLog::log("[launcher] rotating logs on request");
    // The menu holds menu.log open for as long as it runs, and the daemon does
    // the same with daemon.log, so neither can be copied over MTP while the
    // console is up. Both are rotated: this process closes and reopens its own,
    // and the daemon is asked to do the same with its.
    switchu::FileLog::flush();
    switchu::FileLog::open("menu");
    const Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::RotateLogs);
    DebugLog::log("[launcher] log rotation rc=0x%X", rc);
    return rc;
}

Result AppletLauncher::rebuildControlCache() {
    DebugLog::log("[launcher] requesting control cache rebuild");
    return switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::RebuildControlCache);
}

Result AppletLauncher::prepareApplication(uint64_t titleId, AccountUid uid,
                                          switchu::smi::LaunchTransitionTrace& trace) {
    const Result rc = switchu::menu::smi_cmd::prepareApplication(titleId, uid, trace);
    if (R_FAILED(rc))
        DebugLog::log("[launcher] preflight enqueue failed tid=%016lX rc=0x%X", titleId, rc);
    return rc;
}

void AppletLauncher::launchApplication(uint64_t titleId, AccountUid uid,
                                       switchu::smi::LaunchTransitionTrace trace) {
    DebugLog::log("[launcher] tid=%016lX", titleId);
    forgetRunningHomebrew(); // a game, not a homebrew, takes the application slot
    switchu::usb::mtpStop();
    Result rc = switchu::menu::smi_cmd::launchApplication(titleId, uid, trace);
    if (R_FAILED(rc)) {
        DebugLog::log("[launcher] FAIL: 0x%X", rc);
        return;
    }
    DebugLog::log("[launcher] command sent, closing menu");
    if (m_cb.requestExit) m_cb.requestExit();
}

#ifdef SWITCHU_PREFLIGHT_EDGE_TEST
void AppletLauncher::launchApplicationFailureDiagnostic(
    uint64_t titleId, AccountUid uid,
    switchu::smi::LaunchTransitionTrace trace) {
    DebugLog::log("[diagnostic-preflight-edge] sending synthetic create failure tid=%016lX",
                  titleId);
    const Result rc = switchu::menu::smi_cmd::launchApplicationFailureDiagnostic(
        titleId, uid, trace);
    if (R_FAILED(rc)) {
        DebugLog::log("[diagnostic-preflight-edge] command enqueue FAIL: 0x%X", rc);
        return;
    }
    DebugLog::log("[diagnostic-preflight-edge] command sent, closing menu");
    if (m_cb.requestExit) m_cb.requestExit();
}
#endif

void AppletLauncher::resumeApplication(switchu::smi::LaunchTransitionTrace trace) {
    if (m_suspendedTitleId == 0) {
        DebugLog::log("[launcher] no app suspended!");
        return;
    }
    switchu::usb::mtpStop();
    const Result rc = switchu::menu::smi_cmd::resumeApplication(trace);
    if (R_FAILED(rc)) {
        DebugLog::log("[launcher] resume enqueue FAIL: 0x%X", rc);
        return;
    }
    DebugLog::log("[launcher] resume command sent, closing menu");
    if (m_cb.requestExit) m_cb.requestExit();
}

#ifdef SWITCHU_RESUME_FAILURE_TEST
void AppletLauncher::resumeApplicationFailureDiagnostic(
    switchu::smi::LaunchTransitionTrace trace) {
    if (m_suspendedTitleId == 0) {
        DebugLog::log("[diagnostic-resume] ignored: no suspended application");
        return;
    }
    DebugLog::log("[diagnostic-resume] sending synthetic foreground failure tid=%016lX",
                  (uint64_t)m_suspendedTitleId);
    const Result rc = switchu::menu::smi_cmd::resumeApplicationFailureDiagnostic(trace);
    if (R_FAILED(rc)) {
        DebugLog::log("[diagnostic-resume] command enqueue FAIL: 0x%X", rc);
        return;
    }
    DebugLog::log("[diagnostic-resume] command sent, closing menu");
    if (m_cb.requestExit) m_cb.requestExit();
}
#endif

void AppletLauncher::terminateApplication() {
    if (m_suspendedTitleId == 0) {
        DebugLog::log("[launcher] no app suspended, nothing to terminate");
        return;
    }
    DebugLog::log("[launcher] requesting terminate 0x%016lX", (uint64_t)m_suspendedTitleId);
    const Result rc = switchu::menu::smi_cmd::terminateApplication();
    if (R_FAILED(rc))
        DebugLog::log("[launcher] terminate command FAIL: 0x%X", rc);
}

#ifdef SWITCHU_TERMINATION_QUEUE_TEST
void AppletLauncher::terminateApplicationDuplicate() {
    if (m_suspendedTitleId == 0) {
        DebugLog::log("[diagnostic] duplicate terminate ignored: no suspended app");
        return;
    }
    const Result first = switchu::menu::smi_cmd::terminateApplication();
    const Result second = switchu::menu::smi_cmd::terminateApplication();
    DebugLog::log("[diagnostic] duplicate terminate sent tid=%016lX rc=0x%X/0x%X",
                  (uint64_t)m_suspendedTitleId, first, second);
}

void AppletLauncher::terminateApplicationHold() {
    if (m_suspendedTitleId == 0) {
        DebugLog::log("[diagnostic] sleep/wake terminate ignored: no suspended app");
        return;
    }
    const Result rc = switchu::menu::smi_cmd::terminateApplicationHold();
    DebugLog::log("[diagnostic] sleep/wake terminate sent tid=%016lX rc=0x%X",
                  (uint64_t)m_suspendedTitleId, rc);
}

void AppletLauncher::terminateApplicationForce() {
    if (m_suspendedTitleId == 0) {
        DebugLog::log("[diagnostic] forced terminate ignored: no suspended app");
        return;
    }
    const Result rc = switchu::menu::smi_cmd::terminateApplicationForce();
    DebugLog::log("[diagnostic] forced terminate sent tid=%016lX rc=0x%X",
                  (uint64_t)m_suspendedTitleId, rc);
}
#endif

void AppletLauncher::checkRunningApplication() {
}

namespace {
constexpr const char* kHblDir = "sdmc:/switch/OmniLaunch/bin/hbl";
constexpr const char* kHblNsp = "sdmc:/atmosphere/hbl.nsp";
constexpr const char* kLoaderNro = "sdmc:/switch/OmniLaunch/bin/loader.nro";
constexpr const char* kHbmenu = "sdmc:/hbmenu.nro";
constexpr const char* kHbmenuOriginal = "sdmc:/hbmenu.nro.omnilaunch-original";
constexpr const char* kLoaderNext = "sdmc:/config/OmniLaunch/hbl_next.txt";
constexpr const char* kChained = "sdmc:/config/OmniLaunch/hbl_chained.txt";
constexpr const char* kApplicationOnly = "sdmc:/config/OmniLaunch/homebrew_application_only";

static bool patchHblNpdmAsApplication(std::vector<char>& npdm) {
    auto u32At = [&](std::size_t off) -> u32 {
        u32 v = 0;
        if (off + 4 <= npdm.size()) std::memcpy(&v, npdm.data() + off, 4);
        return v;
    };
    auto patchKac = [&](std::size_t kacOff, std::size_t kacSize) {
        int patched = 0;
        for (std::size_t off = kacOff; off + 4 <= kacOff + kacSize && off + 4 <= npdm.size(); off += 4) {
            const u32 cap = u32At(off);
            if ((cap & 0x3FFF) == 0x1FFF && ((cap >> 14) & 7) != 1) {
                const u32 app = (1u << 14) | 0x1FFF;
                std::memcpy(npdm.data() + off, &app, 4);
                ++patched;
            }
        }
        return patched;
    };
    if (npdm.size() < 0x80 || u32At(0) != 0x4154454D /* META */) return false;
    const std::size_t aci = u32At(0x70), acid = u32At(0x78);
    if (u32At(aci) != 0x30494341 /* ACI0 */ || u32At(acid + 0x200) != 0x44494341 /* ACID */) return false;
    const int a = patchKac(aci + u32At(aci + 0x30), u32At(aci + 0x34));
    const int b = patchKac(acid + u32At(acid + 0x230), u32At(acid + 0x234));
    DebugLog::log("[launcher] hbl npdm application type patched aci=%d acid=%d", a, b);
    return true;
}

static std::vector<std::uint8_t> readFileBytes(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static bool nroNamed(const char* path, const char* name) {
    HomebrewApp app;
    return readHomebrew(path, app, false) && strcasecmp(app.name.c_str(), name) == 0;
}

static void writeSingleLine(const char* path, const std::string& line) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (FILE* f = std::fopen(path, "w")) {
        std::fputs((line + "\n").c_str(), f);
        std::fclose(f);
    }
}
} // namespace

bool AppletLauncher::prepareHbl() {
    std::error_code ec;
    const std::string mainPath = std::string(kHblDir) + "/main";
    const std::string npdmPath = std::string(kHblDir) + "/main.npdm";
    const std::string stampPath = std::string(kHblDir) + "/source-size";
    if (!std::filesystem::exists(kHblNsp, ec)) return false;
    const std::string size = std::to_string(std::filesystem::file_size(kHblNsp, ec)) + "-app";
    {
        std::ifstream stamp(stampPath);
        std::string seen;
        if (std::getline(stamp, seen) && seen == size
            && std::filesystem::exists(mainPath, ec) && std::filesystem::exists(npdmPath, ec))
            return true;
    }
    std::ifstream in(kHblNsp, std::ios::binary);
    std::vector<char> nsp{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    struct Header { u32 magic, count, strings, reserved; };
    struct Entry { u64 offset, size; u32 name, reserved; };
    if (nsp.size() < sizeof(Header)) return false;
    Header h;
    std::memcpy(&h, nsp.data(), sizeof(h));
    const std::size_t entriesEnd = sizeof(Header) + std::size_t(h.count) * sizeof(Entry);
    if (h.magic != 0x30534650 /* PFS0 */ || h.count > 16 || entriesEnd + h.strings > nsp.size()) return false;
    const std::size_t dataStart = entriesEnd + h.strings;
    std::filesystem::create_directories(kHblDir, ec);
    bool gotMain = false, gotNpdm = false;
    for (u32 i = 0; i < h.count; ++i) {
        Entry e;
        std::memcpy(&e, nsp.data() + sizeof(Header) + i * sizeof(Entry), sizeof(e));
        if (e.name >= h.strings || dataStart + e.offset + e.size > nsp.size()) return false;
        const char* name = nsp.data() + entriesEnd + e.name;
        const char* stringsEnd = nsp.data() + entriesEnd + h.strings;
        const std::string n(name, std::find(name, stringsEnd, '\0'));
        if (n != "main" && n != "main.npdm") continue;
        std::vector<char> data(nsp.begin() + dataStart + e.offset,
                               nsp.begin() + dataStart + e.offset + e.size);
        if (n == "main.npdm" && !patchHblNpdmAsApplication(data)) return false;
        std::ofstream(std::string(kHblDir) + "/" + n, std::ios::binary | std::ios::trunc)
            .write(data.data(), static_cast<std::streamsize>(data.size()));
        (n == "main" ? gotMain : gotNpdm) = true;
    }
    if (gotMain && gotNpdm) std::ofstream(stampPath) << size;
    DebugLog::log("[launcher] hbl unpacked main=%d npdm=%d", gotMain, gotNpdm);
    return gotMain && gotNpdm;
}

// sdmc:/hbmenu.nro belongs to the player. It is swapped for the loader only
// while a homebrew is launched from HOME, and every step below keeps one of
// two states recoverable: the original at its place, or the loader at its place
// with the original kept as hbmenu.nro.omnilaunch-original. Nothing is removed
// before its replacement is verified on the card.
bool AppletLauncher::installLoader() {
    const std::vector<std::uint8_t> loader = readFileBytes(kLoaderNro);
    if (loader.empty()) {
        DebugLog::log("[launcher] loader.nro missing in switch/OmniLaunch/bin/");
        return false;
    }
    std::error_code ec;
    if (std::filesystem::exists(kHbmenu, ec) && readFileBytes(kHbmenu) == loader)
        return true;

    // 1. Stage the loader next to its destination and verify it.
    const std::string tmp = std::string(kHbmenu) + ".omnilaunch-tmp";
    std::remove(tmp.c_str());
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f)
        return false;
    bool staged = std::fwrite(loader.data(), 1, loader.size(), f) == loader.size();
    staged = (std::fclose(f) == 0) && staged;
    if (!staged || readFileBytes(tmp.c_str()) != loader) {
        std::remove(tmp.c_str());
        DebugLog::log("[launcher] loader staging failed");
        return false;
    }

    // 2. Put the player's hbmenu aside (an older loader is simply replaced).
    const std::string previousBackup = std::string(kHbmenuOriginal) + ".prev";
    const std::string parkedLoader = std::string(kHbmenu) + ".omnilaunch-loader";
    bool movedOriginal = false;
    bool parkedOldLoader = false;
    bool keptPreviousBackup = false;
    if (std::filesystem::exists(kHbmenu, ec)) {
        const bool isLoader = nroNamed(kHbmenu, "OmniLaunch-Loader") || nroNamed(kHbmenu, "HomeNX-Loader");
        if (isLoader) {
            // Parked, not deleted, until the new loader is verified in place.
            std::remove(parkedLoader.c_str());
            if (std::rename(kHbmenu, parkedLoader.c_str()) != 0) {
                std::remove(tmp.c_str());
                DebugLog::log("[launcher] could not set the old loader aside");
                return false;
            }
            parkedOldLoader = true;
        } else {
            // A newer hbmenu than the kept one: it becomes the backup, and the
            // older backup is only dropped once that worked.
            if (std::filesystem::exists(kHbmenuOriginal, ec)) {
                std::remove(previousBackup.c_str());
                if (std::rename(kHbmenuOriginal, previousBackup.c_str()) != 0) {
                    std::remove(tmp.c_str());
                    DebugLog::log("[launcher] could not set the old hbmenu backup aside");
                    return false;
                }
                keptPreviousBackup = true;
            }
            if (std::rename(kHbmenu, kHbmenuOriginal) != 0) {
                if (keptPreviousBackup)
                    std::rename(previousBackup.c_str(), kHbmenuOriginal);
                std::remove(tmp.c_str());
                DebugLog::log("[launcher] backup hbmenu.nro failed; nothing replaced");
                return false;
            }
            movedOriginal = true;
        }
    }

    // 3. The loader into place; on failure the original goes straight back.
    if (std::rename(tmp.c_str(), kHbmenu) != 0 || readFileBytes(kHbmenu) != loader) {
        std::remove(tmp.c_str());
        bool restored = true;
        if (movedOriginal || parkedOldLoader) {
            // Only a file we just placed is in the way; the one set aside
            // goes back. A failed rename keeps it where restoreUserHbmenu()
            // (original) or the next install (parked loader) finds it.
            if (std::filesystem::exists(kHbmenu, ec) && readFileBytes(kHbmenu) == loader)
                std::remove(kHbmenu);
            const char* back = movedOriginal ? kHbmenuOriginal : parkedLoader.c_str();
            restored = std::rename(back, kHbmenu) == 0;
            if (restored && keptPreviousBackup)
                restored = std::rename(previousBackup.c_str(), kHbmenuOriginal) == 0;
        }
        switchu::commitSdCard("hbmenu loader rollback");
        DebugLog::log("[launcher] loader install failed; previous hbmenu.nro %s",
                      restored ? "restored" : "kept aside for recovery");
        return false;
    }
    if (keptPreviousBackup)
        std::remove(previousBackup.c_str());
    if (parkedOldLoader)
        std::remove(parkedLoader.c_str());
    switchu::commitSdCard("hbmenu loader installed");
    DebugLog::log("[launcher] OmniLaunch Loader installed as hbmenu.nro (%zu bytes)", loader.size());
    return true;
}

// Puts the player's hbmenu.nro back once no homebrew runs from HOME. Also the
// recovery for a launch or install that was cut short: whatever state is
// found, the result is the original at sdmc:/hbmenu.nro.
void AppletLauncher::restoreUserHbmenu() {
    std::error_code ec;
    const std::string previousBackup = std::string(kHbmenuOriginal) + ".prev";
    if (!std::filesystem::exists(kHbmenuOriginal, ec) && std::filesystem::exists(previousBackup, ec))
        std::rename(previousBackup.c_str(), kHbmenuOriginal);
    std::remove((std::string(kHbmenu) + ".omnilaunch-tmp").c_str());
    if (!std::filesystem::exists(kHbmenuOriginal, ec))
        return; // nothing of ours to undo (the loader alone chains to nothing)
    const bool hbmenuPresent = std::filesystem::exists(kHbmenu, ec);
    if (hbmenuPresent && !nroNamed(kHbmenu, "OmniLaunch-Loader") && !nroNamed(kHbmenu, "HomeNX-Loader"))
        return; // the player put a new hbmenu there; keep it, keep the backup
    const std::string parked = std::string(kHbmenu) + ".omnilaunch-loader";
    std::remove(parked.c_str());
    if (hbmenuPresent && std::rename(kHbmenu, parked.c_str()) != 0)
        return;
    if (std::rename(kHbmenuOriginal, kHbmenu) != 0) {
        if (hbmenuPresent)
            std::rename(parked.c_str(), kHbmenu);
        DebugLog::log("[launcher] hbmenu.nro restore failed; loader kept");
        return;
    }
    std::remove(parked.c_str());
    std::remove(previousBackup.c_str());
    std::filesystem::remove(kChained, ec);
    switchu::commitSdCard("hbmenu restored");
    DebugLog::log("[launcher] player's hbmenu.nro restored");
}
bool AppletLauncher::launchNro(const std::string& path, uint64_t hostTitleId, AccountUid uid,
                               bool applicationOnly) {
    if (!prepareHbl()) {
        DebugLog::log("[launcher] prepareHbl failed");
        return false;
    }
    if (!installLoader()) {
        DebugLog::log("[launcher] installLoader failed");
        return false;
    }
    writeSingleLine(kLoaderNext, path);
    std::error_code ec;
    std::filesystem::remove(kChained, ec);
    if (applicationOnly) {
        if (FILE* f = std::fopen(kApplicationOnly, "w")) std::fclose(f);
    } else {
        std::filesystem::remove(kApplicationOnly, ec);
    }
    // Written before the hand-off so the next menu (after HOME) knows the
    // suspended host game is this homebrew. Cleared at the next startup with
    // nothing suspended, so a failed launch leaves nothing behind.
    writeSingleLine(kRunningNro, path);
    m_runningHomebrew = path;
    quiesceForPower("launch-nro");
    const Result rc = switchu::menu::smi_cmd::launchHomebrewApp(uid, hostTitleId);
    DebugLog::log("[launcher] launchHomebrewApp host=0x%016llX rc=0x%X",
                  static_cast<unsigned long long>(hostTitleId), rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.requestExit) m_cb.requestExit();
        return true;
    }
    return false;
}

#else

bool AppletLauncher::isAppRunning() const            { return false; }
bool AppletLauncher::isAppSuspended(uint64_t) const  { return false; }
uint64_t AppletLauncher::suspendedTitleId() const    { return 0; }
void AppletLauncher::setAppRunning(bool)             {}
void AppletLauncher::setAppHasForeground(bool)       {}
void AppletLauncher::setSuspendedTitleId(uint64_t)   {}
std::string AppletLauncher::runningHomebrew() const  { return {}; }

void AppletLauncher::launchAlbum()             {}
void AppletLauncher::launchMiiEditor()         {}
void AppletLauncher::launchUserCreator()       {}
void AppletLauncher::launchControllerPairing() {}
void AppletLauncher::launchControllerRemapping() {}
void AppletLauncher::launchNetConnect()        {}
void AppletLauncher::launchUserPage(AccountUid) {}
void AppletLauncher::enterSleep()              {}
void AppletLauncher::shutdown()                {}
void AppletLauncher::reboot()                  {}
bool AppletLauncher::launchNro(const std::string&, uint64_t, AccountUid, bool) { return false; }
bool AppletLauncher::prepareHbl()              { return false; }
bool AppletLauncher::installLoader()           { return false; }
void AppletLauncher::restoreUserHbmenu()       {}
Result AppletLauncher::requestSelfUninstall()  {
    return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
}
Result AppletLauncher::leaveSafeMode()         {
    return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
}
Result AppletLauncher::prepareApplication(uint64_t, AccountUid,
                                          switchu::smi::LaunchTransitionTrace&) { return 0; }
void AppletLauncher::launchApplication(uint64_t, AccountUid,
                                       switchu::smi::LaunchTransitionTrace) {}
Result AppletLauncher::refreshCatalog()        { return 0; }
Result AppletLauncher::rotateLogs()            { return 0; }
Result AppletLauncher::rebuildControlCache()   { return 0; }
void AppletLauncher::resumeApplication(switchu::smi::LaunchTransitionTrace) {}
void AppletLauncher::terminateApplication()    {}
void AppletLauncher::checkRunningApplication() {}

#endif
