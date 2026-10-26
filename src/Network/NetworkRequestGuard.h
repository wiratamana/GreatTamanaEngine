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

// True when a destructive route must refuse the request because it may come from a web page
// (cross-site fetch, foreign Origin, or non-loopback Host). Plain clients pass.
bool RejectBrowserRequest(const BrowserRequestHeaders& headers, int port);

} // namespace gte::Network
