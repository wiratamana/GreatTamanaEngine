# PHASE1 — Aerial LUT Tunable Parameters: Single Source of Truth

Parent: `PHASE0_MASTER_STRATEGY.md`. Read that file first (especially "Locked
Design Decisions" 1, 2, 4, 5, and "Cross-phase file map"). This phase is a
**pure plumbing refactor** — by the end of it, the running engine must look
and behave EXACTLY as it does today (same 10km max distance, same 2.0 depth
exponent, same 8 samples/slice, same visual output, byte-for-byte). Nothing
in this phase is allowed to change a single rendered pixel. Its entire
purpose is to make Phase 2/3's actual behavior changes possible by fixing a
"three disconnected hardcoded copies of the same constant" problem first.

## Step 1 — The Goal (Where are we going?)

Replace the three currently-hardcoded, disconnected GLSL literals —
`kAerialMaxDistanceKm` (in BOTH `AtmosphereAerialPerspectiveVolume.comp` and
`AtmosphereAerialPerspectiveComposite.comp`, independently), `kAerialDepthExponent`
(same, both files), and `kAerialSamplesPerSlice` (only in the volume-generation
shader) — with ONE real, single-sourced, runtime-tunable data path:
`AtmosphereSettings` (C++, Editor-tunable) → `AtmosphereFrameUniforms`
(GPU, per-view storage buffer, already used by the volume-generation shader)
+ the composite pass's own push-constants (already used by the composite
shader) → both shaders read the SAME live values, no shader recompilation
needed to change them, and a live Editor slider can tune them at runtime.

Also add ONE new field now (even though Phase 3 is the one that actually
uses it non-trivially): `aerialPerspectiveScatteringExaggeration`, defaulted
to `1.0` (meaning "no change from the physically-accurate model" — Phase 1
does not wire this into any actual math yet beyond passing it through
unused, so the default of `1.0` combined with "not yet consumed" is
equivalent to "not present at all" for this phase's own zero-behavior-change
contract).

## Step 2 — The Situation (Where are we now?)

Current state (confirmed by direct source inspection this campaign's own
investigation phase already did):

- `AtmosphereAerialPerspectiveVolume.comp` (line ~70):
  ```glsl
  const float kAerialMaxDistanceKm = 10.0;
  const float kAerialDepthExponent = 2.0;
  const int kAerialSamplesPerSlice = 8;
  ```
  consumed at lines ~114-123 (`FroxelSliceToViewDepth()` calls, the
  `kAerialSamplesPerSlice`-driven sub-step loop).
- `AtmosphereAerialPerspectiveComposite.comp` (line ~54):
  ```glsl
  const float kAerialMaxDistanceKm = 10.0;
  const float kAerialDepthExponent = 2.0;
  ```
  consumed at lines ~80-91 (the sky-pixel fallback distance, and the
  `ViewDepthToFroxelSlice()` call).
- `AtmosphereFrameUniforms` (`AtmosphereTypes.h`, mirrored in
  `AtmosphereCommon.glsl`) currently ends at `mat4 invViewProjection` (its
  4th/last 16-byte-aligned group, for a total of 112 bytes — see the
  `static_assert(sizeof(AtmosphereFrameUniforms) == 112, ...)` in
  `AtmosphereTypes.h`). This buffer is already bound (binding 1) by
  `AtmosphereAerialPerspectiveVolume.comp` and already uploaded per-view by
  `AtmosphereLutRenderer::AddAerialPerspectiveVolumePass()` — the natural
  home for the volume-generation shader's own new tunables.
- `AtmosphereAerialPerspectiveComposite.comp`'s push-constant block
  (`AerialPerspectiveCompositePushConstants`, mirrored in
  `AtmosphereLutRenderer.cpp`) is:
  ```cpp
  struct AerialPerspectiveCompositePushConstants {
      float invViewProjection[16];
      float cameraWorldPositionAndScale[4];
      float aerialPerspectiveStrengthAndPad[4]; // x = strength, y/z/w = reserved padding.
  };
  ```
  The `.y`/`.z`/`.w` padding floats of `aerialPerspectiveStrengthAndPad` are
  ALREADY reserved and unused — the natural, zero-struct-growth home for the
  composite pass's own copy of `maxDistanceKm`/`depthExponent` (the composite
  pass does not need `samplesPerSlice` at all — it only inverts the slice
  boundary formula, it never sub-steps).
