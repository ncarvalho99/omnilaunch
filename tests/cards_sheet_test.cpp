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
 // Settings enter on tabs, B returns from adjustable rows without closing.
 CardsSheet::Row choice; choice.label="Choice";
 choice.onChange=[&](int d){changed+=d;};
 CardsSheet::Section home{"Home",{choice},"home"};
 CardsSheet::Section look{"Appearance",{choice},"sun"};
 sheet.open("Settings",{home,look}); in.touch=false; in.down.clear();
 in.held={nxui::Button::DDown}; in.down={nxui::Button::DDown};
 sheet.handleInput(in,1.f/60); assert(sheet.section()==1);
 in.held.clear(); in.down={nxui::Button::A}; sheet.handleInput(in,1.f/60);
 const int before=changed;
 in.down={nxui::Button::DLeft}; sheet.handleInput(in,1.f/60); assert(changed==before-1);
 // Refreshing settings after changing a value must preserve row focus.
 sheet.open("Settings",{home,look},sheet.section(),sheet.row());
 in.down={nxui::Button::DRight}; sheet.handleInput(in,1.f/60); assert(changed==before);
 in.down={nxui::Button::B}; sheet.handleInput(in,1.f/60); assert(sheet.isOpen());
 in.down={nxui::Button::DUp}; sheet.handleInput(in,1.f/60); assert(sheet.section()==0);
 in.down={nxui::Button::B}; sheet.handleInput(in,1.f/60); assert(!sheet.isOpen());
 // Touch can enter content directly from tabs.
 sheet.open("Settings",{home,look}); sheet.update(1);
 in.down.clear(); in.touch=true; in.x=450; in.y=140;
 sheet.handleInput(in,1.f/60); assert(changed==before+1);
 in.touch=false; in.down={nxui::Button::B}; sheet.handleInput(in,1.f/60); assert(sheet.isOpen());
 in.down={nxui::Button::B}; sheet.handleInput(in,1.f/60); assert(!sheet.isOpen());
 std::puts("cards_sheet_test: PASS");
}
