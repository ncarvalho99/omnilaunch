#include "config_backup.hpp"

#include <switchu/file_log.hpp>
#include <switchu/sd_commit.hpp>
#include <switch.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>

// Written against POSIX rather than std::filesystem for the reason given in
// switchu/fs_remove.hpp: fsdev has no openat/fdopendir, and the directory
// walkers in libstdc++ fail quietly without them.
namespace switchu::daemon::config_backup {
namespace {

constexpr const char* kConfigParent       = "sdmc:/config";
constexpr const char* kConfigRoot         = "sdmc:/config/OmniLaunch";
constexpr const char* kBackupParent       = "sdmc:/backup";
constexpr const char* kBackupRoot         = "sdmc:/backup/OmniLaunch";
constexpr const char* kLegacyBackupRoot   = "sdmc:/backup/SwitchU";
// Its presence is what says a configuration exists. Copied last in either
// direction, so an interrupted copy is retried in full at the next boot.
constexpr const char* kConfigFile         = "config.json";
constexpr const char* kPartSuffix         = ".omnilaunch-part";

// Files up to this size are compared byte for byte, which catches an edit that
// kept the size. Larger ones -- artwork, theme media -- are compared by size.
constexpr off_t kCompareContentsBelow = 256 * 1024;

struct Stats {
    unsigned copied = 0;
    unsigned unchanged = 0;
    unsigned failed = 0;
    unsigned long long bytes = 0;
};

// Static so the copy costs nothing from the daemon's heap, which shares a pool
// with every other sysmodule on the card.
unsigned char g_bufferA[32 * 1024];
unsigned char g_bufferB[32 * 1024];

bool endsWith(const char* value, const char* suffix) {
    const std::size_t valueLength = std::strlen(value);
    const std::size_t suffixLength = std::strlen(suffix);
    return valueLength >= suffixLength
        && std::strcmp(value + valueLength - suffixLength, suffix) == 0;
}

// Rebuilt by OmniLaunch on its own, or only meaningful for the boot that wrote
// them: the title cache, update and uninstall staging, logs, leave_frame splash, half-written files.
bool excludedDirectory(const std::string& relative, const char* name) {
    return relative.empty()
        && (std::strcmp(name, "update") == 0
            || std::strcmp(name, "uninstall") == 0
            || std::strcmp(name, "control_cache") == 0);
}

bool excludedFile(const char* name) {
    return endsWith(name, ".log") || std::strncmp(name, "log.txt", 7) == 0
        || endsWith(name, ".tmp") || endsWith(name, ".part")
        || endsWith(name, ".switchu-part") || endsWith(name, kPartSuffix)
        || std::strcmp(name, "leave_frame.bin") == 0;
}

bool fileSize(const std::string& path, off_t& size) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
        return false;
    size = info.st_size;
    return true;
}

bool ensureDirectory(const std::string& path) {
    if (mkdir(path.c_str(), 0777) == 0 || errno == EEXIST)
        return true;
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool sameContents(const std::string& a, const std::string& b) {
    std::FILE* fa = std::fopen(a.c_str(), "rb");
    std::FILE* fb = fa ? std::fopen(b.c_str(), "rb") : nullptr;
    bool same = fa && fb;
    while (same) {
        const std::size_t ra = std::fread(g_bufferA, 1, sizeof(g_bufferA), fa);
        const std::size_t rb = std::fread(g_bufferB, 1, sizeof(g_bufferB), fb);
        if (ra != rb || std::memcmp(g_bufferA, g_bufferB, ra) != 0)
            same = false;
        else if (ra == 0)
            break;
    }
    if (fa) std::fclose(fa);
    if (fb) std::fclose(fb);
    return same;
}

bool upToDate(const std::string& source, off_t sourceSize, const std::string& destination) {
    off_t destinationSize = 0;
    if (!fileSize(destination, destinationSize) || destinationSize != sourceSize)
        return false;
    return sourceSize > kCompareContentsBelow || sameContents(source, destination);
}

// Written beside the target and renamed over it, so a copy cut short by a
// power loss leaves the previous version rather than half of the new one.
bool copyFile(const std::string& source, const std::string& destination, Stats& stats) {
    const std::string part = destination + kPartSuffix;
    std::FILE* in = std::fopen(source.c_str(), "rb");
    std::FILE* out = in ? std::fopen(part.c_str(), "wb") : nullptr;
    bool ok = in && out;
    unsigned long long written = 0;
    while (ok) {
        const std::size_t read = std::fread(g_bufferA, 1, sizeof(g_bufferA), in);
        if (read == 0) {
            ok = !std::ferror(in);
            break;
        }
        if (std::fwrite(g_bufferA, 1, read, out) != read)
            ok = false;
        written += read;
    }
    if (in) std::fclose(in);
    if (out && std::fclose(out) != 0) ok = false;

    if (ok) {
        std::remove(destination.c_str());
        ok = std::rename(part.c_str(), destination.c_str()) == 0;
    }
    if (!ok) {
        std::remove(part.c_str());
        ++stats.failed;
        switchu::FileLog::log("[backup] copy failed: %s -> %s errno=%d",
                              source.c_str(), destination.c_str(), errno);
        return false;
    }
    ++stats.copied;
    stats.bytes += written;
    return true;
}

// Copies what changed from sourceDir into destinationDir. Nothing is ever
// deleted on the destination side: a file removed from the configuration
// stays in the backup, which is the safe way round for a copy whose job is
// to outlive a wipe.
void mirror(const std::string& sourceDir, const std::string& destinationDir,
            const std::string& relative, Stats& stats) {
    if (!ensureDirectory(destinationDir)) {
        ++stats.failed;
        switchu::FileLog::log("[backup] cannot create %s errno=%d",
                              destinationDir.c_str(), errno);
        return;
    }
    DIR* dir = opendir(sourceDir.c_str());
    if (!dir) {
        ++stats.failed;
        switchu::FileLog::log("[backup] cannot open %s errno=%d", sourceDir.c_str(), errno);
        return;
    }
    while (const dirent* entry = readdir(dir)) {
        const char* name = entry->d_name;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0)
            continue;
        const std::string source = sourceDir + "/" + name;
        const std::string destination = destinationDir + "/" + name;
        struct stat info {};
        if (stat(source.c_str(), &info) != 0)
            continue;
        if (S_ISDIR(info.st_mode)) {
            if (!excludedDirectory(relative, name))
                mirror(source, destination, relative + name + "/", stats);
            continue;
        }
        if (!S_ISREG(info.st_mode) || excludedFile(name))
            continue;
        if (relative.empty() && std::strcmp(name, kConfigFile) == 0)
            continue;   // the caller copies it last
        if (upToDate(source, info.st_size, destination))
            ++stats.unchanged;
        else
            copyFile(source, destination, stats);
    }
    closedir(dir);
}

void copyConfigFileLast(const std::string& sourceRoot, const std::string& destinationRoot,
                        Stats& stats) {
    const std::string source = sourceRoot + "/" + kConfigFile;
    const std::string destination = destinationRoot + "/" + kConfigFile;
    off_t size = 0;
    if (!fileSize(source, size))
        return;
    if (upToDate(source, size, destination))
        ++stats.unchanged;
    else
        copyFile(source, destination, stats);
}

bool exists(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0;
}

} // namespace

