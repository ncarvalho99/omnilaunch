#pragma once
#include <set>
namespace nxui {
enum class Button { A,B,X,Y,L,R,ZL,ZR,DUp,DDown,DLeft,DRight,LStickU,LStickD,LStickL,LStickR };
class Input {
public:
 std::set<Button> down, held;
 bool touch=false; float x=0,y=0;
 bool isDown(Button b) const { return down.count(b); }
 bool isHeld(Button b) const { return held.count(b); }
 bool touchDown() const { return touch; }
 float touchX() const { return x; }
 float touchY() const { return y; }
};
}
