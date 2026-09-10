# ATMOSPHERE_PHASE5_SKYVIEW_LUT_v1.md

### Child document 5 of 9 — see `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign map.
### Depends on: Phase 3 (`"AtmosphereTransmittanceLut"`) and Phase 4 (`"AtmosphereMultiScatteringLut"`) both existing and correct.

## Step 1: The Goal

Add the third atmosphere compute pass, and the FIRST one that is genuinely
**per-frame** (not "compute once, reuse forever until parameters change"):
the **Sky-View LUT** — a 2D texture parameterized by view direction (azimuth
+ elevation, relative to the sun) that lets the eventual sky-background draw
(Phase 7) look up a plausible sky color for any background pixel with a
SINGLE cheap texture sample instead of ray-marching the atmosphere per pixel
on screen. This phase also introduces the campaign's first genuinely
per-frame data: `AtmosphereFrameUniforms` (camera height, sun direction),
which Phase 6 also needs.

## Step 2: The Situation

- Unlike Phases 3-4, this LUT's content depends on where the camera currently
  is (its height above the planet) and where the sun currently is (a
  direction, not yet wired to a real ECS entity until Phase 8) — it MUST be
  recomputed every frame, no dirty-flag question to consider at all here.
- `AtmosphereFrameUniforms` was already defined as a plain struct in Phase 1
  (`AtmosphereTypes.h`) but has never been populated from real engine state
  yet — this phase is the first consumer, and must decide EXACTLY where the
  camera position/sun direction come from for now: the camera position
  should come from whichever camera the frame is actually rendering through
  (`RenderSystem::ResolveActiveCameraViewProjection()`'s same active-`Camera`
  resolution, or, for the Scene View specifically, the Editor's own
  `EditorCamera` — see `AGENTS.md`, "Editor Module Structure" — the SAME two
  sources `Application::Run()` already threads a view/projection matrix from
  for Game vs. Scene). The sun direction, until Phase 8 lands the real
  `DirectionalLight` component, should default to a FIXED, hardcoded
  plausible direction (e.g. a 45-degree-elevation direction) — clearly
  commented `// TODO(ATMOSPHERE_PHASE8): replace with real DirectionalLight
  resolution` — so this phase is not blocked waiting for Phase 8.
- The Game View and Scene View render through DIFFERENT cameras/aspect
  ratios in the SAME frame (see `AGENTS.md`'s Camera section) — this means
  the Sky-View LUT is logically PER-VIEW, not a single shared one. Confirm in
  this phase's own completion report whether it computes ONE Sky-View LUT
  per view per frame (two computations, two textures, e.g.
  `"AtmosphereSkyViewLut_GameView"`/`"AtmosphereSkyViewLut_SceneView"`) or a
  single shared one keyed only on sun direction with camera-height baked in
  as a per-pixel ray-march input at DRAW time instead of at LUT-build time —
  the former (one LUT per view) is the simpler, lower-risk default and should
  be preferred unless Phase 1's reference notes show `pl-sky` doing
  something meaningfully different that changes this trade-off.

## Step 3: The Plan

- Extend `AtmosphereCommon.glsl` with the Sky-View LUT's own UV<->(azimuth,
  elevation) parameterization (per `ATMOSPHERE_REFERENCE_NOTES.md` — this one
  is typically split into a non-linear elevation remap that spends more
  texels near the horizon, and a linear/near-linear azimuth remap; transcribe
  exactly).
