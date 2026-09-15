# GreatTamanaEngine

A raw game engine built from scratch.

## Documentation

This README is intentionally thin. Full architecture detail, the complete
project changelog, and contributor conventions all live under
[`docs/`](docs/README.md) — start at **[docs/README.md](docs/README.md)**.

## Goal

The plan is to develop a raw game engine from scratch, with the very foundation
built on **SDL3** (the new generation after SDL2) for window and event handling.

## Architecture

The very first architecture layers the engine like this:

```
SDL -> Application -> Window and Renderer -> Game
```

- **Application** is the only layer that knows about SDL directly. It owns the
  main loop and is responsible for initializing/shutting down SDL.
- **Window** and **Renderer** are custom objects that act as an abstraction
  layer on top of SDL. Other layers (like Game) interact with these custom
  objects instead of touching SDL directly.
- **Game** sits on top of Window and Renderer, and has no direct knowledge of
  SDL either.

At this stage, Window and Renderer will still internally depend on SDL
objects — that's okay for now. The abstraction can be tightened later as the
engine evolves.

Full detail on each of the six subsystems below lives under
[docs/architecture/](docs/architecture/) — see
**[docs/README.md](docs/README.md)** for the full documentation index.

### Event handling

SDL's raw event stream never reaches `Game` (or anything else past
`Application`) directly — every frame, `Application::Run()` polls SDL and
turns each event into the engine's own `gte::Event` vocabulary
(`EventTranslator`), which then feeds both `InputState` (continuous polling)
and `Game::OnEvent()` (discrete reactions).

Full detail: [docs/architecture/event-handling.md](docs/architecture/event-handling.md).

### Math

`src/Math/` (`Vec2`/`Vec3`/`Vec4`/`Mat4`/`Quat`) is a from-scratch math
library — no GLM dependency, the same "own the core data model" philosophy
as the hand-rolled ECS.

Full detail: [docs/architecture/math.md](docs/architecture/math.md).

### Rendering

`Renderer` owns a real Vulkan pipeline built on small RAII wrappers under
`src/Renderer/Vulkan/`, using dynamic rendering (no `VkRenderPass`/
`VkFramebuffer`), with `RenderOffscreen()`/`CreateRenderTexture()` powering
the Editor's "Game"/"Scene" panels and depth-tested, indexed, textured mesh
rendering.

Full detail: [docs/architecture/rendering.md](docs/architecture/rendering.md).

### Entity-Component-System (ECS)

The engine's Scene/World data model lives under `src/ECS/`: `Entity`/
`EntityManager`/`ComponentStorage<T>`/`Registry`, hand-rolled rather than a
third-party library, with `Transform`'s real parent/child hierarchy resolved
by `TransformHierarchy.h` and only `RenderSystem`/`MeshInstantiationSystem`/
`AnimationSystem` allowed to depend on both ECS and `Renderer`.

Full detail: [docs/architecture/ecs.md](docs/architecture/ecs.md).

### Asset Pipeline

`src/Assets/` implements this engine's unified binary asset container format,
`*.gta` ("Great Tamana Asset"), an `AssetDatabase` registry, and import
pipelines for PNG/JPG (→ KTX2 texture), MikuMikuDance `.pmx` (→ Mesh, with
bones/morphs/physics data and Guid-referenced material textures), and `.vmd`
motion (→ Animation).

Full detail: [docs/architecture/asset-pipeline.md](docs/architecture/asset-pipeline.md).

### Editor / Debug UI

An optional in-engine Editor module lives under `src/Editor/`, gated by
`GTE_ENABLE_EDITOR` — Dear ImGui docking, Hierarchy/Inspector/Scene/Game
panels, a transform gizmo, Memory/Profiler/Render Graph/Project panels, and
asset preview/Bone Viewer tooling, all behind the `IEditorLayer` interface so
`Game` never depends on the Editor in either direction.

Full detail: [docs/architecture/editor-debug-ui.md](docs/architecture/editor-debug-ui.md).

## Building

See **[BUILDING.md](BUILDING.md)** for prerequisites and build instructions.

## Testing

See **[TESTING.md](TESTING.md)** for how to build and run the test suite.

## Status

Early foundation stage, but past the basic-scaffolding phase for several
pieces. This section keeps only the most recent entries inline — see
**[docs/CHANGELOG.md](docs/CHANGELOG.md)** for the complete, reverse-
chronological project history from the very first triangle demo onward.

- **The Editor now has a genuine Unity-style Pause/Step control, backed by
  a brand-new, dedicated, explicit `Time` class** (`frame-debugger-1`
  campaign, five phases -
  `task_manager/frame-debugger-1/PHASE0_MASTER_STRATEGY.md`) - a small
  Pause/Resume + Step toolbar (`src/Editor/PlaybackControls.h/.cpp`) drives
  a new `gte::Time`/`gte::EngineContext` (`src/Core/Time.h/.cpp`,
  `src/Core/EngineContext.h`) that `Application` advances once per frame
  and `Game::Update()` reads to skip Animation/Physics/GPU-skinning work
  entirely on a frozen frame - rendering, the Editor UI, and the
  independently-orbitable Scene-view camera all keep working normally
  while paused, and "Step" advances by exactly one deterministic 1/60s
  tick. Resuming from an arbitrarily long pause is clamped to a single
  ordinary-sized simulation step rather than replaying the entire elapsed
  real-world gap. Verified with a full clean build (both
  `GTE_ENABLE_EDITOR=ON` and `=OFF`), a full `ctest` regression pass, and a
  live runtime smoke test.

