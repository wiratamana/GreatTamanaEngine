# PHASE2 — Completion Report: Execute Layer, Record Real MRT Vulkan Calls

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document:
`PHASE2_EXECUTE_LAYER_MRT_RECORDING.md`.

## Status: DONE

`PHASE1_COMPLETION_REPORT.md` was read in full before starting, per this
phase's own prerequisite. It confirms `PassRecord::colorClearValue` was
**KEPT** (not removed) by PHASE1 — `WriteColorAttachment()` populates BOTH
`colorClearValue` (legacy, single-slot) and the new, ordered
`colorAttachments` list on every call, specifically so
`RenderGraph::ExecuteCompiledGraph()` (untouched by PHASE1) kept reading a
valid value until this phase landed. This phase's own plan depended exactly
on that fact (Step 2's "load-bearing fact" analysis assumes
`WriteColorAttachment()` always pushes onto both `pass.writes` AND
`pass.colorAttachments` in lockstep) — re-confirmed directly against
`RenderGraphBuilder.cpp` before writing any code, not just trusted from the
report.

## ⚠️ Administrative correction (branch), noted per this campaign's own
recursive "tell this forward" instruction

The top-level task instructions handed to this phase said "Stay on the
current branch: `feature/logger-impl`". This was a stale/mistaken
copy-paste (the same class of error `PHASE0_MASTER_STRATEGY.md` itself
already had to correct once, for the `task_manager/logger-1/` folder path) —
`feature/logger-impl` is a real, but wholly unrelated, already-finished
branch. The repository's actual current branch at the start of this phase
was `feature/render-pass-impl` (confirmed via `git status`/`git branch`),
which already carried PHASE1's committed MRT changes with a clean working
tree. Confirmed directly with the user via `ask_questions`: **stayed on
`feature/render-pass-impl`, never touched `feature/logger-impl`.** Any
future phase/campaign step that inherits this same "stay on the current
branch: X" instruction should re-verify X against the actual `git status`
output before assuming it's correct, exactly as this phase did — this
instruction is recorded here so it propagates forward (per this campaign's
own recursive `ask_questions`-delegation rule).

## What was done

### 3.1 — Replaced the attachment-scan with a direct read of `pass.colorAttachments`

`RenderGraph::ExecuteCompiledGraph()`'s old scan (`RenderGraph.cpp`) that
walked `pass.writes` looking for `IsColorAttachmentWriteAccess()` and kept
only the LAST match was replaced exactly as the strategy document specified:

- Depth detection is UNCHANGED — still a plain scan of `pass.writes` for
  `TargetsDepthState()`.
- `hasColorWrite` is now `!pass.colorAttachments.empty()` — a direct read of
  PHASE1's new, ordered list, never a re-scan of `pass.writes` for color.
- `IsColorAttachmentWriteAccess()` is no longer called from this file (still
  defined/used elsewhere — `RenderGraphBarrierPlanner.cpp`'s own
  implementation, its own Tier-1 tests, and `RenderGraphBuilder.h`'s doc
  comment) — no `#include` was removed, since `RenderGraphBarrierPlanner.h`
  is still needed here for `TargetsDepthState()`.

### 3.2 — N attachments instead of 1

`RenderGraph::ExecuteCompiledGraph()`'s `if (hasColorWrite) { ... }` block
was rewritten to build one `VkRenderingAttachmentInfo` per entry in
`pass.colorAttachments`, in order, exactly per the strategy document's own
literal code (both locked decisions honored verbatim):

- **Decision 1 honored**: a new, pure, `noexcept`,
  Tier-1-testable free function, `FindMismatchedColorAttachmentExtent()`,
  was added to `RenderGraphTypes.h`/`.cpp` (placed immediately after
  `ColorAttachmentDesc`, per the document's own suggested location — no
  reason was found to prefer `RenderGraphBarrierPlanner.h`/`.cpp` instead,
  so no `ask_questions` was needed for this). It takes a
  `const std::vector<VkExtent2D>&` and returns the 0-based index of the
  first entry that differs from entry 0, or `std::nullopt` for 0/1 entries
  or N identical entries.
