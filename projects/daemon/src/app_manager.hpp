#pragma once

#include <switch.h>
#include <switchu/control_cache.hpp>
#include <switchu/file_log.hpp>
#include "mem_probe.hpp"
#include <cstring>

namespace switchu::daemon::app {

static AppletApplication g_app = {};
static bool g_running = false;
static bool g_hasForeground = false;
static uint64_t g_suspendedTitleId = 0;
static bool g_lastLaunchAcceptsUser = true;
static bool g_lastLaunchNeedsUser = true;
static uint8_t g_lastStartupUserAccount = 1;
static uint8_t g_lastStartupUserAccountOption = 0;
static uint64_t g_lastFinishedTick = 0;

enum class TerminatePhase : uint8_t {
    Idle,
    Graceful,
    Forced,
};

struct TerminateTiming {
    uint64_t beginTick = 0;
    uint64_t libraryEndTick = 0;
    uint64_t requestEndTick = 0;
    uint64_t forceTick = 0;
    uint64_t completeTick = 0;
    uint32_t polls = 0;
    uint32_t beginCore = 0;
    uint32_t completeCore = 0;
    Result libraryRc = 0;
    Result requestRc = 0;
    Result forceRc = 0;
    AppletApplicationExitReason exitReason = AppletApplicationExitReason_Normal;
    bool forced = false;
};

static TerminatePhase g_terminatePhase = TerminatePhase::Idle;
static TerminateTiming g_terminateTiming{};
static uint64_t g_terminateDeadlineTick = 0;
static constexpr uint64_t kTerminateGraceNs = 15'000'000'000ULL;
// Set by beginTerminateWithin() for the next terminate only; 0 = the usual grace.
static uint64_t g_terminateGraceOverrideNs = 0;
#ifdef SWITCHU_TERMINATION_QUEUE_TEST
enum class TerminateDiagnosticMode : uint8_t {
    None,
    HoldForSleepWake,
    ForceAtDeadline,
};
static TerminateDiagnosticMode g_terminateDiagnosticMode = TerminateDiagnosticMode::None;
// A human needs enough time to open Power > Sleep after selecting the close
// action. Release before the unchanged 15-second production force deadline.
static constexpr uint64_t kTerminateSleepWakeHoldNs = 12'000'000'000ULL;
#endif

enum class PreflightRejectReason : uint8_t {
    None = 0,
    Missing,
    TitleMismatch,
    UserMismatch,
    Incomplete,
    Stale,
    MetadataMissing,
    MetadataChanged,
};

inline const char* preflightRejectReasonName(PreflightRejectReason reason) {
    switch (reason) {
    case PreflightRejectReason::None:            return "none";
    case PreflightRejectReason::Missing:         return "missing";
    case PreflightRejectReason::TitleMismatch:   return "title-mismatch";
    case PreflightRejectReason::UserMismatch:    return "user-mismatch";
    case PreflightRejectReason::Incomplete:      return "incomplete";
    case PreflightRejectReason::Stale:           return "stale";
    case PreflightRejectReason::MetadataMissing: return "metadata-missing";
    case PreflightRejectReason::MetadataChanged: return "metadata-changed";
    }
    return "unknown";
}

struct PreflightTiming {
    uint64_t requestSendTick = 0;
    uint64_t commandReceiveTick = 0;
    uint64_t workStartTick = 0;
    uint64_t touchStartTick = 0;
    uint64_t touchEndTick = 0;
    uint64_t saveStartTick = 0;
    uint64_t saveEndTick = 0;
    uint64_t workEndTick = 0;
    uint32_t core = 0;
    bool attempted = false;
    bool complete = false;
    bool cacheHit = false;
    PreflightRejectReason rejectReason = PreflightRejectReason::None;
};

enum class SaveEnsureOutcome : uint8_t {
    NotRequested = 0,
    Existing,
    Created,
    Failed,
};

inline const char* saveEnsureOutcomeName(SaveEnsureOutcome outcome) {
    switch (outcome) {
    case SaveEnsureOutcome::NotRequested: return "not-requested";
    case SaveEnsureOutcome::Existing:     return "existing";
    case SaveEnsureOutcome::Created:      return "created";
    case SaveEnsureOutcome::Failed:       return "failed";
    }
    return "unknown";
}

struct SaveEnsureTrace {
    Result openRc = 0;
    Result createRc = 0;
    SaveEnsureOutcome outcome = SaveEnsureOutcome::NotRequested;
};

struct LaunchTiming {
    uint64_t actionStartTick = 0;
    uint64_t previousExitRequestTick = 0;
    uint64_t previousJoinedTick = 0;
    uint64_t touchStartTick = 0;
    uint64_t touchEndTick = 0;
    uint64_t saveStartTick = 0;
    uint64_t saveEndTick = 0;
    uint64_t accountSaveTicks = 0;
    uint64_t deviceSaveTicks = 0;
    uint64_t temporarySaveTicks = 0;
    uint64_t cacheSaveTicks = 0;
    uint64_t bcatSaveTicks = 0;
    SaveEnsureTrace accountSaveEnsure{};
    SaveEnsureTrace deviceSaveEnsure{};
    SaveEnsureTrace temporarySaveEnsure{};
    SaveEnsureTrace cacheSaveEnsure{};
    SaveEnsureTrace bcatSaveEnsure{};
    uint64_t createStartTick = 0;
    uint64_t createEndTick = 0;
    uint64_t startStartTick = 0;
    uint64_t startEndTick = 0;
    uint64_t foregroundStartTick = 0;
    uint64_t foregroundEndTick = 0;
    uint32_t core = 0;
    PreflightTiming preflight{};
};

struct ResumeTiming {
    uint64_t actionStartTick = 0;
    uint64_t unlockStartTick = 0;
    uint64_t unlockEndTick = 0;
    uint64_t foregroundStartTick = 0;
    uint64_t foregroundEndTick = 0;
    Result unlockResult = 0;
    uint32_t core = 0;
};

struct PreparedLaunch {
    bool present = false;
    uint64_t titleId = 0;
    AccountUid uid{};
    switchu::control_cache::Meta meta{};
    LaunchTiming work{};
    PreflightTiming timing{};
};

static PreparedLaunch g_prepared{};
static constexpr uint64_t kPreparedLaunchMaxAgeNs = 5'000'000'000ULL;
#ifdef SWITCHU_PREFLIGHT_EDGE_TEST
static constexpr Result kPreflightEdgeSyntheticCreateFailure =
    MAKERESULT(Module_Libnx, 0xFC);
#endif
#ifdef SWITCHU_RESUME_FAILURE_TEST
static constexpr Result kResumeSyntheticForegroundFailure =
    MAKERESULT(Module_Libnx, 0xFB);
#endif

#ifdef SWITCHU_PREFLIGHT_MATRIX_TEST
enum class PreflightDiagnosticCase : uint8_t {
    BaselineHit = 0,
    MissingPreparation,
    TitleMismatch,
    UserMismatch,
    IncompletePreparation,
    StalePreparation,
    MetadataMissing,
    MetadataChanged,
    Count,
};

static uint32_t g_preflightDiagnosticSequence = 0;
static uint32_t g_preflightDiagnosticActiveSequence = 0;
static PreflightDiagnosticCase g_preflightDiagnosticCase =
    PreflightDiagnosticCase::BaselineHit;
static PreflightRejectReason g_preflightDiagnosticExpectedReason =
    PreflightRejectReason::None;
static bool g_preflightDiagnosticForceMetadataMissing = false;

static inline const char* preflightDiagnosticCaseName(PreflightDiagnosticCase value) {
    switch (value) {
    case PreflightDiagnosticCase::BaselineHit:           return "baseline-hit";
    case PreflightDiagnosticCase::MissingPreparation:    return "missing-preparation";
    case PreflightDiagnosticCase::TitleMismatch:         return "title-mismatch";
    case PreflightDiagnosticCase::UserMismatch:          return "user-mismatch";
    case PreflightDiagnosticCase::IncompletePreparation: return "incomplete-preparation";
    case PreflightDiagnosticCase::StalePreparation:      return "stale-preparation";
    case PreflightDiagnosticCase::MetadataMissing:       return "metadata-missing";
    case PreflightDiagnosticCase::MetadataChanged:       return "metadata-changed";
    case PreflightDiagnosticCase::Count:                 break;
    }
    return "unknown";
}

static inline void injectPreflightDiagnosticFault() {
    const uint32_t caseCount = static_cast<uint32_t>(PreflightDiagnosticCase::Count);
    g_preflightDiagnosticActiveSequence = ++g_preflightDiagnosticSequence;
    g_preflightDiagnosticCase = static_cast<PreflightDiagnosticCase>(
        (g_preflightDiagnosticActiveSequence - 1) % caseCount);
    g_preflightDiagnosticExpectedReason = PreflightRejectReason::None;
    g_preflightDiagnosticForceMetadataMissing = false;

    switch (g_preflightDiagnosticCase) {
    case PreflightDiagnosticCase::BaselineHit:
        break;
    case PreflightDiagnosticCase::MissingPreparation:
        g_prepared.present = false;
        g_preflightDiagnosticExpectedReason = PreflightRejectReason::Missing;
        break;
    case PreflightDiagnosticCase::TitleMismatch:
        g_prepared.titleId ^= 1;
        g_preflightDiagnosticExpectedReason = PreflightRejectReason::TitleMismatch;
        break;
    case PreflightDiagnosticCase::UserMismatch:
        g_prepared.uid.uid[0] ^= 1;
        g_preflightDiagnosticExpectedReason = PreflightRejectReason::UserMismatch;
        break;
    case PreflightDiagnosticCase::IncompletePreparation:
        g_prepared.timing.complete = false;
        g_preflightDiagnosticExpectedReason = PreflightRejectReason::Incomplete;
        break;
    case PreflightDiagnosticCase::StalePreparation: {
        const uint64_t staleTicks = armNsToTicks(kPreparedLaunchMaxAgeNs + 1'000'000'000ULL);
        const uint64_t now = armGetSystemTick();
        g_prepared.timing.workEndTick = now > staleTicks ? now - staleTicks : 1;
        g_preflightDiagnosticExpectedReason = PreflightRejectReason::Stale;
        break;
    }
    case PreflightDiagnosticCase::MetadataMissing:
        g_preflightDiagnosticForceMetadataMissing = true;
        g_preflightDiagnosticExpectedReason = PreflightRejectReason::MetadataMissing;
        break;
    case PreflightDiagnosticCase::MetadataChanged:
        g_prepared.meta.version ^= 0x80000000U;
        g_preflightDiagnosticExpectedReason = PreflightRejectReason::MetadataChanged;
        break;
    case PreflightDiagnosticCase::Count:
        break;
    }

    switchu::FileLog::log(
        "[diagnostic-preflight] begin sequence=%u/8 case=%s expected=%s",
        g_preflightDiagnosticActiveSequence,
        preflightDiagnosticCaseName(g_preflightDiagnosticCase),
        preflightRejectReasonName(g_preflightDiagnosticExpectedReason));
}
#endif

inline bool isRunning() { return g_running; }
inline bool hasForeground() { return g_hasForeground; }
inline bool isTerminating() { return g_terminatePhase != TerminatePhase::Idle; }
inline uint64_t suspendedTitleId() { return g_suspendedTitleId; }
inline uint64_t lastFinishedTick() { return g_lastFinishedTick; }

inline bool startupUserRequiresInteractiveSelection(uint8_t account, uint8_t option) {
    return account == 1 && option == 0;
}

static inline uint64_t ensureSaveData(uint64_t app_id, uint64_t owner_id,
                                      AccountUid user_id, FsSaveDataType type,
                                      FsSaveDataSpaceId space_id,
                                      uint64_t save_size, uint64_t journal_size,
                                      bool* outSuccess,
                                      SaveEnsureTrace* outTrace) {
    if (outSuccess)
        *outSuccess = true;
    if (outTrace)
        *outTrace = {};
    if (save_size == 0)
        return 0;

    const uint64_t startTick = armGetSystemTick();

    FsSaveDataAttribute attr = {};
    attr.application_id = app_id;
    attr.uid = user_id;
    attr.save_data_type = type;
    attr.save_data_rank = FsSaveDataRank_Primary;

    FsSaveDataCreationInfo cr = {};
    cr.save_data_size = static_cast<s64>(save_size);
    cr.journal_size = static_cast<s64>(journal_size);
    cr.available_size = 0x4000;
    cr.owner_id = owner_id;
    cr.save_data_space_id = static_cast<u8>(space_id);

    FsSaveDataMetaInfo meta = {};
    meta.size = type == FsSaveDataType_Bcat ? 0 : 0x40060;
    meta.type = type == FsSaveDataType_Bcat ? FsSaveDataMetaType_None
                                            : FsSaveDataMetaType_Thumbnail;

    FsFileSystem fs;
    const Result openRc = fsOpenSaveDataFileSystem(&fs, space_id, &attr);
    if (outTrace)
        outTrace->openRc = openRc;
    if (R_SUCCEEDED(openRc)) {
        fsFsClose(&fs);
        if (outTrace)
            outTrace->outcome = SaveEnsureOutcome::Existing;
        return armGetSystemTick() - startTick;
    }

    Result rc = fsCreateSaveDataFileSystem(&attr, &cr, &meta);
    if (outTrace) {
        outTrace->createRc = rc;
        outTrace->outcome = R_SUCCEEDED(rc) ? SaveEnsureOutcome::Created
                                             : SaveEnsureOutcome::Failed;
    }
    if (R_FAILED(rc)) {
        if (outSuccess)
            *outSuccess = false;
        switchu::FileLog::log("[app] ensureSaveData type=%d FAIL: 0x%X", static_cast<int>(type), rc);
    }
    return armGetSystemTick() - startTick;
}

// Temporary storage is not a normal save-data filesystem. In particular, its
// creation metadata must be empty; giving it the thumbnail metadata used by
// account/device saves makes fs reject the request (0x402 on affected titles).
// Use libnx's dedicated wrapper so its exact FS contract stays in one place.
static inline uint64_t ensureTemporaryStorage(uint64_t app_id, uint64_t owner_id,
                                              uint64_t storage_size,
                                              bool* outSuccess,
                                              SaveEnsureTrace* outTrace) {
    if (outSuccess)
        *outSuccess = true;
    if (outTrace)
        *outTrace = {};
    if (storage_size == 0)
        return 0;

    const uint64_t startTick = armGetSystemTick();

    FsSaveDataAttribute attr = {};
    attr.application_id = app_id;
    attr.save_data_type = FsSaveDataType_Temporary;

    FsFileSystem fs;
    const Result openRc = fsOpenSaveDataFileSystem(
        &fs, FsSaveDataSpaceId_Temporary, &attr);
    if (outTrace)
        outTrace->openRc = openRc;
    if (R_SUCCEEDED(openRc)) {
        fsFsClose(&fs);
        if (outTrace)
            outTrace->outcome = SaveEnsureOutcome::Existing;
        return armGetSystemTick() - startTick;
    }

    Result rc = fsCreate_TemporaryStorage(app_id, owner_id,
                                          static_cast<s64>(storage_size), 0);
    if (outTrace) {
        outTrace->createRc = rc;
        outTrace->outcome = R_SUCCEEDED(rc) ? SaveEnsureOutcome::Created
                                             : SaveEnsureOutcome::Failed;
    }
    if (R_FAILED(rc)) {
        if (outSuccess)
            *outSuccess = false;
        switchu::FileLog::log("[app] ensureTemporaryStorage FAIL: 0x%X", rc);
    }
    return armGetSystemTick() - startTick;
}

static inline void resetLaunchMetadata() {
    g_lastLaunchAcceptsUser = true;
    g_lastLaunchNeedsUser = true;
    g_lastStartupUserAccount = 1;
    g_lastStartupUserAccountOption = 0;
}

static inline void applyLaunchMetadata(const switchu::control_cache::Meta& meta) {
    g_lastStartupUserAccount = meta.startup_user_account;
    g_lastStartupUserAccountOption = meta.startup_user_account_option;
    g_lastLaunchAcceptsUser = g_lastStartupUserAccount != 0;
    g_lastLaunchNeedsUser = startupUserRequiresInteractiveSelection(g_lastStartupUserAccount,
                                                                    g_lastStartupUserAccountOption);
}

static inline bool ensureApplicationSaveDataFromMeta(
    uint64_t title_id, AccountUid uid,
    const switchu::control_cache::Meta& meta,
    LaunchTiming* timing) {
    if (timing)
        timing->saveStartTick = armGetSystemTick();
    applyLaunchMetadata(meta);

    bool accountOk = true;
    bool deviceOk = true;
    bool temporaryOk = true;
    bool cacheOk = true;
    bool bcatOk = true;

    const uint64_t accountTicks = ensureSaveData(
        title_id, meta.save_data_owner_id, uid,
        FsSaveDataType_Account, FsSaveDataSpaceId_User,
        meta.user_account_save_data_size,
        meta.user_account_save_data_journal_size, &accountOk,
        timing ? &timing->accountSaveEnsure : nullptr);

    AccountUid emptyUid = {};
    const uint64_t deviceTicks = ensureSaveData(
        title_id, meta.save_data_owner_id, emptyUid,
        FsSaveDataType_Device, FsSaveDataSpaceId_User,
        meta.device_save_data_size,
        meta.device_save_data_journal_size, &deviceOk,
        timing ? &timing->deviceSaveEnsure : nullptr);

    const uint64_t temporaryTicks = ensureTemporaryStorage(
        title_id, meta.save_data_owner_id, meta.temporary_storage_size,
        &temporaryOk, timing ? &timing->temporarySaveEnsure : nullptr);

    const uint64_t cacheTicks = ensureSaveData(
        title_id, meta.save_data_owner_id, emptyUid,
        FsSaveDataType_Cache, FsSaveDataSpaceId_User,
        meta.cache_storage_size,
        meta.cache_storage_journal_size, &cacheOk,
        timing ? &timing->cacheSaveEnsure : nullptr);

    const uint64_t bcatTicks = ensureSaveData(
        title_id, 0x010000000000000C, emptyUid,
        FsSaveDataType_Bcat, FsSaveDataSpaceId_User,
        meta.bcat_delivery_cache_storage_size, 0x200000, &bcatOk,
        timing ? &timing->bcatSaveEnsure : nullptr);

    if (timing) {
        timing->accountSaveTicks = accountTicks;
        timing->deviceSaveTicks = deviceTicks;
        timing->temporarySaveTicks = temporaryTicks;
        timing->cacheSaveTicks = cacheTicks;
        timing->bcatSaveTicks = bcatTicks;
        timing->saveEndTick = armGetSystemTick();
    }
    return accountOk && deviceOk && temporaryOk && cacheOk && bcatOk;
}

static inline bool ensureApplicationSaveData(
    uint64_t title_id, AccountUid uid, LaunchTiming* timing,
    switchu::control_cache::Meta* outMeta = nullptr,
    bool* outMetaLoaded = nullptr) {
    if (outMetaLoaded)
        *outMetaLoaded = false;
    switchu::control_cache::Meta meta{};
    if (!switchu::control_cache::readMeta(title_id, meta)) {
        if (timing) {
            timing->saveStartTick = armGetSystemTick();
            timing->saveEndTick = timing->saveStartTick;
        }
        switchu::FileLog::log("[app] control cache missing for 0x%016lX; save data not precreated",
                              title_id);
        return false;
    }
    if (outMeta)
        *outMeta = meta;
    if (outMetaLoaded)
        *outMetaLoaded = true;
    return ensureApplicationSaveDataFromMeta(title_id, uid, meta, timing);
}

static inline Result touchApplication(uint64_t title_id, LaunchTiming* timing) {
    if (timing)
        timing->touchStartTick = armGetSystemTick();
    const Result rc = nsTouchApplication(title_id);
    if (timing)
        timing->touchEndTick = armGetSystemTick();
    if (R_FAILED(rc))
        switchu::FileLog::log("[app] nsTouchApplication FAIL: 0x%X (non-fatal)", rc);
    else
        switchu::FileLog::log("[app] nsTouchApplication ok");
    return rc;
}

inline void prepare(uint64_t title_id, AccountUid uid,
                    uint64_t requestSendTick, uint64_t commandReceiveTick) {
    g_prepared = {};
    g_prepared.present = true;
    g_prepared.titleId = title_id;
    g_prepared.uid = uid;
    g_prepared.timing.attempted = true;
    g_prepared.timing.requestSendTick = requestSendTick;
    g_prepared.timing.commandReceiveTick = commandReceiveTick;
    g_prepared.timing.workStartTick = armGetSystemTick();
    g_prepared.timing.core = svcGetCurrentProcessorNumber();

    resetLaunchMetadata();
    const Result touchRc = touchApplication(title_id, &g_prepared.work);
    bool metaLoaded = false;
    const bool savesOk = ensureApplicationSaveData(
        title_id, uid, &g_prepared.work, &g_prepared.meta, &metaLoaded);

    g_prepared.timing.touchStartTick = g_prepared.work.touchStartTick;
    g_prepared.timing.touchEndTick = g_prepared.work.touchEndTick;
    g_prepared.timing.saveStartTick = g_prepared.work.saveStartTick;
    g_prepared.timing.saveEndTick = g_prepared.work.saveEndTick;
    g_prepared.timing.workEndTick = armGetSystemTick();
    g_prepared.timing.complete = R_SUCCEEDED(touchRc) && metaLoaded && savesOk;
}

static inline bool preparedLaunchFresh(uint64_t nowTick) {
    return g_prepared.timing.workEndTick != 0
        && nowTick >= g_prepared.timing.workEndTick
        && armTicksToNs(nowTick - g_prepared.timing.workEndTick) <= kPreparedLaunchMaxAgeNs;
}

static inline void copyPreparedWork(LaunchTiming* dst, const LaunchTiming& src) {
    if (!dst)
        return;
    dst->touchStartTick = src.touchStartTick;
    dst->touchEndTick = src.touchEndTick;
    dst->saveStartTick = src.saveStartTick;
    dst->saveEndTick = src.saveEndTick;
    dst->accountSaveTicks = src.accountSaveTicks;
    dst->deviceSaveTicks = src.deviceSaveTicks;
    dst->temporarySaveTicks = src.temporarySaveTicks;
    dst->cacheSaveTicks = src.cacheSaveTicks;
    dst->bcatSaveTicks = src.bcatSaveTicks;
    dst->accountSaveEnsure = src.accountSaveEnsure;
    dst->deviceSaveEnsure = src.deviceSaveEnsure;
    dst->temporarySaveEnsure = src.temporarySaveEnsure;
    dst->cacheSaveEnsure = src.cacheSaveEnsure;
    dst->bcatSaveEnsure = src.bcatSaveEnsure;
}

inline Result launch(uint64_t title_id, AccountUid uid, LaunchTiming* timing = nullptr
#ifdef SWITCHU_PREFLIGHT_EDGE_TEST
                     , bool diagnosticForceCreateFailure = false
#endif
) {
    if (timing) {
        *timing = {};
        timing->actionStartTick = armGetSystemTick();
        timing->core = svcGetCurrentProcessorNumber();
    }
    switchu::FileLog::log("[app] launch request title=0x%016lX running=%d fg=%d suspended=0x%016lX uid_valid=%d uid[0]=0x%016lX uid[1]=0x%016lX",
                          title_id,
                          g_running ? 1 : 0,
                          g_hasForeground ? 1 : 0,
                          g_suspendedTitleId,
                          accountUidIsValid(&uid) ? 1 : 0,
                          uid.uid[0], uid.uid[1]);

#ifdef SWITCHU_PREFLIGHT_EDGE_TEST
    // Never let the diagnostic close a title the tester forgot to terminate.
    // The ordinary launch path remains untouched and still replaces a running
    // application exactly as before.
    if (diagnosticForceCreateFailure && g_running) {
        switchu::FileLog::log(
            "[diagnostic-preflight-edge] synthetic failure refused while app running title=0x%016lX rc=0x%X",
            title_id, kPreflightEdgeSyntheticCreateFailure);
        return kPreflightEdgeSyntheticCreateFailure;
    }
#endif

#ifdef SWITCHU_PREFLIGHT_MATRIX_TEST
    injectPreflightDiagnosticFault();
#endif
    resetLaunchMetadata();
    const uint64_t preflightCheckTick = armGetSystemTick();
    if (g_prepared.present && timing)
        timing->preflight = g_prepared.timing;

    PreflightRejectReason rejectReason = PreflightRejectReason::None;
    if (!g_prepared.present)
        rejectReason = PreflightRejectReason::Missing;
    else if (g_prepared.titleId != title_id)
        rejectReason = PreflightRejectReason::TitleMismatch;
    else if (std::memcmp(&g_prepared.uid, &uid, sizeof(uid)) != 0)
        rejectReason = PreflightRejectReason::UserMismatch;
    else if (!g_prepared.timing.complete)
        rejectReason = PreflightRejectReason::Incomplete;
    else if (!preparedLaunchFresh(preflightCheckTick))
        rejectReason = PreflightRejectReason::Stale;

    if (g_running) {
        switchu::FileLog::log("[app] closing previous app before launch");
        if (timing)
            timing->previousExitRequestTick = armGetSystemTick();
        appletApplicationRequestExit(&g_app);
        appletApplicationJoin(&g_app);
        if (timing)
            timing->previousJoinedTick = armGetSystemTick();
        appletApplicationClose(&g_app);
        g_running = false;
    }
    appletApplicationClose(&g_app);

    switchu::control_cache::Meta currentMeta{};
    if (rejectReason == PreflightRejectReason::None) {
#ifdef SWITCHU_PREFLIGHT_MATRIX_TEST
        const bool metaLoaded = !g_preflightDiagnosticForceMetadataMissing
            && switchu::control_cache::readMeta(title_id, currentMeta);
#else
        const bool metaLoaded = switchu::control_cache::readMeta(title_id, currentMeta);
#endif
        if (!metaLoaded)
            rejectReason = PreflightRejectReason::MetadataMissing;
        else if (std::memcmp(&currentMeta, &g_prepared.meta, sizeof(currentMeta)) != 0)
            rejectReason = PreflightRejectReason::MetadataChanged;
    }
    const bool usePreflight = rejectReason == PreflightRejectReason::None;
    g_prepared.present = false;
    if (timing) {
        timing->preflight.cacheHit = usePreflight;
        timing->preflight.rejectReason = rejectReason;
    }
    switchu::FileLog::log("[preflight] cache_hit=%d reject=%s",
                          usePreflight ? 1 : 0,
                          preflightRejectReasonName(rejectReason));
#ifdef SWITCHU_PREFLIGHT_MATRIX_TEST
    switchu::FileLog::log(
        "[diagnostic-preflight] result sequence=%u/8 case=%s expected=%s actual=%s pass=%d cache_hit=%d",
        g_preflightDiagnosticActiveSequence,
        preflightDiagnosticCaseName(g_preflightDiagnosticCase),
        preflightRejectReasonName(g_preflightDiagnosticExpectedReason),
        preflightRejectReasonName(rejectReason),
        rejectReason == g_preflightDiagnosticExpectedReason ? 1 : 0,
        usePreflight ? 1 : 0);
    g_preflightDiagnosticForceMetadataMissing = false;
#endif

    if (usePreflight) {
        applyLaunchMetadata(g_prepared.meta);
        copyPreparedWork(timing, g_prepared.work);
    } else {
        // The final command is authoritative. A missing, failed, stale, wrong-
        // user, or metadata-mismatched preflight takes the original safe path.
        touchApplication(title_id, timing);
        ensureApplicationSaveData(title_id, uid, timing);
    }

    if (timing)
        timing->createStartTick = armGetSystemTick();
#ifdef SWITCHU_PREFLIGHT_EDGE_TEST
    if (diagnosticForceCreateFailure) {
        if (timing)
            timing->createEndTick = armGetSystemTick();
        switchu::FileLog::log(
            "[diagnostic-preflight-edge] synthetic CreateApp failure title=0x%016lX rc=0x%X",
            title_id, kPreflightEdgeSyntheticCreateFailure);
        return kPreflightEdgeSyntheticCreateFailure;
    }
#endif
    // The pools as they stand the instant before the game gets a process. This
    // is the moment a console loaded with sysmodules is reported to fail at.
    switchu::daemon::mem::snapshot("before-create-app");
    Result rc = appletCreateApplication(&g_app, title_id);
    if (timing)
        timing->createEndTick = armGetSystemTick();
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[app] CreateApp FAIL: 0x%X", rc);
        return rc;
    }

    struct {
        u32 magic;
        u8  is_selected;
        u8  pad[3];
        AccountUid uid;
        u8  unused[0x70];
    } userArg = {};
    static_assert(sizeof(userArg) == 0x88);

    if (g_lastLaunchAcceptsUser && accountUidIsValid(&uid)) {
        switchu::FileLog::log("[app] preselecting user startup_user=%u option=%u needs_user=%d uid[0]=0x%016lX uid[1]=0x%016lX",
                              (unsigned)g_lastStartupUserAccount,
                              (unsigned)g_lastStartupUserAccountOption,
                              g_lastLaunchNeedsUser ? 1 : 0,
                              uid.uid[0], uid.uid[1]);
        userArg.magic       = 0xC79497CA;
        userArg.is_selected = 1;
        userArg.uid         = uid;

        AppletStorage st;
        rc = appletCreateStorage(&st, sizeof(userArg));
        if (R_SUCCEEDED(rc)) {
            Result writeRc = appletStorageWrite(&st, 0, &userArg, sizeof(userArg));
            if (R_SUCCEEDED(writeRc)) {
                Result pushRc = appletApplicationPushLaunchParameter(&g_app,
                    AppletLaunchParameterKind_PreselectedUser, &st);
                if (R_FAILED(pushRc)) {
                    switchu::FileLog::log("[app] PushUser FAIL: 0x%X", pushRc);
                }
            } else {
                switchu::FileLog::log("[app] PushUser storage write FAIL: 0x%X", writeRc);
            }
            appletStorageClose(&st);
        } else {
            switchu::FileLog::log("[app] PushUser storage create FAIL: 0x%X", rc);
        }
    } else {
        switchu::FileLog::log("[app] launch without preselected user (accepts_user=%d needs_user=%d startup_user=%u option=%u uid_valid=%d)",
                              g_lastLaunchAcceptsUser ? 1 : 0,
                              g_lastLaunchNeedsUser ? 1 : 0,
                              (unsigned)g_lastStartupUserAccount,
                              (unsigned)g_lastStartupUserAccountOption,
                              accountUidIsValid(&uid) ? 1 : 0);
    }

    appletUnlockForeground();

    switchu::FileLog::log("[app] Start call");
    if (timing)
        timing->startStartTick = armGetSystemTick();
    rc = appletApplicationStart(&g_app);
    if (timing)
        timing->startEndTick = armGetSystemTick();
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[app] Start FAIL: 0x%X", rc);
        appletApplicationClose(&g_app);
        return rc;
    }
    switchu::FileLog::log("[app] Start ok");
    switchu::daemon::mem::snapshot("game-started");

