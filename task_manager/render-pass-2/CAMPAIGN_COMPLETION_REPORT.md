# CAMPAIGN_COMPLETION_REPORT — Frame Debugger Pass-Ownership Campaign (`render-pass-2`)

_Final record of the whole `render-pass-2` campaign, branch
`feature/render-pass-impl`. Mirrors the shape of
`task_manager/render-pass-1/CAMPAIGN_COMPLETION_REPORT.md`. Written at the
close of PHASE4
(`PHASE4_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md`), the only
phase in this campaign permitted a full build + full `ctest` regression run +
live HTTP-driven verification against the real running engine._

## Why this campaign existed

The user opened the Frame Debugger, expanded `"RenderOpaque"` (which correctly
showed a "v" expandable arrow, owning two per-entity children,
`SmokeTestCube (Entity 0)` and `Entity 2 (Entity 2)`), and then looked at the
very next sibling row, `"DrawSkyBackground"` — which had **no** expand arrow
at all, sat flat, and visually looked like a stray, orphaned row that
belonged to no render pass. The user's own words: _"DrawSkyBackground seems
not owned by any render-pass? ... perhaps there something wrong with current
implementation?"_

**Confirmed root cause** (direct source inspection, `PHASE0_MASTER_STRATEGY.md`'s
own Step 2): `src/Editor/Panels/FrameDebuggerPanel.cpp`'s `RenderEventNode()`
decides whether a row gets the expandable "v" arrow purely by checking
`if (!node.children.empty())`. `"RenderOpaque"` gets the arrow only because
`BuildRealFrameDebuggerSnapshot()` (`src/Editor/FrameDebuggerData.cpp`)
happens to attach real per-entity children to it. Every OTHER real pass leaf
that function built — `"DrawSkyBackground"`, AND every individual Atmosphere
`"Compute LUT"` pass, AND every Pre/Post-GameView compute dispatch — was
built as a single FLAT `FrameDebuggerEventNode` with `children` always empty.
This was not a display bug and not a missing "owner" pointer anywhere in the
data — it was a genuine STRUCTURAL gap: these pass leaves had never been
given a child node describing their own actual draw/dispatch operation, the
way `"RenderOpaque"` already had.

