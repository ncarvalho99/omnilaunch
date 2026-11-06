// See HekateReboot.hpp for the owner-authorized bpc:ams exception.
// Payload handling adapted from HomeNX (GPL-3.0) @ 0448cd4,
// projects/cards/src/Library.cpp:1887-1980 (canRebootToPayload, Power::Hekate).
#include "launcher/HekateReboot.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <cstdio>
#include <cstring>

#ifdef SWITCHU_MENU
#include <switch.h>
#include <switchu/hekate_rtc.hpp>
#endif

namespace hekate {

#ifdef SWITCHU_MENU
namespace {

constexpr size_t kPayloadMax = 0x24000; // Atmosphere IramPayloadMaxSize
alignas(0x1000) u8 g_payload[kPayloadMax];

bool canRebootToPayload() {
    SetSysProductModel model = SetSysProductModel_Invalid;
    setsysGetProductModel(&model); // setsys is open for the process lifetime
    return model == SetSysProductModel_Nx || model == SetSysProductModel_Copper ||
           model == SetSysProductModel_Invalid;
}

} // namespace

bool prepareMenuReboot(std::string& error) {
    auto& i18n = nxui::I18n::instance();
    if (!canRebootToPayload()) {
        const bool ok = switchu::hekate_rtc::requestMenuOnNextBoot();
        DebugLog::log("[hekate] rtc reboot reason set ok=%d", ok ? 1 : 0);
        if (!ok)
            error = i18n.tr("power.hekate_rtc_failed", "The console did not accept the request for Hekate's menu.");
        return ok;
    }
    const char* path = nullptr;
    for (const char* p : {"sdmc:/bootloader/update.bin", "sdmc:/atmosphere/reboot_payload.bin"}) {
        if (FILE* f = std::fopen(p, "rb")) {
            std::fclose(f);
            path = p;
            break;
        }
    }
    if (!path) {
        error = i18n.tr("power.hekate_missing", "Hekate was not found on the SD card.");
        return false;
    }
    std::memset(g_payload, 0, sizeof(g_payload));
    size_t got = 0;
    if (FILE* f = std::fopen(path, "rb")) {
        got = std::fread(g_payload, 1, sizeof(g_payload), f);
        std::fclose(f);
    }
    if (got <= 0x98) {
        error = i18n.tr("power.hekate_read_failed", "The Hekate payload could not be read.");
        return false;
    }
    // Hekate's boot storage: 0x94 boot_cfg bit 0 (force autoboot) with 0x95
    // autoboot 0 opens its menu instead of booting straight back. Only when the
    // payload leaves the area empty.
    if (!g_payload[0x94] && !g_payload[0x95] && !g_payload[0x96] && !g_payload[0x97]) {
        g_payload[0x94] = 0x01;
        g_payload[0x95] = 0x00;
    }
    Handle port = 0;
    Result rc = svcConnectToNamedPort(&port, "bpc:ams");
    if (R_SUCCEEDED(rc)) {
        Service bpcAms;
        serviceCreate(&bpcAms, port);
        rc = serviceDispatch(&bpcAms, 65001,
                             .buffer_attrs = {SfBufferAttr_In | SfBufferAttr_HipcMapAlias},
                             .buffers = {{g_payload, sizeof(g_payload)}}, );
        serviceClose(&bpcAms);
    }
    DebugLog::log("[hekate] payload %s (%zu bytes) staged rc=0x%X", path, got, rc);
    if (R_FAILED(rc)) {
        char code[16];
        std::snprintf(code, sizeof(code), "0x%X", rc);
        error = i18n.tr("power.hekate_payload_failed", "Atmosphere did not accept the Hekate payload.") +
                std::string(" (") + code + ")";
        return false;
    }
    return true;
}
#else
bool prepareMenuReboot(std::string& error) {
    error = "Not available in this build.";
    return false;
}
#endif

} // namespace hekate
