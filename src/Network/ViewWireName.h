#pragma once

// Wire text of a view scope in HTTP bodies.

#include "../Renderer/RenderGraph/RenderGraphHandles.h"

#include <array>

namespace gte::Network {

inline const char* ViewWireName(rg::ViewScope scope) noexcept
{
    switch (scope) {
    case rg::ViewScope::Primary: return "primary";
    case rg::ViewScope::Secondary: return "secondary";
    case rg::ViewScope::Shared: break;
    }
    return "shared";
}

// Every view scope, in wire order.
inline constexpr std::array<rg::ViewScope, rg::kViewScopeCount> kAllViewScopes = {
    rg::ViewScope::Shared, rg::ViewScope::Primary, rg::ViewScope::Secondary};
static_assert(kAllViewScopes.size() == rg::kViewScopeCount);

} // namespace gte::Network
