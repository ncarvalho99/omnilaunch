#pragma once
class DebugLog { public: template<class... T> static void log(const char*, T...) {} };
