#pragma once

// Phase 6 (RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md, part 6 of the
// wider RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md campaign) - the pure,
// Vulkan-free half of "generalize GpuTimingService's fixed 3-slot design
// into an arbitrary, name-keyed set of passes" (see that document's Step
// 3.2). Extracted into its own tiny, allocation-cheap class specifically so
// this DECISION - "does this pass name already have a slot, and if not, can
// it be assigned one within this regime's own fixed budget" - is
// Tier-1-testable with no live VkQueryPool/VkDevice at all, mirroring
// GpuTiming.h's own "pure decision, extracted" precedent
// (ResolveGpuTimingStatus(), ConvertTimestampDeltaToMilliseconds()) and
// FrameProfiler::RecordCpuScope()'s own "string literal, compared by
// pointer first then strcmp() as a fallback" convention for a flat,
// name-keyed table.
//
// RenderGraph (RenderGraph.h) owns exactly TWO independent instances of this
// class - one per ExecuteTimingMode regime (see RenderGraph.h's own
// ExecuteTimingMode enum and RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md's V2
// Revision Note 2 for why the synchronous-offscreen and pipelined-present
// regimes must never share one slot range) - each sized with its own fixed,
// generous upper bound, matching this document's own "a pool sized... two
// generous, independently-sized fixed upper bounds" design. A pass name
// that exhausts its own regime's budget degrades gracefully (see
// AssignOrGetSlot() below) - never a crash, never silently aliasing another
// pass's slot.

#include <cstdint>
#include <cstring>
#include <vector>

namespace gte::rg {

// Sentinel: no slot could be assigned to this name in this table (its
// regime's fixed budget is already fully assigned to OTHER names) - never a
// valid slot index, since a real assigned slot is always >= 0.
inline constexpr std::int32_t kNoNameSlot = -1;

// A small, persistent (across many Execute() calls - NOT rebuilt every
// frame, unlike RenderGraphBuilder itself) name -> slot-index table, bounded
// by a fixed `slotBudget` decided once at construction and never resized.
// `name` is expected to be a string literal / static-storage-duration
// `const char*` (mirrors PassRecord::name's own rule, RenderGraphTypes.h) -
// compared by pointer first (the common case: the SAME pass re-declares the
// SAME string literal every frame) and by strcmp() as a fallback (in case
// two different call sites/frames happen to use two different string
// literals with identical contents).
class RenderGraphNameSlotTable {
public:
    explicit RenderGraphNameSlotTable(std::uint32_t slotBudget) noexcept
        : m_slotBudget(slotBudget)
    {
    }

    // Returns `name`'s existing slot if it has already been assigned one
    // (by this or an earlier call), or assigns and returns a brand-new one
    // from whatever budget remains, or kNoNameSlot if `name` is nullptr, OR
    // `name` has never been seen before AND this table's slotBudget is
    // already fully assigned to other names. Once assigned, a name's slot
    // never changes and never gets reassigned to a different name for this
    // table's entire lifetime.
    std::int32_t AssignOrGetSlot(const char* name) noexcept
    {
        if (name == nullptr) {
            return kNoNameSlot;
        }
        for (std::size_t i = 0; i < m_names.size(); ++i) {
            if (m_names[i] == name || std::strcmp(m_names[i], name) == 0) {
                return static_cast<std::int32_t>(i);
            }
        }
        if (m_names.size() >= static_cast<std::size_t>(m_slotBudget)) {
            return kNoNameSlot;
        }
        m_names.push_back(name);
        return static_cast<std::int32_t>(m_names.size() - 1);
    }

    // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - the exact inverse of
    // AssignOrGetSlot() above: returns the name previously assigned to
    // `slot` (via AssignOrGetSlot()), or nullptr if `slot` is out of range
    // (including kNoNameSlot, which is always negative) or was never
    // assigned. Needed by RenderGraph's own GPU-timing readback code to
    // turn "slot 3 in this regime's pool just resolved" back into "that
    // was the 'SceneView' pass" without RenderGraph having to keep its own,
    // second, parallel name<->slot table.
    const char* NameAtSlot(std::int32_t slot) const noexcept
    {
        if (slot < 0 || static_cast<std::size_t>(slot) >= m_names.size()) {
            return nullptr;
        }
        return m_names[static_cast<std::size_t>(slot)];
    }

    std::uint32_t SlotBudget() const noexcept { return m_slotBudget; }
    std::uint32_t AssignedCount() const noexcept { return static_cast<std::uint32_t>(m_names.size()); }

private:
    std::uint32_t m_slotBudget;
    std::vector<const char*> m_names; // index into this vector == assigned slot.
};

} // namespace gte::rg
