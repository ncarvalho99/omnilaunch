#pragma once

namespace net { class NetService; }

namespace nextendo {

class NextendoService {
public:
    void update(net::NetService&, bool) {}
    int totalOnline() const { return 0; }
    int countForTitle(unsigned long long) const { return 0; }
    bool hasData() const { return false; }
};

} // namespace nextendo
