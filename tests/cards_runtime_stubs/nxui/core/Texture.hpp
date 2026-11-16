#pragma once
#include <cstdint>
#include <vector>
#include <string>
namespace nxui {
class GpuDevice; class Renderer;
struct DecodedImage { std::vector<std::uint8_t> rgba; int width=0,height=0;
bool valid() const { return width>0 && height>0 && rgba.size()>=std::size_t(width)*height*4; } };
class Texture { bool ok=false; public:
bool valid() const { return ok; }
bool loadFromPixels(GpuDevice&,Renderer&,const std::uint8_t*,int w,int h) { return ok=w>0&&h>0; }
bool loadFromDecoded(GpuDevice& g,Renderer& r,const DecodedImage& d) { return loadFromPixels(g,r,d.rgba.data(),d.width,d.height); }
static DecodedImage decodeFile(const std::string&,int) { return {}; }
}; }
