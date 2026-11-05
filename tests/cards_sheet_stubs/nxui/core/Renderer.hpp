#pragma once
#include <nxui/core/Types.hpp>
namespace nxui { class Renderer { public:
void drawRect(const Rect&,const Color&) {}
void drawRoundedRect(const Rect&,const Color&,float) {}
void drawRoundedRectOutline(const Rect&,const Color&,float,float) {}
void drawCircle(const Vec2&,float,const Color&,int) {}
void drawTriangle(const Vec2&,const Vec2&,const Vec2&,const Color&) {}
}; }
