# PHASE4 Pre-Check Report (Heavy-Phase Strategy Double-Check)

_Written before any implementation of the `render-pass-1` campaign. Scope:
a rigorous, skeptical review of `PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md`
(and, where directly overlapping, `PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md`),
per the task's own instructions. No C++ code was changed — only strategy
documents. Stayed on the current branch throughout._

## What was read

- `README.md`, `AGENTS.md` (repo root).
- `task_manager/render-pass-1/PHASE0_MASTER_STRATEGY.md`,
  `PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md`,
  `PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md` (full), plus `PHASE1`,
  `PHASE3`, `PHASE5` (skimmed for cross-references, as instructed).
- Real, current source: `src/Editor/FrameDebuggerData.h/.cpp`,
  `src/Editor/FrameDebuggerCapture.h/.cpp`,
  `src/Renderer/RenderGraph/RenderGraphTypes.h`,
  `src/Renderer/RenderGraph/RenderGraphSnapshot.h`,
  `src/Renderer/RenderGraph/RenderGraphBuilder.h`,
  `docs/conventions/frame-debugger.md`, plus (to verify real execution
  order / grep every "GameView" call site) `src/Application/Application.cpp`,
  `src/Application/RenderPasses.cpp`, and a repo-wide `search_in_dir` for
  the literal string `"GameView"` under `src/` and `tests/`.

## Every factual claim checked against real source

Every fact PHASE0/PHASE2/PHASE4 assert about today's code was independently
re-derived from the real files rather than trusted at face value:
`PassRecord::isComputePass` (bool, not yet `PassKind`), `RenderGraphPassSnapshot::isComputePass`/
`viewScope`, `RenderGraphBuilder::AddPass()`/`AddComputePass()`'s exact 3-arg/
4-arg overload shapes, `BuildRealFrameDebuggerSnapshot()`'s exact
`FindPassByName(..., "GameView")` pivot logic and its Pre/Post compute-group
split, `FrameDebuggerDrawRecord::isSkyBackgroundDraw`/
`RecordSkyBackgroundDraw()`, `BuildGameViewDrawRecordLeaf()`'s two-branch
shape, and `AGENTS.md`'s own description of the already-shipped
`frame-debugger-7`/`frame-debugger-8`/`frame-debugger-9` state (single-capture
`FrameDebuggerCurrentCapture`, per-object replay-rendering mechanism,
`FrameDebuggerStepPreviewKind`). **All of it matched.** No factual claim in
PHASE2 or PHASE4 about current code behavior was found to be incorrect.

## Real gap found and fixed

**`AddFrameDebuggerReplayPasses()` sits structurally inside PHASE4's own
"view region" walk, and PHASE4's original text left its exclusion
conditional on a phase-ordering assumption that would not actually have
held on PHASE4's own first live-verification run.**