- `AtmosphereSettings` (`AtmosphereTypes.h`) currently has
  `groundAlbedoTint`, `aerialPerspectiveStrength`, `skyExposure`,
  `aerialPerspectiveDebugSliceIndex` — exactly the shape this phase's 4 new
  fields should join.
- `AtmospherePanel.cpp` already has the exact `ImGui::DragFloat("Aerial
  Perspective Strength", &settings.aerialPerspectiveStrength, 0.01f, 0.0f,
  2.0f);` pattern to copy for each new field.

## Step 3 — The Plan (Detailed Steps)

### 3.1 — `AtmosphereTypes.h`: extend `AtmosphereFrameUniforms` and `AtmosphereSettings`

Add a NEW 16-byte group to `AtmosphereFrameUniforms`, immediately after the
existing `mat4 invViewProjection` field:

```cpp
// Group 8 (16 bytes, atmosphere-scattering-2 campaign Phase 1) - the
// Aerial Perspective froxel volume's own tunable ray-march parameters,
// now sourced from AtmosphereSettings (Editor "Atmosphere" panel) instead
// of hardcoded shader constants - see AtmosphereAerialPerspectiveVolume.comp.
// Deliberately stored here (per-VIEW frame uniforms), not in
// AtmosphereParametersGpu (session-stable physical constants), since a
// future per-view override is plausible even though today both Game/Scene
// views always receive the SAME AtmosphereSettings-sourced values.
float aerialPerspectiveMaxDistanceKm = 10.0f;
float aerialPerspectiveDepthExponent = 2.0f;
float aerialPerspectiveSamplesPerSliceAsFloat = 8.0f; // Stored as float (GLSL storage-buffer int alignment is fiddly); shader rounds+clamps to [1, 8].
float aerialPerspectiveScatteringExaggeration = 1.0f; // Unused until Phase 3 - see that phase's own file.
```

Update the trailing `static_assert` to `sizeof(AtmosphereFrameUniforms) ==
128` (112 + 16), with an updated comment ("four 16-byte groups plus one
64-byte mat4 group plus one new 16-byte group = 48 + 64 + 16 = 128 bytes").

Add 4 new fields to `AtmosphereSettings` (after
`aerialPerspectiveDebugSliceIndex`):

```cpp
// atmosphere-scattering-2 campaign, Phase 1/3 - Aerial Perspective froxel
// volume ray-march tunables, mirrored into every view's own
// AtmosphereFrameUniforms (Application.cpp) each frame. Defaults here are
// Phase 1's OWN "zero behavior change" values (identical to the pre-Phase-1
// hardcoded shader constants) - Phase 3 changes these DEFAULTS, not this
// phase.
float aerialPerspectiveMaxDistanceKm = 10.0f;
float aerialPerspectiveDepthExponent = 2.0f;
int aerialPerspectiveSamplesPerSlice = 8;
float aerialPerspectiveScatteringExaggeration = 1.0f;
```

### 3.2 — `AtmosphereCommon.glsl`: mirror the new `AtmosphereFrameUniforms` group

Append the identical 4 floats to the GLSL `AtmosphereFrameUniforms` struct
mirror (immediately after `mat4 invViewProjection;`), with a comment
pointing back at `AtmosphereTypes.h`'s own doc comment (same "any change here
must be mirrored by hand" rule this file already documents at its own top).

### 3.3 — `AtmosphereAerialPerspectiveVolume.comp`: read from `frameUniforms`, not local constants

Delete the three `const` declarations at the top (`kAerialMaxDistanceKm`,
`kAerialDepthExponent`, `kAerialSamplesPerSlice`). Replace every use with:

```glsl
float maxDistanceKm = frameUniforms.aerialPerspectiveMaxDistanceKm;
float depthExponent = frameUniforms.aerialPerspectiveDepthExponent;
int samplesPerSlice = clamp(int(frameUniforms.aerialPerspectiveSamplesPerSliceAsFloat + 0.5), 1, 8);
```
placed once near the top of `main()`, right after `viewDirection` is
computed (before the per-slice loop), then used in place of the deleted
constants at every call site (`FroxelSliceToViewDepth()` x2 per slice, and
the inner `for (int sampleIndex = 0; sampleIndex < samplesPerSlice; ...)`
loop bound, replacing the old `kAerialSamplesPerSlice`). The `clamp(..., 1,
8)` mirrors `pl-sky`'s own `PL_AERIAL_MAX_SAMPLES_PER_SLICE` clamp
(documented in `pl-sky_aerial_perspective.md` §4.2) — this campaign's Editor
slider (3.6 below) will also clamp its own range to `[1, 8]` so the two
clamps agree, but the shader-side clamp is the one that is actually load-
bearing (defends against a stale/未-clamped value ever reaching the GPU).

Update this file's own header comment: the sentence claiming these are
"fixed, hardcoded, never a runtime-tunable Editor knob (this phase's own
'What We Will NOT Do')" is now STALE — replace it with a note that these
became live Editor-tunable values as of `atmosphere-scattering-2` Phase 1,
sourced from `AtmosphereSettings` via `AtmosphereFrameUniforms`.

