# PHASE4 — Atmosphere-Aware Volume Debug Preview (the "see the 3D texture" ask)

Parent: `PHASE0_MASTER_STRATEGY.md`. Independent of Phases 1-3's own shader
math (this phase touches the PREVIEW/visualization path, never the
Aerial-Perspective generation/composite shaders themselves) — can technically
be implemented in parallel with Phases 1-3, but is sequenced after them here
because its own "does this actually look useful now" verification step is
much more meaningful once Phase 3's rebalancing has already shipped a
visibly-non-trivial volume to look at.

## Step 1 — The Goal (Where are we going?)

Give the ALREADY-SHIPPED (`network-impl-6` campaign) `GET /get_texture`
volume-texture raymarch preview a second, **atmosphere-aware interpretation
mode**, automatically selected when the requested `texture_name` is the
Aerial Perspective volume (`"AtmosphereAerialPerspectiveVolume_GameView"` /
`"...SceneView"`), so that fetching it over HTTP actually reveals meaningful
spatial structure (near-camera vs. far-camera haze density and color)
instead of an uninformative, nearly-flat dark box — with ZERO behavior
change for every other (non-atmosphere, hypothetical future) volume texture,
and ZERO new HTTP endpoint/query parameter.

## Step 2 — The Situation (Where are we now?)

- `network-impl-6` already ships `VolumeTexturePreviewRenderer`
  (`src/Renderer/VolumeTexturePreviewRenderer.h/.cpp`) and
  `src/Shaders/VolumeTexturePreview.comp`, wired into `Application.cpp`'s
  existing `GET /get_texture` handler: a requested name that resolves to a
  volume (via `RenderGraphDebugVolumeTextureRegistry`) runs this renderer
  instead of a plain pixel copy.
- The shader's current, deliberately generic interpretation (from
  `VolumeTexturePreview.comp`'s own `main()`):
  ```glsl
  vec4 s = texture(volumeTex, uvw);
  float density = s.a;
  vec3 color = s.rgb;
  float alpha = 1.0 - exp(-density * pc.densityScale * stepLength);
  accum.rgb += (1.0 - accum.a) * color * alpha;
  accum.a += (1.0 - accum.a) * alpha;
  ```
  with `pc.densityScale` fixed at `4.0`
  (`VolumeTexturePreviewRenderer::kDensityScale`).
- For the Aerial Perspective volume specifically: `s.a` is **transmittance**
  (how much light SURVIVES the trip from the camera to this froxel — close
  to `1.0` almost everywhere, especially before Phase 3's rebalancing, and
  even after it, transmittance is `1 - "haze amount"`, i.e. the OPPOSITE
  polarity of a density), and `s.rgb` is **raw HDR in-scattered luminance**
  (routinely far below `1.0`, often `1e-5`-`1e-3` even after Phase 3's
  exaggeration multiplier). Feeding `s.a` directly into the existing
  `density` variable and `s.rgb` directly into `color` produces: (a) `alpha`
  saturating almost immediately (since `density ≈ 1.0` nearly everywhere),
  making the preview look like an almost-solid, opaque box regardless of the
  real underlying spatial variation, and (b) `color` reading as
  near-black, since raw in-scattered luminance this small is visually
  indistinguishable from `vec3(0)` without any exposure adjustment.
- `Application.cpp`'s `GET /get_texture` handler (the exact call site
  `network-impl-6` Phase 4 added) already has the requested `texture_name`
  string in scope at the point it calls
  `VolumeTexturePreviewRenderer::RenderPreview()` — the natural, zero-new-
  parameter place to decide which interpretation mode to use.

## Step 3 — The Plan (Detailed Steps)

### 3.1 — `VolumeTexturePreviewRenderer.h`/`.cpp`: new interpretation-mode parameter

Add a new enum (header, `VolumeTexturePreviewRenderer.h`):

```cpp
// atmosphere-scattering-2 campaign, Phase 4 - which raw-texel ->
// density/color interpretation VolumeTexturePreview.comp uses. Auto-
// selected by RenderPreview()'s own caller (Application.cpp) based on the
// requested texture_name string - never exposed as a new HTTP query
// parameter (see PHASE0_MASTER_STRATEGY.md's own Locked Design Decision 6).
enum class VolumeTexturePreviewInterpretation : std::int32_t {
    GenericDensityInAlpha = 0, // network-impl-6's original, still-default interpretation - UNCHANGED.
    AtmosphereAerialPerspective = 1, // atmosphere-scattering-2 Phase 4 - see VolumeTexturePreview.comp's own doc comment.
};
```

`RenderPreview()` gains a new parameter,
`VolumeTexturePreviewInterpretation interpretation =
VolumeTexturePreviewInterpretation::GenericDensityInAlpha` (defaulted, so
any existing/future caller that doesn't care keeps today's exact behavior
with zero source changes required at THEIR call site — though this
codebase's own convention favors explicit call sites over relying on
defaults; check `Application.cpp`'s real call site and pass it explicitly
there regardless). Thread this value into a new push-constant `int
interpretationMode` field (see 3.3).

### 3.2 — `VolumeTexturePreviewRenderer.cpp`: new fixed exposure constant

```cpp
// Fixed exposure multiplier applied ONLY when interpretation ==
// AtmosphereAerialPerspective, BEFORE the shader's own Reinhard tonemap -
// chosen empirically so this campaign's own typical in-scattering
// magnitudes (see AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md's own cited
// numbers, ~1e-5 to ~1e-3 pre-Phase-3, larger after Phase 3's exaggeration
// multiplier) land in a visually legible mid-range rather than crushing to
// black. Re-tune this constant (not densityScale, not kAerialPreviewExposure's
// own call sites) if Phase 3's own final chosen exaggeration default changes
// substantially later.
static constexpr float kAerialPreviewExposure = 2000.0f;
```
Push this alongside `kDensityScale` into the push-constant struct (3.3).