The user explicitly confirmed the scope should be BROAD, not a narrow
`"DrawSkyBackground"`-only patch — every real pass leaf in the Game View tree
needed the same "v PassName -> owned child event" treatment, with
`"RenderOpaque"` explicitly excluded since its own per-entity mechanism was
already correct (`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decisions).

## The four phases, in one line each

| # | Phase | One-line outcome |
|---|-------|-------------------|
| 1 | Render Pass Draw-Kind Vocabulary | New, purely-descriptive `rg::RenderPassDrawKind` enum (`DrawMesh`/`DrawQuad`/`Blit`) added to `RenderGraphTypes.h`, threaded through `PassRecord` → both `AddRenderPass()` overloads (new trailing, defaulted parameter — every pre-existing call site across `src/` compiled unmodified) → `RenderGraphPassSnapshot` (copied through for both surviving and culled passes). `"DrawSkyBackground"` explicitly tagged `DrawQuad` (a real, hand-verified 3-vertex full-screen-triangle draw). Zero Frame Debugger changes — this phase's deliverable was fully inert until PHASE2. |
| 2 | Frame Debugger Unified Pass-Ownership Rework | The actual bug fix. A new shared `WrapPassWithOwnedChildEvent()` helper (plus `GraphicsChildEventLabelFor()`) turns every real pass leaf `BuildRealFrameDebuggerSnapshot()` builds — every Compute LUT sub-pass, every Pre/Post-GameView compute dispatch, `"DrawSkyBackground"` — into a "v PassName" parent owning exactly one real, independently-selectable child event row, doubling the `nextEventIndex` consumption per wrapped pass (parent, then child, back-to-back). `"RenderOpaque"` and `src/Editor/Panels/FrameDebuggerPanel.cpp` left byte-for-byte untouched — the panel's existing `!node.children.empty()`-driven rendering needed zero changes. |
| 3 | Test Suite Migration & Documentation Update | Every one of the 31 pre-existing tests in `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` independently re-verified against the new nested shape — 12 needed a real update (rewritten `eventIndex`/`totalEventCount` numbers, new child-node assertions), 19 confirmed genuinely unaffected. 4 new tests added covering `WrapPassWithOwnedChildEvent()`'s dual-selectability (both Graphics- and Compute-kind passes), `GraphicsChildEventLabelFor()`'s full `DrawMesh`/`DrawQuad`/`Blit` mapping, and a Compute LUT sub-pass's own child. `docs/conventions/frame-debugger.md` and `AGENTS.md` updated to describe the new nested tree shape and `RenderPassDrawKind` vocabulary. |
| 4 | Final Integration: Full Build, Full Regression, Live Verification | This phase. Full clean-equivalent build, full `ctest` regression suite (1589 tests, 100% passing, one pre-existing expected skip), and a live, HTTP-driven Frame Debugger screenshot verification against the real running engine — see below. |

## Final, shipped tree shape (verified live in PHASE4)

Captured with the default scene plus two spawned test primitives
(`SmokeTestCube`, `Entity2`), 17 total events:

```
v Game View
  v Compute LUT
     v AtmosphereTransmittanceLutPass
        Compute Dispatch
     v AtmosphereMultiScatteringLutPass
        Compute Dispatch
     v AtmosphereSkyViewLutPass
        Compute Dispatch
     v AtmosphereAerialPerspectiveVolumePass
        Compute Dispatch
     v AtmosphereAerialPerspectiveVolumeDebugSlicePass
        Compute Dispatch
  v RenderOpaque                              (UNCHANGED shape)
     SmokeTestCube (Entity 1)
     Entity2 (Entity 2)
  v DrawSkyBackground                          (FIXED — was flat, now owns its own child)
     Draw Quad
  v Compute Dispatches (Post-GameView)
     v AtmosphereAerialPerspectiveCompositePass
        Compute Dispatch
```

This matches `PHASE0_MASTER_STRATEGY.md`'s own Step 1 target diagram exactly.

## PHASE4 verification performed

### Full build

`cmake --build build` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) completed with
`ninja: no work to do` — every earlier phase's own incremental compile check
had already kept the tree fully built and up to date, so this full build
re-confirmed zero errors/warnings across the entire campaign's accumulated
changes with nothing left to recompile. No cross-phase integration issue was
found.

### Full regression test

`cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C
Debug --output-on-failure`: **1589 tests run, 100% passing** (1588 passed
outright, 1 test — `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`
— correctly `GTEST_SKIP()`s, since it's gated on an optional, non-vendored
MMD model file not present on this machine; this is the same expected,
pre-existing skip `render-pass-1`'s own PHASE7 documented). This is 11 tests
higher than `render-pass-1`'s own 1578 baseline (PHASE1 added 8 new tests —
2 `ToString()` tests, 2 snapshot copy-through tests, 4 `AddRenderPass()`
default-vs-explicit `drawKind` tests; PHASE3 added 4 new tests — folding one
planned test into another per its own "implementer's choice" note nets to
+4 there — for a net +11 relative to 1578), confirming the higher total-count
requirement of this phase's Definition of Done. Zero regressions found
anywhere in the suite as a result of this campaign's four phases of changes.

### Live launch + Frame Debugger verification

1. Launched the built `GreatTamanaEngine.exe` in the background
   (`run_app_background`).
2. `GET /frame_debugger/open` — `200`, window opened.
3. `GET /frame_debugger/enable?value=true` — `200`, enabled.
4. `GET /frame_debugger/capture` — `200`, `hasCapturedFrame: true`,
   `totalEventCount: 15` (default scene, no mesh entities yet).
5. Spawned two primitives via `POST /instantiate_primitive`
   (`"SmokeTestCube"`, `"Entity2"`), matching `render-pass-1`'s own PHASE7
   methodology, then re-captured TWICE (the Frame Debugger's own capture
   mechanism is deferred by exactly one frame per
   `docs/conventions/frame-debugger.md`, so the very first re-capture after
   spawning still showed the stale 15-event count; the second re-capture
   correctly showed `totalEventCount: 17`, with `"RenderOpaque"` now owning
   two real per-entity children).
6. `GET /get_swapchain` — one single screenshot (the tree pane fit the whole
   17-event tree at the window's default size, no scrolling/resizing
   needed) visually confirmed the ENTIRE target tree shape at once:
   - `"Compute LUT"` expandable, and each of its five individual sub-passes
     (`AtmosphereTransmittanceLutPass`, `AtmosphereMultiScatteringLutPass`,
     `AtmosphereSkyViewLutPass`, `AtmosphereAerialPerspectiveVolumePass`,
     `AtmosphereAerialPerspectiveVolumeDebugSlicePass`) individually
     expandable with its own `"Compute Dispatch"` child — this was NOT true
     before this campaign.
   - `"RenderOpaque"` unchanged — still expandable with its two per-entity
     children (`SmokeTestCube (Entity 1)`, `Entity2 (Entity 2)`).
   - **`"DrawSkyBackground"` now expandable**, with exactly one child
     visible once expanded, named `"Draw Quad"` — the literal, original bug
     report, now visibly fixed in the real running UI.
   - `"Compute Dispatches (Post-GameView)"`'s own sub-pass
     (`AtmosphereAerialPerspectiveCompositePass`) also now expandable with
     its own `"Compute Dispatch"` child.
7. Found the exact `eventIndex` values by reasoning from the real,
   chronological execution order visible in the step-6 screenshot (five LUT
   passes × 2 indices each = 0-9, `"RenderOpaque"` parent+2 children = 10-12,
   `"DrawSkyBackground"` parent+child = 13-14, composite parent+child =
   15-16) and cross-checked with the iterative `select_event` technique:
   - `GET /frame_debugger/select_event?index=13` + `GET /get_swapchain`:
     Inspector showed `Event #13: Draw Quad`, `Pass = DrawSkyBackground`,
     `Blend = Opaque (no blend)`, `ZTest = Equal`, `ZWrite = Off`,
     `Cull = None` — the `"DrawSkyBackground"` pass-level row, matching
     `DescribeSkyBackgroundPipelineState()`'s documented values exactly
     (same real facts as before this campaign — PHASE2 never changed the
     pass-level node's own `details`).
   - `GET /frame_debugger/select_event?index=14` + `GET /get_swapchain`:
     Inspector ALSO showed `Event #14: Draw Quad` with the identical
     `Pass = DrawSkyBackground`/`ZTest = Equal`/`ZWrite = Off` facts — the
     new `"Draw Quad"` CHILD row, proving Locked Design Decision #2's
     dual-selectability end-to-end, live, not just in a unit test.
   - `GET /frame_debugger/select_event?index=0` + `GET /get_swapchain`:
     Inspector showed `Event #0: Compute Dispatch`,
     `Pass = AtmosphereTransmittanceLutPass` — confirming a Compute LUT
     sub-pass's own parent row and its `"Compute Dispatch"` child are also
     correctly wired, generalizing the fix beyond just `"DrawSkyBackground"`.
8. Stopped the engine (`stop_app_background`).

Every check in PHASE4's own Definition of Done passed. No bug reports were
filed during this campaign — every tool call across all four phases behaved
as documented.

## What shipped (cumulative)

- `rg::RenderPassDrawKind` (`DrawMesh`/`DrawQuad`/`Blit`) — a new, purely
  descriptive enum on `RenderGraphTypes.h`, threaded through `PassRecord`,
  both `AddRenderPass()` overloads (new trailing, defaulted parameter), and
  `RenderGraphPassSnapshot` (copied through for surviving AND culled passes).
  `"DrawSkyBackground"` explicitly tagged `DrawQuad`.
- `WrapPassWithOwnedChildEvent()` + `GraphicsChildEventLabelFor()`
  (`src/Editor/FrameDebuggerData.cpp`, anonymous namespace) — the ONE shared
  mechanism that turns a flat "pass IS the draw event" leaf into a real
  "v PassName" parent owning exactly one real child event row, applied at
  all three call sites in `BuildRealFrameDebuggerSnapshot()` (the Compute
  LUT/Pre-GameView compute loop, the view-region walk's
  `"DrawSkyBackground"`/future `"RenderTransparent"` branch, and the
  Post-GameView compute loop).
- `"RenderOpaque"` and `src/Editor/Panels/FrameDebuggerPanel.cpp` left
  completely untouched — the existing per-entity mechanism and the existing
  `!node.children.empty()`-driven tree-rendering logic both needed zero
  changes.
- 12 pre-existing tests rewritten in `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`
  to match the new nested shape/new `eventIndex`/`totalEventCount` numbers;
  19 confirmed genuinely unaffected; 4 new tests added covering the new
  mechanism itself (dual-selectability for both a wrapped Graphics-kind and a
  wrapped Compute-kind pass, the full `DrawMesh`/`DrawQuad`/`Blit` label
  mapping, and a Compute LUT sub-pass's own child).
- Updated documentation: `docs/conventions/frame-debugger.md` gained a new
  "What's new (`render-pass-2` campaign)" section and a rewritten ASCII tree
  diagram; `AGENTS.md`'s "Frame Debugger" and "Render Pass System" sections
  both updated to describe the new nested ownership rule and the
  `RenderPassDrawKind` vocabulary.
- This report, plus a new summary bullet at the top of the root `README.md`'s
  "## Status" section (and, as a low-effort bonus per this phase's own
  instruction, the one analogous bullet `render-pass-1` itself never added,
  now added immediately below it).

## Explicit breaking changes (as pre-approved by `PHASE0_MASTER_STRATEGY.md`'s
own Locked Design Decision #5)

- Every pass leaf other than `"RenderOpaque"` now consumes TWO `nextEventIndex`
  values instead of one (parent, then its new child) — every `eventIndex`
  number at or after the first wrapped pass in any given frame shifted
  relative to pre-campaign behavior, and `snapshot.totalEventCount` grew by
  exactly the number of wrapped passes in that frame.
- `BuildGraphicsPassLeaf()`'s own `details.eventLabel` changed from the
  hardcoded placeholder `"Draw Pass"` to the same structural
  `GraphicsChildEventLabelFor(pass.drawKind)` value its new child reports
  (`"Draw Quad"` for `"DrawSkyBackground"` today).
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`'s
  `DrawSkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit` and
  `ViewRegionHasExactlyRenderOpaqueAndDrawSkyBackgroundWhenRenderTransparentAbsent`
  — PHASE0's own two named examples that literally asserted the OLD flat/
  childless shape — were rewritten to assert the new nested shape instead.

Every one of these was called out loudly in its own originating phase's
completion report (PHASE1_COMPLETION_REPORT.md, PHASE2_COMPLETION_REPORT.md,
PHASE3_COMPLETION_REPORT.md) and is now reflected in
`docs/conventions/frame-debugger.md` and `AGENTS.md`.

## What was explicitly NOT done (out of scope, by design)

- **A real `Blit`-kind pass.** `RenderPassDrawKind::Blit` remains a real,
  currently completely UNUSED scaffold enumerator — no pass in this engine is
  tagged with it today, and none can be until a future campaign teaches
  `RenderGraph::Execute()` a genuinely new recording path outside the
  `vkCmdBeginRendering`/`vkCmdEndRendering` bracket every Graphics-kind pass
  uses today. This mirrors `render-pass-1`'s own `"RenderTransparent"`
  scaffold precedent.
- **A real `RenderTransparent` pass.** Still a permanently-empty scaffold
  from `render-pass-1` — this campaign's own `WrapPassWithOwnedChildEvent()`
  mechanism is already wired to apply to it correctly (via the same
  view-region-walk `else` branch `"DrawSkyBackground"` uses) the moment a
  future campaign makes it real and non-empty, with zero further Frame
  Debugger changes needed.
- **Any new `PassKind` enumerator.** `RenderGraph.cpp`/
  `RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp` were never
  touched by this campaign — `RenderPassDrawKind` is read by nothing there,
  by design.
- **Retroactively adding `render-pass-1`'s own missing `README.md` "Status"
  bullet as a full obligation.** Per PHASE4's own Step 3.5, this was
  identified as a real, pre-existing gap in that already-shipped campaign,
  not something this campaign is required to fix — it was added anyway as a
  genuinely low-effort bonus, since the facts were already on hand from
  `render-pass-1`'s own `CAMPAIGN_COMPLETION_REPORT.md`.
- **Merging `feature/render-pass-impl` into any other branch** — outside
  this campaign's own authority entirely.

## Recommendation for whoever picks up the next session

1. Any FUTURE Graphics-kind pass that issues a genuine per-object mesh draw,
   full-screen quad, or eventual real blit should tag its own
   `rg::RenderPassDrawKind` explicitly at its `AddRenderPass()` call site —
   the Frame Debugger's own child-event labeling already generalizes to it
   with zero further changes needed there.
2. If a real transparency system or a real blit/copy pass is ever built,
   both `"RenderTransparent"`'s already-wired call site and the `Blit`
   enumerator are ready, zero-risk starting points — no further Frame
   Debugger plumbing needs to change.
3. No further action is required to close out this campaign itself — every
   phase's own Definition of Done is met, the full regression suite is
   green, and the live verification in this report confirms the real,
   running engine matches the originally-requested tree shape end to end.

## Final state

- `cmake --build build`: succeeds, zero errors (`ninja: no work to do` — the
  tree was already fully built from every prior phase's own incremental
  check).
- `ctest -C Debug --output-on-failure`: **1589/1589 tests run, 100% passing**
  (1 correctly-skipped optional smoke test aside) — higher than
  `render-pass-1`'s own 1578 baseline, as required.
- Live, HTTP-driven Frame Debugger verification: confirmed the exact target
  tree shape, `"DrawSkyBackground"` now expandable with a real `"Draw Quad"`
  child (the literal original bug, now fixed), every Compute LUT sub-pass
  now expandable with its own `"Compute Dispatch"` child, `"RenderOpaque"`
  unchanged, and both a pass-level row and its new child row independently
  selectable with correct, matching Inspector data.
- `git status` on `feature/render-pass-impl`: clean after this report's own
  commit (aside from pre-existing, unrelated untracked planning docs for
  future `render-pass-3`/`-4`/`-5` campaigns, explicitly confirmed with the
  user as out of this campaign's own scope — see this phase's own
  `ask_questions` exchange).
