#include "settings/SystemPrefs.hpp"
#include "smi_commands.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace settings::system_prefs {

int tvResolution() {
    SetSysTvSettings tv{};
    if (R_FAILED(setsysGetTvSettings(&tv)))
        return 0;
    return std::clamp(static_cast<int>(tv.tv_resolution), 0, kTvResolutionCount - 1);
}

bool setTvResolution(int value) {
    SetSysTvSettings tv{};
    if (R_FAILED(setsysGetTvSettings(&tv)))
        return false;
    tv.tv_resolution = static_cast<s32>(std::clamp(value, 0, kTvResolutionCount - 1));
    return R_SUCCEEDED(setsysSetTvSettings(&tv));
}

float vibrationVolume() {
    float vol = 1.f;
    if (R_FAILED(setsysGetVibrationMasterVolume(&vol)))
        return 1.f;
    return std::clamp(vol, 0.f, 1.f);
}

bool setVibrationVolume(float value) {
    return R_SUCCEEDED(setsysSetVibrationMasterVolume(std::clamp(value, 0.f, 1.f)));
}

std::vector<std::string> timezoneNames() {
    std::vector<std::string> out;
    s32 total = 0;
    if (R_FAILED(timeGetTotalLocationNameCount(&total)) || total <= 0)
        return out;
    out.reserve(static_cast<size_t>(total));
    constexpr s32 kChunk = 64;
    TimeLocationName buf[kChunk];
    for (s32 index = 0; index < total;) {
        s32 got = 0;
        if (R_FAILED(timeLoadLocationNameList(index, buf, kChunk, &got)) || got <= 0)
            break;
        for (s32 i = 0; i < got; ++i)
            if (buf[i].name[0] != '\0')
                out.emplace_back(buf[i].name);
        index += got;
    }
    return out;
}

std::string currentTimezone() {
    TimeLocationName current{};
    if (R_FAILED(timeGetDeviceLocationName(&current)))
        return {};
    return current.name;
}

Result setTimezone(const std::string& name) {
    TimeLocationName loc{};
    std::snprintf(loc.name, sizeof(loc.name), "%s", name.c_str());
    return timeSetDeviceLocationName(&loc);
}

bool currentDateTime(DateTime& value) {
    u64 timestamp = 0;
    TimeCalendarTime calendar{};
    TimeCalendarAdditionalInfo additional{};
    if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &timestamp)) ||
        R_FAILED(timeToCalendarTimeWithMyRule(timestamp, &calendar, &additional)))
        return false;
    value.year = calendar.year;
    value.month = calendar.month;
    value.day = calendar.day;
    value.hour = calendar.hour;
    value.minute = calendar.minute;
    return true;
}

Result setManualDateTime(const DateTime& value) {
    switchu::smi::ManualDateTimeArgs args{};
    args.year = static_cast<uint32_t>(value.year);
    args.month = static_cast<uint32_t>(value.month);
    args.day = static_cast<uint32_t>(value.day);
    args.hour = static_cast<uint32_t>(value.hour);
    args.minute = static_cast<uint32_t>(value.minute);
    return switchu::menu::smi_cmd::setManualDateTime(args);
}

bool internetTimeSync() {
    bool on = false;
    return R_SUCCEEDED(setsysIsUserSystemClockAutomaticCorrectionEnabled(&on)) && on;
}

namespace {
constexpr const char* kDockedNativeFlag = "sdmc:/config/OmniLaunch/docked_1080p";
}

bool dockedNativeRender() {
    std::error_code ec;
    return std::filesystem::exists(kDockedNativeFlag, ec);
}

bool setDockedNativeRender(bool enabled) {
    std::error_code ec;
    if (!enabled)
        return std::filesystem::remove(kDockedNativeFlag, ec) || !std::filesystem::exists(kDockedNativeFlag, ec);
    std::filesystem::create_directories("sdmc:/config/OmniLaunch", ec);
    std::ofstream(kDockedNativeFlag) << "1";
    return dockedNativeRender();
}

} // namespace settings::system_prefs
