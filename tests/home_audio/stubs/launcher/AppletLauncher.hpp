#pragma once
#include <functional>
#include <cstdint>

class AppletLauncher {
public:
    struct Callbacks {
        std::function<void()> playSfxModalHide;
        std::function<void()> requestExit;
        std::function<void()> quiesceWriters;
    };

    void init(Callbacks) {}
    void setStartupStatus(uint64_t, bool) {}
};
