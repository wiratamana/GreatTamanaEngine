#include "NetworkRequestGuard.h"

#include <cctype>
#include <string>

namespace gte::Network {

namespace {

std::string Lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool IsLoopbackName(std::string_view name)
{
    const std::string lower = Lower(name);
    return lower == "127.0.0.1" || lower == "localhost";
}

// Host part of an Origin value such as "http://localhost:8080". Empty when it has no host ("null").
std::string_view OriginHost(std::string_view origin)
{
    const std::size_t scheme = origin.find("://");
    if (scheme == std::string_view::npos) {
        return {};
    }
    std::string_view rest = origin.substr(scheme + 3);
    const std::size_t end = rest.find_first_of(":/");
    return end == std::string_view::npos ? rest : rest.substr(0, end);
}

bool IsLoopbackHostWithPort(std::string_view host, int port)
{
    const std::size_t colon = host.rfind(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    return IsLoopbackName(host.substr(0, colon)) && host.substr(colon + 1) == std::to_string(port);
}

} // namespace

bool RejectBrowserRequest(const BrowserRequestHeaders& headers, int port)
{
    if (headers.secFetchSite && *headers.secFetchSite != "none" && *headers.secFetchSite != "same-origin") {
        return true;
    }
    if (headers.origin && !IsLoopbackName(OriginHost(*headers.origin))) {
        return true;
    }
    // A missing Host is a non-browser client; every browser and DNS rebinding request carries one.
    if (headers.host && !IsLoopbackHostWithPort(*headers.host, port)) {
        return true;
    }
    return false;
}

} // namespace gte::Network
