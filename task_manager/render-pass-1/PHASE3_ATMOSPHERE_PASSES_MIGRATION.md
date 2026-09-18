# PHASE3: Migrate Atmosphere LUT/Composite Passes onto `AddRenderPass()` + `RenderPassCategory::AtmosphereLut`

_Child of `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1` and `PHASE2`
already being merged. Part of the `render-pass-1` campaign._

## Step 1: The Goal

Every Atmosphere compute pass in this engine — Transmittance LUT,
Multi-Scattering LUT, Sky-View LUT, Aerial Perspective Volume, Aerial
Perspective Volume Debug Slice — is migrated to declare itself through
`RenderGraphBuilder::AddRenderPass()` (PHASE1) instead of calling
`builder.AddComputePass()` directly, and is tagged
`RenderPassCategory::AtmosphereLut` so PHASE4's Frame Debugger rework can
generically group them under a `"Compute LUT"` tree heading. The Aerial
Perspective Composite pass (which runs AFTER the view, reading the
now-split `"RenderOpaque"`/`"DrawSkyBackground"` output) is migrated the
same way, but is NOT tagged `AtmosphereLut` (it is not a LUT — see 3.2).

This phase changes NO rendering behavior at all — every pass keeps
exactly the same name, reads, writes, and execution order it has today.
This is a pure "route the exact same declaration through the new
chokepoint, and stamp one new piece of metadata" migration.

## Step 2: The Situation

- Every one of these passes is currently declared via a direct
  `builder.AddComputePass(name, ..., setup, execute)` or
  `builder.AddComputePass(name, viewScope, ..., setup, execute)` call
  inside `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`'s own
  `AddTransmittanceLutPass()`, `AddMultiScatteringLutPass()`,
  `AddSkyViewLutPass()`, `AddAerialPerspectiveVolumePass()`,
  `AddAerialPerspectiveVolumeDebugSlicePass()`, and
  `AddAerialPerspectiveCompositePass()` methods (see
  `src/Renderer/Atmosphere/AtmosphereLutRenderer.h` for every method's
  exact signature — already read and confirmed by PHASE0's own research;
  do not guess signatures, read the real header before editing).
- `AtmosphereLutRenderer` lives under `src/Renderer/Atmosphere/` (the
  RENDERER layer, not Application) — it must NOT gain any dependency on
  ECS/Editor types (Clean Architecture, `AGENTS.md`). `RenderGraphBuilder`
  (and therefore the new `AddRenderPass()`) is already a dependency this
  class has today (it includes `RenderGraphBuilder.h`), so calling the
  new method requires no new `#include` beyond what is already there.
