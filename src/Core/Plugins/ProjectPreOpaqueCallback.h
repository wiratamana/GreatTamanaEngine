#pragma once

#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderPipeline.h"

#include <cassert>
#include <cstddef>
#include <functional>
#include <vector>

namespace gte {

// Callback of a pre-opaque project pass. Runs once per active view in the same frame, so any blackboard key it
// publishes must include `currentView`. Every pass it declares must carry rg::RenderPassEvent::PreOpaques.
using ProjectPreOpaqueCallback = std::function<void(
    rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView)>;

// Indices of passes declared in [before, after) whose event is not exactly PreOpaques (pure, no side effects).
inline std::vector<std::size_t> FindPassesNotTaggedPreOpaque(
    const rg::RenderGraphBuilder& builder, std::size_t before, std::size_t after)
{
    assert(before <= after && after <= builder.DeclaredPassCount()
        && "FindPassesNotTaggedPreOpaque() - [before, after) out of range");

    std::vector<std::size_t> violations;
    for (std::size_t index = before; index < after; ++index) {
        if (builder.PassEventAt(index) != rg::RenderPassEvent::PreOpaques) {
            violations.push_back(index);
        }
    }
    return violations;
}

} // namespace gte
