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

// Enabled state of the three chain passes of one part.
struct ClusterChainToggles {
    bool reset = true;
    bool cull = true;
    bool draw = true;
};

// Outcome of one part's chain.
struct ClusterPartClaim {
    std::span<const Entity> entities;
    bool planReady = false;
    ClusterChainProgress progress = ClusterChainProgress::None;
    ClusterChainToggles toggles;
};

// Frame-level conditions shared by every part.
struct ClusterClaimContext {
    rg::ViewScope scope = rg::ViewScope::Shared;
    bool captureArmed = false; // A frame capture is recording this frame.
};

// False in the shared scope (no camera, no target) and in a Game view frame that is being captured.
constexpr bool ClusterClaimAllowed(const ClusterClaimContext& context) noexcept
{
    if (context.scope == rg::ViewScope::Shared) {
        return false;
    }
    return !(context.captureArmed && context.scope == rg::ViewScope::GameView);
}

// True when the part's chain is complete and fully enabled.
constexpr bool ClusterPartClaimable(const ClusterPartClaim& part) noexcept
{
    return part.planReady && part.progress == ClusterChainProgress::Draw && part.toggles.reset && part.toggles.cull
        && part.toggles.draw;
}

// Entities of every claimable part, in part order. Empty when the context disallows claims.
std::vector<Entity> ClaimedEntitiesOf(std::span<const ClusterPartClaim> parts, const ClusterClaimContext& context);

} // namespace gte
