# PHASE4: Frame Debugger Event-Pivot Fix (the ONE named exception)

_Child of `PHASE0_MASTER_STRATEGY.md` — read that file first, especially
Locked Design Decision 2. Also read `PHASE1_CORE_VOCABULARY_AND_BLACKBOARD.md`
(this phase consumes the `renderPassEvent` field that phase adds) and
`PHASE3_FULL_PRODUCTION_PASS_MIGRATION_AND_VIEW_UNIFICATION.md` (this phase
runs AFTER it, since it needs `"RenderOpaque"` to already be declared with
a real `RenderPassEvent::Opaques` value via the new provider path — though
in practice the field is stamped correctly starting in PHASE2 already, so
this phase could technically run any time after PHASE1). Part of the
`render-pass-3` campaign._

## Step 1: The Goal

Exactly ONE, narrow, already-committed change to
`src/Editor/FrameDebuggerData.cpp`: replace the literal
`FindPassByName(graphSnapshot.passesInExecutionOrder, "RenderOpaque")`
pivot search with a structural, name-free lookup using the new
`RenderPassEvent` field PHASE1 added — "the first pass with `order >=
Opaques`", exactly as `GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`'s own
Section 6 suggests. **Nothing else in the Frame Debugger changes** — every
other `ViewScope`/`RenderPassCategory`/`RenderPassDrawKind` consumer in
`FrameDebuggerData.cpp`, `ComputeBlurValidation.cpp`, and
`GpuSkinningValidation.h` stays exactly as it is today, per Locked Design
Decision 2.

## Step 2: The Situation

- `BuildRealFrameDebuggerSnapshot()` (`FrameDebuggerData.cpp`, line ~698)
  today does:
  ```cpp
  const rg::RenderGraphPassSnapshot* renderOpaquePass =
      FindPassByName(graphSnapshot.passesInExecutionOrder, "RenderOpaque");
  if (renderOpaquePass == nullptr) {
      return FrameDebuggerSnapshot{};
  }
  const int pivotIndex = static_cast<int>(renderOpaquePass - graphSnapshot.passesInExecutionOrder.data());
  ```
  This is a literal string comparison against the pass's own `name` field —
  exactly the "name-string-based structural pivot" the design doc's own
  Phase C migration shape (Section 12, point 3) calls out by name as
  something a future campaign should rewrite to use ordering/tag data
  instead.
- By the time this phase runs, `PHASE1_CORE_VOCABULARY_AND_BLACKBOARD.md`
  has already added `RenderPassEvent` to `RenderGraphTypes.h` and threaded
  it through `PassRecord`/`RenderGraphPassSnapshot` as
  `renderPassEvent` (defaulting to `Opaques`), and
  `PHASE2_GPU_SKINNING_OPAQUE_BLACKBOARD_PROOF.md`/
  `PHASE3_FULL_PRODUCTION_PASS_MIGRATION_AND_VIEW_UNIFICATION.md` have
  already made every production pass in the View region carry a REAL,
  correct value (`"RenderOpaque"` → `Opaques`, `"DrawSkyBackground"` →
  `AfterOpaques`, `"RenderTransparent"` → `Transparents`, the Aerial
  Perspective Composite → `AfterTransparents`; every pre-Opaque
  compute/LUT pass stays at its own default, `Opaques`, or an even earlier
  value if that provider explicitly set one — see PHASE3's own provider
  table).
- `FindPassByName()` (an existing helper, `FrameDebuggerData.cpp`'s
  anonymous namespace) is used ELSEWHERE in this same file too (confirm
  via `search_in_dir` for `FindPassByName(` before touching anything) —
  do NOT delete or change this helper itself; other call sites (if any)
  are out of scope for this phase and must be left exactly as they are.
  Only the ONE call site that searches for `"RenderOpaque"` specifically
  changes.
- A SEPARATE, ALSO-hardcoded string search exists nearby,
  `FindPostGameViewCompositePassExecutionIndex()` (line ~213), which
  matches a pass's WRITE-texture name against the literal string
  `"GameViewComposited"`. **This is explicitly, deliberately OUT OF
  SCOPE for this phase** — the user's own committed exception (Locked
  Design Decision 2) was specifically and only about the `"RenderOpaque"`
  pivot search; this second hardcoded string is a genuinely separate
  concern, and touching it here would be uninstructed scope creep.

## Step 3: The Plan

### 3.1 — Add a small, structural helper

In `FrameDebuggerData.cpp`'s anonymous namespace, next to `FindPassByName()`:

```cpp
// render-pass-3 campaign, PHASE4 - REPLACES the old literal
// FindPassByName(..., "RenderOpaque") pivot search with a structural,
// name-free lookup: the first pass, in true execution order, whose
// rg::RenderPassEvent sort hint is >= RenderPassEvent::Opaques (see
// RenderGraphTypes.h's own doc comment on that enum - "a principled way
// to answer 'where does the view region start' ... instead of searching
// for a specific pass by name"). Deliberately does NOT filter by
// isCulled/kind/viewScope/category here - this mirrors the OLD
// FindPassByName() call's own behavior exactly (it never filtered on
// those either), preserving this function's existing "return nullptr,
// treated as 'no frame captured yet'" behavior for the one genuinely
// reachable failure case (an empty/degenerate graph snapshot).
const rg::RenderGraphPassSnapshot* FindViewRegionPivot(
    const std::vector<rg::RenderGraphPassSnapshot>& passesInExecutionOrder)
{
    for (const rg::RenderGraphPassSnapshot& pass : passesInExecutionOrder) {
        if (pass.renderPassEvent >= rg::RenderPassEvent::Opaques) {
            return &pass;
        }
    }
    return nullptr;
}
```

### 3.2 — Swap the one call site

```cpp
const rg::RenderGraphPassSnapshot* renderOpaquePass =
    FindViewRegionPivot(graphSnapshot.passesInExecutionOrder);
if (renderOpaquePass == nullptr) {
    return FrameDebuggerSnapshot{};
}
const int pivotIndex = static_cast<int>(renderOpaquePass - graphSnapshot.passesInExecutionOrder.data());
```

Everything AFTER this point in `BuildRealFrameDebuggerSnapshot()` (the
"Compute LUT"/"Compute Dispatches (Pre-GameView)" split, the view-region
walk that builds `"RenderOpaque"`'s own per-entity children, the
`isRenderOpaqueLeaf` flag logic) is UNCHANGED — it already only cares
about `pivotIndex` as a plain integer, never about HOW that integer was
found.

### 3.3 — Update/add tests

`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (the existing,
31/35+-test file `render-pass-2`'s own PHASE3 already touched) needs:

- Confirm every EXISTING test in this file still passes unchanged (this
  fix must be behaviorally IDENTICAL for every real scene this file already
  tests — the pivot search finds the exact same pass, at the exact same
  index, as before, since `"RenderOpaque"` is still the first pass tagged
  `RenderPassEvent::Opaques` in every real snapshot this file constructs).
- Add ONE new, small, focused regression test that constructs a synthetic
  `RenderGraphSnapshot` where the pass NAMED something other than
  `"RenderOpaque"` (e.g. a deliberately renamed/relabeled pass — proving
  the lookup is genuinely name-free) is the first one tagged
  `RenderPassEvent::Opaques`, and asserts `BuildRealFrameDebuggerSnapshot()`
  still correctly finds it as the pivot — this is the test that actually
  protects against a future regression back into name-based coupling.

## Definition of Done

- `FindViewRegionPivot()` exists and is the ONLY thing that decides "where
  does the Game View region start" in `FrameDebuggerData.cpp` — the
  literal string `"RenderOpaque"` no longer appears anywhere in a pass-
  lookup/search context in this file (a plain comment mentioning the name
  for human-readability is fine; an actual `==` string comparison against
  it is not).
- Every pre-existing test in `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`
  still passes, unchanged, with zero test-expectation edits needed (if any
  DO need edits, that is a signal this "fix" accidentally changed real
  behavior — stop and re-examine before proceeding).
- One new regression test (Step 3.3) exists and passes, proving the
  lookup is genuinely name-free.
- `FindPostGameViewCompositePassExecutionIndex()`'s own, separate,
  `"GameViewComposited"`-string-based logic is untouched — confirm via
  `git diff` showing zero changes to that function.
- An incremental compile of `gte_core` and `GreatTamanaEngineTests`
  succeeds, and the updated/added Frame Debugger tests pass when run
  directly (do not wait for PHASE5's full suite to first confirm this).
- A live, HTTP-driven check (`GET /frame_debugger/capture` +
  `/get_swapchain`) shows the exact same Game View event tree shape as
  before this phase, for the same test scene — this fix must be
  completely invisible to a human/HTTP client.

## What We Will NOT Do

- Do NOT touch `ComputeBlurValidation.cpp` or `GpuSkinningValidation.h` —
  neither is in scope for this phase.
- Do NOT touch any OTHER `ViewScope`/`RenderPassCategory`/`RenderPassDrawKind`
  consumer inside `FrameDebuggerData.cpp` itself — only the one
  `"RenderOpaque"` pivot search changes. The `viewScope == SceneView`
  checks, the `category == AtmosphereLut`/`category == Debug` checks, and
  `GraphicsChildEventLabelFor(pass.drawKind)` all stay exactly as they are.
- Do NOT touch `FindPostGameViewCompositePassExecutionIndex()`'s own
  `"GameViewComposited"` string match — explicitly out of scope, see Step
  2 above.
- Do NOT run a full build or full `ctest` regression suite in this phase —
  incremental compile + the specific, already-existing Frame Debugger test
  file, run directly, is enough. PHASE5 is the only phase that runs the
  full suite.
