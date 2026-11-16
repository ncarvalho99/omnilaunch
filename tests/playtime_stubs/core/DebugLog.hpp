#pragma once
// Silent sink for host-only coordinator tests; never writes SD/log paths.
class DebugLog { public: template<class... T> static void log(const char*, T...) {} };
