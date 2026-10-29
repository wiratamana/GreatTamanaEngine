#pragma once

// Pure rule for which entities a pass chain may take over from the CPU scene draw.
// Vulkan-free so it is testable without a device.

#include "../ECS/Entity.h"
#include "../Renderer/RenderGraph/RenderGraphHandles.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gte {

// Last pass of the (reset, cull, draw) chain that was declared, in that order. Culled: the whole part lies outside
// the cull frustum, so no pass exists and none is needed.
enum class ClusterChainProgress : std::uint8_t { None, Reset, Cull, Draw, Culled };

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

// The CPU draw cull follows the claim rule: a captured primary frame keeps its full draw list.
constexpr bool SceneCullAllowed(const ClusterClaimContext& context) noexcept
{
    return ClusterClaimAllowed(context);
}

// True when every pass of the part's chain was declared, or the part needs none because it is outside the view.
constexpr bool ClusterPartClaimable(const ClusterPartClaim& part) noexcept
{
    return part.progress == ClusterChainProgress::Draw || part.progress == ClusterChainProgress::Culled;
}

// Entities of every claimable part as one span per part, in part order, empty spans dropped. Nothing when the
// context disallows claims. The spans point into `parts`' own storage.
std::vector<std::span<const Entity>> ClaimedSpansOf(
    std::span<const ClusterPartClaim> parts, const ClusterClaimContext& context);

} // namespace gte
