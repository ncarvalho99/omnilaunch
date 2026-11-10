#pragma once

namespace home { class HomeCore; }

namespace nextendo {

class NextendoService {
public:
    void update(home::HomeCore&, bool) {}
    int totalOnline() const { return 0; }
    int countForTitle(unsigned long long) const { return 0; }
    bool hasData() const { return false; }
};

} // namespace nextendo
