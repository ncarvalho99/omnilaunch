#pragma once

#include <cstdint>
#include <algorithm>

#ifdef __SWITCH__
#include <switch.h>
#include <switch/services/ts.h>
#include <switch/services/tc.h>
#include <switch/services/clkrst.h>
#endif

namespace switchu {

struct ThermalReading {
    float soc = -1.0f;
    float pcb = -1.0f;
    bool hasSoc = false;
    bool hasPcb = false;
};

inline ThermalReading getThermalReading() {
    ThermalReading reading{};
#ifdef __SWITCH__
    s32 soc = 0;
    s32 pcb = 0;

    if (R_SUCCEEDED(tsInitialize())) {
        if (hosversionAtLeast(14, 0, 0)) {
            TsSession s{};
            if (R_SUCCEEDED(tsOpenSession(&s, TsDeviceCode_LocationExternal))) {
                float t = 0.f;
                if (R_SUCCEEDED(tsSessionGetTemperature(&s, &t))) {
                    reading.soc = t;
                    reading.hasSoc = true;
                }
                tsSessionClose(&s);
            }
            if (R_SUCCEEDED(tsOpenSession(&s, TsDeviceCode_LocationInternal))) {
                float t = 0.f;
                if (R_SUCCEEDED(tsSessionGetTemperature(&s, &t))) {
                    reading.pcb = t;
                    reading.hasPcb = true;
                }
                tsSessionClose(&s);
            }
        } else {
            if (R_SUCCEEDED(tsGetTemperature(TsLocation_External, &soc))) {
                reading.soc = static_cast<float>(soc);
                reading.hasSoc = true;
            }
            if (R_SUCCEEDED(tsGetTemperature(TsLocation_Internal, &pcb))) {
                reading.pcb = static_cast<float>(pcb);
                reading.hasPcb = true;
            }
        }
        tsExit();
    }

    if (!reading.hasPcb && R_SUCCEEDED(tcInitialize())) {
        s32 skin = 0;
        if (R_SUCCEEDED(tcGetSkinTemperatureMilliC(&skin))) {
            reading.pcb = static_cast<float>(skin) / 1000.f;
            reading.hasPcb = true;
        }
        tcExit();
    }
#else
    reading.soc = 42.0f;
    reading.pcb = 36.5f;
    reading.hasSoc = true;
    reading.hasPcb = true;
#endif
    return reading;
}

inline uint32_t getGpuClockMhz() {
#ifdef __SWITCH__
    if (!hosversionAtLeast(8, 0, 0) || R_FAILED(clkrstInitialize())) {
        return 0;
    }
    uint32_t mhz = 0;
    ClkrstSession s{};
    if (R_SUCCEEDED(clkrstOpenSession(&s, PcvModuleId_GPU, 3))) {
        u32 hz = 0;
        if (R_SUCCEEDED(clkrstGetClockRate(&s, &hz))) {
            mhz = hz / 1000000u;
        }
        clkrstCloseSession(&s);
    }
    clkrstExit();
    return mhz;
#else
    return 768;
#endif
}

} // namespace switchu