    if (timing)
        timing->foregroundStartTick = armGetSystemTick();
    rc = appletApplicationRequestForApplicationToGetForeground(&g_app);
    if (timing)
        timing->foregroundEndTick = armGetSystemTick();
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[app] ReqFG FAIL: 0x%X", rc);
        appletApplicationClose(&g_app);
        return rc;
    }
    switchu::FileLog::log("[app] ReqFG ok");

    g_running = true;
    g_hasForeground = true;
    g_suspendedTitleId = title_id;
    switchu::FileLog::log("[app] launched 0x%016lX", title_id);
    return 0;
}

inline Result resume(ResumeTiming* timing = nullptr
#ifdef SWITCHU_RESUME_FAILURE_TEST
                     , bool diagnosticForceForegroundFailure = false
#endif
) {
    if (!g_running) return MAKERESULT(Module_Libnx, 0xFE);
    if (timing) {
        *timing = {};
        timing->actionStartTick = armGetSystemTick();
        timing->core = svcGetCurrentProcessorNumber();
    }
    switchu::FileLog::log("[app] resume request fg=%d suspended=0x%016lX",
                          g_hasForeground ? 1 : 0, g_suspendedTitleId);
    if (timing)
        timing->unlockStartTick = armGetSystemTick();
    const Result unlockRc = appletUnlockForeground();
    if (timing) {
        timing->unlockEndTick = armGetSystemTick();
        timing->unlockResult = unlockRc;
    }
    switchu::FileLog::log("[app] resume UnlockForeground rc=0x%X", unlockRc);
    if (timing)
        timing->foregroundStartTick = armGetSystemTick();
#ifdef SWITCHU_RESUME_FAILURE_TEST
    if (diagnosticForceForegroundFailure) {
        if (timing)
            timing->foregroundEndTick = armGetSystemTick();
        g_hasForeground = false;
        switchu::FileLog::log(
            "[diagnostic-resume] synthetic ReqFG failure rc=0x%X unlock_rc=0x%X running=%d suspended=0x%016lX",
            kResumeSyntheticForegroundFailure, unlockRc,
            g_running ? 1 : 0, g_suspendedTitleId);
        return kResumeSyntheticForegroundFailure;
    }
#endif
    Result rc = appletApplicationRequestForApplicationToGetForeground(&g_app);
    if (timing)
        timing->foregroundEndTick = armGetSystemTick();
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[app] resume ReqFG FAIL: 0x%X", rc);
        g_hasForeground = false;
    } else {
        switchu::FileLog::log("[app] resume ReqFG ok");
        g_hasForeground = true;
    }
    return rc;
}

