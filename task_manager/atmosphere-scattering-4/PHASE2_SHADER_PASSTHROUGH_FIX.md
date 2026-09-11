# PHASE2 — The Actual Fix: Pass-Through Branch in the Composite Shader

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first)
**Depends on:** `PHASE1_COMPOSITE_DECISION_CPU_ORACLE_AND_TESTS.md` (read its
own completion report first — the oracle this phase mirrors must already
exist and be passing its own tests before this phase starts)
**Touches shaders?** Yes — this is the one phase that changes GLSL.

---

## Step 1 — The Goal

Apply the minimal, surgical code change to
`src/Shaders/AtmosphereAerialPerspectiveComposite.comp` that makes it agree,
byte-for-byte in *intent*, with Phase 1's CPU oracle
(`ComputeAerialPerspectiveCompositeColor()`/
`ShouldBypassAerialPerspectiveComposite()`): a pixel with no opaque geometry
(`rawDepth >= 0.999999`) must be written back to `destinationImage`
completely unchanged from `sourceColor` — no aerial-perspective volume
sample, no strength/transmittance/in-scattering math, full stop. Every other
pixel (real opaque geometry) keeps behaving exactly as before.

After this phase, recompile shaders and confirm no compile error/validation
warning, load the same repro scene (Camera + Directional Light only, no mesh
entities — the exact scene from the bug report) and confirm visually (via
`gte_send_request`) that the sky no longer changes when
`aerialPerspectiveStrength` is toggled between `0.0` and `2.0` in the
Editor's "Atmosphere" panel.

---

## Step 2 — The Situation

