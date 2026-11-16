#include "core/PlayTimeEvents.hpp"
#include <cassert>
#include <iostream>
using namespace switchu::menu::playtime;
int main() {
    EventTotals totals({11, 22});
    totals.push({PlayEvent::Kind::Launch, 1, {}, 100});
    totals.push({PlayEvent::Kind::Account, 0, {11,22}, 101});
    totals.push({PlayEvent::Kind::InFocus, 1, {}, 110});
    totals.push({PlayEvent::Kind::OutOfFocus, 1, {}, 140});
    totals.push({PlayEvent::Kind::InFocus, 1, {}, 150});
    totals.push({PlayEvent::Kind::OutOfFocus, 1, {}, 170});
    totals.push({PlayEvent::Kind::Exit, 1, {}, 171});
    assert(totals.values().at(1).seconds == 50);
    assert(totals.values().at(1).launches == 1);
    totals.push({PlayEvent::Kind::Launch, 1, {}, 200});
    totals.push({PlayEvent::Kind::Account, 0, {33,44}, 201});
    totals.push({PlayEvent::Kind::InFocus, 1, {}, 210});
    totals.push({PlayEvent::Kind::OutOfFocus, 1, {}, 300});
    totals.push({PlayEvent::Kind::Exit, 1, {}, 301});
    assert(totals.values().at(1).seconds == 50); // another user's play is excluded
    assert(totals.values().at(1).launches == 1);
    // Unclosed sessions and mismatched applet events cannot contribute.
    totals.push({PlayEvent::Kind::Launch, 2, {}, 400});
    totals.push({PlayEvent::Kind::Account, 0, {11,22}, 401});
    totals.push({PlayEvent::Kind::InFocus, 2, {}, 410});
    totals.push({PlayEvent::Kind::OutOfFocus, 99, {}, 510});
    totals.push({PlayEvent::Kind::Exit, 99, {}, 511});
    assert(totals.values().count(2) == 0);
    totals.push({PlayEvent::Kind::OutOfFocus, 2, {}, 430});
    totals.push({PlayEvent::Kind::Exit, 2, {}, 431});
    assert(totals.values().at(2).seconds == 20);
    // Duplicate InFocus must not lose the earlier time or count it twice.
    totals.push({PlayEvent::Kind::Launch, 3, {}, 600});
    totals.push({PlayEvent::Kind::Account, 0, {11,22}, 601});
    totals.push({PlayEvent::Kind::InFocus, 3, {}, 610});
    totals.push({PlayEvent::Kind::InFocus, 3, {}, 620});
    totals.push({PlayEvent::Kind::OutOfFocus, 3, {}, 640});
    totals.push({PlayEvent::Kind::Exit, 3, {}, 641});
    assert(totals.values().at(3).seconds == 30);
    // Restart breaks the unfinished session; no merging across boots.
    totals.push({PlayEvent::Kind::Launch, 4, {}, 700});
    totals.push({PlayEvent::Kind::Account, 0, {11,22}, 701});
    totals.push({PlayEvent::Kind::InFocus, 4, {}, 710});
    totals.push({PlayEvent::Kind::Reset, 0, {}, 0});
    totals.push({PlayEvent::Kind::OutOfFocus, 4, {}, 800});
    totals.push({PlayEvent::Kind::Exit, 4, {}, 801});
    assert(totals.values().count(4) == 0);
    std::cout << "playtime event tests PASS\n";
}
