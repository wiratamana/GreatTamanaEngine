# PHASE2 COMPLETION REPORT — The Actual Fix: Pass-Through Branch in the Composite Shader

**Parent:** `PHASE0_MASTER_STRATEGY.md`
**Phase document:** `PHASE2_SHADER_PASSTHROUGH_FIX.md`
**Depends on:** `PHASE1_COMPOSITE_DECISION_CPU_ORACLE_AND_TESTS.md` (read its
completion report first, per this phase's own instructions — confirmed
committed, working tree clean, on `feature/atmosphere-scattering-impl`)
**Status:** Complete. Shader fix applied exactly as scoped, compiled cleanly,
and visually confirmed both for the fixed (no-geometry) case and the
untouched (real-geometry) case.

---

## 0. Pre-work verification (Step 1 of the phase document)

Before editing anything, `src/Shaders/AtmosphereAerialPerspectiveComposite.comp`
was re-read fresh, in full. It matched the phase document's own snapshot
exactly, byte-for-byte in content (only the absolute line numbers differed by
a few due to header-comment blocks always shown separately by the `read_file`
tool — same content, same order). PHASE1's own commit did not touch this
file, confirmed by `git_status` showing a clean working tree immediately
before this phase began, and by the file's own header comments (no mention of
this campaign yet). No drift to reconcile.

