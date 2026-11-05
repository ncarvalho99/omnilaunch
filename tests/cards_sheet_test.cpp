// Production CardsSheet input/lifecycle test with renderer/input-only fakes.
#include "cards/CardsSheet.hpp"
#include <nxui/core/Input.hpp>
#include <cassert>
#include <cstdio>
using namespace cards;
int main() {
 CardsSheet sheet; nxui::Input in;
 CardsSheet::Row row; row.label="Initial";
 int activated=0, changed=0;
 row.onA=[&]{++activated; CardsSheet::Row replacement; replacement.label="Reopened";
              sheet.open("Other", {{"New", {replacement}}});};
 sheet.open("Initial", {{"One", {row}}, {"Two", {row}}});
 in.down={nxui::Button::A}; sheet.handleInput(in,1.f/60);
 assert(activated==1 && sheet.title()=="Other"); // row copied before self-replacement
 sheet.open("Two tabs", {{"One", {row}}, {"Two", {row}}});
 sheet.onReorderSection([&](int d){changed+=d;});
 in.down={nxui::Button::Y}; sheet.handleInput(in,1.f/60); assert(changed==1);
 in.down={nxui::Button::R}; sheet.handleInput(in,1.f/60); assert(sheet.section()==1);
 in.down={nxui::Button::B}; sheet.handleInput(in,1.f/60); assert(!sheet.isOpen());
 sheet.open("Reset",{{"One",{row}},{"Two",{row}}});
 in.down={nxui::Button::Y}; sheet.handleInput(in,1.f/60); assert(changed==1); // reorder hook reset
 in.down.clear(); in.touch=true; in.x=300;in.y=225;sheet.update(1);sheet.handleInput(in,1.f/60);
 assert(activated==2); // selected-row touch uses same self-replacement contract
 std::puts("cards_sheet_test: PASS");
}
