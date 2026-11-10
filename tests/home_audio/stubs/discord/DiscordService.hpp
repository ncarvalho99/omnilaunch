#pragma once

namespace home { class HomeCore; }

namespace discord {

class DiscordService {
public:
    void update(home::HomeCore&) {}
    void beginShutdown() {}
    void shutdown() {}
};

} // namespace discord
