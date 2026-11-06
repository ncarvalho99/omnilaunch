// Host test for widgets/LockPassword (E9). The hash is a stand-in: the store
// logic (length, check, save/remove, validation) is what is under test.
#include "widgets/LockPassword.hpp"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); std::exit(1); } } while (0)

static std::string fakeHash(const std::string& s) {
    std::string out(64, '0');
    const std::size_t h = std::hash<std::string>{}(s);
    for (int i = 0; i < 16; ++i)
        out[static_cast<std::size_t>(i)] = "0123456789abcdef"[(h >> (i * 4)) & 0xF];
    return out;
}

int main() {
    const std::string path = "/tmp/lock_password_test";
    std::remove(path.c_str());
    const lockpw::HashFn h = fakeHash;
    CHECK(lockpw::storedLength(path) == 0);
    CHECK(!lockpw::check(h, "UDLR", path));
    CHECK(!lockpw::save(h, "UDL", path));       // too short
    CHECK(!lockpw::save(h, "UDLRUDLRU", path)); // too long
    CHECK(!lockpw::save(h, "UDLX", path));      // not a D-pad press
    CHECK(lockpw::save(h, "UUDDLR", path));
    CHECK(lockpw::storedLength(path) == 6);
    CHECK(lockpw::check(h, "UUDDLR", path));
    CHECK(!lockpw::check(h, "UUDDRL", path));
    CHECK(!lockpw::check(h, "UUDDL", path));
    CHECK(lockpw::save(h, "", path));           // remove
    CHECK(lockpw::storedLength(path) == 0);
    CHECK(lockpw::save(h, "", path));           // removing nothing is fine
    std::printf("lock_password_test: PASS\n");
    return 0;
}
