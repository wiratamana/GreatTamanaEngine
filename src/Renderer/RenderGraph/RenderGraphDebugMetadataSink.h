#pragma once

// editor-core-separation-25 campaign - "Core/Editor Separation: PassRecord
// Debug Metadata Sink" (source doc:
// BIG_STEP_1_CORE_EDITOR_SEPARATION_PASSRECORD_METADATA_SINK_2026-09-29.txt).
//
// PassRecord::category/::drawKind/::tags (RenderGraphTypes.h) are, each
// individually, PURELY DESCRIPTIVE metadata read by NOTHING in
// RenderGraph.cpp/RenderGraphCompiler.cpp/RenderGraphBarrierPlanner.cpp -
// their only real reader is the Editor's Frame Debugger. This header
// defines the Core-owned, opaque HOOK PAIR that replaces permanently
// storing all three on the hot, always-live PassRecord struct - mirrors
// GpuMemoryTracker::DebugNameObserver's own "Core defines the hook, Editor
// installs the real implementation" shape (see
// docs/conventions/gpu-resource-memory-tracking.md), with two deliberate
// differences from that precedent (both explained fully in the source
// document's own Section 3):
//
//   (a) IPassDebugMetadataSink carries a SECOND method, BeginFrame(),
//       alongside OnPassDeclared() - a PassRecord has no stable identity
//       across frames (unlike a GpuResourceHandle), so the real
//       implementation's own per-frame table MUST be told exactly when a
//       fresh declaration sequence starts.
//
//   (b) a SEPARATE, second interface, IPassDebugMetadataProvider, exists
//       purely for READING this same data back out - GpuMemoryTracker's
//       own read side (EditorGpuMemoryNameOverlay::GetDebugName()) is a
//       plain static accessor Editor UI code calls directly, because that
//       UI code is already Editor-tier. RenderGraph::ExecuteCompiledGraph()
//       (Core) has no equivalent direct-call option when it builds this
//       frame's RenderGraphSnapshot - it needs a way to ask "what was
//       declared at index N" without ever knowing the real, concrete,
//       Editor-owned answer. IPassDebugMetadataSink itself stays EXACTLY
//       the two write-only methods the source document's own TR1
//       specifies; this read-only sibling is the one piece of NEW
//       mechanism this campaign's own analysis added on top of the source
//       document (confirmed with the user via ask_questions before this
//       phase was implemented - see PHASE0_MASTER_STRATEGY.md's own Locked
//       Decision 1).
//
// Neither interface is ever implemented anywhere in Core - only
// src/Editor/FrameDebuggerPassMetadataRecorder.h (PHASE2) implements both,
// on one concrete object. This codebase has no `GTE_ENABLE_EDITOR`
// preprocessor macro anymore - the Core/Editor boundary is enforced by
// which of the two separately-linked static libraries a file compiles
// into (`gte_core` vs. `gte_editor` - see AGENTS.md's "`gte_core` /
// `gte_editor` Library Separation" section), never by a compile-time
// `#if`. A Player-style build that links `gte_core` alone (never
// `gte_editor` - see tools/ci/gte_core_player_link_probe/) never
// constructs that object at all, so RenderGraph's own installed pointers
// (PHASE4) stay nullptr forever, and every call site guarded by a
// null-pointer check (AddRenderPass(), RenderGraph::Execute()/
// ExecuteCompiledGraph()) costs exactly one branch and stores nothing.

#include "RenderGraphTypes.h"

#include <volk.h>

#include <cstddef>
#include <string>


namespace gte::rg {

// The exact payload PassRecord used to store permanently for these three
// fields - now moved off PassRecord and threaded through this hook pair
// instead. A plain, copyable, no-behavior value struct - never compared for
// equality anywhere (mirrors PassRecord's own "never upserted by name" rule
// for the exact same reason: there is nothing here worth deduplicating by
// value).
struct PassDebugMetadata {
    RenderPassCategory category = RenderPassCategory::General;
    RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;
    RenderPassTagMask tags = 0;
};

// WRITE side. Core-owned, pure-virtual, opaque - Core never implements
// this, never knows what a "category" or a "tag" MEANS, and never knows
// what BeginFrame() actually clears. The ONE call site that ever invokes
// OnPassDeclared() is RenderGraphBuilder::AddRenderPass() (PHASE3) - never
// AddPass()/AddComputePass(), which stamp only PassRecord::kind/::viewScope
// and are explicitly out of this campaign's scope (see the source
// document's own Section 2 for the full reasoning). The ONE call site that
// ever invokes BeginFrame() is RenderGraph::Execute()'s own template body
// (PHASE4), immediately after constructing that call's own fresh
// RenderGraphBuilder and BEFORE that call's build(builder) callback ever
// runs.
class IPassDebugMetadataSink {
public:
    virtual ~IPassDebugMetadataSink() = default;

