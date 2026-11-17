// OmniLaunch Loader shim (HomeNX F0/F2): installed as sdmc:/hbmenu.nro
// before launching homebrew in full application mode. Atmosphere's homebrew loader
// starts sdmc:/hbmenu.nro, which immediately chains to the target .nro specified
// in sdmc:/config/OmniLaunch/hbl_next.txt using envSetNextLoad.
// When that app exits, Atmosphere runs this loader again; detecting its own PID
// in hbl_chained.txt, it restores the original hbmenu.nro (if backed up) and
// exits the process, returning cleanly to OmniLaunch HOME. No UI.
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace {

constexpr const char* kNext = "sdmc:/config/OmniLaunch/hbl_next.txt";
constexpr const char* kOriginal = "sdmc:/hbmenu.nro.omnilaunch-original";
constexpr const char* kHbmenu = "sdmc:/hbmenu.nro";
constexpr const char* kChained = "sdmc:/config/OmniLaunch/hbl_chained.txt";

bool exists(const std::string& path) {
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0;
}

// The whole first line, however long: a deep or non-ASCII path used to be cut
// at 511 bytes, and the cut path made the homebrew fall back to hbmenu.
std::string readLine(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return {};
    std::string line;
    for (int c; (c = std::fgetc(f)) != EOF && c != '\n';)
        line.push_back(static_cast<char>(c));
    std::fclose(f);
    while (!line.empty() && line.back() == '\r')
        line.pop_back();
    return line;
}

// One line per start, overwritten each time: what this loader was asked to
// open and whether it could. Without it a homebrew that did not open left no
// trace at all.
constexpr const char* kLoaderLog = "sdmc:/config/OmniLaunch/loader.log";
void note(const char* what, const std::string& path, Result rc) {
    if (FILE* f = std::fopen(kLoaderLog, "w")) {
        std::fprintf(f, "%s target=%s exists=%d rc=0x%X\n", what, path.c_str(),
                     exists(path) ? 1 : 0, static_cast<unsigned>(rc));
        std::fclose(f);
    }
}

std::string processId() {
    u64 pid = 0;
    svcGetProcessId(&pid, CUR_PROCESS_HANDLE);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(pid));
    return buf;
}

bool chain(const std::string& path) {
    if (!exists(path) || path == "sdmc:/hbmenu.nro") {
        note("chain-missing", path, 0);
        return false;
    }
    const std::string args = "\"" + path + "\"";
    const Result rc = envSetNextLoad(path.c_str(), args.c_str());
    note(R_SUCCEEDED(rc) ? "chain" : "chain-failed", path, rc);
    if (R_FAILED(rc)) return false;
    if (FILE* f = std::fopen(kChained, "w")) {
        std::fputs(processId().c_str(), f);
        std::fclose(f);
    }
    return true;
}

// The loader (this file) is parked first and only removed once the original
// is back, so a failed rename never leaves sdmc:/hbmenu.nro missing.
void restoreHbmenu() {
    if (!exists(kOriginal)) return;
    const char* parked = "sdmc:/hbmenu.nro.omnilaunch-loader";
    std::remove(parked);
    if (exists(kHbmenu) && std::rename(kHbmenu, parked) != 0) return;
    if (std::rename(kOriginal, kHbmenu) != 0) {
        std::rename(parked, kHbmenu);
        return;
    }
    std::remove(parked);
}

} // namespace

// libnx's own __appInit without the time service and HID (adapted from HomeNX
// 8d0fd8b): this program never reads the clock or input, and the default
// timeInitialize() aborted the launch (2345-0038, InitFail_Time) whenever the
// console's time service had run out of sessions.
extern "C" void __appInit(void) {
    if (R_FAILED(smInitialize())) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    if (hosversionGet() == 0 && R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }
    if (R_FAILED(appletInitialize())) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_AM));
    if (R_FAILED(fsInitialize())) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
    fsdevMountSdmc();
}

extern "C" void __appExit(void) {
    fsdevUnmountAll();
    fsExit();
    appletExit();
    smExit();
}

int main(int argc, char** argv) {
    if (readLine(kChained) == processId()) {
        std::remove(kChained);
        restoreHbmenu();
        svcExitProcess();
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.size() > 4 && arg.compare(arg.size() - 4, 4, ".nro") == 0 && chain(arg))
            return 0;
    }

    const std::string next = readLine(kNext);
    if (!next.empty()) {
        std::remove(kNext);
        if (chain(next))
            return 0;
    }

    // Default: chain to original hbmenu if present
    if (exists(kOriginal)) {
        if (chain(kOriginal))
            return 0;
    }

    return 0;
}
