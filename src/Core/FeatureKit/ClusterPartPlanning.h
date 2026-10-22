#pragma once

// Pure parts of the cluster draw verb: grouping, slot assignment and the claim-after-proof flow.
// Vulkan-free so they run without a device.

#include "ClusterDrawTypes.h"
#include "../ClusterClaim.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace gte::fx {

// All candidates of one mesh, as indices into the candidate span.
struct ClusterMeshGroup {
    MeshToken mesh;
    std::vector<std::uint32_t> instances;
};

// Groups candidates by mesh. Groups appear in order of first use; instances keep their order.
std::vector<ClusterMeshGroup> GroupInstancesByMesh(std::span<const ClusterInstance> candidates);

// Size of one part, for slot assignment.
struct ClusterPartSize {
    MeshToken mesh;
    std::uint64_t instanceCount = 0;
    std::uint64_t clusterCount = 0;
};

// Indices into the input: `assigned` in slot order, `dropped` for parts that found no slot.
struct ClusterSlotAssignment {
    std::vector<std::uint32_t> assigned;
    std::vector<std::uint32_t> dropped;
};

// Slots go to the parts with the most work items first. Ties are broken by mesh token.
ClusterSlotAssignment AssignClusterPartSlots(std::span<const ClusterPartSize> parts, std::uint32_t slotCount);

// One part of a chain, as far as the claim rule needs to know.
struct ClusterPartPlan {
    std::span<const Entity> entities;
    bool planReady = true;
    ClusterChainToggles toggles;
};

// Declares reset, cull and draw in that order and stops at the first refusal.
// `declarer` provides bool Reset(), bool Cull(), bool Draw().
template <typename Declarer>
ClusterChainProgress RecordClusterChain(Declarer& declarer)
{
    if (!declarer.Reset()) {
        return ClusterChainProgress::None;
    }
    if (!declarer.Cull()) {
        return ClusterChainProgress::Reset;
    }
    if (!declarer.Draw()) {
        return ClusterChainProgress::Cull;
    }
    return ClusterChainProgress::Draw;
}

// Stats run only for a colour target on the first lane, when the caller asked for them.
constexpr bool ClusterStatsWanted(bool requested, bool depthOnly, std::uint32_t lane) noexcept
{
    return requested && !depthOnly && lane == 0;
}

// Declares clear, dispatch and host copy together or not at all. `declarer` provides bool StatsClear(),
// StatsCompute() and StatsCopy(). A refusal after the clear leaves only a harmless clear behind.
template <typename Declarer>
bool RecordClusterStats(Declarer& declarer)
{
    return declarer.StatsClear() && declarer.StatsCompute() && declarer.StatsCopy();
}

// Chain plus optional stats. The stats are declared only after the draw, and their refusal never changes
// the returned progress, so it cannot revoke a claim.
template <typename Declarer>
ClusterChainProgress RecordClusterPart(Declarer& declarer, bool statsWanted)
{
    const ClusterChainProgress progress = RecordClusterChain(declarer);
    if (progress == ClusterChainProgress::Draw && statsWanted) {
        RecordClusterStats(declarer);
    }
    return progress;
}

// One readback cell of a part slot.
struct ClusterCountSample {
    bool valid = false;
    std::uint32_t value = 0;
    std::uint64_t frame = 0;
};

// Sums the valid cells. Invalid when none is valid; `frame` is the oldest frame used.
ClusterVisibleCount SumClusterCounts(std::span<const ClusterCountSample> samples) noexcept;

struct ClusterRecordOutcome {
    std::vector<ClusterChainProgress> progress; // One entry per part.
    std::vector<Entity> claimed;                // Entities of the parts whose chain was fully declared.
};

// Runs `declarePart(index)` (returns the progress of that part) for every ready part, then builds the claim
// from what was really declared. Nothing is claimed before every call of a part has reported "declared".
template <typename DeclarePart>
ClusterRecordOutcome RecordClusterParts(
    std::span<const ClusterPartPlan> parts, const ClusterClaimContext& context, DeclarePart&& declarePart)
{
    ClusterRecordOutcome outcome;
    outcome.progress.assign(parts.size(), ClusterChainProgress::None);
    std::vector<ClusterPartClaim> claims;
    claims.reserve(parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].planReady) {
            outcome.progress[i] = declarePart(i);
        }
        claims.push_back(ClusterPartClaim{ parts[i].entities, parts[i].planReady, outcome.progress[i], parts[i].toggles });
    }
    outcome.claimed = ClaimedEntitiesOf(claims, context);
    return outcome;
}

} // namespace gte::fx
