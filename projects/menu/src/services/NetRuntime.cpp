#include "NetRuntime.hpp"

#include <switchu/file_log.hpp>
#include <switch.h>
#include <curl/curl.h>

#include <mutex>

namespace switchu::services::net {

namespace {

std::mutex g_mutex;
int g_refs = 0;
// True when this runtime called socketInitialize successfully and therefore may
// call socketExit. If something else already owned the socket layer, it is
// theirs to tear down.
bool g_ownsSocket = false;
bool g_nifmUp = false;

} // namespace

bool acquire() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_refs > 0) {
        ++g_refs;
        return true;
    }

    // nifm is reference-counted by libnx, so this composes with the Settings
    // Internet tab, which opens and closes its own session.
    const Result nifmRc = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(nifmRc)) {
        switchu::FileLog::log("[net] nifmInitialize failed: 0x%X", nifmRc);
        return false;
    }
    g_nifmUp = true;

    const Result sockRc = socketInitializeDefault();
    if (R_SUCCEEDED(sockRc)) {
        g_ownsSocket = true;
    } else if (sockRc == MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized)) {
        g_ownsSocket = false;   // someone else's; never exit it
    } else {
        switchu::FileLog::log("[net] socketInitializeDefault failed: 0x%X", sockRc);
        nifmExit();
        g_nifmUp = false;
        return false;
    }

    g_refs = 1;
    return true;
}

void release() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_refs <= 0)
        return;   // unbalanced release: ignore rather than underflow
    if (--g_refs > 0)
        return;

    if (g_ownsSocket) {
        socketExit();
        g_ownsSocket = false;
    }
    if (g_nifmUp) {
        nifmExit();
        g_nifmUp = false;
    }
}

namespace {
std::mutex g_httpMutex;
int g_httpRefs = 0;
} // namespace

bool acquireHttp() {
    std::lock_guard<std::mutex> lock(g_httpMutex);
    if (g_httpRefs > 0) {
        ++g_httpRefs;
        return true;
    }
    if (!acquire())
        return false;
    const CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) {
        switchu::FileLog::log("[net] curl_global_init failed: %s", curl_easy_strerror(rc));
        release();
        return false;
    }
    g_httpRefs = 1;
    return true;
}

void releaseHttp() {
    std::lock_guard<std::mutex> lock(g_httpMutex);
    if (g_httpRefs <= 0)
        return;
    if (--g_httpRefs > 0)
        return;
    curl_global_cleanup();
    release();
}

} // namespace switchu::services::net
