# PHASE1 — Core-Owned Debug Metadata Interfaces (write + read)

Parent: `PHASE0_MASTER_STRATEGY.md` — **MUST READ FIRST**, along with the
source design document it cites.

## Step 1: The Goal

Add ONE new, brand-new header,
`src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h`, that defines:

1. `PassDebugMetadata` — a plain, 3-field, copyable value struct
   (`RenderPassCategory category`, `RenderPassDrawKind drawKind`,
   `RenderPassTagMask tags`) — the exact payload the sink/provider pair
   moves around.
2. `IPassDebugMetadataSink` — Core-owned, pure-virtual, EXACTLY 2 methods:
   `OnPassDeclared(declarationIndexThisFrame, category, drawKind, tags)`
   and `BeginFrame()`. Write-only. Core defines the hook, never its
   meaning — mirrors `GpuMemoryTracker::DebugNameObserver`'s own "Core
   defines, Editor implements" shape, extended with the one additional
   lifecycle method (`BeginFrame()`) this table's per-frame shape requires
   (source doc, Section 3, difference (c)).
3. `IPassDebugMetadataProvider` — Core-owned, pure-virtual, EXACTLY 1
   method: `QueryPassDebugMetadata(declarationIndex, PassDebugMetadata& out) const -> bool`.
   Read-only, defensive (returns `false`, never throws, for an
   out-of-range index) — this is `PHASE0`'s own Locked Decision 1, the
   resolution to the "Core can't read Editor data" gap the source document
   itself never spells out a mechanism for.

Nothing else changes in this phase. This is pure, additive, brand-new
vocabulary — zero existing file is touched, zero existing behavior
changes. The whole point of doing this first is that the very next
incremental build after this phase is trivially easy to diagnose if it
somehow fails (only one new header, self-contained).

## Step 2: The Situation

- `src/Renderer/RenderGraph/RenderGraphTypes.h` already defines
  `RenderPassCategory` (~line 387), `RenderPassDrawKind` (~line 435), and
  `RenderPassTagMask` (~line 476, `using RenderPassTagMask = std::uint64_t;`)
  — this new header only needs to `#include "RenderGraphTypes.h"` to use
  all three by name; it does not redefine or relocate any of them (Non-Goal
  1 of the source document — enum/vocabulary ownership is untouched).
- `src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h` is the
  closest existing precedent for "a small, single-purpose, Core-owned
  sibling header under this exact folder" — read its own top-of-file
  comment for this folder's established documentation style/tone before
  writing the new header's own comments (do not skip writing thorough
  doc comments — every sibling header in this folder has them; this one
  should not be the exception).
- The root `CMakeLists.txt`'s `gte_core` (or equivalent) explicit source
  list already has an entry for
  `src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h` (confirmed,
  ~line 800). This new header's own `.h` line goes immediately next to
  the other `RenderGraph/` sibling headers in that exact same list (this
  header has NO `.cpp` — it is 100% inline, header-only, exactly like the
  source document's own worked example for `FrameDebuggerPassMetadataRecorder`;
  there is no implementation logic here beyond pure interface declarations
  and one plain struct, so no `.cpp` is needed or wanted).

## Step 3: The Plan

### 3.1 — New file: `src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h`

```cpp
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

#include <cstddef>

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
};

} // namespace gte::rg
```

### 3.2 — CMake registration

Add exactly these two lines to the root `CMakeLists.txt`'s explicit
`gte_core`-tier source list, immediately next to the existing
`src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h` entry
(~line 800 — re-check the exact line before editing, other campaigns may
have landed lines above/below it since this was written):

```
src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h
```

(Header-only — no matching `.cpp` line, since nothing in this file needs a
translation unit of its own: two pure-virtual interfaces and one plain
struct compile entirely from the header.)

### 3.3 — Verification (this phase)

No new test FILE is required for this phase in isolation — there is no
concrete implementation to instantiate yet (PHASE2 is the first real
implementer, and gets its own dedicated test file). Verification here is:

1. **Incremental compile check**: `cmake --build build` succeeds, and this
   new header, `#include`d from nowhere else yet, at minimum compiles
   standalone when included from a throwaway translation unit — the
   simplest real proof is PHASE2's own test file (which `#include`s it and
   derives a concrete class from both interfaces) compiling cleanly; if
   you want an EARLIER, PHASE1-local proof before PHASE2 exists, write a
   tiny, scratch, NOT-committed `.cpp` that does
   `#include "src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h"`
   and nothing else, compile it directly via the `gcc`/`g++` tool
   (`use_gpp: true`) with this project's real include paths, confirm zero
   errors, then delete the scratch file — never commit a throwaway
   compile-only probe file.
2. Confirm, by direct code inspection, that `IPassDebugMetadataSink`
   exposes EXACTLY 2 pure-virtual methods and `IPassDebugMetadataProvider`
   exposes EXACTLY 1 — this is a literal, later acceptance-criteria
   checkbox (Section 8 of the source document, adapted for the
   two-interface split PHASE0 locked in) — get it right here, once, rather
   than needing to revisit it in PHASE5.
3. `git_status` to confirm only the new header (and the one CMakeLists.txt
   line) changed — nothing else in the tree should differ after this
   phase.

### 3.4 — This phase's own completion report

Write `PHASE1_COMPLETION_REPORT.md` into this same folder covering: the
new header's full final content (or a diff), the CMakeLists.txt line
added, the compile-check method used and its result, and confirmation of
the "exactly 2 / exactly 1 methods" checks above. Commit both the code
change and the report together (`git_add` + `git_commit`).

If you discover ANY ambiguity not already resolved by this file or
`PHASE0_MASTER_STRATEGY.md` — use `ask_questions` before proceeding. If you
delegate any part of this phase to a sub-task, that sub-task MUST also be
instructed to use `ask_questions` for its own ambiguities.
