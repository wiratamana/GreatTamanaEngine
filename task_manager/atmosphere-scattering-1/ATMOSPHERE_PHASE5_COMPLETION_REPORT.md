# Atmosphere Phase 5 — Completion Report

**Phase:** `ATMOSPHERE_PHASE5_SKYVIEW_LUT_v1.md`
**Branch:** `feature/atmosphere-scattering-impl`
**Status:** Complete — `gte_core`, `GreatTamanaEngineTests`, and `GreatTamanaEngine` all build cleanly (targeted incremental build only, per this campaign's own workflow rule, including the new `AtmosphereSkyViewLut.comp` shader compiling via `glslc` with zero errors); the engine was actually run (Editor build, Vulkan validation layers enabled by default) and the Sky-View LUT was captured live via `GET /get_texture?texture_name=AtmosphereSkyViewLut_GameView&format=png` — visually confirmed as a recognizable sky gradient (bright glow near the top of the LUT — the hardcoded sun's own elevation band — fading to a darker blue gradient, a visible horizon band, and solid black below it for ground-hitting view directions).

## What changed

### 1. `src/Shaders/AtmosphereCommon.glsl` — extended

Added, right before the closing `#endif`:

- `struct AtmosphereFrameUniforms` — a GLSL mirror of
  `src/Renderer/Atmosphere/AtmosphereTypes.h`'s C++ struct, field-for-field,
  same order (relies on the same std140/std430-identical-padding property
  already established for `AtmosphereParametersGpu`).
- `PlanetVisibility()` — the sun self-shadow test (whether the planet's own
  bulk blocks the sun from a given point), transcribed from
  `_reference/pl-sky/shaders/sky.inc`'s `pl_planet_visibility()`. Declared as
  a genuinely shared function (not inlined once into the Sky-View LUT)
  since Phase 6's aerial-perspective volume is expected to need the exact
  same test along its own ray-march.
- `ClampLutUvHalfTexelInset()` — a half-texel-inset UV clamp, transcribed
  from `pl_clamp_lut_uv()`, used when sampling the Transmittance/
  Multi-Scattering LUTs from inside the Sky-View LUT.
