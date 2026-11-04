#pragma once
// Host fake: records the shutdown order.
#include "bluetooth/BluetoothManager.hpp"
class SteamGridDbManager {
public:
 int cancels = 0;
 void cancelAndWait() { ++cancels; fake::serviceOrder.push_back("steamgriddb"); }
};
