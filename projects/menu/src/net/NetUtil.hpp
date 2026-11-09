#pragma once
// NetService helpers (HomeNX G1, C-NET-1): URL parsing, base64, SHA-1 and the
// RFC 6455 accept key. Written for OmniLaunch; free of curl and libnx so the
// host test (tests/net_test.cpp) covers them.

#include <cstdint>
#include <string>
#include <string_view>

namespace net {

struct UrlParts {
    std::string scheme; ///< lower case: http, https, ws, wss
    std::string host;   ///< lower case
    int port = 0;       ///< explicit port, or the scheme default
    std::string target; ///< path plus query, never empty ("/")
};

/// False for a URL without scheme/host or with a bad port.
bool parseUrl(std::string_view url, UrlParts& out);
/// Lower-case host of `url`, empty when it does not parse.
std::string hostOf(std::string_view url);

std::string base64(const std::uint8_t* data, std::size_t size);
/// 20-byte SHA-1 digest.
void sha1(const std::uint8_t* data, std::size_t size, std::uint8_t out[20]);
/// Sec-WebSocket-Accept for a Sec-WebSocket-Key (RFC 6455 section 4.2.2).
std::string wsAcceptKey(std::string_view clientKey);

} // namespace net