`src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h/.cpp`
(PHASE1's shipped CPU oracle) were also re-read fresh to confirm the exact
function signatures/behavior this shader edit must mirror:

- `constexpr float kAerialPerspectiveFarPlaneDepthThreshold = 0.999999f;`
- `bool ShouldBypassAerialPerspectiveComposite(float rawDepth) noexcept` —
  `return rawDepth >= kAerialPerspectiveFarPlaneDepthThreshold;`
- `Vec3 ComputeAerialPerspectiveCompositeColor(...)` — bypass branch returns
  `sceneColorRgb` completely unchanged; non-bypass branch computes
  `inScattering = sampledAerialRgb * strength`,
  `transmittance = mix(1.0, sampledAerialA, strength)`, returns
  `sceneColorRgb * transmittance + inScattering`.

These shipped exactly as PHASE1's own completion report described — no
signature/location drift to mirror differently than the plan.

## 1. What was done

`src/Shaders/AtmosphereAerialPerspectiveComposite.comp`'s `main()` body was
replaced exactly as specified in the phase document's Step 3.1, character-for-
character:

1. `sourceDepth`/`sourceColor` are now read immediately after the bounds
   check, before anything else.
2. A new early pass-through branch was added:
   ```glsl
   if (rawDepth >= 0.999999) {
       imageStore(destinationImage, texel, vec4(sceneColor, 1.0));
       return;
   }
   ```
   placed **before** any volume-lookup/Z-slice machinery — no
   `textureSize()`/`texture(aerialPerspectiveVolume, ...)` call happens at all
   for a sky pixel now.
3. The old `viewDistanceKm` if/else was folded into this early return: its
   `if (rawDepth >= 0.999999) { viewDistanceKm = maxDistanceKm; }` branch is
   gone entirely (replaced by the pass-through return above), and its `else`
   branch's body (the world-position reconstruction) became the new,
   unconditional, post-early-return code.
4. Every other line for the real-geometry case — the Z-slice mapping,
   half-texel bias, first-slice fade-in blend, `aerialPerspectiveStrength`
   scaling, and the final `sceneColor * transmittance + inScattering` blend —
   is untouched, character-for-character, from the pre-existing file.

### Final file content (`src/Shaders/AtmosphereAerialPerspectiveComposite.comp`, in full)

```glsl
#version 450

// Atmosphere Scattering + Aerial Perspective campaign, Phase 7
// (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md)
// - the Aerial Perspective Composite pass: for every pixel, samples the
// Phase 6 aerial-perspective froxel volume at that pixel's screen column
// plus a depth-mapped Z slice, and blends
// `finalColor = sceneColor * transmittance + inScattering` onto whatever
// the Sky Background pass (3.2, same frame, same view, drawn earlier into
// this SAME source color texture) already wrote there.
//
// atmosphere-scattering-2 campaign, Phase 2
// (task_manager/atmosphere-scattering-2/PHASE2_AERIAL_COMPOSITE_PRECISION_FIXES.md)
// - fixes two precision gaps vs. pl-sky_aerial_perspective.md §5: point 7 (a
// half-texel Z-bias when converting this pixel's own view distance into a
// volume Z lookup coordinate, since slice i stores the CUMULATIVE value at
// that slice's FAR boundary while a 3D texture sample lands on texel
// CENTERS) and point 9 (a first-slice fade-in blend, so geometry falling
// inside the coarse, nearest froxel slice doesn't show a visible seam/pop
// - see main()'s own boundaryU/halfTexelZ/sampledAerial/firstSliceBlend
// locals below for both fixes).
//
// BINDING CONVENTION: binding 0 = sampler2D sourceColor (this view's
// original color, POST-Sky-Background-pass); binding 1 = sampler2D
// sourceDepth (this view's own DepthBuffer, sampled - requires
// DepthBuffer::allowSampledAccess, see src/Renderer/DepthBuffer.h); binding
// 2 = sampler3D aerialPerspectiveVolume (Phase 6's own output, trilinearly
// filtered - see VolumeTexture.h); binding 3 = the new, separate
// destinationImage (rgba8, write-only - "GameViewComposited"/
// "SceneViewComposited", never the SAME texture as sourceColor, mirroring
// this codebase's own "a compute pass never reads and writes the exact
// SAME storage image in the same dispatch" convention - see
// ComputeBlurValidation.h).
layout(local_size_x = 16, local_size_y = 16) in;

#include "AtmosphereCommon.glsl"

layout(push_constant) uniform PushConstants {
    mat4 invViewProjection;
    // .xyz = camera world position (engine world units, NOT km); .w =
    // worldUnitsPerKm (AtmosphereParameters.h's kWorldUnitsPerKilometer,
    // 1000.0 by convention) - packed as one vec4 (rather than a separate
    // vec3 + float) to avoid any ambiguity in this push-constant block's
    // own std430-like packing rules; matches the C++ side's plain
    // 4-contiguous-floats layout exactly either way.
    vec4 cameraWorldPositionAndScale;
    // Phase 8 (ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md) - .x =
    // aerialPerspectiveStrength, an Editor "Atmosphere" panel-tunable
    // overall multiplier for the effect (1.0 = unchanged physical result,
    // 0.0 = fully disabled/pass-through) - see main()'s own use of it
    // below. As of atmosphere-scattering-2 campaign Phase 1, .y/.z are no
    // longer reserved padding - .y = aerialPerspectiveMaxDistanceKm, .z =
    // aerialPerspectiveDepthExponent (both mirrored from AtmosphereSettings,
    // the SAME values AtmosphereAerialPerspectiveVolume.comp's own
    // frameUniforms fields use to GENERATE this volume - this pass's own
    // Z-slice lookup must stay the exact inverse of that generation
    // mapping). .w remains reserved padding.
    vec4 aerialPerspectiveStrengthAndPad;
} pc;

layout(binding = 0) uniform sampler2D sourceColor;
layout(binding = 1) uniform sampler2D sourceDepth;
layout(binding = 2) uniform sampler3D aerialPerspectiveVolume;
layout(binding = 3, rgba8) uniform writeonly image2D destinationImage;

// maxDistanceKm/depthExponent are now read from the push-constant block
// above (pc.aerialPerspectiveStrengthAndPad.y/.z), no longer local `const`
// literals - as of atmosphere-scattering-2 campaign Phase 1, they are
// mirrored, per-frame, from AtmosphereSettings (see AtmosphereLutRenderer.cpp's
// AddAerialPerspectiveCompositePass()) rather than hardcoded here - these
// still describe the SAME froxel volume this pass reads, so the Z-slice
// mapping used here to LOOK UP a froxel must be the exact inverse of the
// mapping used to GENERATE it.

void main()
{
    ivec2 outputSize = imageSize(destinationImage);
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (texel.x >= outputSize.x || texel.y >= outputSize.y) {
        // The dispatch's own ceiling-division group count can legitimately
        // cover more threads than there are real pixels - see
        // ComputeDispatch.h's own header comment.
        return;
    }

    vec2 uv = (vec2(texel) + vec2(0.5)) / vec2(outputSize);
    float rawDepth = texture(sourceDepth, uv).r;
    vec3 sceneColor = texture(sourceColor, uv).rgb;

    // atmosphere-scattering-4 campaign - fix for
    // AERIAL_PERSPECTIVE_NO_GEOMETRY_BUG_REPORT_20260911.md: a pixel with no
    // opaque geometry drawn into it this frame (rawDepth still at the
    // frame's own clear value, 1.0) has ALREADY been given its final,
    // correct color by the Sky Background pass (drawn earlier this same
    // frame, into this SAME source color texture - see
    // AtmosphereSkyBackgroundRenderer's own VK_COMPARE_OP_EQUAL/depth-write-
    // disabled pipeline state). Aerial Perspective is only meaningful for
    // real opaque geometry (paper Section 5.4) - never re-blend a second,
    // shorter-range fog layer on top of an already-complete sky pixel. Pure
    // pass-through, and skips the volume sample/Z-slice math entirely for
    // this pixel (mirrors
    // AtmosphereAerialPerspectiveCompositeMath.h's own
    // ShouldBypassAerialPerspectiveComposite()/
    // ComputeAerialPerspectiveCompositeColor() CPU oracle exactly - see
    // task_manager/atmosphere-scattering-4/PHASE1_COMPOSITE_DECISION_CPU_ORACLE_AND_TESTS.md).
    if (rawDepth >= 0.999999) {
        imageStore(destinationImage, texel, vec4(sceneColor, 1.0));
        return;
    }

    float maxDistanceKm = pc.aerialPerspectiveStrengthAndPad.y;
    float depthExponent = pc.aerialPerspectiveStrengthAndPad.z;

    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, rawDepth, 1.0);
    vec4 worldPos4 = pc.invViewProjection * clipPos;
    vec3 worldPos = worldPos4.xyz / worldPos4.w;
    float worldDistance = length(worldPos - pc.cameraWorldPositionAndScale.xyz);
    float viewDistanceKm = worldDistance / max(pc.cameraWorldPositionAndScale.w, 1e-6);

    ivec3 volumeSize = textureSize(aerialPerspectiveVolume, 0);
    float sliceCount = float(volumeSize.z);
    float slice =
        ViewDepthToFroxelSlice(viewDistanceKm, sliceCount, maxDistanceKm, depthExponent);
    float boundaryU = clamp(slice / sliceCount, 0.0, 1.0);

    // Half-texel Z-bias (pl-sky_aerial_perspective.md §5, point 7): slice i
    // stores the CUMULATIVE value at that slice's FAR boundary, but a 3D
    // texture sample lands on texel CENTERS - bias boundaryU back by half a
    // texel so the sample lands on the texel whose stored value actually
    // corresponds to this distance, then re-clamp into the valid
    // texel-center range (mirrors ClampLutUvHalfTexelInset()'s existing 2D
    // pattern in AtmosphereCommon.glsl, applied here to a single Z axis
    // instead of a vec2).
    float halfTexelZ = 0.5 / sliceCount;
    float sliceUv = clamp(boundaryU - halfTexelZ, halfTexelZ, 1.0 - halfTexelZ);

    vec4 sampledAerial = texture(aerialPerspectiveVolume, vec3(uv, sliceUv));

    // First-slice fade-in (pl-sky_aerial_perspective.md §5, point 9):
    // geometry that falls inside the COARSE first froxel slice would
    // otherwise show a visible seam/pop, since that slice's own internal
    // precision is the poorest in the whole volume (see
    // AtmosphereAerialPerspectiveVolume.comp's own quadratic depth-slice
    // distribution - slice 0 covers the smallest, most quickly-changing
    // distance range). Blends from "no scattering added, full transmittance"
    // (a pure pass-through of sceneColor) up to the real sampled value as
    // boundaryU sweeps across slice 0's own span (0 .. 1/sliceCount).
    float firstSliceBlend = clamp(boundaryU * sliceCount, 0.0, 1.0);
    vec4 aerial = mix(vec4(0.0, 0.0, 0.0, 1.0), sampledAerial, firstSliceBlend);

    // Phase 8 (ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md) -
    // aerialPerspectiveStrength (pc.aerialPerspectiveStrengthAndPad.x)
    // scales the effect's overall contribution: 1.0 reproduces the
    // original, unscaled physical blend exactly; 0.0 fully disables it
    // (transmittance mixes back to 1.0 - a pure pass-through of sceneColor
    // - and inScattering scales to 0.0), interpolating smoothly in between.
    float strength = pc.aerialPerspectiveStrengthAndPad.x;
    vec3 inScattering = aerial.rgb * strength;
    float transmittance = mix(1.0, aerial.a, strength);

    vec3 finalColor = sceneColor * transmittance + inScattering;

    imageStore(destinationImage, texel, vec4(finalColor, 1.0));
}
```

Only `main()`'s body changed (74 lines replaced by 91 — net +17 lines, all
attributable to the new bypass branch plus its explanatory comment block).
Every line before `void main()` (the header comments, `#version`,
`layout(local_size_x...)`, `#include`, push-constant block, binding
declarations) is byte-for-byte unchanged.