- **The Aerial Perspective haze is now actually VISIBLE at this engine's real
  scene scale, its own tuning knobs are live Editor sliders instead of
  hardcoded shader constants, and its froxel volume has a genuinely useful
  live visual + numeric debugging path** (`atmosphere-scattering-2` campaign,
  six phases - `task_manager/atmosphere-scattering-2/PHASE0_MASTER_STRATEGY.md`,
  `AERIAL_PERSPECTIVE_CAMPAIGN_COMPLETION_REPORT.md`). The original
  `atmosphere-scattering-1` pipeline was confirmed logically correct
  end-to-end, but its fixed 10km froxel far-plane and real-Earth-scale
  scattering coefficients made the effect ~2-3 orders of magnitude too faint
  to see at the few-meters-to-few-hundred-meters distances this engine's real
  content actually lives at - a genuine SCALE MISMATCH, not a bug (see
  `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`, kept as permanent historical
  record). `aerialPerspectiveMaxDistanceKm`/`aerialPerspectiveDepthExponent`/
  `aerialPerspectiveSamplesPerSlice`/`aerialPerspectiveScatteringExaggeration`
  are now real `AtmosphereSettings` fields with live sliders in the "Atmosphere"
  panel (Phase 1), the max distance default shrank from 10km to **0.5km** and a
  new scattering-exaggeration multiplier shipped at **30.0x** (Phase 3) -
  applied strictly LOCALLY inside the aerial volume's own shader, never
  touching the shared `AtmosphereMath.h`/`AtmosphereCommon.glsl` oracle the
  Sky-View/Transmittance/Multi-Scattering LUTs also rely on - plus two smaller,
  independently-confirmed composite-pass precision fixes (half-texel Z-bias,
  first-slice fade-in blend, Phase 2). The existing `GET /get_texture`
  volume-preview raymarch (`network-impl-6`) gained a second, atmosphere-aware
  interpretation mode auto-selected by volume name (Phase 4), turning what used
  to render as a flat, uninformative dark box into a legible spatial gradient,
  and a new numeric CPU-readback inspection tool
  (`src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp`, an "Inspect
  Aerial Perspective LUT" button in the "Atmosphere" panel, Phase 5) reports
  live min/max/mean transmittance/in-scattering plus a "likely visible"
  heuristic. Verified with a full clean build, a full `ctest` regression pass,
  and a live runtime smoke test confirming the blueish haze is now clearly,
  smoothly visible on far-distance test geometry relative to near geometry -
  see `AERIAL_PERSPECTIVE_CAMPAIGN_COMPLETION_REPORT.md` for the full
  six-phase writeup and final verification snapshot.
- **The embedded HTTP server's `GET /get_texture`/`GET /list_textures` now
  understand live, GPU-resident 3D (volume) textures, not just 2D ones**
  (`network-impl-6` campaign,
  `task_manager/network-impl-6/PHASE0_MASTER_STRATEGY.md`) - the same class
  of capability Unity's Editor gives you when it draws a raymarched
  "smoke cloud"-style preview thumbnail for a `Texture3D` asset in the
  Inspector, except here the client is an LLM/AI agent talking to
  `GET /get_texture` over loopback HTTP, not a human looking at an Editor
  panel. A new, pure, Tier-1-tested data model,
  `gte::rg::RenderGraphDebugVolumeTextureRegistry`
  (`src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.h/.cpp`),
  mirrors the existing 2D `RenderGraphDebugTextureRegistry`
  (`network-impl-4` campaign) and is auto-populated by
  `gte::rg::RenderGraph::ExecuteCompiledGraph()` every frame, with zero
  opt-in from whichever pass declared the volume texture (today: the
  Atmosphere feature's own two aerial-perspective froxel volumes - see the
  Atmosphere Scattering entry immediately above - this campaign's own first
  real, verified consumer, but the mechanism works generically for any future
  `VolumeTextureHandle`). A requested `texture_name` that resolves to a
  volume now renders a fresh, on-demand, single-fixed-camera, front-to-back
  alpha-composite raymarch (`gte::VolumeTexturePreviewRenderer`, driven by a
  new compute shader, `Shaders/VolumeTexturePreview.comp`, and its own pure
  CPU camera/ray-box math oracle, `VolumeTexturePreviewMath.h` - the same
  "CPU oracle is right by definition" discipline the Atmosphere Scattering
  campaign's own `AtmosphereMath.h` already established) into a persistent
  256x256 RGBA8 thumbnail, then rejoins the exact same PNG-encode/
  `?format=`/`Accept:` negotiation path every existing 2D capture already
  uses - no new endpoint, no new query parameter, no new cross-thread bridge
  type. `GET /list_textures` entries now also carry a
  `"kind":"texture2d"|"texture3d"` field plus a `"depth"` field (a volume's
  Z/texel-count extent), so an LLM/AI agent caller can discover which
  `texture_name`s are volumes worth requesting with no prior knowledge of
  the engine's internal naming convention. Verified end-to-end against a
  live running engine (`GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
  returning a real, visually plausible non-cubic-box PNG thumbnail;
  `channel=depth` against a volume
  name correctly returning `409`; every pre-existing 2D-texture capture
  request behaving byte-for-byte unchanged) and a full clean build plus full
  `ctest` regression pass. See `AGENTS.md`'s "Named Texture Capture"/
  "Atmosphere Scattering" sections for every load-bearing rule this feature
  depends on, and each phase's own `PHASEn_COMPLETION_REPORT.md` for the
  full six-phase campaign writeup.
- **The Aerial Perspective volume's own HTTP preview thumbnail (above) had its
  camera framing corrected in a follow-up campaign, `atmosphere-scattering-3`**
  (`task_manager/atmosphere-scattering-3/`) - the generic volume-preview
  camera/proxy-box math is correct for an ordinary spatial volume, but it
  flattened this particular LUT's one meaningful (near/far) axis into an
  unreadably thin sliver, since its three axes aren't comparable units; a
  dedicated camera, and then a literal tapering frustum-shaped raymarch proxy
  (auto-selected purely by texture name, same convention as the
  color-interpretation fix above, with zero new HTTP parameter), now make the
  preview actually widen away from the camera with a legible near/far haze
  gradient - verified with a full clean build and full `ctest` regression
  pass. See `task_manager/atmosphere-scattering-3/CAMPAIGN_COMPLETION_REPORT.md`
  for the full five-phase writeup.
- **A follow-up campaign, `atmosphere-scattering-4`, fixed a confirmed bug
  where Aerial Perspective was double-applied to empty sky pixels** (no
  opaque geometry drawn into them that frame) — the Aerial Perspective
  Composite pass used to run its full haze blend unconditionally, re-fogging
  a sky pixel the Sky Background pass had already finished, correctly,
  earlier in the same frame (confirmed by toggling `aerialPerspectiveStrength`
  visibly changing the sky itself, which should never happen). The fix is a
  small early pass-through branch in
  `Shaders/AtmosphereAerialPerspectiveComposite.comp`, mirroring a new,
  dedicated, Tier-1-tested CPU oracle
  (`src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h/.cpp`)
  built and tested BEFORE the shader was touched — real opaque geometry
  (a mesh, the reference grid) still fogs progressively with distance exactly
  as before, completely unaffected. A new permanent Editor diagnostic, the
  "Validate Aerial Perspective Sky Purity" button in the "Atmosphere" panel
  (`src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h/.cpp`),
  numerically re-confirms every sky pixel's post-composite color exactly
  matches its pre-composite color, on demand — catching a future regression
  of this exact bug class without relying on a human eyeballing a screenshot.
  Verified with a full clean build, a full `ctest` regression pass, and a
  live runtime smoke test. See
  `task_manager/atmosphere-scattering-4/CAMPAIGN_COMPLETION_REPORT.md` for
  the full four-phase writeup.

- **The embedded HTTP server can now bring a specific Editor panel/tab to
  the front on command, and list every known panel name** (`network-impl-7`
  campaign, `task_manager/network-impl-7/PHASE0_MASTER_STRATEGY.md`) -
  `GET /activate_tab?name=<PanelName>` makes that named tab
  (`"Hierarchy"`/`"Inspector"`/`"Scene"`/`"Game"`/`"Memory"`/`"Profiler"`/
  `"Render Graph"`/`"Jobs"`/`"Atmosphere"`/`"Project"`) the active/focused tab
  this same frame, exactly as if a human had clicked it, and
  `GET /list_tabs` reports every currently-known panel name so a caller
  never has to guess or hardcode the engine's internal naming convention.
  Built on a brand-new, dedicated cross-thread bridge,
  `EditorUiCommandBridge` (`src/Application/EditorUiCommandBridge.h/.cpp`),
  mirroring `FrameCaptureBridge`/`EngineCommandBridge`'s own narrow,
  single-purpose bridge precedent, and a shared
  `src/Editor/EditorPanelCatalog.h` panel-name catalog that `DockLayout.cpp`'s
  own default layout now reads from too, so the set of valid tab names can
  never drift out of sync between the two. Verified end-to-end with a real
  running engine (`GET /activate_tab?name=Profiler` followed by
  `GET /get_swapchain` visually confirming the "Profiler" tab genuinely came
  to the front) and a full `ctest` regression pass. See
  `task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md` for the full
  five-phase campaign writeup.

See **[docs/CHANGELOG.md](docs/CHANGELOG.md)** for the full project history.

## Roadmap

See **[TODO.md](TODO.md)** for known limitations, deliberately deferred
follow-ups (Editor and Memory Profiler), and longer-term engine roadmap
ideas.