- **Decision 2 honored**: a genuinely mismatched extent throws
  `std::runtime_error` unconditionally (never a plain `assert()`), naming
  the pass and both conflicting extents/indices. A separate, cheap,
  debug-only `assert()` (compiles out entirely in `NDEBUG`) defensively
  confirms every `pass.colorAttachments[i].handle` was already resolved
  before being indexed into `physicalTextures` — this is provably always
  true today (see the "load-bearing fact" in the strategy doc's Step 2 and
  re-confirmed above), so this assert is a pure safety net, never expected
  to fire.
- `renderingInfo.colorAttachmentCount`/`pColorAttachments` are now built
  from the real `colorAttachmentInfos` vector's size/data, instead of the
  old hardcoded `1`/`&colorAttachment`.
- `renderArea`/viewport/scissor/`ctx.colorAttachmentExtent` are now all
  sized from `firstExtent` (`resolvedExtents[0]`) instead of the old
  `colorTex.target.extent` local — identical value for every existing
  single-attachment pass, since there's only one entry to be "first".
- The depth-attachment build (`hasDepthWrite`/`depthHandle`/
  `pass.depthClearValue`) and its `renderingInfo.pDepthAttachment` wiring
  are **byte-for-byte unchanged**, exactly as scoped.

### 3.3 — `pass.colorClearValue` disposition (this phase's own decision)

Per the phase document's own Step 3.3: since PHASE1 left `colorClearValue`
in place (see above), this phase's job was to "remove every remaining read
of it in `RenderGraph.cpp`" (**done** — the rewritten attachment-build code
reads `desc.clearColor` exclusively; `RenderGraph.cpp` no longer mentions
`colorClearValue` anywhere, confirmed via `search_in_dir`) and to
"**consider** removing the now-fully-dead field from `RenderGraphTypes.h`"
(explicitly phrased as optional, not locked).

**Decision made**: `PassRecord::colorClearValue` was **NOT** removed, and
`RenderGraphBuilder::PassBuilder::WriteColorAttachment()` still populates it
on every call, unchanged from PHASE1. Reasoning:

- Two of PHASE1's own pre-existing tests
  (`WriteColorAttachmentWithNoClearColorLeavesColorClearValueEmpty`,
  `WriteColorAttachmentWithClearColorRecordsItOnThePass` in
  `RenderGraphBuilderTests.cpp`) assert directly on
  `input.passes[0].colorClearValue` — removing the field would require
  also rewriting those two tests, which is genuine, avoidable scope creep
  for what `PHASE0_MASTER_STRATEGY.md` itself calls "the HIGHEST-RISK phase
  in the whole campaign". The phase document's own "consider removing" is
  optional language, not a locked requirement, so keeping the field is a
  valid choice within scope.
- This mirrors PHASE1's own precedent exactly: PHASE1 explicitly chose to
  keep `colorClearValue` populated specifically to avoid touching those
  same two tests, even though its own strategy document's illustrative code
  sample suggested otherwise. Keeping it once more, now that it really is
  fully dead in production code, is the same judgment call for the same
  reason.
- `colorClearValue` is now genuinely, verifiably DEAD WEIGHT in production:
  nothing under `src/` reads it anymore (confirmed via `search_in_dir` for
  `colorClearValue` across `src/` — the only remaining hits are the
  `WriteColorAttachment()` write in `RenderGraphBuilder.cpp` and doc
  comments). A future cleanup phase (PHASE3/4/5, or a dedicated follow-up)
  is free to remove it together with updating the two dependent tests, at
  a time when that isn't competing with this phase's own "minimize risk in
  the highest-blast-radius function" mandate.
- This did not require `ask_questions` — the phase document's own "consider
  removing" phrasing already flagged this as implementer's discretion, not
  an open ambiguity needing the project owner's input.
- Updated `RenderGraphBuilder.h`'s own doc comment on `WriteColorAttachment()`
  to correct its now-stale "it remains what `RenderGraph::ExecuteCompiledGraph()`
  reads until PHASE2... switches it" wording (that switch already happened) —
  it now correctly states `colorClearValue` is dead weight in production as
  of this phase, kept only for the two tests' sake.

### 3.4 — `PassContext` needs no new fields

