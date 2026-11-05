#pragma once
#include <nxui/core/Types.hpp>
#include <string>
namespace nxui { class Font { public: Vec2 measure(const std::string& s) const { return {float(s.size()*10),20}; } }; }
