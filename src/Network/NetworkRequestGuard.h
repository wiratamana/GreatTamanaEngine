#pragma once

#include <optional>
#include <string_view>

namespace gte::Network {

// The request headers that tell a browser page from a plain tool. A missing header is nullopt.
struct BrowserRequestHeaders {
    std::optional<std::string_view> secFetchSite;
    std::optional<std::string_view> origin;
    std::optional<std::string_view> host;
};

// True when a destructive route must refuse the request because it may come from a web page:
// a cross-site fetch, a foreign Origin, or a Host that is not this loopback server (DNS rebinding).
// Plain clients send none of these headers (or a loopback Host) and pass.
bool RejectBrowserRequest(const BrowserRequestHeaders& headers, int port);

} // namespace gte::Network