- `AtmospherePassSequence.cpp` (`src/Application/`) is the Application-
  layer glue that calls these `AtmosphereLutRenderer` methods in the
  right order every frame (`AddAtmosphereSharedLutPasses()`,
  `AddAtmosphereViewLutPasses()`, `AddAtmosphereCompositePass()`) — this
  file itself never calls `builder.AddComputePass()` directly today (it
  only forwards into `AtmosphereLutRenderer`'s own methods), so THIS file
  needs no changes for this phase beyond making sure its own call sites
  still compile against unchanged `AtmosphereLutRenderer` method
  signatures (they do — this phase changes each method's INTERNAL body
  only, never its public signature).

## Step 3: The Plan

### 3.1 — Migrate each `AtmosphereLutRenderer` method's internal `builder.AddComputePass(...)` call

For EACH of the five methods below, find its own `builder.AddComputePass(...)`
call and replace it with the equivalent `builder.AddRenderPass(name,
rg::PassKind::Compute, viewScope, rg::RenderPassCategory::AtmosphereLut,
setup, execute)` call — same `name`, same `setup`/`execute` lambdas,
same `viewScope` argument (where one is already passed) verbatim,
UNCHANGED. This is intentionally a "swap one function call for another
with the same effective observable behavior, plus one new stamped field"
change — nothing about `reads`/`writes`/`execute` logic changes:

1. `AddTransmittanceLutPass()` — today calls the 3-arg `AddComputePass`
   (no explicit `viewScope`, so it is `ViewScope::Shared` by default).
   Migrate to `AddRenderPass(name, PassKind::Compute, ViewScope::Shared,
   RenderPassCategory::AtmosphereLut, setup, execute)`.
2. `AddMultiScatteringLutPass()` — same shape as above
   (`ViewScope::Shared`).
3. `AddSkyViewLutPass()` — already takes a `viewScope` parameter (its own
   `rg::ViewScope viewScope` argument) forwarded to the 4-arg
   `AddComputePass(name, viewScope, ...)`. Migrate to
   `AddRenderPass(name, PassKind::Compute, viewScope,
   RenderPassCategory::AtmosphereLut, setup, execute)`.
4. `AddAerialPerspectiveVolumePass()` — same shape as
   `AddSkyViewLutPass()` (has its own `viewScope` parameter already).
5. `AddAerialPerspectiveVolumeDebugSlicePass()` — same shape (has its own
   `viewScope` parameter). NOTE: this pass is Editor-debug-tooling-
   adjacent (it exists purely so a slice of the volume is
   `GET /get_texture`-capturable), but it IS a genuine, real Atmosphere
   LUT-family compute dispatch that runs every frame the Aerial
   Perspective Volume pass runs — tag it `AtmosphereLut` too, for
   consistency; do NOT tag it `Debug` (that category, per PHASE1's own
   doc comment, is reserved for Frame-Debugger-internal-only passes like
   the replay passes in PHASE5, which are a fundamentally different kind
   of "debug" — never shown in the tree AT ALL under normal operation vs.
   this pass, which genuinely always runs and IS meant to be visible).

### 3.2 — `AddAerialPerspectiveCompositePass()` — migrate, but tag `General`

This pass is NOT a LUT (it does not compute a lookup table — it
CONSUMES the already-computed LUTs/volume plus the just-rendered
`RenderOpaque`+`DrawSkyBackground` output to produce the final
`"GameViewComposited"`/`"SceneViewComposited"` texture). Migrate its
internal `builder.AddComputePass(...)` call to `AddRenderPass(name,
PassKind::Compute, viewScope, RenderPassCategory::General, setup,
execute)` — same reasoning as every other migration in this phase
(behavior-preserving), but `General`, not `AtmosphereLut` — PHASE4's
Frame Debugger tree keeps this one under the existing, generic
`"Compute Dispatches (Post-GameView)"` heading, not `"Compute LUT"` (a
LUT is an input; this pass is the thing that CONSUMES the LUTs to
produce the frame's final look — conceptually the reader-facing
"apply the Aerial Perspective LUT to the final image" step, matching
the spirit, if not the literal wording, of the user's own example
`"Draw Aerial Perspective LUT"` list item).

### 3.3 — Grep audit

After 3.1/3.2, run `search_in_dir` for the literal substring
`AddComputePass(` across `src/Renderer/Atmosphere/` — it should return
ZERO hits (every call site in this directory now goes through
`AddRenderPass()`). Do the same for `src/Application/AtmospherePassSequence.cpp`
— it should ALSO already be zero (per Step 2 above, that file never
called `AddComputePass()` directly in the first place; this check is
just confirming that assumption was correct).

## Definition of Done

- All 6 Atmosphere pass methods listed above call
  `builder.AddRenderPass(...)` internally instead of
  `builder.AddComputePass(...)`.
- 5 of the 6 (every LUT pass, including the debug-slice pass) are tagged
  `RenderPassCategory::AtmosphereLut`; the Composite pass is tagged
  `RenderPassCategory::General`.
- Zero change to any pass's `name`, `reads`, `writes`, `viewScope`, or
  `execute` behavior — verify via an incremental build +
  `run_app_background` + `gte_send_request` screenshot of `/get_game_view`
  and the Render Graph panel (via `GET /activate_tab?tabName=Render
  Graph`, or simply eyeball the screenshot) showing pixel-identical
  output to PHASE2's own end state.
- `search_in_dir` audit from 3.3 confirms zero remaining direct
  `AddComputePass()` call sites under `src/Renderer/Atmosphere/`.
- Incremental compile succeeds; completion report + git commit as usual.

## What We Will NOT Do

- Do NOT change any Atmosphere pass's PUBLIC method signature
  (`AtmosphereLutRenderer.h`) — every caller in `AtmospherePassSequence.cpp`
  keeps compiling completely unchanged.
- Do NOT touch `AtmosphereSkyBackgroundRenderer`/`DrawSkyBackground()` —
  that is a direct `vkCmdDraw()` call outside the compute-pass family
  entirely, already handled by PHASE2's `AddDrawSkyBackgroundPass()`.
- Do NOT attempt the Frame Debugger tree rework yet — that is PHASE4,
  which depends on EVERY pass this campaign cares about (this phase's
  Atmosphere passes AND PHASE2's Opaque/Sky/Transparent passes) already
  being migrated and tagged correctly first.
