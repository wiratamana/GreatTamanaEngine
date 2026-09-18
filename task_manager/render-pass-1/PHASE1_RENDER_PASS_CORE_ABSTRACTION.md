# PHASE1: Render Pass Core Abstraction (`PassKind` + `RenderPassCategory` + `AddRenderPass()`)

_Child of `PHASE0_MASTER_STRATEGY.md` — read that file first. Part of the
`render-pass-1` campaign._

## Step 1: The Goal

Give this engine exactly ONE, official, uniform way to declare a render
pass — a thin, lightweight, lambda-based chokepoint built directly on top
of the EXISTING `RenderGraphBuilder::AddPass()`/`AddComputePass()`
methods (per PHASE0's Locked Design Decision #1 — explicitly NOT a
polymorphic `class IRenderPass` hierarchy). This is the foundation every
later phase's pass migrations call into. Nothing in this phase changes
any pass's actual runtime BEHAVIOR — it purely adds new vocabulary/
plumbing that nothing calls yet (mirrors this codebase's own established
"Phase 1 ships pure vocabulary, nothing calls it yet" precedent — see
e.g. `RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md`'s own "What We
Will NOT Do").

## Step 2: The Situation

- `src/Renderer/RenderGraph/RenderGraphTypes.h`'s `PassRecord` struct
  carries `bool isComputePass = false;` — a plain, binary flag with no
  room to grow (see the field's own doc comment: "true for every pass
  declared via `RenderGraphBuilder::AddComputePass()`... false... for a
  pass declared via plain `AddPass()`").
- `RenderGraphBuilder.h`'s `AddPass()`/`AddComputePass()` are template
  methods (each with a 3-arg and a 4-arg-with-`ViewScope` overload) that
  push a `PassRecord`, run the caller's `setup` lambda against a
  `PassBuilder&`, and type-erase the caller's `execute` lambda into
  `PassRecord::execute`.
- There is currently no concept at all of "which conceptual GROUP does
  this pass belong to for display purposes" (e.g. "this is one of the
  Atmosphere LUT passes" vs. "this is a GPU Skinning dispatch" vs. "this
  is a Frame-Debugger-internal debug pass"). `ViewScope` exists but only
  answers "which VIEW" (Shared/GameView/SceneView), a completely
  orthogonal question.
- `RenderGraphPassSnapshot` (`RenderGraphSnapshot.h`) already copies
  `isComputePass`/`viewScope` straight through for both a surviving and a
  culled pass — any new field added to `PassRecord` that a downstream
  consumer (the Frame Debugger, PHASE4) needs to see must be threaded
  through here too, exactly the same way.

## Step 3: The Plan

### 3.1 — `RenderGraphTypes.h`: add `PassKind` (REPLACES `isComputePass`)

Add, right next to `ResourceKind`/`ViewScope`:

```cpp
// Render Pass campaign (task_manager/render-pass-1), PHASE1 - REPLACES the
// old plain `bool isComputePass` with a real, extensible enum. Today there
// are exactly two kinds (a pass either records inside a
// vkCmdBeginRendering/vkCmdEndRendering bracket, or it doesn't - it issues
// vkCmdDispatch instead), but "Graphics"/"Compute" as explicit, named
// values (rather than an anonymous bool) is what lets a THIRD kind (e.g. a
// future pure-blit/copy pass) be added later without every call site
// having to re-litigate what `true`/`false` used to mean. Deliberately an
// exhaustive-switch-friendly small enum, mirroring ResourceAccess's own
// "no default: case, ever" convention in this same file.
enum class PassKind : std::uint8_t {
    Graphics,
    Compute,
};

const char* ToString(PassKind kind) noexcept;
```

On `PassRecord`, REPLACE:
```cpp
bool isComputePass = false;
```
with:
```cpp
PassKind kind = PassKind::Graphics;
```
Update every existing reader (grep for `isComputePass` across the whole
repo — expect hits in `RenderGraphBuilder.h`, `RenderGraphSnapshot.h`/.cpp,
`FrameDebuggerData.cpp`, and their respective test files) to instead
compare `kind == PassKind::Compute`. This is a mechanical, low-risk
rename — the enum has exactly the same two states the bool did.

### 3.2 — `RenderGraphTypes.h`: add `RenderPassCategory`

A second, ORTHOGONAL new field — purely descriptive metadata for display/
grouping purposes only (mirrors `ViewScope`'s own "stamped once, read only
by a downstream display consumer, never by the compiler/barrier planner"
precedent exactly):

```cpp
// Render Pass campaign, PHASE1 - which conceptual GROUP this pass belongs
// to, for the Editor Frame Debugger's own tree-grouping purposes ONLY
// (PHASE4 of this same campaign) - completely orthogonal to PassKind
// (Graphics/Compute - WHAT this pass technically does) and ViewScope
// (WHICH view this pass belongs to). Nothing in RenderGraph.cpp/
// RenderGraphCompiler.cpp/RenderGraphBarrierPlanner.cpp ever reads this
// field - exactly like ViewScope's own existing "PURELY DESCRIPTIVE
// metadata" rule (see PassRecord::isComputePass's OLD doc comment, now
// PassRecord::kind above, for the precedent this mirrors).
enum class RenderPassCategory : std::uint8_t {
    General,       // The default - no special Frame Debugger grouping treatment.
    AtmosphereLut, // Every Atmosphere LUT/composite compute pass (PHASE3) - grouped under "Compute LUT".
    GpuSkinning,   // A per-model GPU vertex-skinning compute dispatch - stays under the existing generic "Compute Dispatches" bucket.
    Debug,         // Frame-Debugger-internal replay passes / Compute Blur Validation - never a real Frame Debugger tree citizen themselves (already filtered out, or shown under their own separate heading - see PHASE4/PHASE5).
};

const char* ToString(RenderPassCategory category) noexcept;
```

Add `RenderPassCategory category = RenderPassCategory::General;` as a new
field on `PassRecord`, appended at the END of the struct (never inserted
in the middle — this codebase has an explicit, documented convention
against that, see `BoneViewerWindow.h`'s own precedent quoted inside
`docs/conventions/frame-debugger.md`).

### 3.3 — `RenderGraphSnapshot.h`/`.cpp`: thread `category` through

`RenderGraphPassSnapshot` gains its own
`RenderPassCategory category = RenderPassCategory::General;` field,
copied straight through for BOTH a surviving and a culled pass — mirror
`isComputePass`'s (now `kind`'s) exact copy-through behavior in
`BuildRenderGraphSnapshot()` (`RenderGraphSnapshot.cpp`). Rename the
struct's own `isComputePass` field to `kind` (type `PassKind`) to match
`PassRecord`'s own renamed field, for consistency — anywhere this
snapshot field is read downstream (today: `FrameDebuggerData.cpp`,
PHASE4) will need the matching rename, which is exactly this phase's own
"mechanical, low-risk rename" scope from 3.1.

### 3.4 — `RenderGraphBuilder.h`: the new `AddRenderPass()` chokepoint

Add ONE new template method directly on `RenderGraphBuilder` (a sibling
of `AddPass()`/`AddComputePass()`, in the exact same class, so it can
reach `m_passes.back()` the same way `AddComputePass()`'s 4-arg overload
already does to stamp `viewScope`):

```cpp
// Render Pass campaign (task_manager/render-pass-1), PHASE1 - the ONE,
// OFFICIAL entry point every real pass declaration in this engine should
// use from now on (Application layer AND Renderer layer alike - see
// AtmosphereLutRenderer.cpp for a Renderer-layer example, PHASE3). A thin,
// lightweight wrapper around the two pre-existing methods below - it adds
// NO new capability of its own beyond stamping `kind`/`category` in one
// place, by design (PHASE0's Locked Design Decision #1: no polymorphic
// pass-object hierarchy). AddPass()/AddComputePass() themselves are NOT
// removed or deprecated - they remain the low-level primitives this method
// (and Tier-1 tests) are built on, and existing test-only call sites are
// free to keep using them directly.
template <typename SetupFn, typename ExecuteFn>
void AddRenderPass(const char* name, PassKind kind, ViewScope viewScope, RenderPassCategory category,
    SetupFn&& setup, ExecuteFn&& execute)
{
    if (kind == PassKind::Compute) {
        AddComputePass(name, viewScope, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
    } else {
        AddPass(name, viewScope, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
    }
    m_passes.back().category = category;
}

// Convenience overload defaulting `viewScope` to Shared and `category` to
// General - for the (today, majority of) real call sites that need
// neither. Mirrors AddPass()'s own pre-existing 3-arg/4-arg overload pair
// exactly.
template <typename SetupFn, typename ExecuteFn>
void AddRenderPass(const char* name, PassKind kind, SetupFn&& setup, ExecuteFn&& execute)
{
    AddRenderPass(name, kind, ViewScope::Shared, RenderPassCategory::General,
        std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
}
```

Note the (already-stamped) `kind`/`category` set by the 4-argument
`AddComputePass(name, viewScope, ...)` branch's own OWN `isComputePass`
(now `kind`) stamping — `AddComputePass()` already stamps
`kind = PassKind::Compute` internally, so calling it from inside
`AddRenderPass()`'s `Compute` branch is correct and redundant-but-harmless
with the outer `kind` parameter; do not "optimize" this by skipping
`AddComputePass()`/`AddPass()` and pushing a `PassRecord` by hand — reuse
the existing, already-tested methods verbatim, exactly as written above.

### 3.5 — Tests

Add `tests/Renderer/RenderGraph/RenderPassTests.cpp` (new file, matching
the existing `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`
pattern exactly): build a `RenderGraphBuilder`, call `AddRenderPass()`
with `PassKind::Graphics` and separately with `PassKind::Compute`, call
`Finish()`, and assert the resulting `CompiledGraphInput::passes` entries
carry the expected `kind`/`category`/`viewScope`/`name`. Also add a small
`PassKindTests.cpp`/extend an existing `RenderGraphTypesTests.cpp` file
covering `ToString(PassKind)`/`ToString(RenderPassCategory)`'s exhaustive
coverage (every enumerator, never `nullptr`). Register the new test file
in `tests/CMakeLists.txt` next to its siblings.

## Definition of Done

- `PassKind`/`RenderPassCategory` exist in `RenderGraphTypes.h`, both with
  a `ToString()` helper.
- `PassRecord::isComputePass` (bool) no longer exists anywhere in the
  repo — replaced by `PassRecord::kind` (`PassKind`) — confirmed via
  `search_in_dir` for the literal string `isComputePass` returning zero
  hits outside this phase's own doc-comment history.
- `PassRecord::category` (`RenderPassCategory`, default `General`) exists.
- `RenderGraphBuilder::AddRenderPass()` (both overloads) exists, compiles,
  and is exercised by a new, passing Tier-1 test.
- `RenderGraphPassSnapshot` carries the renamed `kind` field and a new
  `category` field, both copied through unchanged for culled passes.
- Every EXISTING call site that read `PassRecord::isComputePass` or
  `RenderGraphPassSnapshot::isComputePass` (there should be very few this
  early — mainly `FrameDebuggerData.cpp`) is updated to read `kind ==
  PassKind::Compute` instead, and still compiles/passes its own existing
  tests unchanged.
- An incremental compile of `gte_core` and `GreatTamanaEngineTests`
  succeeds.
- Nothing in this phase changes runtime rendering behavior at all — no
  real pass yet calls `AddRenderPass()` (that starts in PHASE2/PHASE3).

## What We Will NOT Do

- Do NOT touch `RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp` —
  `kind`/`category` are read by NOTHING in either file, by design.
- Do NOT migrate any real pass declaration site yet — that is PHASE2
  onward. This phase is pure vocabulary + the one new chokepoint method,
  unused in production code.
- Do NOT remove `AddPass()`/`AddComputePass()` themselves — they remain
  the low-level primitives `AddRenderPass()` is built on.
- Do NOT introduce a virtual/polymorphic pass class of any kind — see
  PHASE0's Locked Design Decision #1.
