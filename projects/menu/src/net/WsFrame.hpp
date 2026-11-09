#pragma once
// RFC 6455 framing for the NetService WebSocket client (HomeNX G1). Written
// for OmniLaunch; pure, so tests/net_test.cpp drives it without a socket.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace net::ws {

enum Opcode : std::uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

/// A client frame (always masked, FIN set) carrying `payload`.
std::string encode(Opcode op, std::string_view payload, const std::uint8_t mask[4]);

struct Message {
    bool binary = false;
    std::string data;
};

/// Incremental reader of server frames. feed() appends received bytes;
/// parse() moves complete messages to `out` and records what the connection
/// owes the server (pongs, the close echo).
class Reader {
public:
    static constexpr std::size_t kMaxMessage = 16u << 20; ///< 16 MiB
    /// Unparsed input never grows past one maximal frame plus its header.
    static constexpr std::size_t kMaxBuffered = kMaxMessage + 14;

    /// Appends received bytes. Call parse() after every chunk; feeding past
    /// kMaxBuffered marks the stream unusable instead of growing.
    void feed(const char* data, std::size_t size) {
        if (m_buf.size() + size > kMaxBuffered) {
            m_error = "receive buffer full";
            return;
        }
        m_buf.append(data, size);
    }
    /// Close code to echo on the wire: none (empty payload) for 1005.
    bool closeHasCode() const { return m_closeCode != 0 && m_closeCode != 1005; }
    /// False once the stream is unusable (close frame, protocol error or a
    /// message over kMaxMessage). Messages parsed before that stay in `out`.
    bool parse(std::vector<Message>& out);

    std::vector<std::string>& pendingPongs() { return m_pongs; }
    bool closeReceived() const { return m_closeCode != 0; }
    int closeCode() const { return m_closeCode; }
    const std::string& closeReason() const { return m_closeReason; }
    const std::string& error() const { return m_error; }

private:
    std::string m_buf;
    std::string m_fragment;
    int m_fragmentOp = -1;
    std::vector<std::string> m_pongs;
    int m_closeCode = 0;
    std::string m_closeReason;
    std::string m_error;
};

} // namespace net::ws
