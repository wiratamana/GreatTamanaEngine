# Aerial Perspective Bug Report — "Applied to everything on screen, even with no object"

**Date:** 2026-09-11
**Project root:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`
**Reference doc read:** `task_manager\atmosphere-scattering-4\PDF_Extraction_SkyAndAtmosphere_20260911_140048.md`
(Hillaire, *A Scalable and Production Ready Sky and Atmosphere Rendering Technique*, EGSR 2020)

---

## 1. TL;DR — Root cause

**Confirmed, reproducible code bug**, not a tuning problem:

`src/Shaders/AtmosphereAerialPerspectiveComposite.comp` runs its full
`sceneColor * transmittance + inScattering` Aerial-Perspective blend on **every
pixel of the screen unconditionally**, including pixels where nothing was ever
drawn (the sky). It does *not* implement the branch your own pasted
pseudocode (and the paper's §5.4) describes:

```
if pixel contains opaque geometry:
    [pixel UV + object depth] -> Aerial Perspective LUT
    finalColor = objectColor * transmittance + inScatteredLight

if pixel contains no geometry / depth is far plane:
    [camera position + view direction] -> Sky View LUT
    finalColor = skyColor        <-- NO aerial-perspective blend here
```

Instead, the shipped shader does this for the "no geometry" case:

```
if pixel contains no geometry / depth is far plane:
    viewDistanceKm = maxDistanceKm   // farthest froxel slice, arbitrarily
    aerial = sample(AerialPerspectiveVolume, farthest slice)
    finalColor = skyColor * aerial.transmittance + aerial.inScattering   // <-- BUG
```

So the sky pixel is first correctly computed by the Sky-View LUT pass
(`AtmosphereSkyBackground.frag`), and then, a few passes later in the SAME
frame, the Aerial-Perspective Composite pass re-processes that exact same
pixel and blends a **second, independent fog layer** on top of it — using a
LUT that was never designed to be sampled for infinitely-far / no-geometry
pixels at all. This is exactly why it "looks like aerial perspective is
applied to everything on screen, despite having no object": there is
genuinely no object at those pixels, and the effect is still being applied
there.

---

## 2. Why this matches the symptom exactly

Aerial Perspective, by definition (paper §5.4, and your own pasted
pseudocode), is a volumetric-fog effect that exists to explain "how much
atmosphere sits between the camera and a piece of *opaque geometry*". It is
**not** meant to ever run for pixels that have no geometry — those pixels
already get 100% of the correct atmosphere-to-infinity look for free from the
Sky-View LUT, which integrates scattering all the way to the top of the
atmosphere (not just out to `maxDistanceKm`).

The current code path forces every "empty" pixel through the Aerial
Perspective LUT anyway, using the **farthest** volume slice as a stand-in for
"infinity". That farthest slice is only ever a ray-marched integral out to
`aerialPerspectiveMaxDistanceKm` (see §3.3 below — currently **0.5 km / 500
world units** by default), which:

- is a wildly different (much shorter, and separately/artistically
  exaggerated) integration than what the Sky-View LUT already computed for
  that same direction, so the two results disagree and the sky gets an
  extra, physically-unjustified tint/haze stacked on top of itself, and
- is applied identically to **every** background pixel on screen (mostly
  differing only by view direction / sun angle), which reads visually as "a
  layer of aerial perspective sitting over the whole screen" even though the
  hierarchy panel confirms there is no mesh entity in the scene at all
  (`Entity 0 (Camera)`, `Entity 1`, `Directional Light` — no renderable
  object).

---

## 3. Full code trail (file + line citations)

### 3.1 The composite shader itself

`src/Shaders/AtmosphereAerialPerspectiveComposite.comp`, `main()`:

```glsl
float rawDepth = texture(sourceDepth, uv).r;

float viewDistanceKm;
if (rawDepth >= 0.999999) {
    // Nothing was drawn at this pixel (still at the clear value, 1.0) -
    // ... always sample the volume's FARTHEST Z slice ...
    viewDistanceKm = maxDistanceKm;                                    // [094]-[100]
} else {
    ... reconstruct real world position / distance for opaque geometry ...
}

