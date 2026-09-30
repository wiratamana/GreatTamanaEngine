# PHASE4 COMPLETION REPORT — Required Companion Audit: `FrameDebuggerData.cpp`'s `PassKind` exhaustiveness

Campaign: `task_manager/editor-core-separation-26/` ("Buffer Roots (KeepBufferOutput) +
Blit/Copy Passes (PassKind::Blit)")
Branch: `feature/editor-core-separation` (unchanged, as required)
Phase file: `PHASE4_FRAMEDEBUGGER_PASSKIND_EXHAUSTIVENESS_AUDIT.md`

## Status: DONE ✅

## What changed

Of the 5 real `PassKind` comparison sites in `src/Editor/FrameDebuggerData.cpp`
(re-confirmed fresh via `search_in_dir` for `\.kind\s*[=!]=` before touching
anything — still exactly 5, all inside this one file, matching PHASE0 Step 2's
own finding byte-for-byte):

- **3 sites converted** to a real, exhaustive, compiler-enforced, `default:`-
  less `switch (pass.kind)` dispatch over `Compute`/`Graphics`/`Blit` — the
  Pre-GameView compute-discovery loop, the GameView view-region walk, and the
  final "Other Render Passes" catch-all sweep.
- **2 sites left as one-way Compute-only filters**, each now carrying a fresh,
  in-code comment recording exactly why `PassKind::Blit` is already safe there
  and why they are NOT one of the 3 sites the source document names for
  conversion — `FindPostGameViewCompositePassExecutionIndex()`'s own check, and
  the Post-GameView compute-discovery loop.
- **2 new Frame-Debugger-tier tests** in
  `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`, plus a new
  `MakeBlitPass()` fixture helper, proving the real correctness fix (a
  `PassKind::Blit` pass is never mistaken for, and never displaces, the real
  "RenderOpaque" leaf; a `PassKind::Blit` pass swept into the final catch-all
  gets a correctly labeled "Blit" child event, never a mislabel).

### `src/Editor/FrameDebuggerData.cpp` — all 5 sites, quoted in full (current state)

**Site 1 (line 237-257, `FindPostGameViewCompositePassExecutionIndex()`) — LEFT AS A FILTER, comment added:**
```cpp
int FindPostGameViewCompositePassExecutionIndex(const rg::RenderGraphSnapshot& graphSnapshot, int gameViewIndex)
{
    for (int i = gameViewIndex + 1; i < static_cast<int>(graphSnapshot.passesInExecutionOrder.size()); ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
        // editor-core-separation-26 campaign, PHASE4's own companion audit -
        // confirmed safe for `PassKind::Blit` too: this is a one-way
        // Compute-only FILTER (a Blit pass is simply skipped, exactly like a
        // Graphics pass already is), never a disguised "not Compute =
        // Graphics" binary branch; the only field read below (`writeNames`)
        // is valid for any `PassKind`. Not one of the 3 sites the source
        // document names for conversion - see PHASE0_MASTER_STRATEGY.md's
        // Locked Decision 2.
        if (pass.kind != rg::PassKind::Compute || pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
            continue;
        }
        for (const std::string& writeName : pass.writeNames) {
            if (writeName == "GameViewComposited") {
                return i;
            }
        }
    }
    return -1;
}
```

**Site 2 (Pre-GameView compute-discovery loop) — CONVERTED:**
```cpp
    for (int i = 0; i < pivotIndex; ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
        bool isComputeKind = false;
        switch (pass.kind) {
        case rg::PassKind::Compute:
            isComputeKind = true;
            break;
        case rg::PassKind::Graphics:
        case rg::PassKind::Blit:
            break;
        }
        if (!isComputeKind) {
            continue; // Not this loop's concern - see this loop's own header comment above.
        }
        if (pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
            claimed[static_cast<std::size_t>(i)] = true; // Honest, already-documented exclusion.
            continue;
        }
        claimed[static_cast<std::size_t>(i)] = true;
        ... // (unchanged: builds the compute-dispatch leaf, buckets by tag/heading)
    }
```

**Site 3 (GameView view-region walk) — CONVERTED, the real correctness fix:**
```cpp
    bool isRenderOpaqueLeaf = true;
    for (int i = pivotIndex; i < static_cast<int>(graphSnapshot.passesInExecutionOrder.size()); ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];

        bool isComputeKind = false;
        switch (pass.kind) {
        case rg::PassKind::Compute:
            isComputeKind = true;
            break;
        case rg::PassKind::Graphics:
        case rg::PassKind::Blit:
            break;
        }

        if (isComputeKind) {
            if (!pass.isCulled) {
                // A genuine surviving compute-pass survivor - stop the walk
                // entirely. Deliberately NOT marked `claimed` here - the
                // post-GameView compute loop below (which starts at
                // pivotIndex + 1) re-visits this exact index and claims it.
                break; // OUTSIDE the switch above - this correctly still breaks the for loop.
            }
            claimed[static_cast<std::size_t>(i)] = true;
            continue;
        }

        // pass.kind is Graphics or Blit from here on - NEVER assume Graphics
        // (editor-core-separation-26 campaign, PHASE4 - the actual
        // correctness fix this whole audit exists for: a PassKind::Blit pass
        // must never be treated as, or silently displace, "RenderOpaque" -
        // see the `isRenderOpaqueLeaf && pass.kind == rg::PassKind::Graphics`
        // guard below).
        if (pass.isCulled || pass.viewScope == rg::ViewScope::SceneView
            || pass.category == rg::RenderPassCategory::FrameDebuggerInternal) {
            claimed[static_cast<std::size_t>(i)] = true;
            continue;
        }
        claimed[static_cast<std::size_t>(i)] = true;

        // THE FIX: a Blit pass is NEVER treated as RenderOpaque, and -
        // critically - does NOT consume isRenderOpaqueLeaf when it isn't.
        // This is what lets the REAL RenderOpaque pass, whenever it is next
        // encountered, still correctly claim the slot even if one or more
        // Blit passes sat in front of it.
        if (isRenderOpaqueLeaf && pass.kind == rg::PassKind::Graphics) {
            FrameDebuggerEventNode renderOpaqueLeaf = BuildRenderOpaqueLeaf(pass, capture, nextEventIndex++);
            ... // (unchanged: per-entity draw-record children)
            root.children.push_back(std::move(renderOpaqueLeaf));
            isRenderOpaqueLeaf = false;
        } else {
            // ... editor-core-separation-26 campaign, PHASE4 - this branch
            // also now correctly covers a real PassKind::Blit pass -
            // isRenderOpaqueLeaf is DELIBERATELY NOT touched here.
            FrameDebuggerEventNode leaf =
                BuildGraphicsPassLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::PreComposite);
            leaf = WrapPassWithOwnedChildEvent(
                std::move(leaf), nextEventIndex++, GraphicsChildEventLabelFor(pass.drawKind));
            root.children.push_back(std::move(leaf));
        }
    }
```

**Site 4 (Post-GameView compute-discovery loop) — LEFT AS A FILTER, comment added:**
```cpp
        // editor-core-separation-26 campaign, PHASE4 - RE-VERIFIED, not just
        // cited: confirmed safe for `PassKind::Blit` too - this is a one-way
        // Compute-only FILTER (a Blit pass here is simply skipped, exactly like
        // a Graphics pass already is, and correctly falls through to the final
        // "Other Render Passes" sweep below). Not one of the 3 sites the source
        // document names for conversion - this is structurally identical to
        // FindPostGameViewCompositePassExecutionIndex()'s own already-safe check
        // above (Site 1) - see PHASE0_MASTER_STRATEGY.md's Locked Decision 2.
        if (pass.kind != rg::PassKind::Compute) {
            continue;
        }
```

**Site 5 (final "Other Render Passes" catch-all sweep) — CONVERTED:**
```cpp
        // editor-core-separation-26 campaign, PHASE4 - converted to a real,
        // exhaustive, compiler-enforced 3-way dispatch (one of the 3 sites the
        // source document names) - defensively future-proofed even though this
        // ternary's own "else" arm was already functionally correct for
        // PassKind::Blit today (BuildGraphicsPassLeaf()/GraphicsChildEventLabelFor()
        // already handle it).
        FrameDebuggerEventNode leaf;
        switch (pass.kind) {
        case rg::PassKind::Compute:
            leaf = WrapPassWithOwnedChildEvent(
                BuildComputeDispatchLeaf(pass, nextEventIndex++, stepPreviewKind), nextEventIndex++,
                "Compute Dispatch");
            break;
        case rg::PassKind::Graphics:
        case rg::PassKind::Blit:
            leaf = WrapPassWithOwnedChildEvent(
                BuildGraphicsPassLeaf(pass, nextEventIndex++, stepPreviewKind), nextEventIndex++,
                GraphicsChildEventLabelFor(pass.drawKind));
            break;
        }
        otherPassesGroup.children.push_back(std::move(leaf));
```

**The one loop-control pitfall the phase file explicitly warned about (Site 3)
was avoided**: every `break;`/`continue;` that controls the enclosing `for`
loop stays in a plain `if` statement OUTSIDE the switch — the switch itself
only ever sets the local `isComputeKind` bool. Confirmed by hand-tracing and
independently re-confirmed by the `dispatch_sub_agent` double-check (see
below).

### `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`

- New `MakeBlitPass(const std::string& name)` helper (mirrors
  `MakeComputePass()`'s exact shape): `pass.kind = rg::PassKind::Blit;
  pass.drawKind = rg::RenderPassDrawKind::Blit;`, default `renderPassEvent`
  (Opaques, from the underlying `MakePass()`).
- `BlitPassImmediatelyAfterPivotIsNeverTreatedAsRenderOpaqueAndDoesNotDisplaceRealRenderOpaque`
  — fixture: `["RenderOpaque", MakeBlitPass("HistoryBufferBlit"),
  "DrawSkyBackground"]`. Asserts: `root.children.size() == 3`; child 0 is the
  real "RenderOpaque" leaf (0 children, since the fixture's `capture` has no
  draw records — never fabricated ones borrowed from the Blit pass); child 1
  is "HistoryBufferBlit" with its own wrapped "Blit"-labeled child (never
  "RenderOpaque"); child 2 is "DrawSkyBackground" as a normal Graphics leaf
  (proving it was not displaced/consumed by the Blit pass sitting in front of
  it).
- `BlitPassInOtherRenderPassesSweepGetsCorrectBlitLabel` — mirrors the
  pre-existing `GraphicsPassAfterSurvivingComputePassIsSweptIntoOtherRenderPassesGroup`
  fixture shape exactly (a real surviving Post-GameView compute pass writing
  `"GameViewComposited"`, then a `MakeBlitPass()` tagged
  `RenderPassEvent::AfterEverything`). Asserts the Blit pass lands inside the
  "Other Render Passes" group with its wrapped child correctly labeled "Blit".

## Re-run of PHASE0's own 5-site grep (Step 3, item 5)

Fresh `search_in_dir` for regex `\.kind\s*[=!]=` across all of `src/` after
this phase's edits: **20 matches in 5 files**. Inside `FrameDebuggerData.cpp`:
line 249 (Site 1, unchanged filter), line 918 (a comment mentioning the new
guard, not executable code), line 932 (the new, real
`pass.kind == rg::PassKind::Graphics` comparison — the actual Site 3 fix),
line 999 (Site 4, unchanged filter). Every other hit across the other 4 files
(`RenderFeatureCompositor.cpp`, `FrameDebuggerPanel.cpp`, `RenderGraph.cpp`,
`RenderGraphCompiler.cpp`) is an unrelated, pre-existing `ResourceKind`/other
`.kind` field comparison, untouched by this phase — confirmed no new
`PassKind` comparison was introduced anywhere else in the engine. This is the
expected, sane result: the raw-comparison count at the 2 untouched sites
stayed the same, while the 3 converted sites' own raw comparisons disappeared
into `switch` statements (which this regex does not match, by design, since
`switch (pass.kind)` has no `.kind ==`/`.kind !=` token), offset by exactly 1
new, necessary comparison introduced by the actual bug fix at Site 3.

## Verification

1. **`ask_questions`**: not needed. The one genuine question the phase file
   flagged as a risk — `FindViewRegionPivot()`'s exact semantics (does the
   view-region walk start AT the pivot pass itself, or immediately after it?)
   — was resolved by direct code reading, not guesswork: `FindViewRegionPivot()`
   returns the first pass (in true execution order) whose `renderPassEvent >=
   RenderPassEvent::Opaques`, `pivotIndex` is that pass's own index, and the
   walk's `for` loop literally starts at `i = pivotIndex` — meaning it
   genuinely re-visits and consumes the pivot pass as its own first iteration.
   This was confirmed independently a second time by the `dispatch_sub_agent`
   double-check below.
2. **Incremental build**: `cmake --build build` — succeeded, zero errors
   (9/9 build steps, including `GreatTamanaEditor.exe` and
   `GreatTamanaEngineTests.exe`).
3. **Targeted test run**:
   `ctest -C Debug -R FrameDebuggerSnapshotBuilderTest --output-on-failure` →
   **40/40 tests passed (100%)** — the entire pre-existing 38-test suite plus
   both new tests, zero regressions.
4. **Independent double-check**: a `dispatch_sub_agent` independently
   re-read the full current `FrameDebuggerData.cpp`, hand-traced the loop-
   control pitfall at Site 3 (confirming every loop-breaking `break;`/
   `continue;` sits outside its switch), re-ran the same regex grep, read
   both new tests' assertions against `BuildRealFrameDebuggerSnapshot()`'s
   real current body and independently confirmed `FindViewRegionPivot()`'s
   semantics, ran `git_status` (confirmed only the 2 expected files touched),
   re-ran the incremental build (no-op, already built) and the same targeted
   `ctest` filter (40/40). It reported **SUCCESS — everything checks out, no
   problems found**, with no ambiguity requiring `ask_questions` on its own
   part either.

## PHASE1/PHASE2/PHASE3/PHASE5-7 confirmation

**PHASE1 (Gap A), PHASE2 (Gap B prerequisite), and PHASE3 (`BlitSpec`/
`PassKind::Blit`/pure helpers) untouched by this phase** — confirmed by
`git status` showing only `src/Editor/FrameDebuggerData.cpp` and
`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` modified (plus this
report). `RenderGraphCompiler.cpp/.h`, `RenderGraphBuilder.h/.cpp`,
`RenderGraphTypes.h/.cpp` were not touched at all by this phase.

**PHASE5 not started** — no `RenderGraphBuilder` method exists yet that can
produce a real, running `PassKind::Blit` pass (confirmed unchanged from
PHASE3's own finding: `PassKind::Blit` still appears only inside
`RenderGraphTypes.h`/`.cpp`, this phase's own test file, and — as of this
phase — the doc comments/`switch` case labels this phase added to
`FrameDebuggerData.cpp`, none of which construct one). This phase's own
required audit is now landed, satisfying the ordering requirement PHASE0
imposed before `AddBlitPass()` (PHASE5) may start producing real ones.

## Files touched

- `src/Editor/FrameDebuggerData.cpp`
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`
- `task_manager/editor-core-separation-26/PHASE4_COMPLETION_REPORT.md` (this file)

No new source file, and no `CMakeLists.txt`/`tests/CMakeLists.txt` change,
was needed — matching PHASE0 Rule 7's expectation exactly.

## Next phase

PHASE5 (`PHASE5_ADDBLITPASS_BUILDER_ENTRYPOINT.md`) — `RenderGraphBuilder::
AddBlitPass()`, the real, official, first-class pass-declaration entry point
that finally makes `PassKind::Blit` producible by a real pass for the first
time in this campaign. This phase's own companion audit is now a landed
prerequisite, per PHASE0's explicit ordering requirement.