inline Result beginTerminate(
#ifdef SWITCHU_TERMINATION_QUEUE_TEST
    TerminateDiagnosticMode diagnosticMode = TerminateDiagnosticMode::None
#endif
) {
    if (!g_running) {
        switchu::FileLog::log("[app] terminate request ignored: no running application");
        return 0;
    }
    if (isTerminating()) {
        switchu::FileLog::log("[app] terminate request already pending phase=%u",
                              static_cast<unsigned>(g_terminatePhase));
        return 0;
    }

    g_terminateTiming = {};
    g_terminateTiming.beginTick = armGetSystemTick();
    g_terminateTiming.beginCore = svcGetCurrentProcessorNumber();
    g_terminatePhase = TerminatePhase::Graceful;
#ifdef SWITCHU_TERMINATION_QUEUE_TEST
    g_terminateDiagnosticMode = diagnosticMode;
#endif

    switchu::FileLog::log("[app] terminate begin app=0x%016lX fg=%d",
                          g_suspendedTitleId, g_hasForeground ? 1 : 0);
#ifdef SWITCHU_TERMINATION_QUEUE_TEST
    bool hadLibraryApplet = false;
    const Result libraryProbeRc =
        appletApplicationAreAnyLibraryAppletsLeft(&g_app, &hadLibraryApplet);
    switchu::FileLog::log(
        "[diagnostic] terminate library-applet-before active=%d rc=0x%X",
        hadLibraryApplet ? 1 : 0, libraryProbeRc);
#endif
    g_terminateTiming.libraryRc = appletApplicationTerminateAllLibraryApplets(&g_app);
    g_terminateTiming.libraryEndTick = armGetSystemTick();
    switchu::FileLog::log("[app] terminate TerminateAllLibraryApplets rc=0x%X",
                          g_terminateTiming.libraryRc);

    g_terminateTiming.requestRc = appletApplicationRequestExit(&g_app);
    g_terminateTiming.requestEndTick = armGetSystemTick();
    const uint64_t graceNs = g_terminateGraceOverrideNs ? g_terminateGraceOverrideNs : kTerminateGraceNs;
    g_terminateGraceOverrideNs = 0;
    g_terminateDeadlineTick = g_terminateTiming.requestEndTick + armNsToTicks(graceNs);
    switchu::FileLog::log("[app] terminate RequestExit rc=0x%X; polling for up to %llums",
                          g_terminateTiming.requestRc, (unsigned long long)(graceNs / 1'000'000ULL));
#ifdef SWITCHU_TERMINATION_QUEUE_TEST
    if (g_terminateDiagnosticMode == TerminateDiagnosticMode::HoldForSleepWake) {
        switchu::FileLog::log("[diagnostic] terminate mode=sleep-wake hold_ms=%llu",
                              (unsigned long long)(kTerminateSleepWakeHoldNs / 1'000'000ULL));
    } else if (g_terminateDiagnosticMode == TerminateDiagnosticMode::ForceAtDeadline) {
        switchu::FileLog::log("[diagnostic] terminate mode=force-at-deadline deadline_ms=%llu",
                              (unsigned long long)(kTerminateGraceNs / 1'000'000ULL));
    }
#endif
    return 0;
}

