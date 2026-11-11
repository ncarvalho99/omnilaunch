#pragma once
#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>
namespace cards { class CardsChrome { nxui::Font f; public:
inline static const nxui::Color kWhite{1,1,1,1}, kGreen{0,1,0,1};
static nxui::Color accent(float a) { return {0,0,1,a}; }
static nxui::Color glassTint(float a = 1.f) { return {0.1f,0.1f,0.15f,0.5f*a}; }
nxui::Font& iconFont(){return f;} nxui::Font& titleFont(){return f;} nxui::Font& labelFont(){return f;} nxui::Font& smallFont(){return f;}
std::string fit(nxui::Font&,const std::string& s,float){return s;}
void textMid(nxui::Renderer&,nxui::Font&,const std::string&,float,float,const nxui::Color&) {}
void icon(nxui::Renderer&,const char*,float,float,float,const nxui::Color&) {}
}; }
