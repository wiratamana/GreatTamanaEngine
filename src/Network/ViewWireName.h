#pragma once

// Wire text of a view scope in HTTP bodies.

#include "../Renderer/RenderGraph/RenderGraphHandles.h"

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

} // namespace gte::Network
