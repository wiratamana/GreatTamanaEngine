# PHASE1_COMPLETION_REPORT — Aerial LUT Tunable Parameters: Single Source of Truth

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE1_AERIAL_LUT_TUNABLE_PARAMETERS_SINGLE_SOURCE_OF_TRUTH.md` exactly, as
revised (sections 3.6/3.7 carrying the VERIFIED "add a new parameter to
`AddAtmosphereViewLutPasses()`" fix rather than a hedged choice). This is a
**pure plumbing refactor** — the running engine looks and behaves exactly as
it did before this phase.

## What was done

Every step from the phase file's own "Step 3 — The Plan" was implemented
literally, in order:

1. **`src/Renderer/Atmosphere/AtmosphereTypes.h`**
   - Added a new 16-byte "Group 8" to `AtmosphereFrameUniforms`, immediately
     after `invViewProjection`: `aerialPerspectiveMaxDistanceKm` (10.0f),
     `aerialPerspectiveDepthExponent` (2.0f),
     `aerialPerspectiveSamplesPerSliceAsFloat` (8.0f),
     `aerialPerspectiveScatteringExaggeration` (1.0f, unused until Phase 3).
   - Updated the trailing `static_assert` from 112 to 128 bytes, with an
     updated comment explaining the new 48 + 64 + 16 = 128 byte total.
   - Added the same 4 fields (with the plain, non-storage-buffer-constrained
     `int aerialPerspectiveSamplesPerSlice` this time, since this struct isn't
     GPU-uploaded directly) to `AtmosphereSettings`, defaulted to the exact
     pre-existing hardcoded values (10.0f / 2.0f / 8 / 1.0f).

2. **`src/Shaders/AtmosphereCommon.glsl`**
   - Mirrored the identical 4-float group onto the GLSL
     `AtmosphereFrameUniforms` struct, immediately after `mat4
     invViewProjection;`, with a doc comment pointing back at the C++ struct.

3. **`src/Shaders/AtmosphereAerialPerspectiveVolume.comp`**
   - Deleted the three `const` declarations (`kAerialMaxDistanceKm`,
     `kAerialDepthExponent`, `kAerialSamplesPerSlice`).
   - Added `maxDistanceKm`/`depthExponent`/`samplesPerSlice` local reads from
     `frameUniforms` at the top of `main()`, with the exact
     `clamp(int(... + 0.5), 1, 8)` rounding/clamping the plan specified.
   - Replaced every call site (`FroxelSliceToViewDepth()` x2 per slice, the
     inner sample-loop bound/`sampleU`/`segmentLengthKm`) to use the new local
     variables instead of the deleted constants.
   - Updated the header comment above the (now-deleted) constants block to
     describe the new live-tunable data path instead of the stale "fixed,
     hardcoded, never a runtime-tunable Editor knob" claim.

4. **`src/Shaders/AtmosphereAerialPerspectiveComposite.comp`**
   - Deleted `kAerialMaxDistanceKm`/`kAerialDepthExponent`.
   - Kept the push-constant field's existing name
     (`aerialPerspectiveStrengthAndPad`) per the plan's own "simpler, avoids
     touching the C++ struct's field NAME" preference, and repurposed `.y`/`.z`
     (`maxDistanceKm`/`depthExponent`) — `.w` remains reserved padding.
     Updated the push-constant block's own doc comment to describe the new
     meaning.
   - Added local `maxDistanceKm`/`depthExponent` reads from
     `pc.aerialPerspectiveStrengthAndPad.y`/`.z` at the top of `main()`, and
     used them at both existing call sites (the sky-pixel fallback
     `viewDistanceKm = maxDistanceKm;` and the `ViewDepthToFroxelSlice(...)`
     call).

5. **`src/Renderer/Atmosphere/AtmosphereLutRenderer.h`/`.cpp`**
   - `AddAerialPerspectiveCompositePass()` gained two new `float` parameters,
     `maxDistanceKm`/`depthExponent`, inserted right after
     `aerialPerspectiveStrength` (both the header declaration and the .cpp
     definition), with the header doc comment updated to describe them.
   - Inside the function, `pushConstants.aerialPerspectiveStrengthAndPad[1]`/
     `[2]` are now populated from the new parameters (replacing the previous
     `= 0.0f;` placeholders) — `[3]` stays `0.0f`.
   - `AerialPerspectiveCompositePushConstants`'s own struct-level doc comment
     was updated to describe the `.y`/`.z` repurposing.

6. **`src/Application/AtmospherePassSequence.h`/`.cpp`**
   - `AddAtmosphereViewLutPasses()` gained a new `const AtmosphereSettings&
     atmosphereSettings` parameter, inserted right after
     `atmosphereParameters` (exactly the position the revised strategy file
     specifies). Its body now sets the 4 new fields on
     `result.frameUniforms` immediately after `ResolveAtmosphereFrameUniforms()`
     returns and `invViewProjection` is set, and strictly BEFORE
     `AddSkyViewLutPass()`/`AddAerialPerspectiveVolumePass()` are called — so
     both of those GPU-facing calls already see the live, Editor-tunable
     values this same call. `ResolveAtmosphereFrameUniforms()` itself was left
     completely untouched, exactly as the plan required.
   - `AddAtmosphereCompositePass()` gained the same two new `float`
     parameters (`maxDistanceKm`, `depthExponent`), forwarded straight
     through, unchanged, to
     `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`.
   - Both header doc comments were updated to describe the new parameters.

7. **`src/Application/Application.cpp`**
   - Both `AddAtmosphereViewLutPasses()` call sites (Game View and Scene View)
     now pass `m_atmosphereSettings` as the new argument, in the position the
     plan specifies.
   - Both `AddAtmosphereCompositePass()` call sites now pass
     `m_atmosphereSettings.aerialPerspectiveMaxDistanceKm`/
     `aerialPerspectiveDepthExponent` as the two new trailing-before-`extent`
     arguments.
   - No other change was needed at these call sites — `gameAtmosphere`/
     `sceneAtmosphere` are read exactly as before afterward.

8. **`src/Editor/Panels/AtmospherePanel.cpp`**
   - Added the 4 new sliders immediately after the existing "Aerial
     Perspective Strength" `DragFloat`, using the exact ranges/steps the plan
     specified (`Aerial Max Distance (km)`: 0.01 step, [0.01, 50.0]; `Aerial
     Depth Exponent`: 0.05 step, [1.0, 4.0]; `Aerial Samples Per Slice`:
     `SliderInt` [1, 8]; `Aerial Scattering Exaggeration`: 0.1 step,
     [0.1, 50.0]), plus a `TextDisabled()` note explaining the Scattering
     Exaggeration slider has no effect yet until Phase 3 lands.

## Deviations from the written plan

None of substance. Two purely cosmetic notes:

- Section 3.4 offered a choice between renaming the push-constant field or
  keeping its existing name and just repurposing `.y`/`.z`, explicitly
  recommending the simpler option "to minimize diff noise" — this was
  followed literally (the field is still named
  `aerialPerspectiveStrengthAndPad`, only its usage/doc comment changed).
- A couple of doc-comment wording choices (e.g. exactly how the "Group 8"
  static_assert comment is phrased) are this session's own wording, not a
  verbatim copy of the strategy file's own snippet — the numeric content
  (128 bytes, 48 + 64 + 16) matches exactly.

No behavioral deviation of any kind. The `AddAtmosphereViewLutPasses()`
signature change followed the strategy file's own VERIFIED "option (a)" fix
literally — a new `atmosphereSettings` parameter, consumed entirely inside
the function, with `ResolveAtmosphereFrameUniforms()` itself untouched.

## Verification performed

1. **`cmake --build build --target gte_core`** — succeeded, zero errors
   (26/26 objects built, `libgte_core.a` linked). The only console output
   besides the build steps was a pre-existing, unrelated KTX-Software
   `git describe` warning ("No names found, cannot describe anything" —
   falls back to a hardcoded version string; present before this phase's
   changes too, not something this phase introduced or touched).
2. **`cmake --build build --target GreatTamanaEngine`** — succeeded; glslc
   recompiled both touched `.comp` shaders
   (`AtmosphereAerialPerspectiveVolume.comp.spv`,
   `AtmosphereAerialPerspectiveComposite.comp.spv`) plus the other two
   atmosphere LUT shaders that `#include "AtmosphereCommon.glsl"` and are
   therefore correctly tracked as dependents
   (`AtmosphereTransmittanceLut.comp`, `AtmosphereMultiScatteringLut.comp`,
   `AtmosphereSkyViewLut.comp`, `AtmosphereSkyBackground.frag`) — zero shader
   compile errors, full `.exe` link succeeded.
