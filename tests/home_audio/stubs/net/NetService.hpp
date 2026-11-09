#pragma once
// Host fake: NetService lifetime only (G1); records its place in the Services phase.
#include "bluetooth/BluetoothManager.hpp"
namespace net {
class NetService {
public:
    int shutdownCalls = 0;
    void shutdown() { ++shutdownCalls; fake::serviceOrder.push_back("net"); }
};
}
