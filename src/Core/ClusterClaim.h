#pragma once

// Pure rule for which entities a pass chain may take over from the CPU scene draw.
// Vulkan-free so it is testable without a device.

#include "../ECS/Entity.h"
#include "../Renderer/RenderGraph/RenderGraphHandles.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gte {

// Last pass of the (reset, cull, draw) chain that was declared, in that order.
enum class ClusterChainProgress : std::uint8_t { None, Reset, Cull, Draw };

// Outcome of one part's chain.
struct ClusterPartClaim {
    std::span<const Entity> entities;
    ClusterChainProgress progress = ClusterChainProgress::None;
};

// Frame-level conditions shared by every part.
struct ClusterClaimContext {
    rg::ViewScope scope = rg::ViewScope::Shared;
    bool captureArmed = false; // A frame capture is recording this frame.
};

// False in the shared scope (no camera, no target) and in a primary view frame that is being captured.
constexpr bool ClusterClaimAllowed(const ClusterClaimContext& context) noexcept
{
    if (context.scope == rg::ViewScope::Shared) {
        return false;
    }
    return !(context.captureArmed && context.scope == rg::ViewScope::Primary);
}

// True when every pass of the part's chain was declared.
constexpr bool ClusterPartClaimable(const ClusterPartClaim& part) noexcept
{
    return part.progress == ClusterChainProgress::Draw;
}

// Entities of every claimable part, in part order. Empty when the context disallows claims.
std::vector<Entity> ClaimedEntitiesOf(std::span<const ClusterPartClaim> parts, const ClusterClaimContext& context);

} // namespace gte