### 3.4 — `AtmosphereAerialPerspectiveComposite.comp`: read from push constants, not local constants

Delete `const float kAerialMaxDistanceKm = 10.0;` and `const float
kAerialDepthExponent = 2.0;`. Rename the push-constant field (purely a
readability improvement, not required, but keeps the field's real meaning
honest) from `aerialPerspectiveStrengthAndPad` to
`aerialPerspectiveStrengthDistanceExponentAndPad` — OR keep the existing
name and just repurpose `.y`/`.z` (simpler, avoids touching the C++ struct's
field NAME, only its USAGE — prefer this simpler option to minimize diff
noise). Read:

```glsl
float strength = pc.aerialPerspectiveStrengthAndPad.x;
float maxDistanceKm = pc.aerialPerspectiveStrengthAndPad.y;
float depthExponent = pc.aerialPerspectiveStrengthAndPad.z;
```
and use `maxDistanceKm`/`depthExponent` at both existing call sites (the
sky-pixel fallback `viewDistanceKm = maxDistanceKm;` and the
`ViewDepthToFroxelSlice(viewDistanceKm, float(volumeSize.z), maxDistanceKm,
depthExponent)` call). Update the field's own doc comment in the
push-constant block to describe the new `.y`/`.z` meaning (no longer
"reserved padding" for those two slots; `.w` remains reserved).

### 3.5 — `AtmosphereLutRenderer.h`/`.cpp`: thread the two composite floats through

`AddAerialPerspectiveCompositePass()`'s signature gains two new `float`
parameters, `maxDistanceKm` and `depthExponent`, inserted right after the
existing `aerialPerspectiveStrength` parameter (both declaration in the
header and definition in the .cpp). Inside the function, populate:

```cpp
pushConstants.aerialPerspectiveStrengthAndPad[1] = maxDistanceKm;
pushConstants.aerialPerspectiveStrengthAndPad[2] = depthExponent;
```
(replacing the current `= 0.0f;` placeholders for indices 1/2 — index 3
stays `0.0f`). Update this method's own header doc comment in
`AtmosphereLutRenderer.h` to describe the two new parameters.

### 3.6 — `AtmospherePassSequence.h`/`.cpp`: thread through the Application layer

`AddAtmosphereCompositePass()`'s signature gains the same two new `float`
parameters (`maxDistanceKm`, `depthExponent`), forwarded straight through to
`AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()` unchanged.
Update the header doc comment.

**VERIFIED against the real, current source (this is no longer an open
choice — read `AtmospherePassSequence.cpp`'s actual body before starting this
phase to confirm it still matches, but as of this double-check pass it does):**
`AtmosphereViewLutHandles::frameUniforms` is constructed INSIDE
`AddAtmosphereViewLutPasses()` itself —