- Confirmed by directly reading `Application.cpp`'s Game-View block: the
  real declaration order is `AddGameViewPass(...)` → (conditionally, on an
  armed capture-trigger frame) `AddFrameDebuggerReplayPasses(...)` →
  `AddAtmosphereCompositePass(...)`. After PHASE2 splits the first call
  into `AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()`/
  `AddRenderTransparentPass()`, the N replay passes still sit strictly
  between that split and the composite pass — i.e. exactly inside the
  index range PHASE4's Step 3.3 "view region" walk sweeps over. They are
  real `PassKind::Graphics`, real `ViewScope::GameView` passes
  (`RenderPasses.cpp`'s `builder.AddPass(passName, rg::ViewScope::GameView,
  setup, execute)`), with `RenderPassCategory` defaulting to `General`
  until something tags it otherwise.
- `PHASE5_REMAINING_PASSES_MIGRATION.md` already noticed this exact risk in
  its own Step 2, but only as a conditional, easy-to-miss aside in a LATER
  phase ("if [the `category != Debug` guard] does not yet exist, add it
  now, as part of THIS phase") — this does not actually fix the problem,
  because a `category != Debug` guard is a no-op for as long as the pass in
  question is untagged (defaults to `General`, which also satisfies
  `!= Debug`). The real fix requires the TAG itself (`RenderPassCategory::Debug`)
  to exist on that call site BEFORE PHASE4's own walk runs against it —
  which, under the campaign's original phase order (PHASE4 before PHASE5),
  would not be true yet. Concretely: PHASE4's own Definition-of-Done live
  HTTP check (`enable` + `capture` + screenshot) is precisely the sequence
  that causes `AddFrameDebuggerReplayPasses()` to declare its N passes that
  same frame — so PHASE4's own verification step would have shown N
  spurious extra tree rows, on its very first run, regardless of whether
  PHASE4 remembered to add the exclusion guard or not.
- **Fix applied**: `PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md` now:
  1. Documents this confirmed ordering fact explicitly (new Step 2 bullet
     and a pre-check note at the top of the file).
  2. Rewrites Step 3.3's "view region" walk as a concrete, bounded
     algorithm (start at the pivot, walk forward, stop at the first
     surviving Compute-kind pass, skip-but-continue past any culled/
     SceneView-scoped/`Debug`-category pass) instead of the prior looser
     "walk generically" prose, which was not precise enough for an
     implementer to act on unambiguously.
  3. Adds a new Step 3.3b: PHASE4 itself migrates
     `AddFrameDebuggerReplayPasses()`'s pass declaration onto
     `builder.AddRenderPass(..., rg::RenderPassCategory::Debug, ...)` —
     pulling this one, narrow piece of PHASE5's originally-planned scope
     forward, specifically because PHASE4's own correctness depends on it,
     while explicitly NOT expanding PHASE4's scope to GPU Skinning/Present/
     Compute Blur Validation (those stay PHASE5's job).
  4. Adds a new required Tier-1 test (Step 3.6) proving a `Debug`-category
     Graphics pass positioned exactly where the real replay passes sit
     produces zero extra tree leaves.
  5. Strengthens the Definition of Done to explicitly require the live
     HTTP screenshot check to confirm no spurious replay-pass rows appear,
     not just that `RenderOpaque`/`DrawSkyBackground` are visible.
  6. Adds a "What We Will NOT Do" guardrail against expanding this
     pulled-forward scope any further.
- **`PHASE5_REMAINING_PASSES_MIGRATION.md` was updated to match** (Step 1
  and Step 2's bullet on `AddFrameDebuggerReplayPasses()`, and Step 3.3):
  it now says this migration is ALREADY done by PHASE4, and PHASE5's own
  job for this one item is reduced to CONFIRMING it (part of its existing
  3.5 grep audit) rather than re-implementing it — removing the prior
  ambiguous "add it now if missing" phrasing that left it unclear which
  phase actually owns the fix.

## Smaller corrections folded into the same PHASE4 rewrite

While verifying Step 3.3 in detail against the real `FrameDebuggerData.cpp`
source, three smaller, genuine ambiguities/gaps were found and fixed in the
same edit (all low-risk, all inside Step 3.3):

1. **Stale child `passName` literal not addressed.** The real per-entity
   draw-record child leaves currently set
   `FrameDebuggerEventDetails::passName = "GameView (Entity Draw)"`.
   PHASE4's original text only said to rename the PARENT leaf's row label
   to `"RenderOpaque"`; it never mentioned this child-leaf string, which
   would otherwise have been left reading a stale, factually-wrong
   `"GameView..."` value forever after the pass itself is renamed. Fixed:
   Step 3.3 now explicitly requires renaming it to
   `"RenderOpaque (Entity Draw)"`, and Step 3.6's test-update guidance was
   extended to say the existing `FrameDebuggerDataTests.cpp` fixture using
   the old literal needs updating too.
2. **`stepPreviewKind` for the two new Graphics leaves was never
   specified.** PHASE4 said `"DrawSkyBackground"`/`"RenderTransparent"`
   become "ordinary, childless leaves... same shape a compute-dispatch
   leaf already has today" but never stated which
   `FrameDebuggerStepPreviewKind` value they should carry — a real
   ambiguity, since an implementer could plausibly (and wrongly) invent a
   new enumerator or guess incorrectly. Fixed: both now explicitly get
   `PreComposite` (the same value `"RenderOpaque"` itself already uses),
   with the reasoning spelled out (there is only one retained whole-frame
   `preview` texture for the "after the view region, before composite"
   moment; no new enumerator is warranted).
3. **Minor, self-correcting cosmetic nit.** Step 1's ASCII tree diagram
   wrote the root label as `"GameView"` (no space) even though the real
   root node's own literal `name` field is `"Game View"` (with a space,
   confirmed via `BuildRealFrameDebuggerSnapshot()`'s existing
   `root.name = "Game View";` line, unchanged by this campaign). The
   original text already told the implementer to "re-read
   `FrameDebuggerSnapshot::rootNodes`'s own shape to confirm before
   assuming otherwise," so this was already self-correcting, but the
   diagram/prose was tightened to state the real value directly rather
   than rely on the reader catching the discrepancy themselves.

## Things checked and confirmed already correct (no change needed)

- **PHASE4's `"RenderOpaque"` pivot-lookup migration, the `"Compute LUT"`
  vs. `"Compute Dispatches (Pre-GameView)"` split (3.2), the
  `"RenderTransparent"` never-appears-when-absent behavior (3.5), and the
  `isSkyBackgroundDraw`/`RecordSkyBackgroundDraw()` deletion (3.4)** are
  all accurate against the real source and sufficiently detailed as
  written; no changes made to those sections beyond the 3.3 rewrite above.
- **PHASE4's HTTP/network contract audit (3.7)** — re-verified directly: a
  repo-wide `search_in_dir` for the literal `"GameView"` confirms every
  hit under `src/` outside `FrameDebuggerData.cpp` (and its own tests) is a
  RenderTexture/debug-texture NAME (`Application.cpp`'s
  `b.ImportTexture("GameView", ...)`, `ImGuiEditorLayer.cpp`'s
  `CreateRenderTexture(..., "GameView", ...)`,
  `AtmospherePanel.cpp`'s `ValidateAerialPerspectiveSkyPurity(..., "GameView",
  "GameViewComposited")`), never a render-graph PASS name — PHASE4's own
  claimed distinction holds. `tests/Application/FrameDebuggerCommandBridgeTests.cpp`
  has zero `"GameView"` hits, confirming the HTTP routes are genuinely
  index-based/name-agnostic as claimed — folded this confirmation directly
  into 3.7's text so a future implementer doesn't have to re-derive it.
- **PHASE4's test-file list (3.6)** matches
  `docs/conventions/frame-debugger.md`'s own "Testing this feature" section
  exactly (`FrameDebuggerDataTests.cpp`, `FrameDebuggerSnapshotBuilderTests.cpp`,
  `FrameDebuggerCaptureTests.cpp`) — no missing test file found.
- **PHASE2's own split-pass plan** (the part of PHASE2 that PHASE4 directly
  depends on and overlaps with) was re-verified against the real
  `AddGameViewPass()`/`Application.cpp` call-site order and found accurate
  — no changes made to `PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md`
  itself.
- **`ChooseFrameDebuggerPreviewSource()`** — confirmed, by reading the real
  function body, that it genuinely operates only on
  `FrameDebuggerStepPreviewKind`/retained-texture-presence booleans, never
  a pass name — PHASE4's claim that this function needs no changes holds.

## Judgment calls / ambiguity

No genuine design ambiguity requiring `ask_questions` was hit during this
review — every finding above had a single, clearly-correct resolution
derivable directly from the real source and this campaign's own already-
locked design decisions (PHASE0's Locked Design Decisions, PHASE1's
`RenderPassCategory` vocabulary). Nothing was guessed.

## Files changed

- `task_manager/render-pass-1/PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md`
  — overwritten in place (same filename) with the fixes described above.
- `task_manager/render-pass-1/PHASE5_REMAINING_PASSES_MIGRATION.md` —
  overwritten in place to match (Step 1/Step 2/Step 3.3 updated to reflect
  that the Frame Debugger Replay Passes migration is now PHASE4's job,
  confirmed rather than re-implemented by PHASE5). This is the one
  DIRECT, confirmed cross-phase inconsistency the task's own rules allow
  touching a non-PHASE2/PHASE4 file for.
- `task_manager/render-pass-1/PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md`
  — left unchanged (verified correct as-is).
- No C++ source under `src/`/`tests/` was changed — this was a strategy-
  document-only review, per the task's own rules.
