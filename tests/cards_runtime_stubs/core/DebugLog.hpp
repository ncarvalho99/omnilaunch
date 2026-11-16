#pragma once
struct DebugLog { template<class... A> static void log(const char*,A...) {} };
