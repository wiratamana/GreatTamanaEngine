# PHASE1 — Setup Layer: Ordered, N-Ary Color Attachment Declarations

Parent: `PHASE0_MASTER_STRATEGY.md` — **read that file first.** This phase
implements the how-to document's "STAGE 1 — SETUP LAYER" in full, plus the
Tier-1 half of "STAGE 6 — VERIFICATION" (proving the compiler/snapshot layers
genuinely need zero changes, rather than trusting that claim blindly).

**Use `ask_questions` whenever you hit a genuine ambiguity or a design choice
this document (or `PHASE0_MASTER_STRATEGY.md`) doesn't already pin down.** If
you delegate any further sub-task, that delegation prompt must repeat this
same instruction.

## Step 1: The Goal (Where are we going?)

Let a render-graph pass declare an ORDERED list of N color attachments
(`N` up to 8), where attachment index in that list deterministically equals
the shader's `layout(location = N) out` index — with each attachment
carrying its own, independent, optional clear color — while every existing
single-color-attachment pass in the engine keeps compiling and behaving
completely unmodified (same handle shape, same `WriteColorAttachment()`
call signature for the common case).

## Step 2: The Situation (Where are we now?)

- `PassRecord` (`src/Renderer/RenderGraph/RenderGraphTypes.h`, line 533) has
  `reads`/`writes` (generic `std::vector<ResourceUsage>`) and exactly one
  `colorClearValue`/`depthClearValue` pair (line 599-600). There is no
  ordered, index-addressable color-attachment concept anywhere.
- `RenderGraphBuilder::PassBuilder::WriteColorAttachment()`
  (`RenderGraphBuilder.cpp` line 11-18) pushes ONE `ResourceUsage` onto
  `pass.writes` and unconditionally overwrites `pass.colorClearValue` if a
  clear color was supplied — called twice on one pass today, the SECOND
  call's clear color silently wins, with no way to know which handle it was
  even "for".
- `RenderGraphBuilder.h` line 189-200's doc comment on `WriteColorAttachment()`
  explicitly states the old assumption in writing: "A pass with more than one
  `WriteColorAttachment()`/`WriteDepthStencilAttachment()` call this frame
  simply has its LAST supplied clear value (if any) win — the Phases 1-8 MVP
  never declares more than one color/depth write per pass anyway". This
  comment must be corrected/replaced as part of this phase (it will become
  actively misleading once this phase ships).