`src/Shaders/AtmosphereAerialPerspectiveComposite.comp`'s current `main()`
(confirmed via direct file read — line numbers below match the file's actual
current 0-based line indices at the time this strategy was written; **always
re-read the file fresh immediately before editing**, since Phase 1 does not
touch this file and these numbers should still be accurate, but the "always
re-check before editing" discipline applies regardless):

```
[074] void main()
[075] {
[076]     ivec2 outputSize = imageSize(destinationImage);
[077]     ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
[078]     if (texel.x >= outputSize.x || texel.y >= outputSize.y) {
[079]         return;
[080]     }
[081]
[082]     float maxDistanceKm = pc.aerialPerspectiveStrengthAndPad.y;
[083]     float depthExponent = pc.aerialPerspectiveStrengthAndPad.z;
[084]
[085]     vec2 uv = (vec2(texel) + vec2(0.5)) / vec2(outputSize);
[086]     vec2 ndc = uv * 2.0 - 1.0;
[087]
[088]     float rawDepth = texture(sourceDepth, uv).r;
[089]
[090]     float viewDistanceKm;
[091]     if (rawDepth >= 0.999999) {
[092]         viewDistanceKm = maxDistanceKm;
[093]     } else {
[094]         vec4 clipPos = vec4(ndc, rawDepth, 1.0);
[095]         vec4 worldPos4 = pc.invViewProjection * clipPos;
[096]         vec3 worldPos = worldPos4.xyz / worldPos4.w;
[097]         float worldDistance = length(worldPos - pc.cameraWorldPositionAndScale.xyz);
[098]         viewDistanceKm = worldDistance / max(pc.cameraWorldPositionAndScale.w, 1e-6);
[099]     }
[100]
[101]     ivec3 volumeSize = textureSize(aerialPerspectiveVolume, 0);
[102]     float sliceCount = float(volumeSize.z);
[103]     float slice = ViewDepthToFroxelSlice(viewDistanceKm, sliceCount, maxDistanceKm, depthExponent);
[104]     float boundaryU = clamp(slice / sliceCount, 0.0, 1.0);
[105]
[106]     float halfTexelZ = 0.5 / sliceCount;
[107]     float sliceUv = clamp(boundaryU - halfTexelZ, halfTexelZ, 1.0 - halfTexelZ);
[108]
[109]     vec4 sampledAerial = texture(aerialPerspectiveVolume, vec3(uv, sliceUv));
[110]
[111]     float firstSliceBlend = clamp(boundaryU * sliceCount, 0.0, 1.0);
[112]     vec4 aerial = mix(vec4(0.0, 0.0, 0.0, 1.0), sampledAerial, firstSliceBlend);
[113]
[114]     float strength = pc.aerialPerspectiveStrengthAndPad.x;
[115]     vec3 inScattering = aerial.rgb * strength;
[116]     float transmittance = mix(1.0, aerial.a, strength);
[117]
[118]     vec3 sceneColor = texture(sourceColor, uv).rgb;
[119]     vec3 finalColor = sceneColor * transmittance + inScattering;
[120]
[121]     imageStore(destinationImage, texel, vec4(finalColor, 1.0));
[122] }
```

There is no early return for the `rawDepth >= 0.999999` case — that branch
only decides *which distance to fake* for the volume lookup, it never skips
the volume lookup or the final blend. This is the entire bug.

---

## Step 3 — The Plan

### 3.1 Exact edit to `main()`

Replace the body of `main()` (from immediately after the bounds-check early
return down through the final `imageStore`) with a version that:

1. Reads `sourceDepth`/`sourceColor` **first**, right after the bounds check
   (so both are available regardless of which branch is taken).
2. Adds an early `if (rawDepth >= 0.999999) { imageStore(..., vec4(sceneColor,
   1.0)); return; }` pass-through **before** any of the volume-lookup/Z-slice
   machinery runs at all — not just before the final blend. This is
   deliberately stronger than "compute everything then discard it": it also
   avoids a wasted `texture(aerialPerspectiveVolume, ...)` sample and
   `textureSize()` call for every sky pixel on screen, which is a genuine
   (small but real) performance win alongside the correctness fix.
3. Leaves every line for the `rawDepth < 0.999999` (real geometry) case
   **completely untouched**, character-for-character, other than removing
   the now-dead `viewDistanceKm` if/else's `if (rawDepth >= 0.999999)` branch
   (folded into the new early return) and keeping its `else` branch's body as
   the new, unconditional (post-early-return) world-position reconstruction.

Full replacement `main()` body:

```glsl
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
    float slice = ViewDepthToFroxelSlice(viewDistanceKm, sliceCount, maxDistanceKm, depthExponent);
    float boundaryU = clamp(slice / sliceCount, 0.0, 1.0);

    // Half-texel Z-bias (pl-sky_aerial_perspective.md §5, point 7): slice i
    // stores the CUMULATIVE value at that slice's FAR boundary, but a 3D
    // texture sample lands on texel CENTERS - bias boundaryU back by half a
    // texel so the sample lands on the texel whose stored value actually
    // corresponds to this distance, then re-clamp into the valid
    // texel-center range.
    float halfTexelZ = 0.5 / sliceCount;
    float sliceUv = clamp(boundaryU - halfTexelZ, halfTexelZ, 1.0 - halfTexelZ);

    vec4 sampledAerial = texture(aerialPerspectiveVolume, vec3(uv, sliceUv));

    // First-slice fade-in (pl-sky_aerial_perspective.md §5, point 9):
    // geometry that falls inside the COARSE first froxel slice would
    // otherwise show a visible seam/pop.
    float firstSliceBlend = clamp(boundaryU * sliceCount, 0.0, 1.0);
    vec4 aerial = mix(vec4(0.0, 0.0, 0.0, 1.0), sampledAerial, firstSliceBlend);

    // Phase 8 (ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md) -
    // aerialPerspectiveStrength scales the effect's overall contribution:
    // 1.0 reproduces the original, unscaled physical blend exactly; 0.0
    // fully disables it. Mirrors
    // AtmosphereAerialPerspectiveCompositeMath.h's
    // ComputeAerialPerspectiveCompositeColor()'s own non-bypass branch
    // exactly.
    float strength = pc.aerialPerspectiveStrengthAndPad.x;
    vec3 inScattering = aerial.rgb * strength;
    float transmittance = mix(1.0, aerial.a, strength);

    vec3 finalColor = sceneColor * transmittance + inScattering;

    imageStore(destinationImage, texel, vec4(finalColor, 1.0));
}
```

### 3.2 What must NOT change in this file

- The `#version`/`layout(local_size_x = ...)`/`#include`/push-constant block/
  binding declarations (lines 0-73) — untouched.
- `AtmosphereCommon.glsl`'s `ViewDepthToFroxelSlice()` — untouched, this bug
  is not in that function.
- Bindings 0-3 (`sourceColor`/`sourceDepth`/`aerialPerspectiveVolume`/
  `destinationImage`) — no new binding needed, confirming the bug report's
  own "no C++/CPU-side changes are required" conclusion (§4).

### 3.3 No C++/render-graph changes required

Confirm (do not just assume) that no caller needs updating:
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`'s
  `AddAerialPerspectiveCompositePass()` — its descriptor bindings, push
  constants, and dispatch size are unaffected; it already binds exactly the
  four resources this shader still uses.
- `src/Application/AtmospherePassSequence.cpp`'s
  `AddAtmosphereCompositePass()` — unaffected, it only forwards handles.
- `src/Application/RenderPasses.cpp` / `Application.cpp` — unaffected.

If, upon a fresh read of these files at implementation time, anything is
found to actually require a change, stop and treat that as new information
that must be reconciled with this document (flag it in the completion
report) rather than silently expanding scope.

### 3.4 Recompile shaders

Confirm the project's shader build step (`cmake/CompileShaders.cmake` /
`glslc`, per this repository's existing build wiring) is re-run so
`AtmosphereAerialPerspectiveComposite.comp.spv` picks up the change — this
typically happens automatically as part of the normal `cmake --build`
step for this project; confirm by checking the `.spv` file's timestamp
changes after building, or by inspecting `cmake/CompileShaders.cmake`
directly if unsure how shader compilation is wired into the build graph.

### 3.5 Verification (fast compile check + visual confirmation, still no full build/ctest)

1. Fast targeted build (shader compile step + `gte_core`/the main
   executable target) — confirm zero compiler/`glslc` errors or warnings.
2. Launch the engine via `run_app_background`, reproduce the exact bug-report
   scenario (a scene with only Camera + Directional Light, no mesh entity —
   or hide any existing mesh/grid temporarily if easier) so most of the
   screen is sky.
3. Use `gte_send_request` (`GET /get_game_view` or `GET /get_swapchain`) to
   capture a frame.
4. In the Editor's "Atmosphere" panel, set `Aerial Perspective Strength` to
   `0.0`, capture again; set it to `2.0`, capture again. **The three captures
   must be visually/pixel-identical in the sky region** (above the horizon/
   grid) — this is the direct, human-visible confirmation the bug is fixed,
   mirroring the bug report's own §5 point 3 exactly.
5. If a reference grid or test mesh is visible in the scene, confirm it
   *still* progressively fogs/darkens with distance exactly as before (this
   code path is untouched by this phase, but must be re-confirmed working,
   not just assumed).
6. `stop_app_background` the running engine instance once captures are done.
7. Write `PHASE2_COMPLETION_REPORT.md` describing the exact diff applied
   (quote the final file content), the compile check result, and the visual
   confirmation (describe what the captured images showed — attach/save
   captured images if the available tooling supports it, otherwise describe
   them precisely in words), then `git_add` + `git_commit`.
