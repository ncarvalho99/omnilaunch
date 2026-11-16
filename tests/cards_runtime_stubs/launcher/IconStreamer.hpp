#pragma once
#include <cstdint>
#include <vector>
class IconStreamer { public: struct DecodedIcon { std::vector<std::uint8_t> rgba; int w=0,h=0; };
static DecodedIcon decodeIconData(const std::vector<std::uint8_t>&) { return {}; } };
