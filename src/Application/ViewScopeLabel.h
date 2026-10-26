#pragma once

// Display label of a view scope, shared by the panels and the HTTP bodies.

#include "../Renderer/RenderGraph/RenderGraphHandles.h"

namespace gte {

// "Shared" / "Game" / "Scene".
constexpr const char* ViewScopeLabel(rg::ViewScope scope) noexcept
{
    switch (scope) {
    case rg::ViewScope::Primary: return "Game";
    case rg::ViewScope::Secondary: return "Scene";
    case rg::ViewScope::Shared: break;
    }
    return "Shared";
}

} // namespace gte
