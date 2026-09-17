# PHASE1 — RenderGraph ViewScope Choke-Point Infrastructure

_Read `PHASE0_MASTER_STRATEGY.md` in full first — in particular Section 2
("The Situation") and Locked Design Decisions #2/#3/#5/#6. This phase is
Workstream A's FOUNDATION — it adds a new, generic tag but changes NO
Frame-Debugger display logic at all yet (that is PHASE2). This phase alone
must not change what the Frame Debugger currently shows._

## Step 1: The Goal (Where are we going?)

Give every `PassRecord`/`RenderGraphPassSnapshot` in this engine a new,
structurally-tracked field — `ViewScope viewScope` — that truthfully answers
"which view (if any) does this pass conceptually belong to", stamped ONCE at
the small set of Application-layer call sites that already unambiguously know
the answer, via new, backward-compatible overloads of
`RenderGraphBuilder::AddPass()`/`AddComputePass()`. By the end of this phase:

- `enum class ViewScope { Shared, GameView, SceneView };` exists in
  `src/Renderer/RenderGraph/RenderGraphTypes.h`, right next to the existing
  `enum class ResourceKind`.
- `PassRecord::viewScope` (default `ViewScope::Shared`) exists and is copied
  through into `RenderGraphPassSnapshot::viewScope` by
  `BuildRenderGraphSnapshot()`, exactly mirroring how `PassRecord::
  isComputePass` already flows into `RenderGraphPassSnapshot::isComputePass`.
- `RenderGraphBuilder` gains two new template overloads,
  `AddPass(name, viewScope, setup, execute)` and
  `AddComputePass(name, viewScope, setup, execute)`, that stamp
  `m_passes.back().viewScope = viewScope` after delegating to the existing
  3-argument overloads. Every EXISTING call site of the 3-argument overloads
  keeps compiling completely unchanged and keeps defaulting to
  `ViewScope::Shared`.
- Every pass that is genuinely per-view gets re-wired to call the NEW
  4-argument overload with the correct, explicit `ViewScope` (full, confirmed
  list in Step 3 below) — nothing else about those passes' declared
  reads/writes/execute bodies changes at all.
- This phase does NOT change `FrameDebuggerData.cpp` at all, and does NOT
  change what the Frame Debugger currently displays — it only makes the new
  data available for PHASE2 to consume. Confirm this via the incremental
  build + a quick, throwaway manual check (see Step 5) that the Frame Debugger
  still shows the exact same (bug included) tree shape as before this phase —
  if it changed, something was touched that shouldn't have been.

## Step 2: The Situation (Where are we now?)

Re-confirmed by direct source reading during this campaign's planning:

- `src/Renderer/RenderGraph/RenderGraphTypes.h` defines `PassRecord` (holds
  `name`, `reads`/`writes` vectors, `execute`, and — since `frame-debugger-5`
  — `isComputePass`) and `enum class ResourceKind { Texture, Buffer,
  VolumeTexture };`. Read this file first to find `PassRecord`'s exact current
  field layout before adding `viewScope`.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.h` defines
  `RenderGraphPassSnapshot` (the already-resolved-into-plain-strings display
  copy `FrameDebuggerData.cpp` actually reads) and
  `BuildRenderGraphSnapshot(const CompiledGraph&, const CompiledGraphInput&,
  statsLookup)` in the matching `.cpp` file — this function is what stamps
  `RenderGraphPassSnapshot::isComputePass = pass.isComputePass` (or similar)
  for every surviving/culled pass; `viewScope` must be copied through at the
  exact same place, for BOTH a surviving and a culled pass (mirrors
  `isComputePass`'s own "must still truthfully report this even when culled"
  rule, per that field's own doc comment).
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` defines the existing
  `template <typename SetupFn, typename ExecuteFn> void AddPass(const char*
  name, SetupFn&&, ExecuteFn&&)` and `AddComputePass(...)` (the latter simply
  calls `AddPass` then sets `m_passes.back().isComputePass = true;` — copy
  this EXACT pattern for the new overloads, see Step 3.1 below for the literal
  code to add).
