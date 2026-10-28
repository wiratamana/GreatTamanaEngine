#pragma once

// Pure parts of the cluster draw verb: grouping, slot assignment, the round order and the claim-after-proof flow.
// Vulkan-free so they run without a device.

#include "ClusterDrawTypes.h"
#include "../ClusterClaim.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace gte::fx {

// All candidates of one part (one mesh, and one material for a lit colour draw), as indices into the candidate span.
struct ClusterMeshGroup {
    MeshToken mesh;
    MaterialToken material; // Zero when the group is not split by material.
    ClusterMaterialKind kind = ClusterMaterialKind::None;
    std::vector<std::uint32_t> instances;
};

// Groups candidates by mesh, or by (mesh, material) when `splitByMaterial`. Groups appear in order of first use;
// instances keep their order. A group takes the kind of its first instance.
std::vector<ClusterMeshGroup> GroupInstances(std::span<const ClusterInstance> candidates, bool splitByMaterial);

// Size of one part, for slot assignment.
struct ClusterPartSize {
    MeshToken mesh;
    MaterialToken material;
    std::uint64_t instanceCount = 0;
    std::uint64_t clusterCount = 0;
};

// Indices into the input: `assigned` in slot order, `dropped` for parts that found no slot.
struct ClusterSlotAssignment {
    std::vector<std::uint32_t> assigned;
    std::vector<std::uint32_t> dropped;
};

// Slots go to the parts with the most work items first. Ties are broken by mesh token, then material token.
ClusterSlotAssignment AssignClusterPartSlots(std::span<const ClusterPartSize> parts, std::uint32_t slotCount);

// Last step of a part's chain that was declared. Round 1 is Reset1..Draw1, round 2 is Reset2..Draw2.
// A single-phase chain only has round 1.
enum class ClusterStep : std::uint8_t { None, Reset1, Cull1, Draw1, Reset2, Cull2, Draw2 };

// The claim rule's view of a step: Draw only when the last step of the part's chain was declared.
constexpr ClusterChainProgress ClusterProgressOf(ClusterStep step, bool twoPhase) noexcept
{
    switch (step) {
    case ClusterStep::None: return ClusterChainProgress::None;
    case ClusterStep::Reset1: return ClusterChainProgress::Reset;
    case ClusterStep::Cull1: return ClusterChainProgress::Cull;
    case ClusterStep::Draw1: return twoPhase ? ClusterChainProgress::Cull : ClusterChainProgress::Draw;
    case ClusterStep::Reset2:
    case ClusterStep::Cull2: return ClusterChainProgress::Cull;
    case ClusterStep::Draw2: return ClusterChainProgress::Draw;
    }
    return ClusterChainProgress::None;
}

// Declares reset, cull and draw of one round of one part, in that order, and stops at the first refusal.
// `declarer` provides bool Reset(part, round), Cull(part, round) and Draw(part, round); round is 0 or 1.
template <typename Declarer>
ClusterStep RecordClusterRound(Declarer& declarer, std::size_t part, std::uint32_t round)
{
    const bool second = round != 0;
    if (!declarer.Reset(part, round)) {
        return second ? ClusterStep::Draw1 : ClusterStep::None;
    }
    if (!declarer.Cull(part, round)) {
        return second ? ClusterStep::Reset2 : ClusterStep::Reset1;
    }
    if (!declarer.Draw(part, round)) {
        return second ? ClusterStep::Cull2 : ClusterStep::Cull1;
    }
    return second ? ClusterStep::Draw2 : ClusterStep::Draw1;
}