3. **Runtime smoke test** via `run_app_background` +
   `gte_send_request`:
   - Launched `GreatTamanaEngine.exe`, waited for the first frame, and
     captured `GET /get_swapchain` — the Editor's "Scene" view renders the
     expected physically-plausible dusk sky gradient (blue zenith fading to a
     warm horizon glow) with no visible artifact, crash, or black-screen
     regression. (`GET /get_game_view` correctly 409'd since "Game" was the
     currently-inactive/hidden dock tab this session — expected engine
     behavior per `AGENTS.md`'s "Editor Module Structure" visibility-driven
     rendering rule, not a bug.)
   - `GET /list_textures` confirmed both `AtmosphereSkyViewLut_GameView`/
     `_SceneView` and both `GameViewComposited`/`SceneViewComposited` textures
     are present, live, and updating with sane extents — proving the
     `AddAtmosphereViewLutPasses()`/`AddAtmosphereCompositePass()` signature
     changes threaded through both the Game View and Scene View call sites
     without breaking either pass sequence.
   - Stopped the app via `stop_app_background` afterward.
4. **New sliders default to the exact pre-existing hardcoded values,
   confirming zero visual behavior change**: `AtmosphereSettings`'s own
   default member initializers are `aerialPerspectiveMaxDistanceKm = 10.0f`,
   `aerialPerspectiveDepthExponent = 2.0f`,
   `aerialPerspectiveSamplesPerSlice = 8`,
   `aerialPerspectiveScatteringExaggeration = 1.0f` — byte-for-byte identical
   to the three constants (`kAerialMaxDistanceKm = 10.0`,
   `kAerialDepthExponent = 2.0`, `kAerialSamplesPerSlice = 8`) this phase
   deleted from both `.comp` files. Since `AtmosphereSettings` has no
   persistence/serialization (per `AGENTS.md`'s existing rule), every fresh
   session starts with these exact defaults, and
   `AddAtmosphereViewLutPasses()` copies them onto `frameUniforms` before any
   GPU pass reads them — so the very first frame a fresh session renders
   already exercises the new plumbing with numerically identical inputs to
   the old hardcoded constants. Combined with the screenshot showing a normal
   sky render and no artifacts, this confirms the refactor is genuinely
   behavior-preserving.

No full build/full regression test was run (per the Master Strategy's own
"Workflow rules" rule 1 — deferred to Phase 6).

## Next phase

Phase 2 (`PHASE2_AERIAL_COMPOSITE_PRECISION_FIXES.md`) can now safely change
the composite pass's own precision behavior (half-texel Z-bias, first-slice
fade-in), and Phase 3 can safely change the actual shipped default numeric
values in exactly one place (`AtmosphereSettings`'s own default member
initializers) — both `AtmosphereFrameUniforms`/push-constants readers are
already wired and confirmed working.
