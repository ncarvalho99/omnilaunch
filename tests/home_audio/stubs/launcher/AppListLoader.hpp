#pragma once
// Host fake: records the shutdown order and the cleared frontend transform.
#include "bluetooth/BluetoothManager.hpp"
#include <functional>
#include <vector>
struct PendingApp {};
class AppListLoader {
public:
 using PendingTransform = std::function<void(std::vector<PendingApp>&)>;
 PendingTransform transform;
 void setPendingTransform(PendingTransform t) { transform = std::move(t); }
 void waitIdle() { fake::serviceOrder.push_back("catalogue"); }
};
