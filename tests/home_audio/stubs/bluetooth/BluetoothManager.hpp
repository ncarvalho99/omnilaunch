#pragma once
// Host fake: counts lifecycle calls and their order.
#include <string>
#include <vector>
namespace fake { inline std::vector<std::string> serviceOrder; }
namespace bluetooth {
inline int initializeCalls = 0;
inline int finalizeCalls = 0;
inline void Initialize() { ++initializeCalls; }
inline void Finalize() { ++finalizeCalls; fake::serviceOrder.push_back("bluetooth"); }
inline bool IsAvailable() { return initializeCalls > finalizeCalls; }
}
