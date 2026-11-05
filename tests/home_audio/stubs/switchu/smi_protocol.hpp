#pragma once
#include <cstdint>

namespace switchu::smi {

enum class MenuTransitionReason : uint32_t {
    Unknown = 0,
    StartupBoot = 1,
    HomeRequest = 2,
    ApplicationFinished = 3,
};

enum class MenuStartMode : uint32_t {
    MainMenu = 0,
    StartupBoot = 1,
};

constexpr uint32_t kStatusFlagSafeMode = (1u << 0);
constexpr uint32_t kStatusFlagSafeModeNotice = (1u << 1);

struct SystemStatus {
    uint64_t suspended_app_id = 0;
    uint8_t selected_user[16] = {};
    bool app_running = false;
    uint8_t _pad[7] = {};
    uint64_t transition_origin_tick = 0;
    MenuTransitionReason transition_reason = MenuTransitionReason::Unknown;
    uint32_t boot_flags = 0;
    uint32_t _pad2 = 0;
};

} // namespace switchu::smi
