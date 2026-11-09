// Host test for the pure NetService pieces (HomeNX G1): URL parsing, base64,
// SHA-1, the RFC 6455 accept key, frame encode/decode and the retry policy.
#include "net/NetBackoff.hpp"
#include "net/NetUtil.hpp"
#include "net/WsFrame.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string hex(const std::uint8_t* d, std::size_t n) {
    std::string s;
    char b[3];
    for (std::size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02x", d[i]);
        s += b;
    }
    return s;
}

// A server frame (unmasked) for the reader tests.
std::string serverFrame(int op, const std::string& payload, bool fin = true) {
    std::string f;
    f.push_back(static_cast<char>((fin ? 0x80 : 0) | op));
    const std::size_t n = payload.size();
    if (n < 126) {
        f.push_back(static_cast<char>(n));
    } else if (n < 65536) {
        f.push_back(126);
        f.push_back(static_cast<char>(n >> 8));
        f.push_back(static_cast<char>(n & 0xFF));
    } else {
        f.push_back(127);
        for (int i = 7; i >= 0; --i)
            f.push_back(static_cast<char>((static_cast<std::uint64_t>(n) >> (i * 8)) & 0xFF));
    }
    return f + payload;
}

} // namespace

int main() {
    using namespace net;
    // URLs
    UrlParts u;
    assert(parseUrl("https://Example.COM/a/b?c=1#frag", u));
    assert(u.scheme == "https" && u.host == "example.com" && u.port == 443 && u.target == "/a/b?c=1");
    assert(parseUrl("wss://gateway.discord.gg/?v=10&encoding=json", u));
    assert(u.scheme == "wss" && u.port == 443 && u.target == "/?v=10&encoding=json");
    assert(parseUrl("ws://host:8080", u) && u.port == 8080 && u.target == "/");
    assert(parseUrl("http://host?x=1", u) && u.target == "/?x=1");
    assert(!parseUrl("ftp://host/", u));
    assert(!parseUrl("https://", u));
    assert(!parseUrl("https://host:0/", u) && !parseUrl("https://host:70000/", u) && !parseUrl("https://host:x/", u));
    assert(!parseUrl("https://user:pw@host/", u)); // credentials never sent
    assert(hostOf("HTTPS://Api.Example.org:444/x") == "api.example.org");
    assert(hostOf("nonsense").empty());

    // base64 (RFC 4648 vectors)
    auto b64 = [](const char* s) {
        return base64(reinterpret_cast<const std::uint8_t*>(s), std::char_traits<char>::length(s));
    };
    assert(b64("") == "" && b64("f") == "Zg==" && b64("fo") == "Zm8=" && b64("foo") == "Zm9v");
    assert(b64("foobar") == "Zm9vYmFy");

    // SHA-1 (FIPS 180 vectors)
    std::uint8_t d[20];
    sha1(reinterpret_cast<const std::uint8_t*>("abc"), 3, d);
    assert(hex(d, 20) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    sha1(nullptr, 0, d);
    assert(hex(d, 20) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    const std::string longMsg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha1(reinterpret_cast<const std::uint8_t*>(longMsg.data()), longMsg.size(), d);
    assert(hex(d, 20) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    std::string million(1000000, 'a');
    sha1(reinterpret_cast<const std::uint8_t*>(million.data()), million.size(), d);
    assert(hex(d, 20) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");

    // RFC 6455 section 1.3 example
    assert(wsAcceptKey("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    // Client frames: masked, FIN, lengths 7/16/64-bit
    const std::uint8_t mask[4] = {0x37, 0xfa, 0x21, 0x3d};
    std::string hello = ws::encode(ws::Text, "Hello", mask);
    const unsigned char rfcHello[] = {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58};
    assert(hello == std::string(reinterpret_cast<const char*>(rfcHello), sizeof(rfcHello)));
    assert(ws::encode(ws::Binary, std::string(200, 'x'), mask).size() == 200 + 2 + 2 + 4);
    assert(ws::encode(ws::Binary, std::string(70000, 'x'), mask).size() == 70000 + 2 + 8 + 4);

    // Reader: split delivery, fragmentation, ping, close
    {
        ws::Reader r;
        std::vector<ws::Message> out;
        const std::string a = serverFrame(ws::Text, "Hel", false) + serverFrame(ws::Continuation, "lo");
        const std::string big = serverFrame(ws::Binary, std::string(300, 'b'));
        const std::string all = a + serverFrame(ws::Ping, "p1") + big;
        for (char ch : all) { // one byte at a time
            r.feed(&ch, 1);
            assert(r.parse(out));
        }
        assert(out.size() == 2 && out[0].data == "Hello" && !out[0].binary);
        assert(out[1].binary && out[1].data.size() == 300);
        assert(r.pendingPongs().size() == 1 && r.pendingPongs()[0] == "p1");
        std::string closeFrame = serverFrame(ws::Close, std::string("\x0f\xa0", 2) + "bye");
        r.feed(closeFrame.data(), closeFrame.size());
        assert(!r.parse(out) && r.closeReceived() && r.closeCode() == 4000 && r.closeReason() == "bye");
    }
    {
        ws::Reader r; // a masked server frame is a protocol error
        std::vector<ws::Message> out;
        std::string f = ws::encode(ws::Text, "x", mask);
        r.feed(f.data(), f.size());
        assert(!r.parse(out) && !r.error().empty());
    }
    {
        ws::Reader r; // oversized announcement fails before buffering it
        std::vector<ws::Message> out;
        std::string f;
        f.push_back(static_cast<char>(0x82));
        f.push_back(127);
        const std::uint64_t n = (16u << 20) + 1;
        for (int i = 7; i >= 0; --i) f.push_back(static_cast<char>((n >> (i * 8)) & 0xFF));
        r.feed(f.data(), f.size());
        assert(!r.parse(out) && r.error() == "message too large");
    }
    {
        ws::Reader r; // empty close: status 1005, echoed with no code
        std::vector<ws::Message> out;
        std::string f = serverFrame(ws::Close, "");
        r.feed(f.data(), f.size());
        assert(!r.parse(out) && r.closeReceived() && r.closeCode() == 1005 && !r.closeHasCode());
    }
    {
        ws::Reader r; // one-byte close payload is a protocol error
        std::vector<ws::Message> out;
        std::string f = serverFrame(ws::Close, "x");
        r.feed(f.data(), f.size());
        assert(!r.parse(out) && !r.closeReceived() && r.error() == "bad close payload");
    }
    {
        ws::Reader r; // 1005/1006 never appear on the wire
        std::vector<ws::Message> out;
        std::string f = serverFrame(ws::Close, std::string("\x03\xed", 2));
        r.feed(f.data(), f.size());
        assert(!r.parse(out) && r.error() == "bad close code");
    }
    {
        ws::Reader r; // input past the buffer cap is refused, not stored
        std::vector<ws::Message> out;
        std::string chunk(1u << 20, 'z');
        for (int i = 0; i < 17; ++i)
            r.feed(chunk.data(), chunk.size());
        assert(!r.parse(out) && !r.error().empty());
    }
    {
        ws::Reader r; // continuation without a start
        std::vector<ws::Message> out;
        std::string f = serverFrame(ws::Continuation, "x");
        r.feed(f.data(), f.size());
        assert(!r.parse(out));
    }

    // Retry policy
    assert(retryableStatus(429) && retryableStatus(503) && !retryableStatus(404) && !retryableStatus(200));
    assert(backoffMs(1) == 500 && backoffMs(2) == 1000 && backoffMs(3) == 2000 && backoffMs(10) == 8000);
    assert(backoffMs(1, 2.5) == 2500 && backoffMs(1, 120) == 30000);

    std::puts("net_test: all passed");
    return 0;
}