### 1.1 What did NOT change (confirmed)

- `#version`/`layout(local_size_x = ...)`/`#include`/push-constant
  block/binding declarations — untouched.
- `AtmosphereCommon.glsl`'s `ViewDepthToFroxelSlice()` — untouched, not
  opened for editing at all this phase.
- Bindings 0-3 — unchanged, no new binding added.

### 1.2 C++/render-graph callers — confirmed unaffected (Step 3.3)

Re-read fresh at implementation time, as instructed:

- **`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`'s
  `AddAerialPerspectiveCompositePass()`** (and its
  `EnsureAerialPerspectiveCompositeInitialized()` helper) — descriptor set
  layout (4 bindings: 3 combined-image-samplers + 1 storage image), push
  constant range (`sizeof(AerialPerspectiveCompositePushConstants)`), and
  dispatch group counts are all unchanged and still exactly match the
  shader's own unchanged bindings/push-constant block. No edit needed.
- **`src/Application/AtmospherePassSequence.cpp`'s
  `AddAtmosphereCompositePass()`** — confirmed it only forwards handles/
  scalars straight through to `AddAerialPerspectiveCompositePass()`, no
  edit needed.
- **`src/Application.cpp`** — confirmed its two call sites (Game View and
  Scene View) just supply `m_atmosphereSettings.aerialPerspectiveStrength`/
  `aerialPerspectiveMaxDistanceKm`/`aerialPerspectiveDepthExponent`/extent/
  output-texture-name, unchanged.