... // slice lookup, first-slice fade-in, etc. — all UNCONDITIONAL

vec4 sampledAerial = texture(aerialPerspectiveVolume, vec3(uv, sliceUv));  // [126]
vec4 aerial = mix(vec4(0,0,0,1), sampledAerial, firstSliceBlend);          // [138]

float strength = pc.aerialPerspectiveStrengthAndPad.x;                    // [146]
vec3 inScattering = aerial.rgb * strength;                                // [147]
float transmittance = mix(1.0, aerial.a, strength);                       // [148]

vec3 sceneColor = texture(sourceColor, uv).rgb;                           // [150]
vec3 finalColor = sceneColor * transmittance + inScattering;              // [151]  <-- BUG: unconditionally applied, including to sky pixels
imageStore(destinationImage, texel, vec4(finalColor, 1.0));
```

There is **no early-out / pass-through branch** for the `rawDepth >= 0.999999`
case. `sceneColor` at that point is *already* the fully-resolved sky color
(see 3.2 below), yet it still gets multiplied by `aerial.a` and has
`aerial.rgb` added to it.

### 3.2 Confirmed: the sky is drawn into the SAME texture, BEFORE this pass runs

`src/Application/RenderPasses.cpp`, `AddSceneViewPass()` (and `AddGameViewPass()`
identically):

```cpp
game.Render(renderer, aspectWidthOverHeight, &sceneViewProjection);   // opaque geometry
...
// Sky background BEFORE the grid overlay - see this file's own
// header comment (AddSceneViewPass()'s doc comment) for the
// full ordering reasoning.
if (recordSkyBackground) {
    recordSkyBackground(cmd);      // -> AtmosphereLutRenderer::DrawSkyBackground()
}
if (recordSceneOverlay) {
    recordSceneOverlay(cmd, sceneViewProjection);
}
```

`src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.cpp`,
`EnsurePipeline()` confirms the sky pass's own depth state:

```cpp
// CONFIRMED depth convention ... depth TEST enabled with VK_COMPARE_OP_EQUAL
// (not LESS - this pass must survive ONLY at a pixel whose depth is still
// exactly the frame's own clear value, 1.0), depth WRITE disabled (nothing is
// ever meant to occlude the sky itself).
depthStencil.depthTestEnable = VK_TRUE;
depthStencil.depthWriteEnable = VK_FALSE;
depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL;
```

So: after the sky pass, every "empty" pixel's **color** is the final,
correct sky radiance, and its **depth** is still exactly `1.0` (unchanged,
since the sky pass never writes depth). Both facts are then read back by the
composite pass:

`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`,
`AddAerialPerspectiveCompositePass()` — the composite pass's `sourceColorHandle`
is literally the SAME `SceneView`/`GameView` color+depth render target the sky
pass just wrote into (see the shader's own header comment at
`AtmosphereAerialPerspectiveComposite.comp` lines 22-27: *"binding 0 =
sampler2D sourceColor (this view's original color, POST-Sky-Background-pass)"*).
This is also confirmed end-to-end in `src/Application/AtmospherePassSequence.cpp`,
`AddAtmosphereCompositePass()`, which forwards `sourceColorHandle` straight
into `AddAerialPerspectiveCompositePass()` with no branching on depth at the
C++ level either — the *entire* screen-sized dispatch always runs the full
blend, every pixel, every frame.

### 3.3 Why the effect looks so strong (a compounding, separate tuning issue worth flagging too)

Even once 3.1 is fixed, be aware of these current defaults in
`src/Renderer/Atmosphere/AtmosphereTypes.h` (`AtmosphereSettings`, lines
298-301):

```cpp
float aerialPerspectiveMaxDistanceKm = 0.5f;             // 500 world units (1 unit = 1m, see kWorldUnitsPerKilometer = 1000)
float aerialPerspectiveDepthExponent = 2.0f;
int   aerialPerspectiveSamplesPerSlice = 8;
float aerialPerspectiveScatteringExaggeration = 30.0f;   // artistic 30x density multiplier, applied inside AtmosphereAerialPerspectiveVolume.comp
```

and `aerialPerspectiveStrength = 1.0f` (full effect, no attenuation) at line 245.

This means the Aerial-Perspective volume itself is deliberately tuned to look
heavy at short (500 m) range for the current test scene's scale. This is a
legitimate, working *design* for real opaque geometry (e.g. it is what makes
the reference/ground grid go hazy past a few hundred meters) — but it also
means the bug in §3.1 is not subtle: the "farthest slice" fog stacked onto
every sky pixel uses this same aggressively-tuned volume, so the sky ends up
visibly tinted/darkened everywhere, not just faintly.

This part is **not** a code bug by itself, just something to sanity-check
once §3.1 is fixed — if the sky (and/or nearby geometry) still looks wrong
afterward, revisit `aerialPerspectiveMaxDistanceKm` /
`aerialPerspectiveScatteringExaggeration` in the Editor's "Atmosphere" panel
(`src/Editor/Panels/AtmospherePanel.cpp`) relative to your scene's real-world
scale.

### 3.4 About the gray box in your screenshot

Your hierarchy panel shows no mesh entity at all (`Entity 0 (Camera)`,
`Entity 1`, `Directional Light`), so the large flat gray shape in the
Scene view is almost certainly the engine's own reference/ground grid mesh
(drawn by `recordSceneOverlay` → `SceneGrid.frag`, right after the sky
background pass — see 3.2). That grid **is** real opaque geometry with a
real depth value from the renderer's point of view, so it is legitimately
subject to Aerial Perspective (unlike the sky, which should not be). At the
current `aerialPerspectiveScatteringExaggeration = 30.0f` /
`aerialPerspectiveMaxDistanceKm = 0.5f` settings, the grid rapidly fades to a
flat, fogged gray past a short distance from the camera — which is why grid
lines are only crisp near the very bottom of the frame (closest to camera)
and the rest reads as a flat gray block. This is a separate, secondary
observation from the main bug in §3.1 (it is "working as tuned", just tuned
very aggressively for this scene's scale) — worth a quick visual re-check
after applying the §4 fix, using `GET /get_game_view` or
`GET /get_swapchain` (via the `gte` tool category) with the grid temporarily
hidden, to confirm the sky itself is clean.

---

## 4. Recommended fix

Add the missing early branch so that pixels with no geometry (`rawDepth >=
0.999999`) are passed straight through, unchanged — matching your own
pasted pseudocode and the paper's §5.4 ("aerial perspective volume texture is
applied on opaque objects... at the same time as the Sky-View LUT is applied
on screen", i.e. two mutually-exclusive per-pixel outcomes, not one stacked
on the other).

Minimal patch to `src/Shaders/AtmosphereAerialPerspectiveComposite.comp`'s
`main()`:

```glsl
void main()
{
    ivec2 outputSize = imageSize(destinationImage);
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (texel.x >= outputSize.x || texel.y >= outputSize.y) {
        return;
    }

    vec2 uv = (vec2(texel) + vec2(0.5)) / vec2(outputSize);
    float rawDepth = texture(sourceDepth, uv).r;
    vec3 sceneColor = texture(sourceColor, uv).rgb;

    if (rawDepth >= 0.999999) {
        // No opaque geometry at this pixel - the Sky Background pass (drawn
        // earlier this same frame, into this SAME source color texture) has
        // ALREADY produced the final, correct sky color for it by sampling
        // the Sky-View LUT out to the top of the atmosphere. Aerial
        // Perspective is only meaningful for opaque geometry (paper §5.4) -
        // do NOT re-blend a second, shorter-range fog layer on top of an
        // already-complete sky pixel. Pure pass-through.
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

    float halfTexelZ = 0.5 / sliceCount;
    float sliceUv = clamp(boundaryU - halfTexelZ, halfTexelZ, 1.0 - halfTexelZ);

    vec4 sampledAerial = texture(aerialPerspectiveVolume, vec3(uv, sliceUv));

    float firstSliceBlend = clamp(boundaryU * sliceCount, 0.0, 1.0);
    vec4 aerial = mix(vec4(0.0, 0.0, 0.0, 1.0), sampledAerial, firstSliceBlend);

    float strength = pc.aerialPerspectiveStrengthAndPad.x;
    vec3 inScattering = aerial.rgb * strength;
    float transmittance = mix(1.0, aerial.a, strength);

    vec3 finalColor = sceneColor * transmittance + inScattering;
    imageStore(destinationImage, texel, vec4(finalColor, 1.0));
}
```

Key points about this fix:
- No C++/CPU-side changes are required — `sourceDepth`/`sourceColor` are
  already bound exactly as before; this is a pure shader logic fix.
- Remember to recompile shaders (`cmake/CompileShaders.cmake` /
  `glslc`) so `shaders/AtmosphereAerialPerspectiveComposite.comp.spv` picks
  up the change.
- This does **not** touch `AtmosphereAerialPerspectiveVolume.comp` (the LUT
  generation pass) at all — that shader is fine as-is; it is only ever
  meant to be sampled for real geometry distances. The bug was entirely in
  how the composite pass *consumed* it for the "no geometry" case.
- `aerialPerspectiveVolume`'s farthest slice / `maxDistanceKm` machinery is
  now used exclusively for genuinely opaque, far-away geometry (still
  correctly clamped there), never as a fake "infinity" stand-in for the sky.

---

## 5. Suggested validation after the fix

1. Recompile shaders + rebuild, load the same empty scene (Camera +
   Directional Light only, reference grid visible).
2. Capture a frame via the `gte` tool (`GET /get_game_view` or
   `GET /get_swapchain`) and confirm the sky region (above the grid/horizon)
   now matches the Sky-View LUT's own untouched output — no extra
   haze/tint band should appear near the grid/horizon boundary that wasn't
   there in the Sky-View LUT alone.
3. Toggle `aerialPerspectiveStrength` in the Editor's "Atmosphere" panel
   between `0.0` and `1.0` — with the fix applied, the sky must look
   **100% identical** at both settings (since Aerial Perspective no longer
   touches sky pixels at all). Before the fix, changing `strength` visibly
   changed the sky too, which was itself a second, easy visual confirmation
   of this same bug.
4. Re-introduce a real mesh in front of the camera at varying distances and
   confirm it still correctly gets progressively more fogged/desaturated
   with distance (this path — `rawDepth < 0.999999` — is untouched by the
   fix and should behave exactly as before).
5. Re-check the ground/reference grid's own look per §3.4; if it still
   seems to "gray out" too aggressively at short range, that is the
   separate `aerialPerspectiveMaxDistanceKm` / `aerialPerspectiveScattering
   Exaggeration` tuning question, not this bug.

---

## 6. Summary

| # | Location | Issue | Fix |
|---|----------|-------|-----|
| 1 (root cause) | `AtmosphereAerialPerspectiveComposite.comp` `main()` | Aerial-Perspective blend runs unconditionally for every pixel, including pixels with no geometry (`rawDepth >= 0.999999`), double-applying atmosphere on top of the already-correct Sky-View LUT sky color | Add an early pass-through branch for `rawDepth >= 0.999999` that outputs `sceneColor` unchanged (§4) |
| 2 (contributing, not a bug) | `AtmosphereTypes.h` `AtmosphereSettings` defaults (`aerialPerspectiveMaxDistanceKm = 0.5f`, `aerialPerspectiveScatteringExaggeration = 30.0f`) | Very aggressive tuning for the current test scene's scale, makes real opaque geometry (e.g. the reference grid) fog out heavily at short range | Not required to fix the reported symptom, but worth re-tuning per scene scale once §4 is applied |
| 3 (observation) | Screenshot's gray rectangular shape | Very likely the engine's own reference/ground grid mesh (real opaque geometry, not a scene object), heavily fogged by issue #2 | Re-check after fixing #1; hide the grid temporarily to isolate sky-only rendering |

No tool malfunctions occurred while investigating this — this was a pure
source-code read-through (`read_file`/`read_line`/`search_in_dir`) across
`src/Shaders/AtmosphereAerialPerspectiveComposite.comp`,
`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`,
`src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.cpp`,
`src/Renderer/Atmosphere/AtmosphereTypes.h`,
`src/Application/RenderPasses.cpp`, and
`src/Application/AtmospherePassSequence.cpp`.