// Round 1 of every part, then one pyramid build, then round 2 of every part that finished round 1.
// A single-phase chain stops after round 1. `declarer` also provides bool Begin(part) (acquire the part's
// buffers) and bool Build() (declare the pyramid). A refused Begin leaves the part at None; a refused Build
// leaves every part at Draw1, which never counts as claimable in a two-phase chain.
template <typename Declarer>
std::vector<ClusterStep> RecordClusterRounds(Declarer& declarer, std::size_t partCount, bool twoPhase)
{
    std::vector<ClusterStep> steps(partCount, ClusterStep::None);
    std::size_t finishedFirst = 0;
    for (std::size_t part = 0; part < partCount; ++part) {
        if (!declarer.Begin(part)) {
            continue;
        }
        steps[part] = RecordClusterRound(declarer, part, 0);
        finishedFirst += steps[part] == ClusterStep::Draw1 ? 1 : 0;
    }
    if (!twoPhase || finishedFirst == 0 || !declarer.Build()) {
        return steps;
    }
    for (std::size_t part = 0; part < partCount; ++part) {
        if (steps[part] == ClusterStep::Draw1) {
            steps[part] = RecordClusterRound(declarer, part, 1);
        }
    }
    return steps;
}

// Draws of published survivors: round 1 of every part, then round 2 of every part that finished round 1 (only when
// the publisher ran two phases). There is no reset, cull or pyramid. `declarer` provides bool Begin(part) (resolve
// the part's buffers) and bool Draw(part, round). A refused Begin leaves the part at None.
template <typename Declarer>
std::vector<ClusterStep> RecordSharedRounds(Declarer& declarer, std::size_t partCount, bool twoRounds)
{
    std::vector<ClusterStep> steps(partCount, ClusterStep::None);
    for (std::size_t part = 0; part < partCount; ++part) {
        if (declarer.Begin(part) && declarer.Draw(part, 0)) {
            steps[part] = ClusterStep::Draw1;
        }
    }
    if (!twoRounds) {
        return steps;
    }
    for (std::size_t part = 0; part < partCount; ++part) {
        if (steps[part] == ClusterStep::Draw1 && declarer.Draw(part, 1)) {
            steps[part] = ClusterStep::Draw2;
        }
    }
    return steps;
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

// One readback cell of a part slot.
struct ClusterCountSample {
    bool valid = false;
    std::uint32_t value = 0;
    std::uint64_t frame = 0;
};

// Sums the valid cells. Invalid when none is valid; `frame` is the oldest frame used.
ClusterVisibleCount SumClusterCounts(std::span<const ClusterCountSample> samples) noexcept;

// One readback cell of a part slot with its outcome counters.
struct ClusterReasonSample {
    bool valid = false;
    ClusterStatsWords words;
    std::uint64_t frame = 0;
};

// Sums the valid cells. Invalid when none is valid; `frame` is the oldest frame used.
ClusterReasonCounts SumClusterReasons(std::span<const ClusterReasonSample> samples) noexcept;

// What a part slot's history buffer was last written for.
struct ClusterHistoryRecord {
    std::uint64_t key = 0;
    std::uint64_t frame = 0;
    bool written = false;
};

// The history is trusted only when the previous frame wrote it for the same contents (see ClusterHistoryKey).
constexpr bool ClusterHistoryUsable(const ClusterHistoryRecord& record, std::uint64_t key, std::uint64_t frame) noexcept
{
    return record.written && record.key == key && record.frame + 1 == frame;
}

struct ClusterRecordOutcome {
    std::vector<ClusterChainProgress> progress; // One entry per part.
    std::vector<Entity> claimed;                // Entities of the parts whose chain was fully declared.
};

// Builds the claim from what was really declared. `parts` holds the entities of each part.
inline ClusterRecordOutcome ClusterOutcomeOf(std::span<const std::span<const Entity>> parts,
    const ClusterClaimContext& context, std::span<const ClusterStep> steps, bool twoPhase)
{
    ClusterRecordOutcome outcome;
    outcome.progress.assign(parts.size(), ClusterChainProgress::None);
    std::vector<ClusterPartClaim> claims;
    claims.reserve(parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        outcome.progress[i] = i < steps.size() ? ClusterProgressOf(steps[i], twoPhase) : ClusterChainProgress::None;
        claims.push_back(ClusterPartClaim{ parts[i], outcome.progress[i] });
    }
    outcome.claimed = ClaimedEntitiesOf(claims, context);
    return outcome;
}

} // namespace gte::fx
