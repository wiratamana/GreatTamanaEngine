# PHASE3 COMPLETION REPORT — Atmosphere Passes Migration

_Child of `PHASE0_MASTER_STRATEGY.md`. Campaign: `render-pass-1`, branch
`feature/render-pass-impl`._

## Summary

Implemented PHASE3 exactly per `PHASE3_ATMOSPHERE_PASSES_MIGRATION.md`: all six
Atmosphere compute pass declarations inside
`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` now route through PHASE1's
`RenderGraphBuilder::AddRenderPass()` chokepoint instead of calling
`builder.AddComputePass(...)` directly, each stamped with the correct
`RenderPassCategory`. This is a pure "swap one function call for an equivalent
one, plus one new stamped metadata field" migration — no pass's `name`,
`reads`, `writes`, `viewScope`, or `execute` behavior changed at all.

## What Changed

### `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`

Six `builder.AddComputePass(...)` call sites replaced with
`builder.AddRenderPass(name, rg::PassKind::Compute, viewScope, category, setup,
execute)`, `setup`/`execute` lambdas left completely untouched:

1. `AddTransmittanceLutPass()` — `"AtmosphereTransmittanceLutPass"`,
   `rg::ViewScope::Shared` (previously implicit via the 3-arg overload, now
   explicit), `RenderPassCategory::AtmosphereLut`.
2. `AddMultiScatteringLutPass()` — `"AtmosphereMultiScatteringLutPass"`,
   `rg::ViewScope::Shared` (same as above), `RenderPassCategory::AtmosphereLut`.
3. `AddSkyViewLutPass()` — `"AtmosphereSkyViewLutPass"`, forwards its own
   `viewScope` parameter unchanged, `RenderPassCategory::AtmosphereLut`.
4. `AddAerialPerspectiveVolumePass()` — `"AtmosphereAerialPerspectiveVolumePass"`,
   forwards its own `viewScope` parameter, `RenderPassCategory::AtmosphereLut`.
5. `AddAerialPerspectiveVolumeDebugSlicePass()` —
   `"AtmosphereAerialPerspectiveVolumeDebugSlicePass"`, forwards its own
   `viewScope` parameter, `RenderPassCategory::AtmosphereLut` (per PHASE3's own
   explicit instruction: this is a real, always-running LUT-family dispatch,
   NOT `RenderPassCategory::Debug` — that category is reserved for
   Frame-Debugger-internal-only passes, PHASE5).
6. `AddAerialPerspectiveCompositePass()` —
   `"AtmosphereAerialPerspectiveCompositePass"`, forwards its own `viewScope`
   parameter, tagged `RenderPassCategory::General` (NOT a LUT — it consumes the
   already-computed LUTs/volume plus the RenderOpaque+DrawSkyBackground output
   to produce the final composited frame).

### `src/Renderer/Atmosphere/AtmosphereLutRenderer.h`

One present-tense doc comment on `AddSkyViewLutPass()` that literally named
"the underlying builder.AddComputePass() call" was updated to say
`AddRenderPass()` instead (with a short note on the rename), since it asserted
a fact about current behavior, not narrating history — same discipline PHASE1
already established for this exact kind of comment.

### `src/Application/AtmospherePassSequence.cpp`

No changes needed — confirmed (per Step 2 of the phase document, and reconfirmed
by this phase's own grep audit) that this file never calls
`builder.AddComputePass()` directly; it only forwards into
`AtmosphereLutRenderer`'s own methods, whose PUBLIC signatures are completely
unchanged by this phase.

## Verification

- `search_in_dir` for the literal substring `AddComputePass(` across
  `src/Renderer/Atmosphere/` now returns exactly **one** hit — a rename-history
  doc comment ("...was AddComputePass() before this migration") in
  `AtmosphereLutRenderer.h`, not a real call site. Zero actual
  `builder.AddComputePass(...)` calls remain in that directory.
- `search_in_dir` for the same substring across
  `src/Application/AtmospherePassSequence.cpp` returns **zero** hits, confirming
  the Step 2 assumption that this file never called it directly.
- Incremental compile: `cmake --build build --target gte_core` — clean build,
  zero errors/warnings.
- Incremental compile: `cmake --build build --target GreatTamanaEngine` — clean
  build.
- Incremental compile: `cmake --build build --target GreatTamanaEngineTests` —
  clean build.
- Live runtime smoke test (`run_app_background` + `gte_send_request`):
  - `GET /get_game_view` on a fresh scene (Camera + Directional Light only)
    showed the identical blue-sky-to-horizon gradient as PHASE2's own end
    state — no visual regression.
  - `GET /activate_tab?name=Render Graph` + `GET /get_swapchain` visually
    confirmed the Editor's "Render Graph" panel still lists every Atmosphere
    pass by its exact same name (`AtmosphereTransmittanceLutPass`,
    `AtmosphereMultiScatteringLutPass`, `AtmosphereSkyViewLutPass` (both
    `_GameView` and `_SceneView` view-scoped rows), `AtmosphereAerialPerspectiveVolumePass`
    (both views), `AtmosphereAerialPerspectiveVolumeDebugSlicePass`,
    `AtmosphereAerialPerspectiveCompositePass` (both views)), same Reads/Writes
    columns, same GPU timings/draw counts, in the same execution order as
    before this phase — proving the migration changed no observable behavior.
  - Stopped the app afterward (`stop_app_background`).

## Deviations From The Phase Document

None in substance. One trivial addition beyond the phase document's explicit
scope: fixed one present-tense doc comment in `AtmosphereLutRenderer.h` that
literally named the old `builder.AddComputePass()` call as a still-true fact
(it no longer is, directly) — a one-line accuracy fix in a file already being
read closely for this phase, mirroring PHASE1's own established precedent for
this exact class of touch-up. No other files were changed.

## Definition of Done — Checklist

- [x] All 6 Atmosphere pass methods call `builder.AddRenderPass(...)`
      internally instead of `builder.AddComputePass(...)`.
- [x] 5 of the 6 (every LUT pass, including the debug-slice pass) are tagged
      `RenderPassCategory::AtmosphereLut`; the Composite pass is tagged
      `RenderPassCategory::General`.
- [x] Zero change to any pass's `name`, `reads`, `writes`, `viewScope`, or
      `execute` behavior — confirmed via incremental build + live
      `/get_game_view` + Render Graph panel screenshot, pixel-identical to
      PHASE2's own end state.
- [x] `search_in_dir` audit confirms zero remaining direct `AddComputePass()`
      call sites under `src/Renderer/Atmosphere/` (one harmless rename-history
      doc comment aside).
- [x] Incremental compile succeeds; completion report + git commit follow.

## Handoff To PHASE4

Every pass this campaign cares about — `"RenderOpaque"`/`"DrawSkyBackground"`/
`"RenderTransparent"` (PHASE2) and all 6 Atmosphere passes (this phase) — is
now declared through `AddRenderPass()` and correctly tagged with
`RenderPassCategory`. PHASE4
(`PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md`) can now rewrite
`BuildRealFrameDebuggerSnapshot()` to build its tree generically from
`PassKind`/`RenderPassCategory`/execution order, grouping the 5
`AtmosphereLut`-tagged passes under a new `"Compute LUT"` heading, removing the
`"GameView"`-literal search and the `isSkyBackgroundDraw` hack entirely (the
latter flagged as an outstanding cleanup item by PHASE2's own handoff note).
