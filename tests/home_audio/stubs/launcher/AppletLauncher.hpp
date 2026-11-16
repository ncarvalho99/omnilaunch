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

    Callbacks callbacks;
    void init(Callbacks cb) { callbacks = std::move(cb); }
    void enterSleep() { if (callbacks.quiesceWriters) callbacks.quiesceWriters(); }
    void setStartupStatus(uint64_t, bool) {}
};
