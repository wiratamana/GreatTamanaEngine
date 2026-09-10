# Atmosphere Phase 8 — Completion Report

**Phase:** `ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md`
**Branch:** `feature/atmosphere-scattering-impl`
**Status:** Complete — `gte_core`, `GreatTamanaEngineTests` (including the new Tier-1
`DirectionalLightResolverTests.cpp`, 6/6 passing), and `GreatTamanaEngine` (including both
new/modified shaders recompiling via `glslc` with zero errors) all build cleanly (targeted
incremental build only, per this campaign's own workflow rule). The engine was actually run
(Editor build, Vulkan validation layers enabled by default) and the sun's live effect on the
sky was visually confirmed via `GET /get_texture`/`GET /get_swapchain`.

## What changed

### 1. `src/ECS/Components/DirectionalLight.h` (new) — Step 3.1

Plain-data component mirroring `Camera`'s own shape exactly: `Vec3 color = Vec3::One()`,
`float illuminanceLux = 100000.0f` (a physically-plausible full-daylight default), `bool
active = true`. Direction is deliberately **not** a field — it is derived from the entity's
own `Transform` (`transform.rotation.RotateVector(Vec3::Forward())`), exactly like `Camera`
never stores eye/target/up itself. Got the sign right per the strategy document's own
explicit callout: the entity's forward vector is the direction the light *shines in*;
`AtmosphereFrameUniforms::sunDirection` wants the direction *toward* the sun instead — the
negation — and this is covered by a dedicated non-trivial-rotation regression test (see
below).

### 2. `src/Renderer/Atmosphere/DirectionalLightResolver.h/.cpp` (new) — Step 3.2

`ResolveActiveDirectionalLight(Registry&)` mirrors `RenderSystem::
ResolveActiveCameraViewProjection()`'s exact "first active entity, in `ComponentStorage`
order, fall back to a sane default" pattern. Returns a small `ResolvedDirectionalLight`
struct (`directionTowardSun`, `sunIlluminance` — already lux→shader-scale converted, per the
strategy document's instruction to do unit conversion in the resolution helper, never inside
the component itself). Walks the entity's full **world** transform via
`TransformHierarchy::ComputeWorldTransform()`, so a parented Sun correctly follows its parent
(covered by a dedicated test). Falls back to the *exact* hardcoded placeholder Phase 5
originally used (`Normalize(Vec3(0.70710678f, 0.70710678f, 0.0f))`, color `(1, 0.95, 0.85) *
3.0`) when no active `DirectionalLight` exists — a scene with no Sun entity still renders a
plausible sky, unchanged from before this phase.

The lux→shader-scale conversion uses two named constants
(`kReferenceIlluminanceLux = 100000.0f`, `kReferenceShaderIlluminanceScale = 3.0f`) chosen so
a freshly-spawned, default-valued `DirectionalLight` entity reproduces the exact same
*overall magnitude* as the old placeholder — this is a deliberate, documented deviation from
an exact color match (the new default is neutral white, `Vec3::One()`, rather than the
placeholder's own warm tint) since a real entity's color is now user-tunable in the
Inspector.

**Deviation from the plan's literal file-location wording:** the strategy document said "in
`src/Renderer/Atmosphere/` or `src/Application/AtmospherePassSequence.*`" — this landed in
`src/Renderer/Atmosphere/` as a **new, standalone, Vulkan-header-free file pair**, not bolted
onto `AtmosphereLutRenderer.h` itself, so the Tier-1 test (`tests/Renderer/Atmosphere/
DirectionalLightResolverTests.cpp`) never has to pull in any Vulkan-dependent headers
transitively — mirrors `AtmosphereMath.h`'s own "Vulkan-free despite living under
`Renderer/Atmosphere/`" precedent exactly.

`AtmosphereLutRenderer.cpp`'s `ResolveAtmosphereFrameUniforms()` now calls
`ResolveActiveDirectionalLight(registry)` and assigns its two fields straight into
`AtmosphereFrameUniforms::sunDirection`/`sunIlluminance` — both `// TODO(ATMOSPHERE_PHASE8)`
markers (one in the `.h`, two in the `.cpp`) are gone. The function's signature is unchanged,
exactly as required.

**Tier-1 test** (`tests/Renderer/Atmosphere/DirectionalLightResolverTests.cpp`, added to
`tests/CMakeLists.txt`, 6 tests, all passing): empty-Registry fallback, inactive-light
fallback, a **non-trivial 90° yaw rotation** proving the direction-toward-sun sign is
correct (explicitly requested by the strategy document), color/illuminance-lux → shader-scale
conversion, a **parented** light resolving through its parent's world rotation, and a
zero-lux light producing zero `sunIlluminance`.

### 3. `Game::CreateDirectionalLightEntity()` + Hierarchy/Inspector — Step 3.3

- **`src/Game/Game.h/.cpp`**: new `Entity CreateDirectionalLightEntity()` — no `Renderer&`
  parameter at all (unlike `CreatePrimitiveEntity()`), since a light has no GPU mesh to
  build. Spawns `Transform` (a late-afternoon-ish default rotation,
  `Quat::FromEulerDegrees(45.0f, -30.0f, 0.0f)`) + `DirectionalLight` + an auto-de-duplicated
  `Name` ("Directional Light", "Directional Light (1)", ... via the existing
  `MakeUniqueEntityName()`).
- **`src/Editor/Panels/HierarchyPanel.cpp`**: a new top-level "Create Directional Light"
  right-click menu entry (a plain `MenuItem`, not a submenu — there's only ever this one
  light kind), selecting the freshly spawned entity immediately, same UX as "Create 3D
  Object".
- **`src/Editor/Panels/InspectorPanel.cpp`**: a new "Directional Light" `CollapsingHeader`
  section (shown when the selected entity has the component) directly mirroring the existing
  "Camera" section's layout/style: `ImGui::ColorEdit3("Color", ...)`,
  `ImGui::DragFloat("Illuminance (lux)", ...)`, `ImGui::Checkbox("Active", ...)`.

### 4. `AtmosphereSettings` + Editor "Atmosphere" panel — Step 3.4

- **`src/Renderer/Atmosphere/AtmosphereTypes.h`**: new plain `AtmosphereSettings` struct —
  `Vec3 groundAlbedoTint = Vec3::One()`, `float aerialPerspectiveStrength = 1.0f`, `float
  skyExposure = 12.0f` (matching the old hardcoded constant it replaces). Deliberately kept
  SHORT per the strategy document's own explicit warning against exposing every physical
  constant.
- **`src/Application/Application.h/.cpp`**: `Application` now owns
  `AtmosphereSettings m_atmosphereSettings` (declared right after `m_atmosphereLutRenderer`,
  for the same "the per-frame pass-building lambda needs it alive" reason). Each frame:
  `groundAlbedoTint` is folded into a **mutable** local `atmosphereParameters` (component-wise
  multiply against `AtmosphereParametersGpu::groundAlbedo`) before the shared LUT passes run;
  `skyExposure` is threaded into both `MakeRecordSkyBackgroundCallback()` calls (Game View,
  Scene View); `aerialPerspectiveStrength` is threaded into both
  `AddAtmosphereCompositePass()` calls. No dirty-flag optimization anywhere (unchanged from
  every prior phase).
- **`src/Editor/Panels/AtmospherePanel.h/.cpp`** (new) — a small, **stateless free-function**
  panel (`BuildAtmospherePanel(EditorContext&, AtmosphereSettings&)`), exactly the convention
  `HierarchyPanel`/`InspectorPanel`/`MemoryPanel` already establish (explicitly **not** a
  small stateful class like `ProfilerPanel`/`RenderGraphPanel`/`JobsPanel` — this panel has no
  cross-frame state at all). A color picker + two `DragFloat`s, plus a plain text hint
  pointing at "Hierarchy" → "Create Directional Light" for sun control.
- **`src/Editor/DockLayout.cpp`**: `"Atmosphere"` added to `kAllPanelNames` and docked
  alongside `"Memory"`/`"Profiler"`/`"Render Graph"`/`"Jobs"` along the bottom in
  `BuildDefaultDockLayout()`.
- **`IEditorLayer::BuildUI()`** (`EditorLayer.h`/`NullEditorLayer.cpp`/`ImGuiEditorLayer.cpp`)
  gained a new `AtmosphereSettings& atmosphereSettings` parameter, threaded from
  `Application::Run()`'s existing `m_editorLayer->BuildUI(...)` call site
  (`m_atmosphereSettings`) — `ImGuiEditorLayer::BuildUI()` calls
  `BuildAtmospherePanel(m_ctx, atmosphereSettings)` right after the "Render Graph" panel.
  `NullEditorLayer`'s override is a no-op, unchanged in spirit.
- **Shader/push-constant wiring**:
  - `Shaders/AtmosphereSkyBackground.frag`/`AtmosphereSkyBackgroundRenderer.h/.cpp`: the
    former hardcoded `kSkyExposure = 12.0` constant is now `pc.skyExposure`, a genuine push
    constant field (repurposing a previously-reserved padding float — the struct's total
    size/layout is unchanged, 80 bytes).
  - `Shaders/AtmosphereAerialPerspectiveComposite.comp`/`AtmosphereLutRenderer.h/.cpp`: a new
    `aerialPerspectiveStrengthAndPad` `vec4` push-constant group (96 bytes total now, up from
    80) — `.x` scales the effect: `inScattering *= strength`, `transmittance = mix(1.0,
    transmittance, strength)`, so `1.0` reproduces the original physical blend exactly and
    `0.0` fully disables the effect (pure pass-through of `sceneColor`).

## Visual verification actually performed

Ran the engine twice this session (`run_app_background`, Editor build, Vulkan validation
layers enabled by default):

- **Confirmed the new "Atmosphere" panel exists and is correctly docked**: `GET
  /get_swapchain` shows it tabbed alongside "Memory"/"Profiler"/"Render Graph"/"Jobs"/"Project"
  along the bottom of the Editor UI.
- **Confirmed the sky genuinely reacts live to a `DirectionalLight` entity's `Transform`
  rotation** — see "Deviation" note below for exactly how this was driven, since this tooling
  environment has no mouse/keyboard automation for a native Vulkan window (only HTTP). Three
  `GET /get_texture?texture_name=SceneViewComposited` captures at three different points in
  time (with the Sun's yaw continuously rotating underneath), several seconds apart, show
  three genuinely different sky appearances: a bright sun glow near the top-left-of-center,
  then a dimmer/shifted glow a few seconds later, then a bright glow reappearing on the
  *right* edge of the frame a few seconds after that — conclusively proving the rendered sky
  is being driven by the Sun entity's live rotation, not a static/cached value. The
  `AtmosphereSkyViewLut_SceneView` LUT itself was captured at the same two of those three
  instants and shows the corresponding gradient/highlight shift.
- `GET /list_textures` continued to show every expected texture name updating normally
  throughout (no regression in the existing Named Texture Capture machinery).

### Deviation: how the "rotate in the Inspector" step was actually performed

This tooling environment has **no GUI mouse/keyboard automation for a native Win32/Vulkan
window** (no Playwright/Avalonia-style control exists for `GreatTamanaEngine.exe`, and the
engine's own HTTP surface has no endpoint to create a `DirectionalLight` or edit an arbitrary
component's field over the network — only `POST /instantiate_primitive`/`POST
/delete_entity` exist, and extending that surface is explicitly out of this phase's scope).
To still produce a genuine, live, engine-rendered proof that a `DirectionalLight` entity's
`Transform` drives the atmosphere, a **temporary, clearly-marked** block was added to
`Game::Update()` for the duration of this verification only: on first call it spawned one
`DirectionalLight` entity via `CreateDirectionalLightEntity()` and then continuously rotated
its yaw over time (30°/second). The engine was rebuilt, run, and captured three times a few
seconds apart (see above), then **the temporary block was fully removed** and `Game.cpp`
rebuilt again back to its exact original, permanent form — confirmed via `git status`/`git
diff` showing zero changes to `Game.cpp` relative to the rest of this phase's real diff. This
is functionally equivalent to a human dragging the Sun's rotation gizmo/Inspector field frame
to frame (the resolver has no way to distinguish "rotated by a human drag" from "rotated by
code" — both go through the exact same `Transform.rotation` → `ResolveActiveDirectionalLight()`
path), and is flagged here explicitly rather than silently used.

## Deviations from the plan (and why)

1. **`ResolveActiveDirectionalLight()` lives in a brand-new, standalone
   `DirectionalLightResolver.h/.cpp` pair**, not folded into `AtmosphereLutRenderer.h/.cpp`
   itself — see "What changed" §2 for the Tier-1-testability reasoning.
2. **The lux→shader-scale conversion constants
   (`kReferenceIlluminanceLux`/`kReferenceShaderIlluminanceScale`) are a pragmatic, documented
   choice**, not derived from any real photometric formula (`pl-sky`'s own reference never
   used lux at all) — chosen purely so the new default reproduces the old placeholder's
   overall visual magnitude.
3. **The "rotate in the Inspector" verification step used a temporary, clearly-marked,
   fully-reverted code-level rotation instead of manual GUI interaction** — see the dedicated
   "Deviation" note above for the full reasoning; this tooling environment has no way to
   drive a native window's mouse/keyboard directly.
4. **`aerialPerspectiveStrength`/`skyExposure` were wired as genuine, new push-constant
   fields (not `AtmosphereSettings` fields silently ignored)** — the strategy document only
   asked for the Editor panel to exist and "wire its fields into the per-frame pass-building
   code Phase 7 already established"; this required extending two shaders' own
   `layout(push_constant)` blocks (repurposing reserved padding for `skyExposure`, adding a
   new `vec4` group for `aerialPerspectiveStrength`) — a small, real, additive shader change,
   not just a C++-side plumbing change.
5. No other deviations — `DirectionalLight`'s field shape/defaults, the "first active wins"
   resolution rule, `CreateDirectionalLightEntity()`'s signature shape, the Hierarchy/
   Inspector UI conventions, and `AtmosphereSettings`'s short field list were all followed
   exactly as the strategy document specified.

## What was explicitly NOT done (per Step 4)

- No point/spot lights, no support for more than one simultaneously-active `DirectionalLight`
  beyond the existing "first active wins" rule.
- No scene (de)serialization of `DirectionalLight`/`AtmosphereSettings` — a spawned Sun
  entity/tuned `AtmosphereSettings` exist only for the current running session, exactly like
  `Camera` entities today.
- No animated/keyframed sun movement as a real, permanent engine feature — the TEMPORARY
  verification-only rotation described above was written, exercised, and then **completely
  removed** before this commit; it is not part of the shipped diff.
- No "sync sun direction to the `EditorCamera`'s own direction" shortcut, no multi-sun
  creation UI.
- The release-build/both-panels-hidden direct-to-swapchain path (flagged as an open question
  by Phase 7) was not touched by this phase either — still an open question for Phase 9.

## Build/run verification actually performed

- `cmake --build build --target gte_core` — compiled cleanly.
- `cmake --build build --target GreatTamanaEngineTests` — compiled/linked cleanly; the new
  `DirectionalLightResolverTests.cpp` (6 tests) run directly via
  `GreatTamanaEngineTests.exe --gtest_filter=DirectionalLightResolverTest.*` — all 6 passing.
  Per this campaign's own "fast compile check only" workflow rule, the full `ctest`
  regression suite was **not** run (reserved for Phase 9).
- `cmake --build build --target GreatTamanaEngine` — compiled, both new/modified shaders
  (`AtmosphereSkyBackground.frag`, `AtmosphereAerialPerspectiveComposite.comp`) recompiled via
  `glslc` with zero errors, and linked cleanly.
- Ran the engine (`run_app_background`, Editor build, Vulkan validation layers enabled by
  default) three times total this session: once with the temporary verification rotation
  hook (three captures over ~13 seconds, see above), once more after fully reverting that
  hook and rebuilding, purely to confirm the final, permanent diff still runs cleanly and the
  new "Atmosphere" dock tab is present (`GET /get_swapchain`). The engine stayed responsive
  to every HTTP request across all runs (no crash, no hang).
- Per this campaign's own workflow rule, **no full clean build and no full `ctest` regression
  run** were performed (reserved for Phase 9 only).

## Open questions / notes for Phase 9

- **No network endpoint exists to create/edit ECS entities beyond
  `instantiate_primitive`/`delete_entity`** — this made a fully "hands-off," code-free live
  verification of the Editor's own Hierarchy/Inspector UI impossible in this tooling
  environment; Phase 9 (or a future network campaign) could consider a `set_transform`/
  generic-component-edit endpoint (already anticipated as a natural extension in
  `AGENTS.md`'s "Networking" section) if this kind of verification needs to be repeatable
  without a temporary code hook in the future.
- **The release-build/both-panels-hidden direct-to-swapchain path still does not get the
  atmosphere effect** (a gap Phase 7 already flagged) — still open for Phase 9 to decide on.
- **`AtmosphereSettings`'s field list is intentionally short** (ground-albedo tint, aerial-
  perspective strength, sky exposure) — Phase 9 should resist the temptation to grow it
  without a genuine, stated need, per the strategy document's own explicit warning.
- **Scene serialization of `DirectionalLight`/`AtmosphereSettings` remains explicitly
  out of scope** — a spawned Sun and tuned Atmosphere settings do not survive a Save/Load
  cycle, same as `Camera` today; a future scene-serialization follow-up could close this if
  ever prioritized.