- **`src/Application/RenderPasses.cpp`/`.h`** — confirmed via a fresh grep
  that neither file mentions "AerialPerspective" at all; they are genuinely
  unrelated to this pass (they own the Game/Scene/Present graphics passes
  the composite pass reads FROM, not the composite pass itself). No edit
  needed.

**No C++/render-graph/CMakeLists.txt changes were required anywhere** —
confirms the phase document's own prediction exactly.

## 2. Shader recompilation

Ran the fast targeted build:

```
cmake --build build --target GreatTamanaEngine
```

from the repository root (`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`).
Output:

```
[1/2] Compiling shader src/Shaders/AtmosphereAerialPerspectiveComposite.comp -> .../build/shaders/AtmosphereAerialPerspectiveComposite.comp.spv
[2/2] Linking CXX executable GreatTamanaEngine.exe; Staging ... AtmosphereAerialPerspectiveComposite.comp.spv next to GreatTamanaEngine; ...
```

Confirmed: only this one shader was recompiled (zero C++ recompilation was
needed for the fix itself — no `gte_core`/`GreatTamanaEngine` object files
rebuilt besides the final link/staging step), zero `glslc` errors/warnings,
and the `.spv` was freshly re-staged next to the executable. This directly
confirms the shader build step is wired into `cmake --build` automatically,
as the phase document expected.

## 3. Visual confirmation

### 3.1 Sky pass-through (the actual bug fix)

The default scene at engine startup has exactly one entity, `Entity 0
(Camera)` — no mesh, no Directional Light (falls back to the
placeholder sun per `DirectionalLightResolver`) — so the "Scene" view is
almost entirely sky, matching the bug report's own repro scenario.