// Same as beginTerminate(), but gives up on a graceful exit after graceNs. For
// titles known to ignore RequestExit, where the usual 15 s is only a wait.
inline Result beginTerminateWithin(uint64_t graceNs) {
    if (!g_running || isTerminating())
        return beginTerminate();
    g_terminateGraceOverrideNs = graceNs;
    const Result rc = beginTerminate();
    g_terminateGraceOverrideNs = 0;
    return rc;
}

static inline uint64_t terminateTickDeltaUs(uint64_t start, uint64_t end) {
    if (start == 0 || end == 0 || end < start)
        return 0;
    return armTicksToNs(end - start) / 1'000ULL;
}

inline bool pollTerminate() {
    if (!isTerminating())
        return false;

    ++g_terminateTiming.polls;
#ifdef SWITCHU_TERMINATION_QUEUE_TEST
    const uint64_t now = armGetSystemTick();
    const uint64_t sleepWakeReleaseTick =
        g_terminateTiming.requestEndTick + armNsToTicks(kTerminateSleepWakeHoldNs);
    if (g_terminateDiagnosticMode == TerminateDiagnosticMode::HoldForSleepWake &&
        now < sleepWakeReleaseTick)
        return false;
    // The controlled force test suppresses only the observation of a
    // cooperatively signalled event. At the real 15-second deadline it sends
    // the same AM force command as production, then lets the next poll join.
    const bool suppressFinished =
        g_terminateDiagnosticMode == TerminateDiagnosticMode::ForceAtDeadline &&
        g_terminatePhase == TerminatePhase::Graceful;
#else
    constexpr bool suppressFinished = false;
#endif
    if (!suppressFinished && appletApplicationCheckFinished(&g_app)) {
        // CheckFinished observed the non-autoclear state event, so Join cannot
        // block here; it only consumes GetResult and records the exit reason.
        appletApplicationJoin(&g_app);
        g_terminateTiming.completeTick = armGetSystemTick();
        g_terminateTiming.completeCore = svcGetCurrentProcessorNumber();
        g_terminateTiming.exitReason = appletApplicationGetExitReason(&g_app);
        g_lastFinishedTick = g_terminateTiming.completeTick;

        switchu::FileLog::log(
            "[trace-terminate] begin_to_complete_us=%llu library_ipc_us=%llu request_ipc_us=%llu grace_us=%llu force_to_complete_us=%llu polls=%u forced=%d library_rc=0x%X request_rc=0x%X force_rc=0x%X reason=%u cores=%u/%u",
            (unsigned long long)terminateTickDeltaUs(g_terminateTiming.beginTick,
                                                     g_terminateTiming.completeTick),
            (unsigned long long)terminateTickDeltaUs(g_terminateTiming.beginTick,
                                                     g_terminateTiming.libraryEndTick),
            (unsigned long long)terminateTickDeltaUs(g_terminateTiming.libraryEndTick,
                                                     g_terminateTiming.requestEndTick),
            (unsigned long long)terminateTickDeltaUs(
                g_terminateTiming.requestEndTick,
                g_terminateTiming.forced ? g_terminateTiming.forceTick
                                         : g_terminateTiming.completeTick),
            (unsigned long long)terminateTickDeltaUs(g_terminateTiming.forceTick,
                                                     g_terminateTiming.completeTick),
            g_terminateTiming.polls,
            g_terminateTiming.forced ? 1 : 0,
            g_terminateTiming.libraryRc,
            g_terminateTiming.requestRc,
            g_terminateTiming.forceRc,
            static_cast<unsigned>(g_terminateTiming.exitReason),
            g_terminateTiming.beginCore,
            g_terminateTiming.completeCore);

        appletApplicationClose(&g_app);
        g_running = false;
        g_hasForeground = false;
        g_suspendedTitleId = 0;
        g_terminatePhase = TerminatePhase::Idle;
        g_terminateDeadlineTick = 0;
#ifdef SWITCHU_TERMINATION_QUEUE_TEST
        g_terminateDiagnosticMode = TerminateDiagnosticMode::None;
#endif
        return true;
    }

#ifndef SWITCHU_TERMINATION_QUEUE_TEST
    const uint64_t now = armGetSystemTick();
#endif
    if (g_terminatePhase == TerminatePhase::Graceful &&
        now >= g_terminateDeadlineTick) {
        g_terminateTiming.forceTick = now;
        g_terminateTiming.forced = true;
        g_terminateTiming.forceRc = appletApplicationTerminate(&g_app);
        g_terminatePhase = TerminatePhase::Forced;
        switchu::FileLog::log("[app] terminate grace expired; force rc=0x%X",
                              g_terminateTiming.forceRc);
    }
    return false;
}