Confirmed, unchanged — `ctx.colorAttachmentExtent` is populated exactly as
before (from `firstExtent` now, instead of the old single `colorTex.target.extent`
local, but the same value for every existing pass), and no other
`PassContext` field needed touching.

### 3.5 — What this phase did NOT touch

- `Pipeline.h`/`.cpp`, `GpuResourceFactory` — untouched (PHASE3's job).
- No new pass was declared anywhere (PHASE4's job).
- `RenderGraphBarrierPlanner.h`/`.cpp` — untouched. `TargetsDepthState()`/
  `IsColorAttachmentWriteAccess()` keep their exact existing definitions.
  `FindMismatchedColorAttachmentExtent()` lives in `RenderGraphTypes.h`/
  `.cpp` (the document's own suggested default location — see 3.2 above).
- `RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp` — untouched, per
  `PHASE0_MASTER_STRATEGY.md`'s Non-Goals (already proven unnecessary by
  PHASE1's own tests).

## Byte-for-byte equivalence checklist (Step 3.2) — confirmed

- Exactly one loop iteration through `pass.colorAttachments` for every
  existing pass today → exactly one `VkRenderingAttachmentInfo` pushed,
  `resolvedExtents.size() == 1`. Confirmed by inspection (every real
  `WriteColorAttachment()` call site in the engine today calls it exactly
  once per pass) and by the live sanity check below (nothing crashed/threw).
- `FindMismatchedColorAttachmentExtent()` always returns `std::nullopt` for
  a 0- or 1-element input — proven by its own Tier-1 tests
  (`ZeroExtentsHasNoMismatch`/`SingleExtentHasNoMismatch`) — so the new
  `throw` path is provably unreachable for every pass that exists in the
  engine today; only a genuinely new 2+-attachment pass (PHASE4) can ever
  reach it.
- `renderingInfo.colorAttachmentCount == 1` for every existing pass,
  identical to the old hard-coded value.
- `firstExtent == colorTex.target.extent` (today's old local variable) for
  the single-attachment case, used identically for
  `renderArea`/`viewport`/`scissor`/`ctx.colorAttachmentExtent`.
- `colorAttachment.imageView`/`imageLayout`/`storeOp`/`loadOp`/`clearValue`
  are populated identically to the old single-attachment build — the only
  change is the clear-color SOURCE, from `pass.colorClearValue` to
  `desc.clearColor`, which PHASE1 guarantees carry the exact same value for
  every existing single-`WriteColorAttachment()` call site (both are
  populated from the exact same `clearColor` parameter, in the same call).
- The depth-attachment build and `renderingInfo.pDepthAttachment` are
  untouched, byte-for-byte, including its own gating on `hasColorWrite`.

## Verification

- **Fast, targeted incremental compile check**:
  `cmake --build build --target GreatTamanaEngineTests` — **succeeded**, no
  warnings promoted to errors, both immediately after the `RenderGraph.cpp`/
  `RenderGraphTypes.h`/`.cpp` changes and again after the small
  `RenderGraphBuilder.h` doc-comment correction (Step 3.3).
- **Test binary run, filtered to every Render Graph suite**:
  `GreatTamanaEngineTests.exe --gtest_filter=RenderGraph*` — **209/209
  passed**, including:
  - Every pre-existing test, unmodified, still passing (in particular the 2
    `colorClearValue`-asserting tests from PHASE1, proving the disposition
    decision above didn't break them).
  - PHASE1's own 3-write survival/snapshot tests, still passing against
    this phase's rewritten `RenderGraph.cpp` (they exercise
    `RenderGraphCompiler`/`RenderGraphSnapshot`, not `ExecuteCompiledGraph()`
    directly, so they were expected to be unaffected — confirmed).
  - 5 new `RenderGraphFindMismatchedColorAttachmentExtentTest` cases: zero
    extents, one extent, N identical extents (all `std::nullopt`), a
    mismatch at a non-zero index, and a mismatch detected via height alone
    (width matching) — covering every case the Definition of Done requires.
- **Live, running-engine sanity check** (per this phase's own Definition of
  Done — compiling alone cannot prove "still renders the same pixels"):
  - Built the full engine (`cmake --build build --target GreatTamanaEngine`)
    and launched it via `run_app_background`.
  - `GET /get_swapchain` and `GET /get_game_view` (after `GET
    /activate_tab?name=Game`) both returned real, correctly-rendered PNG
    frames — the Editor UI, Scene View (sky background gradient visible,
    correct camera framing), and Game View (matching sky background) all
    rendered exactly as expected, with no crash, no thrown
    `std::runtime_error`, and no validation-layer-visible corruption.
  - `GET /activate_tab?name=Render Graph` plus a follow-up `/get_swapchain`
    confirmed the Render Graph panel lists every real production pass
    (`AtmosphereTransmittanceLut`, `AtmosphereMultiScatteringLut`,
    `AtmosphereSkyViewLut`, `AtmosphereAerialPerspectiveVolume`, ...) with
    correct GPU timings and Reads/Writes columns, populated by exactly the
    same `RenderGraphSnapshot`-building code path this phase's rewritten
    `ExecuteCompiledGraph()` feeds — i.e. every existing single-attachment
    pass in the engine (Atmosphere LUTs, sky background, opaque/present,
    etc.) is confirmed still executing and recording correctly end-to-end,
    not just compiling.
  - The app was cleanly terminated via `stop_app_background` after the
    check.

## Design decisions made (not fully pinned down verbatim by PHASE0/PHASE2)

1. **Branch correction** — see the dedicated section near the top of this
   report. Stayed on `feature/render-pass-impl`; the "feature/logger-impl"
   instruction was a stale copy/paste, confirmed via `ask_questions`.
2. **`FindMismatchedColorAttachmentExtent()` location** —
   `RenderGraphTypes.h`/`.cpp` (the document's own suggested default), no
   reason found to prefer `RenderGraphBarrierPlanner.h`/`.cpp` instead. No
   `ask_questions` needed (the document explicitly said to ask only "if a
   strong reason is found to prefer" the alternative — none was).
3. **`colorClearValue` disposition** — kept, not removed (see Step 3.3
   above for the full reasoning). No `ask_questions` needed — the phase
   document's own "consider removing" phrasing already marked this as
   implementer's discretion.

No other genuine ambiguity was hit during this phase.

## Deviations from the plan

None beyond the two documented decisions above (both were explicitly left
open by the phase document itself, not deviations from a locked
instruction). The attachment-build code matches the strategy document's own
literal quoted code essentially verbatim (adjusted only for the surrounding
comment style/wording).

## Files changed

- `src/Renderer/RenderGraph/RenderGraphTypes.h` (new
  `FindMismatchedColorAttachmentExtent()` declaration, `<cstddef>` include)
- `src/Renderer/RenderGraph/RenderGraphTypes.cpp` (new
  `FindMismatchedColorAttachmentExtent()` definition)
- `src/Renderer/RenderGraph/RenderGraph.cpp` (the actual PHASE2 rewrite:
  attachment-scan + N-attachment `vkCmdBeginRendering` build; new
  `<cassert>`/`<stdexcept>` includes)
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` (doc-comment correction
  only, describing `colorClearValue`'s now-fully-dead production status)
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` (5 new
  `FindMismatchedColorAttachmentExtent()` tests)
- `task_manager/mrt-1/PHASE2_COMPLETION_REPORT.md` (this file)

## Handoff notes for PHASE3

- `RenderGraph::ExecuteCompiledGraph()` now records a real, correct
  N-color-attachment `vkCmdBeginRendering` for any pass that declares 2+
  `WriteColorAttachment()` calls — but **no real `Pipeline` can yet bind
  against such a pass** without a validation-layer error, since `Pipeline`
  still only supports exactly one color format/blend-attachment. That is
  PHASE3's job entirely, unaffected by anything in this phase.
  `RenderGraph.cpp`/`RenderGraphTypes.h`/`.cpp` need no further changes for
  PHASE3 to proceed.
- `PassRecord::colorClearValue` is confirmed fully dead in production code
  (see Step 3.3) — a future phase/cleanup that wants to actually remove it
  must also update `RenderGraphBuilder.cpp`'s `WriteColorAttachment()` (stop
  populating it) and rewrite/remove the 2 dependent
  `RenderGraphBuilderTests.cpp` tests in the same change.