```cpp
AtmosphereViewLutHandles result;
result.frameUniforms = ResolveAtmosphereFrameUniforms(registry, eyeWorldPosition);
result.frameUniforms.invViewProjection = viewProjection.Inverse();

result.skyViewLutHandle = atmosphereLutRenderer.AddSkyViewLutPass(builder, renderer, atmosphereParameters,
    result.frameUniforms, sharedLuts.transmittanceLutHandle, sharedLuts.multiScatteringLutHandle, skyViewLutName);

result.aerialPerspectiveVolumeHandle = atmosphereLutRenderer.AddAerialPerspectiveVolumePass(builder, renderer,
    atmosphereParameters, result.frameUniforms, sharedLuts.transmittanceLutHandle,
    sharedLuts.multiScatteringLutHandle, aerialPerspectiveVolumeName);

return result;
```

`AddAtmosphereViewLutPasses()` returns `AtmosphereViewLutHandles` BY VALUE —
`Application.cpp`'s call site never holds an `AtmosphereFrameUniforms` value
of its own BEFORE calling this function, only AFTER it returns. This makes
Phase 0's originally-sketched "option (b)" (the caller sets the 4 new fields
on the value before passing it into `AddAtmosphereViewLutPasses()`) literally
impossible as described — there is nothing for the caller to set fields on
until the call has already returned, by which point `AddSkyViewLutPass()`/
`AddAerialPerspectiveVolumePass()` (the two functions that actually consume
`frameUniforms` on the GPU side) have already run using the UN-set default
values. **Use option (a): add a new parameter to `AddAtmosphereViewLutPasses()`
itself** so the 4 new values can be set on `result.frameUniforms` from INSIDE
the function, after `ResolveAtmosphereFrameUniforms()` returns and BEFORE
`AddSkyViewLutPass()`/`AddAerialPerspectiveVolumePass()` are called:

```cpp
// AtmospherePassSequence.h - new parameter, inserted right after atmosphereParameters
// (AtmosphereSettings is already a complete type here via the existing
// "../Renderer/Atmosphere/AtmosphereLutRenderer.h" include, which itself
// includes AtmosphereTypes.h - no new #include needed).
AtmosphereViewLutHandles AddAtmosphereViewLutPasses(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, Registry& registry,
    const AtmosphereParametersGpu& atmosphereParameters, const AtmosphereSettings& atmosphereSettings,
    const AtmosphereSharedLutHandles& sharedLuts, Vec3 eyeWorldPosition, const Mat4& viewProjection,
    const char* skyViewLutName, const char* aerialPerspectiveVolumeName);
```

```cpp
// AtmospherePassSequence.cpp - inserted between the two existing lines that
// already set result.frameUniforms.invViewProjection and the AddSkyViewLutPass() call:
result.frameUniforms = ResolveAtmosphereFrameUniforms(registry, eyeWorldPosition);
result.frameUniforms.invViewProjection = viewProjection.Inverse();
result.frameUniforms.aerialPerspectiveMaxDistanceKm = atmosphereSettings.aerialPerspectiveMaxDistanceKm;
result.frameUniforms.aerialPerspectiveDepthExponent = atmosphereSettings.aerialPerspectiveDepthExponent;
result.frameUniforms.aerialPerspectiveSamplesPerSliceAsFloat =
    static_cast<float>(atmosphereSettings.aerialPerspectiveSamplesPerSlice);
result.frameUniforms.aerialPerspectiveScatteringExaggeration = atmosphereSettings.aerialPerspectiveScatteringExaggeration;

result.skyViewLutHandle = atmosphereLutRenderer.AddSkyViewLutPass(/* unchanged */);
```

`ResolveAtmosphereFrameUniforms()` itself (`AtmosphereLutRenderer.h`/`.cpp`)
stays COMPLETELY UNCHANGED — its own signature/callers/doc comment are not
touched by this phase at all; only `AddAtmosphereViewLutPasses()`'s own body
(one function, already the sole caller of `ResolveAtmosphereFrameUniforms()`)
gains the extra assignment. Update `AddAtmosphereViewLutPasses()`'s own header
doc comment (`AtmospherePassSequence.h`) to describe the new
`atmosphereSettings` parameter and note it is consumed only by the Aerial
Perspective volume's own tunables (the Sky-View LUT pass ignores these 4
fields entirely).

### 3.7 — `Application.cpp`: wire `m_atmosphereSettings` into both call sites

