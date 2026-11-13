#include "update/UpdateStager.hpp"

#include "core/DebugLog.hpp"
#include "themeshop/ThemeHttp.hpp"
#include "themeshop/ZipReader.hpp"
#include <switchu/install_txn.hpp>
#include <switchu/sd_commit.hpp>
#ifdef __SWITCH__
#include <switch.h>
#endif

#include <cstdio>
#include <stdexcept>
#include <string>
#include <sys/stat.h>

namespace update {
namespace {

constexpr const char* kUpdateDir = "sdmc:/config/OmniLaunch/update";
constexpr const char* kUpdateArchive = "sdmc:/config/OmniLaunch/update/update.zip";
constexpr const char* kReadyMarker = "sdmc:/config/OmniLaunch/update/ready";
// Written once the daemon inside the archive is already on the card, which is
// what lets the boot that applies the rest skip its own restart.
constexpr const char* kDaemonMarker = "sdmc:/config/OmniLaunch/update/daemon";
// The archive lays out atmosphere/ and switch/ exactly as they sit on the card,
// so the card root is the extraction target.
constexpr const char* kCardRoot = "sdmc:/";

// SHA-256 of a file, read in chunks. Empty when the file cannot be read (and on
// hosts without libnx, where updates are not applied anyway).
std::string fileSha256Hex(const char* path) {
#ifdef __SWITCH__
    std::FILE* f = std::fopen(path, "rb");
    if (!f)
        return {};
    Sha256Context ctx;
    sha256ContextCreate(&ctx);
    static unsigned char buf[64 * 1024];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        sha256ContextUpdate(&ctx, buf, n);
    const bool readOk = std::ferror(f) == 0;
    std::fclose(f);
    if (!readOk)
        return {};
    std::uint8_t digest[SHA256_HASH_SIZE];
    sha256ContextGetHash(&ctx, digest);
    static const char hex[] = "0123456789abcdef";
    std::string out;
    for (std::uint8_t b : digest) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0x0f]);
    }
    return out;
#else
    (void)path;
    return {};
#endif
}

// The staging directory does not exist on a fresh card, and curl will not
// create it. Only this one level is needed: its parent is the config directory
// the menu already owns.
bool ensureUpdateDirectory() {
    struct stat info {};
    if (stat(kUpdateDir, &info) == 0)
        return true;
    return mkdir(kUpdateDir, 0777) == 0;
}

void clearStaged() {
    std::remove(kUpdateArchive);
    std::remove(kReadyMarker);
    std::remove(kDaemonMarker);
}

void stageVerified(const UpdateClient::Release& release,
                   const std::function<void(std::uint64_t, std::uint64_t)>& onProgress,
                   const std::function<void()>& onApplying) {
    if (!ensureUpdateDirectory())
        throw std::runtime_error("could not create the update folder");
    std::remove(kUpdateArchive);
    const std::uint64_t written = themeshop::http::getToFile(release.downloadUrl, kUpdateArchive, onProgress);

    // Only now, with the whole archive on the card and its length agreeing
    // with what the release published, is anything replaced.
    if (release.sizeBytes && written != release.sizeBytes)
        throw std::runtime_error("download size did not match the release");

    // Verify the whole archive before creating the ready marker or replacing
    // the daemon. Missing or malformed release digests are refused when the
    // release is parsed. A digest from the same HTTPS API response protects
    // against a corrupt/swapped download, not a compromised publisher.
    if (release.sha256.empty())
        throw std::runtime_error("release has no published SHA-256 checksum");
    const std::string actual = fileSha256Hex(kUpdateArchive);
    if (actual != release.sha256) {
        DebugLog::log("[update] SHA-256 mismatch");
        throw std::runtime_error("download did not match the published checksum");
    }
    DebugLog::log("[update] SHA-256 verified");
    if (onApplying)
        onApplying();

    // Not unpacked here: the running menu holds its own files open. The
    // daemon applies the update at the next boot, before the menu exists.
    // The archive is still checked now, while there is a screen to report on.
    themeshop::ZipExtractPolicy policy;
    policy.allowExecutablePayload = true;
    policy.requiredRoots = {"atmosphere/", "switch/"};
    const auto inspection = themeshop::inspectZipFile(kUpdateArchive, policy);
    if (!inspection.success)
        throw std::runtime_error(inspection.error.empty() ? "the update package is not valid" : inspection.error);

    // Written once the archive is whole and inspected: the daemon treats this
    // marker as the only sign that an update is ready.
    std::remove(kDaemonMarker);
    if (std::FILE* marker = std::fopen(kReadyMarker, "wb")) {
        std::fputs("ready", marker);
        std::fclose(marker);
    } else {
        throw std::runtime_error("could not mark the update as ready");
    }

    // The one file in the payload that can be replaced from here: the daemon.
    // Atmosphère read it at boot and closed it, and the extractor swaps every
    // file in by renaming a .part over it. Putting it in place before the
    // restart lets the console come up on the new daemon, which unpacks the
    // menu half and goes straight into it. If this fails the daemon marker is
    // not written and the old daemon applies everything and restarts once more.
    switchu::install_txn::Txn daemonTxn(switchu::install_txn::kUpdateBackupRoot, kCardRoot);
    themeshop::ZipExtractPolicy daemonOnly = policy;
    daemonOnly.onlyRoots = {"atmosphere/"};
    themeshop::ZipExtractResult daemonApply;
    // An earlier pass or rollback that was cut short is the daemon's to resolve
    // at boot; this thread never undoes or builds on it.
    const bool blocked = switchu::install_txn::interrupted(switchu::install_txn::kUpdateBackupRoot);
    const bool journalOk = !blocked && daemonTxn.begin();
    if (!journalOk) {
        daemonApply.success = false;
        daemonApply.error = blocked ? "an earlier update pass is waiting for recovery"
                                    : "could not open the rollback journal";
    } else {
        daemonOnly.txn = &daemonTxn;
        daemonApply = themeshop::extractZipFile(kUpdateArchive, kCardRoot, {}, daemonOnly);
        if (daemonApply.success && !daemonTxn.completePass())
            daemonApply.success = false;
    }
    if (journalOk && !daemonApply.success) {
        std::string failed;
        const bool restored = switchu::install_txn::rollback(switchu::install_txn::kUpdateBackupRoot, kCardRoot, &failed);
        DebugLog::log("[update] daemon half failed (%s); rolled back (%s)", daemonApply.error.c_str(),
                      restored ? "ok" : failed.c_str());
    }
    if (daemonApply.success) {
        switchu::commitSdCard("update daemon in place");
        if (std::FILE* marker = std::fopen(kDaemonMarker, "wb")) {
            std::fputs(release.version.c_str(), marker);
            std::fclose(marker);
            switchu::commitSdCard("update daemon marked");
        }
    } else {
        DebugLog::log("[update] daemon half not applied (%s); the boot that applies the rest will restart once more",
                      daemonApply.error.c_str());
    }
}

} // namespace

bool restartPending() {
    struct stat info {};
    return stat(kReadyMarker, &info) == 0;
}

void stageRelease(const UpdateClient::Release& release,
                  const std::function<void(std::uint64_t, std::uint64_t)>& onProgress,
                  const std::function<void()>& onApplying) {
    try {
        stageVerified(release, onProgress, onApplying);
    } catch (const std::exception&) {
        clearStaged();
        throw;
    } catch (...) {
        clearStaged();
        throw std::runtime_error("unknown update failure");
    }
}

} // namespace update
