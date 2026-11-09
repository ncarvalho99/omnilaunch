#include "net/WsFrame.hpp"

namespace net::ws {

std::string encode(Opcode op, std::string_view payload, const std::uint8_t mask[4]) {
    std::string f;
    const std::size_t n = payload.size();
    f.reserve(n + 14);
    f.push_back(static_cast<char>(0x80 | op));
    if (n < 126) {
        f.push_back(static_cast<char>(0x80 | n));
    } else if (n < 65536) {
        f.push_back(static_cast<char>(0x80 | 126));
        f.push_back(static_cast<char>((n >> 8) & 0xFF));
        f.push_back(static_cast<char>(n & 0xFF));
    } else {
        f.push_back(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            f.push_back(static_cast<char>((static_cast<std::uint64_t>(n) >> (i * 8)) & 0xFF));
    }
    f.append(reinterpret_cast<const char*>(mask), 4);
    for (std::size_t i = 0; i < n; ++i)
        f.push_back(static_cast<char>(payload[i] ^ mask[i & 3]));
    return f;
}

bool Reader::parse(std::vector<Message>& out) {
    if (!m_error.empty() || m_closeCode != 0)
        return false;
    for (;;) {
        if (m_buf.size() < 2)
            return true;
        const auto* b = reinterpret_cast<const std::uint8_t*>(m_buf.data());
        const bool fin = (b[0] & 0x80) != 0;
        const int op = b[0] & 0x0F;
        if ((b[0] & 0x70) != 0) {
            m_error = "reserved bits set";
            return false;
        }
        if (b[1] & 0x80) {
            m_error = "masked server frame"; // RFC 6455 5.1
            return false;
        }
        std::uint64_t len = b[1] & 0x7F;
        std::size_t pos = 2;
        if (len == 126) {
            if (m_buf.size() < 4) return true;
            len = (std::uint64_t(b[2]) << 8) | b[3];
            pos = 4;
        } else if (len == 127) {
            if (m_buf.size() < 10) return true;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | b[2 + i];
            pos = 10;
        }
        const bool control = (op & 0x8) != 0;
        if (control && (len > 125 || !fin)) {
            m_error = "bad control frame";
            return false;
        }
        if (len > kMaxMessage || m_fragment.size() + len > kMaxMessage) {
            m_error = "message too large";
            return false;
        }
        if (m_buf.size() < pos + len)
            return true;
        std::string payload = m_buf.substr(pos, static_cast<std::size_t>(len));
        m_buf.erase(0, pos + static_cast<std::size_t>(len));

        switch (op) {
        case Continuation:
            if (m_fragmentOp < 0) {
                m_error = "continuation without start";
                return false;
            }
            m_fragment += payload;
            if (fin) {
                out.push_back({m_fragmentOp == Binary, std::move(m_fragment)});
                m_fragment.clear();
                m_fragmentOp = -1;
            }
            break;
        case Text:
        case Binary:
            if (m_fragmentOp >= 0) {
                m_error = "new message inside a fragmented one";
                return false;
            }
            if (fin) {
                out.push_back({op == Binary, std::move(payload)});
            } else {
                m_fragmentOp = op;
                m_fragment = std::move(payload);
            }
            break;
        case Close: {
            if (payload.size() == 1) {
                m_error = "bad close payload";
                return false;
            }
            if (payload.empty()) {
                m_closeCode = 1005; // "no status": echoed with an empty payload
                return false;
            }
            const int code = (std::uint8_t(payload[0]) << 8) | std::uint8_t(payload[1]);
            // RFC 6455 7.4: codes a peer may put on the wire.
            const bool valid = (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1011) ||
                               (code >= 3000 && code <= 4999);
            if (!valid) {
                m_error = "bad close code";
                return false;
            }
            m_closeCode = code;
            m_closeReason = payload.substr(2);
            return false;
        }
        case Ping:
            m_pongs.push_back(std::move(payload));
            break;
        case Pong:
            break;
        default:
            m_error = "unknown opcode";
            return false;
        }
    }
}

} // namespace net::ws