inline bool checkFinished() {
    // pollTerminate owns the same non-autoclear state event while a requested
    // termination is active. Never let the ordinary natural-exit path consume
    // and close it first.
    if (!g_running || isTerminating()) return false;
    if (appletApplicationCheckFinished(&g_app)) {
        g_lastFinishedTick = armGetSystemTick();
        switchu::FileLog::log("[app] finished (reason=%d)",
            (int)appletApplicationGetExitReason(&g_app));
        appletApplicationJoin(&g_app);
        appletApplicationClose(&g_app);
        g_running = false;
        g_hasForeground = false;
        g_suspendedTitleId = 0;
        return true;
    }
    return false;
}

inline void onHomeSuspend() {
    switchu::FileLog::log("[app] onHomeSuspend fg %d -> 0 app=0x%016lX",
                          g_hasForeground ? 1 : 0, g_suspendedTitleId);
    g_hasForeground = false;
}

inline void cleanup() {
    if (g_running) {
        appletApplicationRequestExit(&g_app);
        appletApplicationJoin(&g_app);
        appletApplicationClose(&g_app);
        g_running = false;
    }
    g_terminatePhase = TerminatePhase::Idle;
    g_terminateDeadlineTick = 0;
#ifdef SWITCHU_TERMINATION_QUEUE_TEST
    g_terminateDiagnosticMode = TerminateDiagnosticMode::None;
#endif
}

}