- `src/Shaders/AtmosphereSkyViewLut.comp` — bindings: `AtmosphereParametersGpu`
  uniform buffer, `AtmosphereFrameUniforms` uniform buffer (NEW — a second,
  small, host-writable uniform buffer, written fresh every frame, unlike the
  session-stable `AtmosphereParametersGpu` one), `sampler2D transmittanceLut`,
  `sampler2D multiScatteringLut`, `image2D destinationImage`. For each texel:
  decode `(azimuth, elevation)` relative to the sun, ray-march from the
  camera's current height in that direction toward/away from the planet
  (per the reference notes' exact sample count), accumulating single
  scattering (sampling `transmittanceLut` per step) plus the
  `multiScatteringLut` contribution, `imageStore()` the result.
- `AtmosphereLutRenderer` gains
  `TextureHandle AddSkyViewLutPass(RenderGraphBuilder&, Renderer&, const
  AtmosphereParametersGpu&, const AtmosphereFrameUniforms&, TextureHandle
  transmittanceLutHandle, TextureHandle multiScatteringLutHandle, const char*
  outputTextureName)` — the `outputTextureName` parameter is what lets the
  SAME method serve both the Game View and Scene View call sites with two
  distinct registered texture names (per Step 2's own per-view decision),
  without duplicating the method itself. Internally this needs its OWN
  `AtmosphereFrameUniforms` uniform buffer instance PER VIEW if two are
  computed per frame — do not let the Scene View's camera height overwrite
  the Game View's mid-frame; `AtmosphereLutRenderer` should own two small
  buffer+descriptor-set instances (or a tiny fixed-size array indexed by
  view) rather than one shared mutable one.
- A small, explicit helper (in `AtmosphereLutRenderer.cpp` or a sibling free
  function) resolves `AtmosphereFrameUniforms` for a given view: given a
  `Registry&` (for the eventual real `DirectionalLight`/`Camera` lookup) and
  a view/eye world position, returns a populated `AtmosphereFrameUniforms`.
  Keep this function's SHAPE stable now even though its sun-direction branch
  is still the Step 2 hardcoded placeholder — Phase 8 will only need to
  replace the body of ONE branch inside it, not change its signature or every
  call site.
- Extend the running temporary validation call site (still marked
  `// TODO(ATMOSPHERE_PHASE7)`) to call `AddSkyViewLutPass()` once for
  whichever single view is easiest to exercise right now (Game View is
  simplest, since it doesn't need `EditorCamera` wiring) and capture it via
  `/get_texture?texture_name=<the Game View's Sky-View LUT name>` — expect a
  recognizable sky-gradient shape (bright disk/glow near the hardcoded sun
  direction, darker toward the anti-solar point, a visible horizon band).

## Step 4: What We Will NOT Do

- No wiring to the Scene View's real `EditorCamera` position yet if that
  turns out to be nontrivial to reach from wherever this pass is being added
  standalone — it is fully acceptable for THIS phase to only prove the Game
  View path end-to-end and leave a `// TODO(ATMOSPHERE_PHASE7)` for wiring
  the Scene View's own call, since Phase 7 is what permanently relocates and
  finalizes every call site into the real per-frame sequence anyway.
- No real `DirectionalLight` ECS lookup yet (Phase 8) — the hardcoded sun
  direction placeholder is deliberate and expected here.
- No attempt to unify the Sky-View LUT computation with the aerial-perspective
  volume's own ray-marching code (Phase 6) even though they are
  mathematically related — keep them as two separate shaders/methods for now,
  matching how the reference implementation itself keeps them separate
  passes; a shared-helper refactor is only worth doing later if real
  duplication pain shows up, not preemptively.

## Step 5: Their Role

- This phase's `AtmosphereFrameUniforms`-resolution helper is exactly what
  Phase 6 will also call (for the froxel volume) and what Phase 8 will
  finally correct the sun-direction branch of — write it once, cleanly,
  here, rather than duplicating a second "resolve the frame uniforms" helper
  in Phase 6.
- If the per-view-vs-shared LUT decision (Step 2) turns out differently than
  this document's own default recommendation once you're looking at the real
  `Application.cpp`/`EditorCamera` code, that's fine — just document the
  actual decision and reasoning clearly in this phase's completion report,
  since Phase 7's wiring plan is written assuming "one Sky-View LUT per view"
  and would need a small, explicit adjustment note if that assumption turns
  out to be wrong.
