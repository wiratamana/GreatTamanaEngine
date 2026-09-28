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
//
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE5 - found-and-fixed a REAL, CONFIRMED-BY-CRASH dangling-
// pointer hazard, discovered live by this phase's own isolation test (the
// FIRST time anything in this engine's history ever actually FreeLibrary()'d
// a module whose own static-storage string literal had been captured here).
// This table used to store a raw `const char*` per assigned slot, on the
// documented assumption that every debugName is a process-lifetime string
// literal (true for every BUILT-IN pass, e.g. "RenderOpaque"/"SceneView") -
// but a Project Assembly's own RenderPassDesc::debugName (RenderPipeline.h)
// is a string literal living INSIDE THAT PROJECT ASSEMBLY'S OWN .dll IMAGE,
// which ProjectAssemblyHost::UnloadProjectAssembly() can now genuinely
// FreeLibrary() at runtime (Hazard 4, this campaign). Since THIS table is
// the ONE place a name is deliberately kept ALIVE PAST the pass's own single-
// frame declaration lifetime (every other `const char*` debugName usage in
// the render graph is re-read fresh each frame while the pass is still being
// declared, and simply stops being touched the frame after a pass disappears)
// - RenderGraph::FinalizeSynchronousGpuTiming()/Execute()'s own pipelined-
// readback preamble both loop over EVERY EVER-ASSIGNED slot, UNCONDITIONALLY,
// every single frame, regardless of whether that pass ran this frame -
// calling NameAtSlot() on a slot whose pass had since been unregistered and
// its owning .dll unloaded returned a genuinely DANGLING pointer, and the verified
// crash: RenderGraph::UpdateTimingFor()'s own std::strcmp() against that
// dangling pointer immediately AccessViolation-crashed the WHOLE process, on
// the very first frame after ProjectAssemblyHost::UnloadProjectAssembly()
// returned. Fixed by making this table OWN a real copy of every name
// (std::string) instead of ever storing a raw pointer whose lifetime it does
// not control - the fast, common "same pass re-declares the same string
// literal every frame" comparison path is preserved (std::string::operator==
// against a const char* short-circuits on length before touching content,
// and the slot budget here is always small/bounded, so this is not a hot-loop
// concern), it simply never trusts that pointer to remain valid AFTER the
// comparison/copy.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gte::rg {

// Sentinel: no slot could be assigned to this name in this table (its
// regime's fixed budget is already fully assigned to OTHER names) - never a
// valid slot index, since a real assigned slot is always >= 0.
inline constexpr std::int32_t kNoNameSlot = -1;

// A small, persistent (across many Execute() calls - NOT rebuilt every
// frame, unlike RenderGraphBuilder itself) name -> slot-index table, bounded
// by a fixed `slotBudget` decided once at construction and never resized.
// `name` is typically a string literal / static-storage-duration `const
// char*` (mirrors PassRecord::name's own rule, RenderGraphTypes.h) - but,
// per this class's own PHASE5 fix (see file header comment above), this
// table always makes its OWN OWNED COPY of whatever it is given, precisely
// because a Project Assembly's own debugName literal can genuinely stop
// being valid at runtime (FreeLibrary()) while a BUILT-IN pass's literal
// never does - this table cannot tell the two apart, so it never trusts
// EITHER kind of caller to keep its own pointer alive past this call.
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
            m_lastCallOverflowed = false;
            return kNoNameSlot;
        }
        for (std::size_t i = 0; i < m_names.size(); ++i) {
            if (m_names[i] == name) {
                m_lastCallOverflowed = false;
                return static_cast<std::int32_t>(i);
            }
        }
        if (m_names.size() >= static_cast<std::size_t>(m_slotBudget)) {
            m_lastCallOverflowed = true;
            return kNoNameSlot;
        }
        m_lastCallOverflowed = false;
        m_names.emplace_back(name); // OWNED copy - see this class's own doc comment above for why.
        return static_cast<std::int32_t>(m_names.size() - 1);
    }

    // PHASE1 (render-pass-6 campaign, item 2.4) - true if and only if the MOST
    // RECENT AssignOrGetSlot() call returned kNoNameSlot specifically because
    // this table's fixed slotBudget was already fully assigned to OTHER names
    // (a genuinely new name, budget genuinely exhausted) - false for every other
    // outcome (name == nullptr, or name already had a slot). Lets a caller
    // distinguish "this exact call just discovered a real overflow" from
    // "kNoNameSlot for an unrelated, harmless reason", without AssignOrGetSlot()
    // itself needing to log/assert/mutate any wider state - see RenderGraph.cpp's
    // own call site for how this drives a real, one-time diagnostic.
    bool JustOverflowed() const noexcept { return m_lastCallOverflowed; }

    // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - the exact inverse of
    // AssignOrGetSlot() above: returns the name previously assigned to
    // `slot` (via AssignOrGetSlot()), or nullptr if `slot` is out of range
    // (including kNoNameSlot, which is always negative) or was never
    // assigned. Needed by RenderGraph's own GPU-timing readback code to
    // turn "slot 3 in this regime's pool just resolved" back into "that
    // was the 'SceneView' pass" without RenderGraph having to keep its own,
    // second, parallel name<->slot table. editor-core-separation-13
    // campaign, PHASE5 - the returned pointer is now this table's OWN
    // `std::string::c_str()` (valid for as long as this table itself is,
    // i.e. this whole RenderGraph's lifetime), never a possibly-already-
    // unloaded caller's own memory - see this class's own header comment.
    const char* NameAtSlot(std::int32_t slot) const noexcept
    {
        if (slot < 0 || static_cast<std::size_t>(slot) >= m_names.size()) {
            return nullptr;
        }
        return m_names[static_cast<std::size_t>(slot)].c_str();
    }

    std::uint32_t SlotBudget() const noexcept { return m_slotBudget; }
    std::uint32_t AssignedCount() const noexcept { return static_cast<std::uint32_t>(m_names.size()); }

private:
    std::uint32_t m_slotBudget;
    std::vector<std::string> m_names; // index into this vector == assigned slot. OWNED copies - see file header comment.
    // PHASE1 (render-pass-6 campaign, item 2.4) - see JustOverflowed() above.
    bool m_lastCallOverflowed = false;
};

} // namespace gte::rg