    // declarationIndexThisFrame is always exactly equal to however many
    // passes have been declared so far THIS sequence (a dense,
    // monotonically increasing 0, 1, 2, ... index with no gaps) - the real
    // implementation's own invariant to maintain, never this interface's
    // job to enforce. Fires exactly once per pass declared via
    // AddRenderPass(), synchronously, at declaration time (long before
    // that pass's own execute() callback ever runs, and long before the
    // graph this pass belongs to is compiled/executed at all).
    virtual void OnPassDeclared(std::size_t declarationIndexThisFrame, RenderPassCategory category,
        RenderPassDrawKind drawKind, RenderPassTagMask tags) = 0;

    // Must be called exactly once per fresh graph declaration - i.e. once
    // per RenderGraph::Execute() call, unconditionally, before that call's
    // own build(builder) callback runs. A PassRecord has no stable
    // identity across frames (RenderGraphTypes.h's own PassRecord doc
    // comment), so the real implementation's own dense, index-addressed
    // table has no cross-frame identity to fall back on - this is what
    // tells it "a new sequence starts now, forget the previous one". Core
    // only ever knows this must be called once per fresh declaration - it
    // never learns what the real implementation clears.
    virtual void BeginFrame() = 0;

    // Fires once per WRITE usage that actually required a barrier, at
    // execute time (RenderGraph::Execute()'s own per-pass barrier loop).
    // declarationIndex matches OnPassDeclared()'s own index space (same
    // pass, same frame).
    virtual void OnResourceBarrierApplied(std::size_t declarationIndex, const std::string& resourceName,
        VkImageLayout oldLayout, VkImageLayout newLayout) = 0;
};

// READ side. Core-owned, pure-virtual, opaque - the ONE piece of NEW
// mechanism this campaign's own analysis added beyond the source
// document's own worked example (see this header's own top comment,
// difference (b)). Deliberately DEFENSIVE (returns bool, never throws) -
// unlike the source document's own FrameDebuggerPassMetadataRecorder::At()
// example (which uses std::vector::at(), throwing on an out-of-range
// index), a Core caller building a snapshot must never crash/throw just
// because it queries an index the real implementation does not (yet, or
// any longer) have an entry for - e.g. querying before ANY pass has been
// declared this sequence, or querying a stale index left over from a
// mismatched BeginFrame()/OnPassDeclared() call order bug elsewhere. The
// ONE call site that ever invokes this is
// RenderGraphSnapshot.cpp's BuildRenderGraphSnapshot()/BuildPassSnapshot()
// (PHASE3), via a caller-supplied std::function the way statsLookup
// already works - RenderGraph::ExecuteCompiledGraph() (PHASE4) is the one
// production code path that actually constructs that std::function from a
// real, installed IPassDebugMetadataProvider*.
class IPassDebugMetadataProvider {
public:
    virtual ~IPassDebugMetadataProvider() = default;

    // Returns true and fills outMetadata when declarationIndex has a real,
    // currently-live entry (i.e. it was OnPassDeclared()'d since the most
    // recent BeginFrame() on the SAME concrete object this provider
    // pointer refers to - always true in practice, since RenderGraph
    // installs both pointers onto the SAME object, see PHASE4). Returns
    // false (leaving outMetadata completely untouched) for any
    // out-of-range or not-yet-declared index - the caller must treat this
    // exactly like "no sink was installed at all" (i.e. leave
    // RenderGraphPassSnapshot's own category/drawKind/tags fields at their
    // own struct defaults for that one pass), never a hard failure.
    virtual bool QueryPassDebugMetadata(std::size_t declarationIndex, PassDebugMetadata& outMetadata) const = 0;

    // Returns true and fills outLabel when a barrier transition label was
    // recorded for this (declarationIndex, resourceName) pair this frame.
    // False means "no barrier was applied this frame" - never a hard
    // failure, same defensive convention as QueryPassDebugMetadata().
    virtual bool QueryBarrierTransitionLabel(
        std::size_t declarationIndex, const std::string& resourceName, std::string& outLabel) const = 0;
};

} // namespace gte::rg
