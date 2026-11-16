#pragma once

namespace home { class HomeCore; }

namespace discord {

class DiscordService {
public:
    void update(home::HomeCore&) {}
    bool asleep = false;
    int terminalStops = 0;
    void sleep() { asleep = true; }
    void wake() { asleep = false; }
    void beginShutdown() { ++terminalStops; }
    void shutdown() {}
};

} // namespace discord
