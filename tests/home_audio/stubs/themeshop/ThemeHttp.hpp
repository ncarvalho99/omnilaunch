#pragma once
// Host fake for the HTTP module: the real cancel scope (header-only), fake
// runtime. Requests are modelled by tokens taken from the shared scope.
#include "themeshop/ThemeHttpScope.hpp"
#include "bluetooth/BluetoothManager.hpp"
namespace themeshop::http {
inline CancelScope scope;
inline int shutdownCalls = 0;
inline void shutdown() { ++shutdownCalls; fake::serviceOrder.push_back("http"); }
inline void cancelPendingRequests() { scope.cancelTerminal(); }
inline void openFrontendScope() { scope.openFrontend(); }
inline void closeFrontendScope() { scope.closeFrontend(); }
}
