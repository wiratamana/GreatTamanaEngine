# ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md

### Child document 8 of 9 — see `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign map.
### Depends on: Phase 7 fully landed (the real, permanent per-frame atmosphere sequence exists and is visible) — this phase replaces its hardcoded sun-direction placeholder and adds the first Editor-facing tuning surface.

## Step 1: The Goal

Make the sun direction a real, first-class, ECS-authored thing (Locked
Design Decision 1, Phase 0) instead of the hardcoded placeholder every prior
phase used, and give a scene author ONE place to tune the handful of
non-spatial atmosphere parameters that don't belong on any entity (turbidity/
ground-albedo-style knobs, an overall exposure/intensity multiplier) — a new
`AtmosphereSettings` global, edited via a small new Editor panel.

## Step 2: The Situation

- `src/ECS/Components/` today has exactly `Transform.h`, `MeshRenderer.h`,
  `Camera.h`, `Name.h`, `SkeletalAnimator.h` — no light of any kind. `Camera`
  (read it in full first) is the closest, best precedent to copy for
  `DirectionalLight`'s own shape: plain data, a couple of small pure-math
  helper methods, an `active` bool, and `RenderSystem::
  ResolveActiveCameraViewProjection()`'s "pick the first active one, ignore
  the rest, fall back to a sane default if none exists" resolution pattern.
- `RenderSystem`/`MeshInstantiationSystem`/`AnimationSystem`/`PhysicsSystem`
  are the only classes allowed to depend on both ECS and Renderer (see
  `AGENTS.md`'s "Entity-Component-System" section) — resolving
  `DirectionalLight` into `AtmosphereFrameUniforms::sunDirection` each frame
  is exactly this kind of ECS-to-Renderer-adjacent bridging work; it should
  live alongside Phase 5/7's existing `AtmosphereFrameUniforms`-resolution
  helper (in `src/Renderer/Atmosphere/` or `src/Application/
  AtmospherePassSequence.*`, wherever Phase 7 actually put it), taking a
  `Registry&` parameter, NOT as a new method bolted onto `RenderSystem`
  itself (this campaign's atmosphere code is deliberately its own module, not
  an extension of `RenderSystem`'s existing responsibilities).
- The Editor's Hierarchy panel already has a "Create 3D Object" right-click
  menu (`Panels/HierarchyPanel.cpp`) spawning primitives via
  `Game::CreatePrimitiveEntity()` — a parallel "Create Light > Directional
  Light" entry (or a top-level "Create Directional Light" entry, whichever
  reads more naturally next to the existing menu's real structure — check it
  directly) is the natural, discoverable way to let a user actually add a Sun
  entity, mirroring that exact menu-driven spawn convention.
- `Game` currently has no `CreateDirectionalLightEntity()`-shaped method —
  add one mirroring `Game::CreatePrimitiveEntity()`'s own shape (spawns a
  `Transform` + `DirectionalLight` entity, gives it a sensible default
  rotation looking somewhat downward, like a late-afternoon sun, and a
  default `Name` of `"Directional Light"`, auto-de-duplicated the same
  Unity-style way `POST /instantiate_primitive` already does for primitives).