At BOTH the Game View and Scene View call sites (currently threading
`m_atmosphereSettings.aerialPerspectiveStrength` into
`AddAtmosphereCompositePass()`, confirmed still at lines ~472/527; the two
`AddAtmosphereViewLutPasses()` call sites themselves are at lines ~411/493):

1. Pass `m_atmosphereSettings` as the new argument to BOTH
   `AddAtmosphereViewLutPasses()` call sites (inserted right after
   `atmosphereParameters`, matching 3.6's new parameter position exactly) —
   e.g. `AddAtmosphereViewLutPasses(b, m_renderer, m_atmosphereLutRenderer,
   m_game.GetRegistry(), atmosphereParameters, m_atmosphereSettings,
   atmosphereSharedLuts, gameEyeWorldPosition, gameViewProjection, ...)`.
   Nothing else about these two call sites changes — `gameAtmosphere`/
   `sceneAtmosphere` are used exactly as before afterward (`.skyViewLutHandle`,
   `.aerialPerspectiveVolumeHandle`, `.frameUniforms`).
2. Pass `m_atmosphereSettings.aerialPerspectiveMaxDistanceKm`/
   `aerialPerspectiveDepthExponent` as the two new arguments to
   `AddAtmosphereCompositePass()` (both call sites), per 3.6's own
   `AddAtmosphereCompositePass()` signature change above.

No separate "set fields on a returned frameUniforms value" step is needed at
this call site at all — 3.6's fix moves that responsibility entirely inside
`AddAtmosphereViewLutPasses()`, so `Application.cpp` only ever passes
`m_atmosphereSettings` in and reads `gameAtmosphere.frameUniforms`/
`sceneAtmosphere.frameUniforms` back out, unchanged from today's shape.

### 3.8 — `AtmospherePanel.cpp`: 4 new Editor sliders

Immediately after the existing `ImGui::DragFloat("Aerial Perspective
Strength", ...)` line, add:

```cpp
ImGui::DragFloat("Aerial Max Distance (km)", &settings.aerialPerspectiveMaxDistanceKm, 0.01f, 0.01f, 50.0f);
ImGui::DragFloat("Aerial Depth Exponent", &settings.aerialPerspectiveDepthExponent, 0.05f, 1.0f, 4.0f);
ImGui::SliderInt("Aerial Samples Per Slice", &settings.aerialPerspectiveSamplesPerSlice, 1, 8);
ImGui::DragFloat("Aerial Scattering Exaggeration", &settings.aerialPerspectiveScatteringExaggeration, 0.1f, 0.1f, 50.0f);
```
with a one-line `ImGui::TextDisabled(...)` note (mirroring the existing
"Mirrors one Z-slice..." disabled-text convention already used for the debug
slice slider) explaining these are unused-by-default-value-wise but now live
(the "Exaggeration" slider's real effect only begins in Phase 3 — a
Phase-1-only reader of this file should not be surprised that turning it up
does nothing yet; add a one-line comment saying so, to be removed once
Phase 3 lands and makes it real).

## Verification

- Fast, targeted compile check: `cmake --build build --target gte_core` (the
  atmosphere renderer/types live in `gte_core`), then a second targeted
  build of `GreatTamanaEngine` specifically to force `glslc` to recompile
  the two touched `.comp` shaders (shader compilation is driven by the app
  target's own custom build step, not `gte_core` — mirrors
  `network-impl-6` Phase 3's own precedent for why a full app build was
  needed to "genuinely exercise `glslc`").
- No full build/full regression test yet (Master Strategy rule 1).
- A manual runtime smoke check via `run_app_background` +
  `gte_send_request` against `GET /get_game_view` (or `/get_swapchain`) is
  worthwhile here specifically BECAUSE this phase claims "zero visual
  change" — capture one screenshot before and after this phase's edits (or
  simply confirm the new sliders show `10.0`/`2.0`/`8`/`1.0` on first
  Editor launch and that dragging "Aerial Perspective Strength" still
  behaves exactly as before) to prove the refactor really is behavior-
  preserving. Don't forget `stop_app_background` afterward.
- Write `PHASE1_COMPLETION_REPORT.md` in this same folder, then commit.
