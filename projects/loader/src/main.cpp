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

std::string readLine(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return {};
    char buf[512] = {};
    std::fgets(buf, sizeof(buf), f);
    std::fclose(f);
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    return line;
}

std::string processId() {
    u64 pid = 0;
    svcGetProcessId(&pid, CUR_PROCESS_HANDLE);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(pid));
    return buf;
}

bool chain(const std::string& path) {
    if (!exists(path) || path == "sdmc:/hbmenu.nro") return false;
    const std::string args = "\"" + path + "\"";
    if (R_FAILED(envSetNextLoad(path.c_str(), args.c_str()))) return false;
    if (FILE* f = std::fopen(kChained, "w")) {
        std::fputs(processId().c_str(), f);
        std::fclose(f);
    }
    return true;
}

void restoreHbmenu() {
    if (!exists(kOriginal)) return;
    std::remove(kHbmenu);
    std::rename(kOriginal, kHbmenu);
}

} // namespace

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