- There is no existing global-settings-panel precedent that is a PURE
  non-ECS singleton edited live (the closest are `EditorContext` itself,
  which is Editor-only and not gameplay-visible, and the Editor's various
  per-panel data classes) — `AtmosphereSettings` should be a small,
  plain-data struct owned by whichever class ends up owning the atmosphere
  pass sequence (Phase 7's new file), NOT stored inside `EditorContext`
  (`EditorContext` is Editor-UI-state only — visibility flags, selection,
  dock state — never gameplay/rendering DATA; see `AGENTS.md`'s own "Editor
  Module Structure" section for this exact boundary). The Editor's new
  "Atmosphere" panel reads/writes it by reference, the same way
  `Panels/InspectorPanel.cpp` reads/writes a selected entity's `Camera`
  component by reference, just without an ECS entity backing this one.

## Step 3: The Plan

### 3.1 — `src/ECS/Components/DirectionalLight.h`

```
struct DirectionalLight {
    Vec3 color = Vec3::One();       // linear color, pre-intensity
    float illuminanceLux = 100000.0f; // physically-plausible daylight default
    bool active = true;
});
```

(Adjust exact field names/defaults once Phase 1's reference notes pin down
what unit/scale `pl-sky` itself uses for "sun illuminance" — keep this
component's OWN fields human-tunable/physically-named, and do any unit
conversion into `AtmosphereFrameUniforms::sunIlluminance` inside the
resolution helper below, never inside the component itself, mirroring
`Camera::fovYDegrees`'s own "human-facing degrees, converted to radians only
at the point of use" convention.) Direction is deliberately NOT a field on
this component — exactly like `Camera`, it is derived from the entity's own
`Transform` (`transform.rotation.RotateVector(Vec3::Forward())` — the sun
shines in the direction this entity's Transform is facing; the direction
TOWARD the sun, which `AtmosphereFrameUniforms::sunDirection` actually wants,
is simply the negation of that forward vector — get this sign right, and add
a Tier-1 test for whichever small pure resolution function computes it,
covering at least one non-trivial rotation).

### 3.2 — Resolution helper

- `DirectionalLight* ResolveActiveDirectionalLight(Registry&)` (or an
  equivalent returning both the component and its resolved world direction,
  via `TransformHierarchy::ComputeWorldMatrix()` for a parented sun) — mirrors
  `RenderSystem::ResolveActiveCameraViewProjection()`'s exact "first active
  one, `ComponentStorage` order, ignore the rest" pattern; falls back to the
  SAME hardcoded placeholder direction Phase 5 used when no
  `DirectionalLight` entity exists yet (never a crash/exception — a scene
  with no sun entity must still render a plausible-looking sky).
- Tier-1 test it exactly like `RenderSystemTests.cpp` tests
  `ResolveActiveCameraViewProjection()` — a `Registry&`-only test, no
  Renderer/GPU involved.
- Update Phase 5/7's `AtmosphereFrameUniforms`-resolution call site to call
  this instead of the hardcoded placeholder — delete the
  `// TODO(ATMOSPHERE_PHASE8)` comment left there.

### 3.3 — `Game::CreateDirectionalLightEntity(...)` + Hierarchy menu entry

- Mirrors `Game::CreatePrimitiveEntity()`'s signature/behavior shape
  (world position + optional parent, returns the new `Entity`, auto-
  de-duplicated `Name`).
- `Panels/HierarchyPanel.cpp`'s right-click menu gains the new entry,
  spawning and immediately selecting the new entity — same UX convention
  "Create 3D Object" already establishes.
- `Panels/InspectorPanel.cpp` gains a `DirectionalLight` section (shown when
  the selected entity has one) — color picker, illuminance field, active
  checkbox — mirroring its existing `Camera` section's own layout/style
  exactly (read that section's real code first and copy its conventions,
  don't invent a new widget style).

### 3.4 — `AtmosphereSettings` + new Editor "Atmosphere" panel

- A small plain struct (wherever Phase 7's atmosphere-pass-sequence class
  lives) carrying whatever handful of tunable, non-spatial knobs Phase 1's
  reference notes suggest are actually worth exposing (a ground-albedo
  tint, an overall aerial-perspective strength/distance-scale multiplier, an
  exposure multiplier for the sky background) — keep this list SHORT and
  driven by what's genuinely useful to tune live, not every single field of
  `AtmosphereParametersGpu` (most of those, e.g. exact Rayleigh scattering
  coefficients, are physical constants nobody needs to live-tune — leave
  them as `AtmosphereParameters.cpp`'s fixed defaults from Phase 1).
- `src/Editor/Panels/AtmospherePanel.h/.cpp` — a new, small, stateless
  free-function panel (`BuildAtmospherePanel(EditorContext&,
  AtmosphereSettings&)`), following the exact "stateless free function taking
  `EditorContext&`" convention `AGENTS.md`'s "Editor Module Structure" section
  documents for `HierarchyPanel`/`InspectorPanel`/etc. (NOT a small stateful
  class like `ProfilerPanel`/`BoneViewerWindow` — this panel has no
  cross-frame state of its own to justify that heavier pattern). Dock it
  alongside "Memory"/"Profiler"/"Render Graph"/"Project" along the bottom
  (`DockLayout.cpp`).
- Wire `AtmosphereSettings`'s fields into `AtmosphereParametersGpu`/the
  composite pass's push constants at the exact point Phase 7's sequence
  already builds/uploads them each frame — no new per-frame data path is
  needed beyond threading these extra scalars through.

## Step 4: What We Will NOT Do

- No point lights, no spot lights, no more than one simultaneous
  `DirectionalLight` treated as active (Step 2's "first active" rule) — see
  Phase 0's own explicit refusal.
- No scene (de)serialization of `DirectionalLight`/`AtmosphereSettings` — see
  Phase 0's own explicit refusal; a spawned Sun entity/tuned
  `AtmosphereSettings` exist only for the current running session, same as
  today's `Camera` entities.
- No animated/keyframed sun movement, no "sync sun direction to the
  `EditorCamera`'s own direction" shortcut, no UI for creating MULTIPLE
  suns with a warning about which one wins — just the plain, single
  right-click "Create Directional Light" entry and the existing
  first-active-wins resolution rule, exactly like `Camera`.

## Step 5: Their Role

- Verify visually, via `gte_send_request` against the Game/Scene View (or
  `/get_texture` for the Sky-View LUT specifically), that rotating a spawned
  `DirectionalLight` entity's `Transform` in the Inspector actually changes
  the rendered sky/aerial-perspective look live, frame to frame — this is the
  concrete, end-user-visible proof this whole campaign's core promise (a
  scene author can control the sun and see the atmosphere react) is real,
  not just individually-correct passes that never got connected to anything
  a person can turn a dial on.
- Keep `AtmosphereSettings`'s field list small and genuinely useful — resist
  the temptation to expose every physical constant just because it exists;
  Phase 1's `AtmosphereParameters.cpp` defaults are the right home for
  anything that isn't worth a human tuning live.