- `ResourceUsage`/`PassRecord::writes` are ALREADY correctly N-ary at the
  `RenderGraphCompiler`/`RenderGraphSnapshot` level (confirmed directly —
  see `PHASE0_MASTER_STRATEGY.md`'s Step 2) — this phase's job is to make the
  **ordering** explicit and addressable (for STAGE 2's attachment-index ==
  shader-location contract), not to teach those two files anything new about
  multiplicity, which they already handle.
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` already has 3
  tests exercising `WriteColorAttachment()`'s CURRENT (single-write, last-
  clear-wins) behavior:
  `PassBuilderWriteColorAttachmentAppendsColorAttachmentWrite`,
  `WriteColorAttachmentWithNoClearColorLeavesColorClearValueEmpty`,
  `WriteColorAttachmentWithClearColorRecordsItOnThePass` (lines 198-266).
  These must keep passing UNCHANGED after this phase (they only ever call
  `WriteColorAttachment()` ONCE per pass, so their assertions about
  `pass.writes.size() == 1`/`pass.colorClearValue` remain valid under the new
  shape) — do not "helpfully" rewrite them; only ADD new tests for the
  multi-attachment case.

## Step 3: The Plan (How do we get there?)

### 3.1 — `RenderGraphTypes.h`: the new ordered attachment descriptor

Add, near `PassRecord` (before its definition, following this file's own
convention of small plain structs preceding the struct that uses them):

```cpp
// Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE1 - one
// ORDERED color-attachment slot on a pass. Attachment INDEX in
// PassRecord::colorAttachments (below) is the CONTRACT: it equals the
// shader's own `layout(location = N) out` index for that pass's fragment
// shader. This is deliberately NOT inferred by scanning `writes` (a pass's
// `writes` vector may, in the future, also contain non-attachment texture
// writes once mixed with WriteTexture()-style compute writes on the same
// pass - order there is declaration order for ALL kinds combined, not
// guaranteed to match render-target order) - `colorAttachments` is the one
// and only source of truth for "which handle is attachment slot N", kept
// in lockstep with a mirrored ColorAttachmentWrite entry in `writes` (see
// PassBuilder::WriteColorAttachment() below) purely so barrier planning/
// dependency-edge computation - which only ever reads `writes`, never this
// new field - keeps working with NO changes at all.
struct ColorAttachmentDesc {
    TextureHandle handle;
    std::optional<std::array<float, 4>> clearColor;
};
```

Cap constant, near `kInvalidIndex`:

```cpp
// Multi-Render-Target (MRT) campaign, PHASE1 - matches typical real-world
// VkPhysicalDeviceLimits::maxColorAttachments (locked via ask_questions,
// see task_manager/mrt-1/PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision 2). Enforced by an assert() in
// RenderGraphBuilder::PassBuilder::WriteColorAttachment() (RenderGraphBuilder.cpp) -
// never silently truncated.
inline constexpr std::uint32_t kMaxColorAttachments = 8;
```

On `PassRecord` itself: **ADD** a new field, appended at the END of the
struct (never inserted in the middle — this file's own established rule,
see e.g. `category`/`drawKind`/`renderPassEvent`'s own identical placement
comments):

```cpp
    // Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE1 - the
    // ordered list of this pass's color attachments (index == shader
    // `layout(location = N) out`) - see ColorAttachmentDesc's own doc
    // comment above. Populated exclusively by
    // RenderGraphBuilder::PassBuilder::WriteColorAttachment() (one push_back
    // per call, in call order) - NEVER read by RenderGraphCompiler.cpp
    // (culling/lifetime/dependency-edge computation still only ever reads
    // `writes`, completely unchanged by this campaign) - read ONLY by
    // RenderGraph::ExecuteCompiledGraph() (PHASE2 of this campaign) to build
    // the real vkCmdBeginRendering attachment array, in this exact order.
    // Size is bounded by kMaxColorAttachments (asserted in
    // WriteColorAttachment(), never here).
    std::vector<ColorAttachmentDesc> colorAttachments;
```

**Do NOT remove `colorClearValue`.** Two options exist for how
`colorClearValue` and the new per-attachment `ColorAttachmentDesc::clearColor`
coexist — resolve this via a quick self-check against Phase 2's needs, or use
`ask_questions` if genuinely unsure before proceeding:
- Recommended: `colorClearValue` becomes legacy/unused by any NEW code this
  campaign writes (PHASE2 reads clear colors exclusively from
  `pass.colorAttachments[i].clearColor` from now on) but is left in place,
  untouched, since existing single-attachment call sites
  (`src/Application/RenderPasses.cpp`, `Application.cpp`) still go through
  `WriteColorAttachment()`, which this phase changes to ALSO populate
  `colorAttachments[0].clearColor` — so as long as PHASE2 reads exclusively
  from the new field, `colorClearValue` naturally becomes dead weight you may
  choose to remove in this same phase (grep every reader first — only
  `RenderGraph.cpp` line 426-429 reads it) or leave for PHASE2 to clean up.
  Prefer removing it HERE, in the same phase that stops writing anything new
  into it being the source of truth, to avoid a confusing in-between state —
  but only after confirming (via `search_in_dir`) that `RenderGraph.cpp`'s
  read of it is updated in the SAME phase or immediately after in PHASE2. If
  in doubt about sequencing, ask.

### 3.2 — `RenderGraphBuilder.h`/`.cpp`: `WriteColorAttachment()` appends

Replace `RenderGraphBuilder::PassBuilder::WriteColorAttachment()`
(`RenderGraphBuilder.cpp` line 11-18) with:

```cpp
void RenderGraphBuilder::PassBuilder::WriteColorAttachment(
    TextureHandle handle, const std::optional<std::array<float, 4>>& clearColor)
{
    assert(m_pass.colorAttachments.size() < kMaxColorAttachments &&
        "RenderGraphBuilder::PassBuilder::WriteColorAttachment: exceeded kMaxColorAttachments per pass");

    // ALWAYS ALSO push a ColorAttachmentWrite onto `writes` (Phase 6's
    // original behavior, kept byte-for-byte) - this is what keeps
    // RenderGraphCompiler's dependency-edge/culling computation and
    // RenderGraph::ApplyUsageBarrierIfNeeded()'s per-usage barrier loop
    // working per-target with ZERO changes to either - see
    // task_manager/mrt-1/PHASE0_MASTER_STRATEGY.md's Step 2.
    m_pass.writes.push_back(ResourceUsage::ForTexture(handle, ResourceAccess::ColorAttachmentWrite));

    ColorAttachmentDesc desc;
    desc.handle = handle;
    desc.clearColor = clearColor;
    m_pass.colorAttachments.push_back(desc);
}
```

Update `RenderGraphBuilder.h`'s doc comment on `WriteColorAttachment()`
(line 189-200) — replace the sentence "A pass with more than one
`WriteColorAttachment()`/... call this frame simply has its LAST supplied
clear value (if any) win — the Phases 1-8 MVP never declares more than one
color/depth write per pass anyway" with an accurate description of the new
append behavior, referencing `ColorAttachmentDesc`/`PassRecord::colorAttachments`
and this campaign (`task_manager/mrt-1`). `WriteDepthStencilAttachment()`
is explicitly UNCHANGED by this campaign (still exactly one depth attachment
per pass, per `PHASE0`'s own scope note) — do not touch it beyond, if
necessary, a small doc-comment clarification that it remains single-slot by
design.

### 3.3 — Tier-1 tests: prove the claim, don't just trust it

In `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`, ADD (do not
modify the 3 existing tests named in Step 2 above):

- `WriteColorAttachmentCalledTwiceAppendsBothInOrder` — build one pass,
  call `WriteColorAttachment(handleA, clearA)` then
  `WriteColorAttachment(handleB, clearB)`; assert
  `pass.colorAttachments.size() == 2`,
  `pass.colorAttachments[0].handle == handleA`,
  `pass.colorAttachments[0].clearColor == clearA`,
  `pass.colorAttachments[1].handle == handleB`,
  `pass.colorAttachments[1].clearColor == clearB`, AND
  `pass.writes.size() == 2` (both `ColorAttachmentWrite`, in the same order).
- `WriteColorAttachmentExceedingCapAssertsInDebug` — a death-test
  (`ASSERT_DEATH`/`EXPECT_DEATH`, mirroring this codebase's existing death-
  test precedent — see `RenderGraphBarrierPlannerDeathTest` in
  `build/Testing`'s test list for the exact macro/style already in use in
  this repo) calling `WriteColorAttachment()` `kMaxColorAttachments + 1`
  times on one pass. Only meaningful in a build where `assert()` is live
  (non-`NDEBUG`) — follow whatever existing death-test guard convention this
  repo already uses for its other death tests (search for one nearby, e.g.
  `RenderGraphBarrierPlannerDeathTest`, and mirror its exact CMake/gating
  shape).

In `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`, ADD:

- `PassWithThreeColorWritesSurvivesCompileAndIsNotCulled` — build a graph
  with ONE pass declaring three `WriteColorAttachment()` calls against three
  distinct `CreateTexture()` handles, with all three reachable from
  `finalOutputs` (e.g. via a later pass that `ReadTexture()`s all three, kept
  alive itself), and assert the G-buffer-style pass is NOT culled after
  `Compile()`. This directly proves the how-to document's "Compile stage
  needs ZERO changes" claim for a REAL 3-write pass, not just today's
  existing 1-write tests.

In `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`, ADD:

- `SnapshotOfThreeColorWritePassListsAllThreeWriteNames` — the same
  3-write pass shape as above, asserting the resulting
  `RenderGraphPassSnapshot::writeNames`/`writeKinds` have exactly 3 entries,
  matching the 3 declared handles' real names, in declaration order. This
  directly proves the how-to document's "Snapshot needs ZERO changes, would
  already show 3 write names today" claim.

If ANY of these three new tests reveal that `RenderGraphCompiler.cpp` or
`RenderGraphSnapshot.cpp` actually DOES need a change (contradicting
`PHASE0`'s Step 2 findings) — STOP, do not silently patch either file beyond
what's minimally necessary to make the test pass, write up exactly what was
wrong in `PHASE1_COMPLETION_REPORT.md`, and use `ask_questions` to confirm
the fix before proceeding to PHASE2 — this would be a genuine, unplanned
scope change to `PHASE0`'s own Non-Goals.

### 3.4 — What this phase does NOT touch

- `RenderGraph::ExecuteCompiledGraph()` (`RenderGraph.cpp`) — still reads the
  OLD single-`colorHandle` shape (line 332-354/407-483) after this phase;
  PHASE2 rewrites it to read `pass.colorAttachments` instead. This phase's
  own new field is dead/unread code from `RenderGraph.cpp`'s point of view
  until PHASE2 lands — that's fine and expected; confirm the engine still
  builds and every existing pass still renders identically after this phase
  (a fast, targeted incremental build only — no full regression, per
  `PHASE0`'s Locked Design Decision 8).
- `Pipeline.h`/`.cpp`, `GpuResourceFactory` — untouched (PHASE3).
- No new pass is declared anywhere yet (PHASE4).

### Definition of Done for this phase

- `RenderGraphTypes.h` has `ColorAttachmentDesc`, `kMaxColorAttachments`, and
  `PassRecord::colorAttachments`.
- `WriteColorAttachment()` appends to both `pass.writes` AND
  `pass.colorAttachments`, asserts the cap, and its doc comment is corrected.
- The 3 pre-existing `RenderGraphBuilderTests.cpp` tests still pass,
  unmodified.
- New tests (Step 3.3) exist and pass, proving multi-write survives
  `Compile()`/`BuildRenderGraphSnapshot()` unmodified.
- A fast, targeted incremental compile of the affected translation units (at
  minimum: `RenderGraphTypes.h`/`RenderGraphBuilder.cpp`/every test file
  above) succeeds with no warnings promoted to errors.
- `PHASE1_COMPLETION_REPORT.md` written, documenting the `colorClearValue`
  disposition decision from Step 3.1 and any test-driven discrepancy found
  in Step 3.3 (or explicitly confirming none was found).