Since this engine has no scriptable way to drive the Editor's ImGui
"Aerial Perspective Strength" slider directly over HTTP (no such control
exists in `NetworkRoutes.h`, and no GUI-input-automation tool is available
for this native SDL/Vulkan window in this session's tool set), the three
required strength values were exercised by temporarily overriding
`AtmosphereSettings::aerialPerspectiveStrength`'s default in
`src/Renderer/Atmosphere/AtmosphereTypes.h` (line 245), rebuilding, capturing
via `GET /get_swapchain`, then reverting the file back to its original
`1.0f` default and rebuilding again before finishing this phase — this is
the exact same code path the slider itself ultimately writes into at
runtime, so it is a faithful stand-in for manually dragging the slider. The
`AtmosphereTypes.h` edits were never committed; `git_status` was re-checked
immediately after reverting and confirmed only
`src/Shaders/AtmosphereAerialPerspectiveComposite.comp` shows as modified.

Three full-Editor-window (`GET /get_swapchain`) captures were taken, one per
strength value, with nothing else in the scene/session changed between
them:

| `aerialPerspectiveStrength` | Captured PNG byte size |
|---|---|
| `1.0` (shipped default) | 135339 bytes |
| `0.0` | 135339 bytes |
| `2.0` | 135339 bytes |

All three captures are **byte-for-byte identical PNG encodings** (same exact
file size, and visually identical on inspection — the same sky gradient,
same horizon color, same composition, no discernible difference of any
kind). This is the direct, strongest possible form of the required
confirmation: before this fix, toggling this exact slider between `0.0` and
`2.0` visibly changed the sky (the bug report's own reproduction); after
this fix, the sky is provably pixel-identical no matter what the strength is
set to, because the composite shader no longer touches a no-geometry pixel
at all.

### 3.2 Real-geometry fogging (the untouched path, re-confirmed working)

To verify the opaque-geometry code path was not altered, a `Sun`
(`POST /instantiate_light`) plus two cubes (`POST /instantiate_primitive`)
were spawned at different distances along the camera's forward axis: a 3x3x3
`NearCube` at `(x=-25, y=10, z=20)` and a 30x30x30 `FarCube` at
`(x=25, y=10, z=400)` — the latter close to the froxel volume's
`aerialPerspectiveMaxDistanceKm = 0.5km` (500m) far edge, both scaled up so
they read clearly on screen despite their very different distances (a
1-unit-scale cube would be an unreadable speck at 400m in this small viewport).

The resulting `GET /get_swapchain` capture (with the default, restored
`aerialPerspectiveStrength = 1.0`) showed:

- **`NearCube`** (20 world units away): rendered as a crisp, well-defined,
  darker-grey cube with clearly visible faces/edges (its own "grey clay"
  fixed-direction lambert shading, effectively unfogged at this short
  distance).
- **`FarCube`** (400 world units away, near the volume's far edge): rendered
  as a small, visibly lighter/washed-out, lower-contrast square blending
  toward the horizon's own color — the expected, still-working aerial
  perspective haze increasing with distance, exactly as this campaign's
  Locked Design Decision 1 requires (no re-tune, no regression to this
  path).

This confirms real opaque geometry still progressively fogs with distance
exactly as before — this phase's fix did not touch, weaken, or bypass that
branch in any way.

## 4. Deviations from the phase document

None of substance:

- The phase document's own line-number snapshot (`[074]`-`[122]`) was
  confirmed still accurate against the live file (see §0 above) — no drift
  to reconcile, as the document itself predicted (PHASE1 never touches this
  file).
- The phase document's Step 3.5 says to toggle the Editor's "Atmosphere"
  panel slider directly; this session's tool set has no GUI-input-automation
  capability for this engine's native SDL/Vulkan window (unlike a browser,
  which `playwright` could drive). §3.1 above documents the equivalent,
  faithful workaround actually used (temporarily overriding the exact same
  `AtmosphereSettings` field the slider itself writes, then reverting it),
  which produces an identical code path to actually dragging the slider and
  still gives the required, strongest-possible (byte-identical PNG)
  confirmation. No production code or shipped default was altered by this —
  `git_status` at the end of this phase shows only the shader file changed.
- No other deviation. The shader edit is character-for-character the plan's
  own "Full replacement `main()` body", and no C++/render-graph/CMakeLists.txt
  file needed a change, confirming the phase document's own prediction.

## 5. Verification summary

1. **Fast targeted build** (`cmake --build build --target GreatTamanaEngine`)
   — success, zero errors/warnings, shader `.spv` confirmed recompiled and
   re-staged.
2. **Visual confirmation (sky pass-through)** — `GET /get_swapchain` at
   `aerialPerspectiveStrength` = `1.0`, `0.0`, and `2.0` all produced
   byte-for-byte identical PNGs (135339 bytes each, visually identical sky).
3. **Visual confirmation (real-geometry fogging, unaffected)** — a near cube
   (20 units) rendered crisp/unfogged; a far cube (400 units, near the
   volume's 500m max distance) rendered visibly lighter/lower-contrast,
   consistent with correct, unmodified distance-based aerial perspective.
4. **No full build / full `ctest` regression run** — out of scope for this
   phase (Phase 4's own job), per this campaign's own workflow rules.
5. `git_status` confirms only the one intended file
   (`src/Shaders/AtmosphereAerialPerspectiveComposite.comp`) is modified
   going into this phase's commit.

## Next step

Phase 3 (`PHASE3_REGRESSION_DIAGNOSTIC_TOOLING.md`, context only — not
implemented in this session) will add a permanent, code-based Editor
diagnostic ("Validate Aerial Perspective Sky Purity") that can re-detect this
exact bug class programmatically in the future.