void restoreOrBackUp() {
    const u64 start = armGetSystemTick();
    const std::string configFile = std::string(kConfigRoot) + "/" + kConfigFile;
    std::string backupRoot = kBackupRoot;
    std::string backupFile = std::string(kBackupRoot) + "/" + kConfigFile;
    if (!exists(backupFile) && exists(std::string(kLegacyBackupRoot) + "/" + kConfigFile)) {
        backupRoot = kLegacyBackupRoot;
        backupFile = std::string(kLegacyBackupRoot) + "/" + kConfigFile;
    }

    const bool haveConfig = exists(configFile);
    const bool haveBackup = exists(backupFile);

    Stats stats;
    const char* action = nullptr;
    if (!haveConfig && haveBackup) {
        action = "restored";
        switchu::FileLog::log("[backup] configuration missing; restoring from %s", backupRoot.c_str());
        ensureDirectory(kConfigParent);
        mirror(backupRoot, kConfigRoot, "", stats);
        if (stats.failed == 0)
            copyConfigFileLast(backupRoot, kConfigRoot, stats);
        else
            switchu::FileLog::log("[backup] restore incomplete; %s held back so the next boot retries",
                                  kConfigFile);
    } else if (haveConfig) {
        action = "backed up";
        ensureDirectory(kBackupParent);
        mirror(kConfigRoot, kBackupRoot, "", stats);
        if (stats.failed == 0)
            copyConfigFileLast(kConfigRoot, kBackupRoot, stats);
    } else {
        switchu::FileLog::log("[backup] no configuration and no backup yet");
        return;
    }

    const u64 elapsedMs = armTicksToNs(armGetSystemTick() - start) / 1'000'000ULL;
    switchu::FileLog::log("[backup] %s: copied=%u (%llu bytes) unchanged=%u failed=%u in %llu ms",
                          action, stats.copied, stats.bytes, stats.unchanged, stats.failed,
                          static_cast<unsigned long long>(elapsedMs));
    if (stats.copied > 0)
        switchu::commitSdCard("config backup");
}

} // namespace switchu::daemon::config_backup