- `src/Application/RenderPasses.h/.cpp` defines `AddGameViewPass()`,
  `AddSceneViewPass()`, `AddPresentPass()`, `AddGpuSkinningPasses()` — plain
  `AddPass`/`AddComputePass` callers today.
- `src/Application/AtmospherePassSequence.h/.cpp` defines
  `AddAtmosphereSharedLutPasses()` (Transmittance + Multi-Scattering, called
  ONCE per frame, view-independent) and `AddAtmosphereViewLutPasses()`
  (Sky-View LUT + Aerial Perspective Volume, called ONCE per view — twice per
  frame total, once for `gameTarget`, once for `sceneTarget`) plus
  `AddAtmosphereCompositePass()` (also called once per view).
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp` is where the ACTUAL
  `builder.AddComputePass("AtmosphereSkyViewLutPass", ...)` /
  `"AtmosphereMultiScatteringLutPass"` / `"AtmosphereTransmittanceLutPass"` /
  `"AtmosphereAerialPerspectiveVolumePass"` /
  `"AtmosphereAerialPerspectiveVolumeDebugSlicePass"` /
  `"AtmosphereAerialPerspectiveCompositePass"` calls live — confirmed exact
  method names: `AddTransmittanceLutPass()`, `AddMultiScatteringLutPass()`,
  `AddSkyViewLutPass()`, `AddAerialPerspectiveVolumePass()`,
  `AddAerialPerspectiveVolumeDebugSlicePass()`,
  `AddAerialPerspectiveCompositePass()`. Every one of these already takes an
  `outputTextureName`/similarly-suffixed parameter distinguishing Game View
  from Scene View calls — none of them take a `ViewScope` yet.
- `src/Application/Application.cpp` (the real per-frame graph-building lambda,
  search for `AddAtmosphereViewLutPasses(` to find both call sites) is the ONE
  place that unambiguously knows, at the call site, whether it is inside the
  `if (gameTarget != nullptr)` block (Game View) or the
  `if (sceneTarget != nullptr)` block (Scene View) — this is where the new
  `ViewScope` VALUE originates from; every function between here and the
  actual `builder.AddComputePass()` call just has to forward it through.
- `src/Editor/ComputeBlurValidation.h/.cpp`'s `AddPass()` method (confirmed via
  `Application.cpp`: called only inside the `sceneTarget != nullptr` block,
  reading the Scene View's own pre-composite texture `h`) is Scene-View-only
  and currently untagged.
- `AddGpuSkinningPasses()` (`RenderPasses.cpp`) is called ONCE per frame,
  outside both the `gameTarget`/`sceneTarget` `if` blocks — it is genuinely
  `ViewScope::Shared` (the default) and needs NO call-site change at all.
- `AddAtmosphereSharedLutPasses()`'s two passes (Transmittance,
  Multi-Scattering) are likewise genuinely `ViewScope::Shared` and need no
  call-site change.

## Step 3: The Plan (exact changes)

### 3.1 `RenderGraphTypes.h` — the new enum + `PassRecord` field

Add, near `enum class ResourceKind { Texture, Buffer, VolumeTexture };`:

```cpp
// frame-debugger-6 campaign, PHASE1 - which real VIEW (if any) this pass
// conceptually belongs to. Stamped exactly once, at the same small set of
// Application-layer call sites that already unambiguously know the answer
// (see RenderGraphBuilder::AddPass()/AddComputePass()'s new 4-argument
// overloads below) - never guessed/derived from a pass's own name or
// resource names. `Shared` (the default - see PassRecord::viewScope below)
// means "this pass is not duplicated per view" (e.g. the Transmittance/
// Multi-Scattering LUTs, computed once per frame; GPU Skinning dispatches).
// A pass tagged GameView/SceneView exists as a genuinely separate PassRecord
// instance per view that calls it - see AtmospherePassSequence.cpp's own
// AddAtmosphereViewLutPasses(), called once per view, each call producing
// its own distinct PassRecord(s) even when two calls happen to share an
// identical literal pass `name` string.
enum class ViewScope {
    Shared,
    GameView,
    SceneView,
};
```

Add to `PassRecord`:

```cpp
// frame-debugger-6 campaign, PHASE1 - see ViewScope's own doc comment above.
// Defaults to Shared so every pre-existing AddPass()/AddComputePass() call
// site (which never mentions ViewScope at all) keeps its exact prior
// behavior/meaning unchanged.
ViewScope viewScope = ViewScope::Shared;
```

Field placement inside `PassRecord` does not matter for compilation — this is
a plain struct with in-class default member initializers, never aggregate-
initialized positionally anywhere in this codebase today (re-confirmed: no
test file constructs a bare `PassRecord{ ... }` with positional field values
— `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`, which DOES exist,
only ever builds a `PassRecord` indirectly, through `RenderGraphBuilder::
AddPass()`/`AddComputePass()`), so adding `viewScope` anywhere in the struct
(e.g. immediately after `isComputePass`, for readability) is completely safe.

### 3.2 `RenderGraphSnapshot.h`/`.cpp` — stamp it through

In `RenderGraphPassSnapshot` (the header), add, parallel to `isComputePass`:

```cpp
// frame-debugger-6 campaign, PHASE1 - copied straight through for BOTH a
// surviving AND a culled pass, exactly like isComputePass above (a culled
// pass must still truthfully report which view it belonged to).
ViewScope viewScope = ViewScope::Shared;
```

In `BuildRenderGraphSnapshot()` (the `.cpp`), the actual `pass.isComputePass`
copy happens in exactly ONE place, not two: a small anonymous-namespace
helper function, `BuildPassSnapshot(pass, input, isCulled, statsLookup)`,
which BOTH the surviving-passes loop and the culled-passes loop each call
once per pass — the copy is NOT duplicated inline into each loop itself (the
loops only ever call this shared helper). Add
`snapshot.viewScope = pass.viewScope;` ONCE, inside `BuildPassSnapshot()`,
immediately alongside its existing `snapshot.isComputePass = pass.isComputePass;`
line — placed BEFORE the `if (!isCulled && statsLookup) { ... }` block that
gates `stats` (i.e. unconditional, exactly like `isComputePass` itself
already is), so it runs identically regardless of which loop invoked this
helper. This single line automatically covers both call sites/loops with no
further changes needed — do NOT add a second, separate copy inside either
loop body; there is nothing to copy there, since neither loop touches
`RenderGraphPassSnapshot` fields directly.

### 3.3 `RenderGraphBuilder.h` — the new overloads

Add immediately after the existing `AddPass()` template and immediately after
the existing `AddComputePass()` template, respectively:

```cpp
// frame-debugger-6 campaign, PHASE1 - identical to AddPass() above, plus
// stamping PassRecord::viewScope. Every pre-existing 3-argument AddPass()
// call site is completely unaffected - this is purely an additive overload.
template <typename SetupFn, typename ExecuteFn>
void AddPass(const char* name, ViewScope viewScope, SetupFn&& setup, ExecuteFn&& execute)
{
    AddPass(name, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
    m_passes.back().viewScope = viewScope;
}
```

```cpp
// frame-debugger-6 campaign, PHASE1 - identical to AddComputePass() above,
// plus stamping PassRecord::viewScope. Every pre-existing 3-argument
// AddComputePass() call site is completely unaffected.
template <typename SetupFn, typename ExecuteFn>
void AddComputePass(const char* name, ViewScope viewScope, SetupFn&& setup, ExecuteFn&& execute)
{
    AddComputePass(name, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
    m_passes.back().viewScope = viewScope;
}
```

(Overload resolution note: since `ViewScope` is a distinct enum type, there is
no ambiguity between the 3-argument and 4-argument overloads at any real call
site — verify this compiles cleanly at every call site touched below.)

### 3.4 Thread `ViewScope` through the Application-layer helpers

Every function below currently has NO `ViewScope`/similar parameter. Add one,
named `viewScope`, placed logically near the other per-view parameters
(e.g. right after `outputTextureName`/near `skyViewLutName`/
`aerialPerspectiveVolumeName` — use judgement to keep each signature
readable), and forward it into that function's own `builder.AddComputePass(
name, viewScope, setup, execute)` call(s):

- `AtmosphereLutRenderer::AddSkyViewLutPass(...)` — 1 call site inside.
- `AtmosphereLutRenderer::AddAerialPerspectiveVolumePass(...)` — 1 call site.
- `AtmosphereLutRenderer::AddAerialPerspectiveVolumeDebugSlicePass(...)` — 1
  call site (only ever invoked for Game View today per `Application.cpp`, but
  still take an explicit parameter rather than hardcoding `GameView` inside
  this function — no assumption about "this is only ever called for one view"
  should live inside `AtmosphereLutRenderer` itself, per Locked Design
  Decision #2's "no future special case" spirit).
- `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass(...)` — 1 call
  site (this is the function `AtmospherePassSequence.cpp::
  AddAtmosphereCompositePass()` wraps).
- `AtmospherePassSequence.cpp::AddAtmosphereViewLutPasses(...)` — add a
  `ViewScope viewScope` parameter, forward it to BOTH the
  `AddSkyViewLutPass()` and `AddAerialPerspectiveVolumePass()` calls it
  makes.
- `AtmospherePassSequence.cpp::AddAtmosphereCompositePass(...)` — add a
  `ViewScope viewScope` parameter, forward it to
  `atmosphereLutRenderer.AddAerialPerspectiveCompositePass(...)`.
- Do **NOT** add a `viewScope` parameter to `AddAtmosphereSharedLutPasses()` —
  its two passes are genuinely `Shared`; leave its own two internal
  `builder.AddComputePass(name, setup, execute)` calls (inside
  `AtmosphereLutRenderer::AddTransmittanceLutPass()`/
  `AddMultiScatteringLutPass()`) on the existing 3-argument overload, which
  already defaults to `ViewScope::Shared` — this is correct, deliberate,
  zero-change behavior, not an oversight.
- `RenderPasses.cpp::AddGameViewPass()` — change its own
  `builder.AddPass("GameView", setup, execute)` call to the 4-argument
  overload with `rg::ViewScope::GameView`.
- `RenderPasses.cpp::AddSceneViewPass()` — change its own
  `builder.AddPass("SceneView", setup, execute)` call to
  `rg::ViewScope::SceneView`.
- `RenderPasses.cpp::AddPresentPass()` — tag its `"Present"` pass
  `rg::ViewScope::Shared` EXPLICITLY (still the default value, but pass it
  explicitly here for documentation clarity, since "Present" genuinely serves
  both views at once) — OPTIONAL, skip if it adds noise; leaving it on the
  3-argument overload (implicit `Shared`) is equally correct and is the
  simpler choice — use your own judgement, this one line does not affect
  PHASE2's correctness either way.
- `Editor/ComputeBlurValidation.cpp::ComputeBlurValidation::AddPass(...)` —
  change its own internal `builder.AddComputePass("ComputeBlurValidation",
  setup, execute)` call to `rg::ViewScope::SceneView` (confirmed Scene-View-only
  by reading `Application.cpp`'s own call site — re-verify this assumption
  still holds by reading `Application.cpp`'s `AddBlurValidationPass(b,
  m_renderer, h, extent)` call site again at implementation time before
  hardcoding this; if a future change ever makes this pass dual-purpose, this
  is exactly the one place to revisit).

### 3.5 `Application.cpp` — supply the real `ViewScope` value at each call site

At the two `AddAtmosphereViewLutPasses(...)` call sites (inside
`if (gameTarget != nullptr)` and `if (sceneTarget != nullptr)` respectively),
pass `rg::ViewScope::GameView` / `rg::ViewScope::SceneView` respectively as
the new argument — NOT `gte::rg::ViewScope::...`: `Application.cpp` is
already entirely inside `namespace gte { ... }` (confirmed — see its own
top-of-file `namespace gte {` opening), and every other `rg::`-qualified
symbol at these exact call sites (`rg::TextureHandle`, `rg::ResourceAccess::
ShaderRead`, etc.) is already spelled the short, unqualified way throughout
this file — match that existing convention, don't introduce a redundant
`gte::` prefix nothing else in this file uses. Same for the two
`AddAtmosphereCompositePass(...)` call sites and the one
`AddAerialPerspectiveVolumeDebugSlicePass(...)` call site inside the
`gameTarget` block (pass `rg::ViewScope::GameView`). Re-locate each exact call
site by searching for `AddAtmosphereViewLutPasses(` /
`AddAtmosphereCompositePass(` / `AddAerialPerspectiveVolumeDebugSlicePass(` in
`Application.cpp` — do not rely on the specific line numbers noted during
this campaign's planning, as they will drift.

## Step 4: Tests

- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` exists today
  (re-confirmed by directly reading the `tests/Renderer/RenderGraph/` folder
  during this doc's own review — independently cross-referenced by
  `RenderGraphBuilder.h`'s own header comment too) — add a case there
  asserting the new 4-argument `AddPass`/`AddComputePass` overloads correctly
  stamp `PassRecord::viewScope`, and that the pre-existing 3-argument
  overloads still leave it at `ViewScope::Shared`. Still re-verify it is
  present at implementation time (files can be renamed/removed between this
  review and actual implementation) rather than assuming this instruction
  is stale-proof forever.
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` exists today (same
  re-confirmation as above) — add a case feeding a hand-built
  `CompiledGraph`/`CompiledGraphInput` containing a pass with a non-`Shared`
  `viewScope` and asserting `BuildRenderGraphSnapshot()`'s output correctly
  carries it through for BOTH a surviving and a deliberately-culled pass —
  one assertion per case is enough for both, since (per Step 3.2 above) both
  the surviving-loop and culled-loop route through the SAME shared
  `BuildPassSnapshot()` helper; you are not proving two independent code
  paths, just confirming that one shared line runs correctly regardless of
  which loop invoked it.
- Do not modify any Frame-Debugger test file in this phase — PHASE2 owns
  those.

## Step 5: Definition of Done for PHASE1

- [ ] `ViewScope` enum added to `RenderGraphTypes.h`, `PassRecord::viewScope`
      field added (default `Shared`).
- [ ] `RenderGraphPassSnapshot::viewScope` added, stamped through
      `BuildRenderGraphSnapshot()` for both surviving and culled passes.
- [ ] `RenderGraphBuilder::AddPass()`/`AddComputePass()` 4-argument overloads
      added; every pre-existing call site across the whole codebase still
      compiles UNCHANGED.
- [ ] Every genuinely per-view pass identified in Step 3.4 now calls the new
      4-argument overload with the correct, explicit `ViewScope` — re-verify
      each one against `Application.cpp`'s real call sites, do not trust stale
      line numbers.
- [ ] `AddGpuSkinningPasses()` and `AddAtmosphereSharedLutPasses()`'s own
      internal passes are UNCHANGED (still implicitly `Shared` via the
      3-argument overload) — confirmed by reading the final diff, not just
      assumed.
- [ ] New/updated Tier-1 tests per Step 4 pass.
- [ ] Incremental build succeeds (`cmake --build build`).
- [ ] Manual sanity check: launch `build/GreatTamanaEngine.exe`
      (`run_app_background`), load `TestScene.gtscene`
      (`POST /load_scene` with `{}` body), open+enable+capture the Frame
      Debugger over HTTP, and confirm via `GET /get_swapchain` that the tree
      shape is **BYTE-FOR-BYTE THE SAME AS BEFORE THIS PHASE** (duplicate bug
      still present, unfixed — that is CORRECT and EXPECTED for this phase;
      PHASE2 is what fixes it). If the tree changed shape at all in this
      phase, something outside this phase's intended scope was touched —
      find and revert it before proceeding. Stop the background process
      (`stop_app_background`) when done.
- [ ] Write a short phase completion note (can be folded into the git commit
      message, or a `PHASE1_COMPLETION_REPORT.md` in this same folder if you
      prefer matching `frame-debugger-5`'s own per-phase report convention)
      and commit via `git_add`/`git_commit`.