### 3.3 — Push-constant struct + `VolumeTexturePreview.comp`: the actual branch

Extend the existing push-constant struct (both the C++ mirror in
`VolumeTexturePreviewRenderer.cpp` and the GLSL block in
`VolumeTexturePreview.comp`) with two new fields, appended at the end
(preserving every existing field's own offset, to avoid re-deriving the
whole struct's padding):

```cpp
// C++ side (VolumeTexturePreviewRenderer.cpp)
struct PushConstants {
    // ... all existing fields, UNCHANGED ...
    std::int32_t interpretationMode = 0; // VolumeTexturePreviewInterpretation, as int.
    float aerialPreviewExposure = 0.0f;  // Only meaningful when interpretationMode == 1; 0 when mode == 0 (unused).
};
```
```glsl
// GLSL side (VolumeTexturePreview.comp)
layout(push_constant) uniform PushConstants {
    // ... all existing fields, UNCHANGED ...
    int interpretationMode;
    float aerialPreviewExposure;
} pc;
```
(Recompute/verify std140-style 16-byte alignment for the new tail fields
exactly like every other push-constant struct in this codebase already
documents doing by hand — an `int` immediately followed by a `float` packs
into 8 bytes with no gap, which may or may not need one more explicit
padding float depending on where the existing struct's own tail currently
sits; inspect the REAL current struct layout before finalizing this, don't
assume the sketch above is already correctly packed.)

Inside `main()`'s per-step loop, replace the current unconditional
`density`/`color` derivation with a branch:

```glsl
vec4 s = texture(volumeTex, uvw);
float density;
vec3 color;
if (pc.interpretationMode == 1) {
    // Atmosphere Aerial Perspective volume: s.a is TRANSMITTANCE (how much
    // light survives - opposite polarity of a density), s.rgb is raw HDR
    // in-scattered luminance (tiny; needs exposure + tonemap to be legible
    // at all). See this campaign's PHASE4_ATMOSPHERE_AWARE_VOLUME_DEBUG_PREVIEW.md.
    density = 1.0 - s.a; // "haze amount" - 0 where fully transparent/no haze, ->1 where opaque/heavy haze.
    vec3 exposed = s.rgb * pc.aerialPreviewExposure;
    color = exposed / (exposed + vec3(1.0)); // Simple fixed Reinhard tonemap - keeps arbitrarily bright HDR input in [0,1) without hard clipping.
} else {
    // Generic interpretation - network-impl-6's own original, UNCHANGED behavior.
    density = s.a;
    color = s.rgb;
}
float alpha = 1.0 - exp(-density * pc.densityScale * stepLength);
```

Update this shader's own top-of-file doc comment to describe the new
branch, citing this campaign by name (mirrors every other shader's own
"which campaign/phase added this" citation convention already used
throughout `src/Shaders/`).

### 3.4 — `Application.cpp`: select the interpretation mode by name

At the existing `GET /get_texture` volume-branch call site (the one
`network-impl-6` Phase 4 added), before calling `RenderPreview()` (CONFIRMED
by direct inspection: the real local variable holding the requested texture
name at this call site is `const std::string requestedName =
m_captureBridge.RequestedTextureName();`, declared once near the top of this
handler and reused by both the 2D and volume branches — NOT
`requestedTextureName` as an earlier draft of this document guessed; use the
real name below):

```cpp
const bool isAerialPerspectiveVolume =
    requestedName.rfind("AtmosphereAerialPerspectiveVolume", 0) == 0; // starts-with check.
const VolumeTexturePreviewInterpretation interpretation = isAerialPerspectiveVolume
    ? VolumeTexturePreviewInterpretation::AtmosphereAerialPerspective
    : VolumeTexturePreviewInterpretation::GenericDensityInAlpha;
```
place this right before the existing `m_volumeTexturePreviewRenderer.RenderPreview(m_renderer,
volumeSnapshot->target, volumeSnapshot->state);` call inside the `else`
branch of the `requestedChannel == DebugTextureChannel::Depth` check, then
pass `interpretation` as `RenderPreview()`'s new trailing argument.

### 3.5 — Zero regression to the generic path

Confirm (by inspection, and via the smoke test below) that requesting ANY
volume texture name that does NOT start with
`"AtmosphereAerialPerspectiveVolume"` still produces `interpretationMode ==
0` and therefore byte-for-byte the same preview `network-impl-6` already
shipped — this campaign adds a new branch, it must never change the
existing one's own code path or output.

## Verification

- Fast, targeted compile: build `GreatTamanaEngine` (forces `glslc` to
  recompile `VolumeTexturePreview.comp`).
- `run_app_background` + `gte_send_request`:
  - `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
    (both before/after this phase's own change, if practical, to visually
    compare) — confirm the new preview shows a plausible spatial gradient
    (denser/more colored haze toward whichever edge of the box represents
    "farther from camera", roughly uniform/near-empty near the "close to
    camera" edge) rather than a flat, uniformly dark box.
  - Re-confirm a known, pre-existing 2D texture name (e.g. `"Swapchain"`)
    still returns its byte-for-byte unchanged capture.
  - `stop_app_background` when done.
- Write `PHASE4_COMPLETION_REPORT.md`, then commit.