- `SkyViewLutSubUvToUnit()` / `SkyViewLutUnitToSubUv()` /
  `SkyViewLutUvToViewDirection()` / `ViewDirectionToSkyViewLutUv()` — the
  Sky-View LUT's own UV↔(azimuth, elevation) parameterization, transcribed
  from `sky.inc`'s `skyLutSubUvToUnit()`/`skyLutUnitToSubUv()`/
  `fromSkyLut()`/`pl_to_sky_lut()`: a non-linear, horizon-biased elevation
  remap (quadratic from zenith to horizon, sqrt from horizon to nadir) plus
  a linear full-360°-azimuth remap. Both directions were added (mirroring
  Phase 3's own "add the inverse too, for a later phase" precedent) even
  though only `SkyViewLutUvToViewDirection()` (the generation-time
  direction) is used by this phase's own `.comp` file —
  `ViewDirectionToSkyViewLutUv()` is there for Phase 7's Sky Background
  pass to sample this LUT given a per-pixel camera ray.

### 2. `src/Shaders/AtmosphereSkyViewLut.comp` — new

Binding 0 = `AtmosphereParametersGpu` (read-only storage buffer, reused from
Phase 3/4), binding 1 = `AtmosphereFrameUniforms` (a SECOND read-only
storage buffer, this phase's own new per-view/per-frame data — never a true
`uniform` block, per the campaign's "Revision Notes"), binding 2 =
`sampler2D transmittanceLut`, binding 3 = `sampler2D multiScatteringLut`,
binding 4 = `image2D destinationImage` (`rgba16f`, write-only).
`local_size_x/y = 8` (matches the reference's own established convention
for this one shader specifically, unlike the 16×16 Transmittance/
Multi-Scattering LUTs). Per texel: decodes a view direction via
`SkyViewLutUvToViewDirection()`, combines a ground-hit test with an
atmosphere-exit test (mirroring the reference's own
`pl_ray_intersection_planet()`), ray-marches with a variable 32–64 sample
count (quadratic distance distribution, `t=0.3` mid-segment sampling —
matching `ATMOSPHERE_REFERENCE_NOTES.md`'s own Section 5 exactly),
accumulating direct + multiple-scattered luminance and writing the result.

**Deliberate deviation from the reference** (documented at the top of the
file): the per-segment source integral reuses this campaign's own
`IntegrateInscattering()` (added in Phase 4) rather than transcribing the
reference's own separate `pl__integrate_source()` a second time — both
compute the exact same closed-form integral; the only difference is a
minor numerical-robustness nuance in how a near-zero extinction coefficient
is handled (an extra linear-fallback `mix()` in the reference vs. a simple
`max(extinction, 1e-5)` clamp here), not a formula difference — avoiding a
third near-duplicate integral function.

### 3. `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp` — extended

Added `AddSkyViewLutPass(RenderGraphBuilder&, Renderer&, const
AtmosphereParametersGpu&, const AtmosphereFrameUniforms&, TextureHandle
transmittanceLutHandle, TextureHandle multiScatteringLutHandle, const char*
outputTextureName) -> TextureHandle` — the `outputTextureName` parameter is
what lets this ONE method serve both the Game View and Scene View call
sites with two distinct registered texture names.

**Per-view-vs-shared-LUT decision (Step 2): ONE Sky-View LUT PER VIEW —
the simpler, recommended default, and the one actually implemented.**
`AtmosphereLutRenderer` now owns a private nested `SkyViewLutViewState`
struct (a `ComputeDescriptorSet` + a per-view `Buffer` for
`AtmosphereFrameUniforms` + a per-view HDR `RenderTexture` output), stored
in `std::unordered_map<std::string, SkyViewLutViewState>
m_skyViewLutViewStates` keyed by `outputTextureName` — a new view is
lazily created the first time its name is seen. Only the compute
`ComputePipeline`/`VkDescriptorSetLayout` are SHARED across every view
(same shader, same binding layout, no per-view state of their own) — this
is exactly what the strategy document's own Step 3 called for ("do not let
the Scene View's camera height overwrite the Game View's mid-frame;
`AtmosphereLutRenderer` should own two small buffer+descriptor-set
instances... rather than one shared mutable one"), generalized from "two"
to "however many distinct `outputTextureName`s are ever passed in" via the
map, since the shape is identical either way and a map costs nothing extra
in this Tier-2, main-thread-only, once-per-view-per-frame code path.
**Reasoning for why per-view (not a single shared LUT with camera height
baked in at draw time) is correct here:** the Game View and Scene View
render through genuinely different cameras/aspect ratios in the same frame
(`Application::Run()`'s existing dual offscreen regime — confirmed
directly against the real source, per this phase's own Step 2), so their
own camera heights/eye positions can legitimately differ every frame; a
single shared LUT would need the LATER sky-background draw (Phase 7) to
carry a per-pixel camera-height correction term the reference
implementation itself does not have, which would be a real, undocumented
deviation from `pl-sky`'s own design — the per-view approach keeps this
campaign's own GLSL a faithful, unmodified transcription of the reference
and defers any real memory/perf cost question to Phase 9's own tooling
(two 200×100 `RGBA16F` textures is a negligible ~160 KB total either way).

A capturing-by-reference subtlety worth recording: the compute pass's
`execute` lambda captures `&viewState` (a reference into the
`std::unordered_map`'s stored value) — this is safe even if a LATER call in
the same frame (a future Scene View call, Phase 7) inserts a new entry into
the same map, since `std::unordered_map` guarantees references to
already-inserted elements are never invalidated by inserting more elements
(only erasing that specific element would invalidate it) — documented
inline at the capture site.

`ResolveAtmosphereFrameUniforms(Registry&, Vec3 eyeWorldPosition) ->
AtmosphereFrameUniforms` (a free function, declared in
`AtmosphereLutRenderer.h`, defined in `AtmosphereLutRenderer.cpp`) is the
small, explicit per-view frame-uniforms resolution helper this phase's own
Step 3 asked for. It:

- Computes `cameraPositionAtmosphere` as `Vec3(0, planetRadiusKm +
  max(WorldPositionToAtmosphereSpaceKm(eyeWorldPosition).y, 0), 0)` — the
  planet-centered frame every `AtmosphereMath.h`/`AtmosphereCommon.glsl`
  function already works in (only the eye's HEIGHT above the ground is
  used, matching every LUT pass in this campaign and the reference itself,
  which never fold in the camera's horizontal world position either).
- Sets `sunDirection` to a HARDCODED placeholder — a fixed 45°-elevation,
  0°-azimuth direction (`Normalize(Vec3(0.70710678, 0.70710678, 0))`) —
  clearly commented `// TODO(ATMOSPHERE_PHASE8): replace with real
  DirectionalLight resolution`, exactly as this phase's own Step 2/3
  specified. No ECS `DirectionalLight` lookup was attempted.
- Sets `sunIlluminance` to the reference's own sun color × illuminance
  scale (`(1.0, 0.95, 0.85) * 3.0`, `ATMOSPHERE_REFERENCE_NOTES.md`,
  Section 1).
- Takes `Registry&` purely so its SIGNATURE never needs to change once
  Phase 8 replaces the sun-direction branch's body — `registry` itself is
  completely unused (`(void)registry;`) today.

### 4. Wiring: `src/Application/Application.cpp`

Extended the existing (Phase 3/4) temporary validation call site:
captured `AddMultiScatteringLutPass()`'s return value into a named
`multiScatteringLutHandle` (previously pushed straight into `outputs`
anonymously), then added a new, small, local anonymous-namespace helper,
`ResolveActiveCameraWorldPosition(Registry&) -> Vec3` (mirrors
`RenderSystem::ResolveActiveCameraViewProjection()`'s own "first active
Camera, in `ComponentStorage<Camera>` order" resolution, but returns just
the world position via `ECS/TransformHierarchy.h`'s `ComputeWorldTransform()`
rather than a combined view-projection matrix — falls back to
`Vec3::Zero()` when there is no active Camera). The call site then resolves
the Game View's own `AtmosphereFrameUniforms` via
`ResolveAtmosphereFrameUniforms(m_game.GetRegistry(),
ResolveActiveCameraWorldPosition(m_game.GetRegistry()))` and calls
`AddSkyViewLutPass(..., "AtmosphereSkyViewLut_GameView")`, pushing its
result into `outputs`. Per this phase's own "What We Will NOT Do", the
Scene View's own Sky-View LUT is deliberately NOT wired here — the
`// TODO(ATMOSPHERE_PHASE7)` comment was preserved and extended to mention
this. `Application.h` itself needed NO changes — no new member was added,
since every new per-view resource lives inside the existing
`m_atmosphereLutRenderer` member.

### 5. `CMakeLists.txt`

`gte_add_shader(GreatTamanaEngine src/Shaders/AtmosphereSkyViewLut.comp
EXTRA_DEPENDS src/Shaders/AtmosphereCommon.glsl)` added right after the
Multi-Scattering LUT's own registration, unconditional (not
`GTE_ENABLE_EDITOR`-gated), same reasoning as Phase 3/4.

## Visual verification actually performed

- `GET /list_textures` confirmed `"AtmosphereSkyViewLut_GameView"` (200×100,
  `VkFormat(97)` = `VK_FORMAT_R16G16B16A16_SFLOAT`, `has_depth: true`,
  regime `"synchronous"`, `frames_since_update: 0` — updating every frame,
  confirming this is genuinely per-frame, unlike Phase 3/4's own LUTs which
  also update every frame today but for a different reason — no dirty-flag
  logic exists anywhere yet).
- `GET /get_texture?texture_name=AtmosphereSkyViewLut_GameView&format=png`
  returned a real 200×100 PNG (the HDR-capture path from Phase 4's own
  `ConvertHdrRgba16fToRgba8()` fix worked immediately, with zero further
  changes needed — confirming that phase's own prediction) — visually
  confirmed as a recognizable sky gradient: a bright glow concentrated near
  the top portion of the LUT (the hardcoded sun's own ~45° elevation band),
  fading smoothly to a darker blue gradient toward lower elevations, a
  visible horizon band, and solid black for the lower half of the image
  (ground-hitting view directions, correctly reporting zero luminance).
  This matches this phase's own required "bright glow near the hardcoded
  sun direction, darker toward the anti-solar point, a visible horizon
  band" signal.
- Also re-confirmed `GET /list_textures` still shows
  `"AtmosphereTransmittanceLut"`/`"AtmosphereMultiScatteringLut"` updating
  normally, unaffected by this phase's changes.
- Stopped the engine (`stop_app_background`) once verification completed.

## Deviations from the plan (and why)

1. **Per-view state stored in a `std::unordered_map<std::string,
   SkyViewLutViewState>` keyed by `outputTextureName`, rather than a fixed
   two-slot array** — the strategy document offered both as acceptable
   options ("two small buffer+descriptor-set instances (or a tiny
   fixed-size array indexed by view)"); the map was chosen for simplicity
   (no need to invent a stable "view index" convention) and because this
   code path is Tier-2, main-thread-only, and called at most a handful of
   times per frame (not a hot per-vertex/per-pixel path) — the map's small
   overhead is irrelevant here. Documented as a request-time reasoning
   note in `AtmosphereLutRenderer.h`'s own header comment.
2. **`PlanetVisibility()`/`ClampLutUvHalfTexelInset()` added to
   `AtmosphereCommon.glsl`** — not explicitly named in this phase's own
   Step 3 bullet list (which named only the UV parameterization functions),
   but genuinely shared, unconditional math needed to faithfully
   reproduce the reference's own Sky-View LUT shader, following Phase 3/4's
   own precedent of placing such math in the shared file even when not
   strictly enumerated by that specific phase's own plan.
3. **The per-segment source integral reuses `IntegrateInscattering()`
   instead of transcribing a second, separate `pl__integrate_source()`** —
   documented inline in the `.comp` file itself as a deliberate, narrow
   simplification (see "What changed" above); functionally near-identical,
   avoids a third independently-maintained copy of the same closed-form
   integral.
4. **`ResolveActiveCameraWorldPosition()` was added as a small, local,
   anonymous-namespace helper in `Application.cpp`, not as a new
   `RenderSystem` method** — `RenderSystem` already exposes
   `ResolveActiveCameraViewProjection()` (a combined matrix) but nothing
   that returns just a world position; adding a new public method to a
   Tier-1-tested, stable class for what is explicitly TEMPORARY
   validation-call-site wiring (per this campaign's own workflow rules,
   Phase 7 relocates/finalizes every call site here anyway) felt like the
   wrong permanence trade-off. This is flagged here explicitly as an open
   question for Phase 6/7 to reconsider (see below).
5. No other deviations — the LUT resolution (200×100), sample-count knobs
   (32–64, quadratic distribution, `t=0.3`), binding order/convention, the
   "reuse Phase 3's buffer, don't duplicate it" rule, and the hardcoded
   45°-elevation placeholder sun direction were all followed exactly as
   written.

## What was explicitly NOT done (per Step 4)

- No wiring to the Scene View's real `EditorCamera` position — only the
  Game View's Sky-View LUT is computed/wired this phase, exactly as this
  phase's own "What We Will NOT Do" anticipated as fully acceptable.
- No real `DirectionalLight` ECS lookup — the hardcoded sun direction
  placeholder is deliberate and expected here (Phase 8's own job).
- No attempt to unify the Sky-View LUT computation with the (not-yet-built)
  aerial-perspective volume's own ray-marching code (Phase 6) — kept as two
  separate shaders/methods, matching the reference implementation's own
  separate-passes shape.
- No dirty-flag optimization — this LUT (and the per-view
  `AtmosphereFrameUniforms` buffer) is recomputed/re-uploaded
  unconditionally, every single call, which is correct here regardless
  (this is the campaign's first genuinely per-frame pass in the first
  place — there is no "unchanged" case to skip).
- No Editor parameter editing, no GPU timing registration, no new Tier-1
  test file (this phase's shader/class work is Tier 2 — no live-`VkDevice`
  test infrastructure exists, same as Phase 3/4).

## Build/run verification actually performed

- `cmake --build build --target gte_core` — compiled cleanly.
- `cmake --build build --target GreatTamanaEngineTests` — compiled/linked
  cleanly (not run — per this campaign's own "fast compile check only"
  workflow rule, full `ctest` is reserved for Phase 9).
- `cmake --build build --target GreatTamanaEngine` — compiled, the new
  `AtmosphereSkyViewLut.comp` shader compiled via `glslc` with zero errors
  (alongside the two pre-existing atmosphere shaders), and linked cleanly.
- Ran the engine (`run_app_background`, Editor build, Vulkan validation
  layers enabled by default in this non-`NDEBUG` configuration) once,
  confirmed `GET /list_textures`/`GET /get_texture` both behave as
  described above, then stopped it (`stop_app_background`). No validation-
  layer failures observed (the engine ran, responded to HTTP requests, and
  was cleanly stoppable — the strongest signal available given this
  session's tooling cannot read the running GUI process's own
  stdout/stderr back, the same limitation Phase 4's own completion report
  already recorded).
- Per this campaign's own workflow rule, **no full clean build and no full
  `ctest` regression run** were performed (reserved for Phase 9 only).

## Open questions / notes for Phase 6

- **`AtmosphereLutRenderer` is still the one class to extend** — add a
  fourth `AddAerialPerspectiveVolumePass(...)` (or similarly named) method
  alongside the three `AddXxxLutPass()` methods here, reusing the same
  `m_atmosphereParametersBuffer` and (per this phase's own "no attempt to
  unify" scope note) writing its own independent ray-march rather than
  sharing code with `AtmosphereSkyViewLut.comp`.
- **`ResolveAtmosphereFrameUniforms(Registry&, Vec3 eyeWorldPosition)` is
  exactly the helper Phase 6 should call too** (per this phase's own Step 5
  instruction) — its SHAPE is stable now; only Phase 8 will change its
  sun-direction branch's body. Phase 6 will need `AtmosphereFrameUniforms`
  for its own froxel-volume ray-march, resolved the same way this phase
  resolves it for the Sky-View LUT.
- **`PlanetVisibility()` (new this phase, in `AtmosphereCommon.glsl`) was
  added specifically anticipating Phase 6's own need for an identical
  self-shadow test** — Phase 6 should reuse it directly rather than
  re-deriving/re-transcribing the same ray/sphere self-shadow check a
  second time.
- **Per-view state storage pattern (`std::unordered_map<std::string,
  ViewState>`) is now a real, working precedent** — if Phase 6's own
  aerial-perspective volume turns out to also need per-view state (it
  likely does, for the same Game-View-vs-Scene-View reason this phase's
  Sky-View LUT does), reuse this exact same map-keyed-by-output-name shape
  rather than inventing a different one.
- **Open question explicitly flagged for a future phase to reconsider:**
  `ResolveActiveCameraWorldPosition()` currently lives as a small, local,
  anonymous-namespace helper in `Application.cpp` (see "Deviations" above)
  rather than a `RenderSystem`/`AGENTS.md`-sanctioned shared location. If
  Phase 6/7 also needs "the active camera's current world position" (very
  likely, since the aerial-perspective volume and the eventual Scene View
  wiring both need an eye position), it should either reuse this exact
  function (moving it somewhere more shared/reusable first, e.g. a small
  free function in `RenderSystem.h` alongside
  `ResolveActiveCameraViewProjection()`) or make an explicit, documented
  decision to keep it local/duplicated — this was NOT decided here, since
  it was out of this phase's own explicit Step 3 scope.
- The temporary validation call site in `Application.cpp`/`Application.h`
  (`m_atmosphereLutRenderer`, the `TODO(ATMOSPHERE_PHASE7)` comment) is
  still in place and must stay in place through Phase 6 — only Phase 7
  relocates it into the real, permanent sky background/aerial-perspective
  pass sequence, including finally wiring up the Scene View's own Sky-View
  LUT this phase deliberately left unwired.
