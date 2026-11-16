#include "discord/DiscordGateway.hpp"
#include "net/WebSocket.hpp"
#include "net/NetService.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;
namespace {
std::atomic<int> connects{0}, closes4000{0}, resumeMessages{0}, identifies{0}, phase{0};
std::atomic<bool> holdConnect{false}, entered{false};
}
namespace net {
NetService::~NetService() = default;
WebSocket::~WebSocket() { close(); }
bool WebSocket::connect(const std::string&, const std::vector<std::string>&, std::string&,
                        const std::atomic<bool>* cancel) {
    ++connects; entered = true;
    // Deliberately noncancellable DNS: pause must not join on the UI thread.
    while (holdConnect.load()) std::this_thread::sleep_for(1ms);
    if (cancel && cancel->load()) return false;
    m_curl = this; m_open = true; phase = 0;
    return true;
}
void WebSocket::close(int code) {
    if (m_open && code == 4000) ++closes4000;
    m_open = false; m_curl = nullptr;
}
bool WebSocket::sendText(const std::string& s, const std::atomic<bool>*) {
    if (s.find("\"op\":6") != std::string::npos) ++resumeMessages;
    if (s.find("\"op\":2") != std::string::npos) ++identifies;
    return true;
}
bool WebSocket::wait(std::vector<ws::Message>& out, int, const std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) return true;
    ws::Message msg;
    if (phase == 0) {
        msg.data = R"({"op":10,"d":{"heartbeat_interval":40000}})";
        ++phase;
    } else if (phase == 1) {
        msg.data = R"({"op":0,"s":5,"t":"READY","d":{"session_id":"test","resume_gateway_url":"wss://example.invalid"}})";
        ++phase;
    } else { std::this_thread::sleep_for(1ms); return true; }
    out.push_back(std::move(msg));
    return true;
}
}
template<class F> void eventually(F f) {
    for (int n = 0; n < 3000 && !f(); ++n) std::this_thread::sleep_for(1ms);
    assert(f());
}
int main() {
    net::NetService net;
    discord::DiscordGateway gw;
    holdConnect = true;
    gw.start(net, "test-a");
    eventually([]{return entered.load();});
    auto before = std::chrono::steady_clock::now();
    gw.pause(); gw.pause();
    assert(std::chrono::steady_clock::now()-before < 100ms);
    assert(!gw.resume(net)); // still connecting: never launch a second worker
    holdConnect = false;
    eventually([&]{return gw.done();});
    assert(gw.resume(net));
    eventually([&]{return gw.isConnected();});
    assert(identifies == 1);
    gw.pause(); eventually([&]{return gw.done();});
    assert(closes4000 == 1);
    assert(!gw.drainEvents().empty()); // pause retains dispatches for the store
    assert(gw.resume(net));
    eventually([]{return resumeMessages.load() == 1;});
    gw.stop();
    assert(!gw.resume(net)); // a terminal stop cannot be resurrected by wake
    gw.start(net, "test-b");
    eventually([]{return identifies.load() == 2;}); // account switch cannot resume A
    gw.stop();
    std::cout << "Discord gateway sleep/resume tests PASS\n";
}
