#include "net/NetUtil.hpp"

#include <cstring>

namespace net {

namespace {
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
std::uint32_t rol(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
} // namespace

bool parseUrl(std::string_view url, UrlParts& out) {
    out = {};
    const std::size_t sep = url.find("://");
    if (sep == std::string_view::npos || sep == 0)
        return false;
    for (char c : url.substr(0, sep))
        out.scheme.push_back(lower(c));
    int defaultPort = 0;
    if (out.scheme == "http" || out.scheme == "ws") defaultPort = 80;
    else if (out.scheme == "https" || out.scheme == "wss") defaultPort = 443;
    else return false;

    std::string_view rest = url.substr(sep + 3);
    const std::size_t end = rest.find_first_of("/?#");
    std::string_view authority = rest.substr(0, end);
    if (authority.find('@') != std::string_view::npos)
        return false; // credentials in URLs are never sent
    const std::size_t colon = authority.rfind(':');
    std::string_view host = authority;
    out.port = defaultPort;
    if (colon != std::string_view::npos) {
        host = authority.substr(0, colon);
        std::string_view portText = authority.substr(colon + 1);
        if (portText.empty() || portText.size() > 5)
            return false;
        int port = 0;
        for (char c : portText) {
            if (c < '0' || c > '9')
                return false;
            port = port * 10 + (c - '0');
        }
        if (port <= 0 || port > 65535)
            return false;
        out.port = port;
    }
    if (host.empty())
        return false;
    for (char c : host)
        out.host.push_back(lower(c));
    if (end == std::string_view::npos) {
        out.target = "/";
    } else {
        std::string_view tail = rest.substr(end);
        const std::size_t hash = tail.find('#');
        tail = tail.substr(0, hash);
        out.target = (tail.empty() || tail[0] != '/') ? "/" + std::string(tail) : std::string(tail);
    }
    return true;
}

std::string hostOf(std::string_view url) {
    UrlParts parts;
    return parseUrl(url, parts) ? parts.host : std::string();
}

std::string base64(const std::uint8_t* d, std::size_t n) {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (std::size_t i = 0; i < n; i += 3) {
        const std::uint32_t v = (std::uint32_t(d[i]) << 16) |
                                (i + 1 < n ? std::uint32_t(d[i + 1]) << 8 : 0) |
                                (i + 2 < n ? std::uint32_t(d[i + 2]) : 0);
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(i + 1 < n ? kAlphabet[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < n ? kAlphabet[v & 63] : '=');
    }
    return out;
}

void sha1(const std::uint8_t* data, std::size_t size, std::uint8_t out[20]) {
    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    // Message plus 0x80, zero padding and the 64-bit bit length.
    const std::size_t total = ((size + 8) / 64 + 1) * 64;
    std::uint8_t block[64];
    for (std::size_t off = 0; off < total; off += 64) {
        for (std::size_t i = 0; i < 64; ++i) {
            const std::size_t p = off + i;
            if (p < size) block[i] = data[p];
            else if (p == size) block[i] = 0x80;
            else if (p >= total - 8) block[i] = static_cast<std::uint8_t>((std::uint64_t(size) * 8) >> ((total - 1 - p) * 8));
            else block[i] = 0;
        }
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16) |
                   (std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
            const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = static_cast<std::uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
    }
}

std::string wsAcceptKey(std::string_view clientKey) {
    std::string s(clientKey);
    s += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::uint8_t digest[20];
    sha1(reinterpret_cast<const std::uint8_t*>(s.data()), s.size(), digest);
    return base64(digest, sizeof(digest));
}

} // namespace net
