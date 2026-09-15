# GreatTamanaEngine Documentation Index

This folder holds the full-detail documentation that the root
[`README.md`](../README.md) and [`AGENTS.md`](../AGENTS.md) were split apart
from, so both root files could stay short, GitHub-style entry points. The
split is two-way: **[`architecture/`](architecture/)** mirrors the root
`README.md`'s old "Architecture" section (what the engine's subsystems *are*
and how they layer together), and **[`conventions/`](conventions/)** mirrors
the root `AGENTS.md`'s old subsystem sections (the coding rules to follow
when touching each subsystem). Each root file today keeps every original
`##`/`###` heading, shrunk to a short summary ending in a link into one of
these two folders.

## Architecture

One file per subsystem, each expanding on the matching `### ` subsection
still summarized in the root [`README.md`](../README.md)'s "Architecture"
section:

- **[Event handling](architecture/event-handling.md)** — how SDL's raw event
  stream is translated into the engine's own `gte::Event` vocabulary before
  reaching `InputState`/`Game::OnEvent()`.
- **[Math](architecture/math.md)** — the from-scratch `Vec2`/`Vec3`/`Vec4`/
  `Mat4`/`Quat` library (no GLM dependency) and its coordinate-system
  conventions.
- **[Rendering](architecture/rendering.md)** — the real Vulkan pipeline
  behind the `Renderer` abstraction: dynamic rendering, RAII wrappers,
  buffers, meshes, depth testing, and offscreen render targets.
- **[Entity-Component-System (ECS)](architecture/ecs.md)** — the engine's
  Scene/World data model (`Entity`/`EntityManager`/`ComponentStorage<T>`/
  `Registry`), `Transform`'s parent/child hierarchy, and the
  `RenderSystem`/`MeshInstantiationSystem`/`AnimationSystem` boundary rule.
- **[Asset Pipeline](architecture/asset-pipeline.md)** — the `*.gta` binary
  asset container format, `AssetDatabase`, and the PNG/JPG, MikuMikuDance
  `.pmx`, and `.vmd` import pipelines.
- **[Editor / Debug UI](architecture/editor-debug-ui.md)** — the optional
  `src/Editor/` module: Hierarchy/Inspector/Scene/Game panels, the transform
  gizmo, Memory/Profiler/Render Graph/Project panels, and asset preview/Bone
  Viewer tooling.

## Conventions

One file per subsystem-specific coding convention, each expanding on the
matching `## ` section still summarized in the root [`AGENTS.md`](../AGENTS.md)
(which keeps `## Coding Guidelines` and `## Testability & Regression Safety`
inline in full, since both are short and universally cross-cutting rather
than subsystem-specific):

- **[GPU Resource Memory Tracking](conventions/gpu-resource-memory-tracking.md)**
  — every GPU resource type must register with `GpuMemoryTracker`, identified
  by a cheap, generational `GpuResourceHandle`.
- **[CPU Dependency Memory Tracking](conventions/cpu-dependency-memory-tracking.md)**
  — `SdlMemoryTracker`/`ImGuiMemoryTracker` track third-party CPU memory
  usage, surfaced by the Editor's "Memory" panel.
- **[Profiling](conventions/profiling.md)** — `src/Profiling/`'s always-
  compiled CPU scope-timer instrumentation (`GTE_PROFILE_SCOPE`), draw-call/
  GPU-memory/GPU-timestamp data feeding the "Profiler" panel.
- **[Job System](conventions/job-system.md)** — `src/Jobs/`'s general-purpose
  worker-thread pool, `Schedule()`/`Dispatch()`/dependencies/continuations,
  and the thread-safety classification of every shared subsystem.
- **[Networking](conventions/networking.md)** — the embedded, loopback-only
  HTTP server (`gte::Network::NetworkServer`), its cross-thread bridges, and
  every route family (frame/texture capture, ECS-mutating commands, Editor UI
  control) — includes the nested "Named Texture Capture" subsection.
- **[Render Target Format Matching](conventions/render-target-format-matching.md)**
  — always read `Renderer::ColorFormat()`/`DepthFormat()` rather than
  hardcoding a `VkFormat` literal.
- **[Skeletal Animation Pose Resolution](conventions/skeletal-animation-pose-resolution.md)**
  — the per-frame MMD pose evaluation pipeline under `src/Animation/`
  (`BoneChainResolver.h`, `BonePoseMath.h`, `AnimationPoseEvaluator.h`).
- **[GPU Vertex Skinning](conventions/gpu-vertex-skinning.md)** — the
  compute-shader mirror of the CPU vertex-skinning path, switchable via
  `AnimationSystem::SkinningMode`.
- **[Atmosphere Scattering](conventions/atmosphere-scattering.md)** — the
  physically-based real-time atmosphere-scattering + aerial-perspective
  system and its permanent CPU oracle, `AtmosphereMath.h/.cpp`.
- **[Entity-Component-System (ECS)](conventions/ecs.md)** — the `AGENTS.md`-
  side coding conventions for entities/components (distinct from, and
  cross-linked with, [`architecture/ecs.md`](architecture/ecs.md)'s engine
  architecture overview).
- **[Scene Serialization](conventions/scene-serialization.md)** — the
  `src/Scene/` Save/Load module (`SceneDocument.h`/`SceneTextFormat.h/.cpp`/
  `SceneBuilder.h/.cpp`) wired into `File > Save Scene`/`File > Open Scene`.
- **[Editor Module Structure](conventions/editor-module-structure.md)** —
  the `src/Editor/` folder boundary, `ImGuiEditorLayer`'s composition root,
  `EditorContext`, `Selection`, `DockLayout`, and the fixed `Panels/*.cpp`
  builder-function convention.

## Changelog

See **[CHANGELOG.md](CHANGELOG.md)** for the complete, reverse-chronological
project history from the very first triangle demo onward — the root
`README.md`'s "Status" section keeps only the last few entries inline and
links here for everything older.

## Other Project Documentation

These three top-level files are just as much a part of the project's
documentation as anything under `docs/`, but are already properly separated,
focused files and were left untouched by the `doc-refactor-1` campaign that
created this folder:

- **[BUILDING.md](../BUILDING.md)** — prerequisites and build instructions.
- **[TESTING.md](../TESTING.md)** — how to build and run the test suite.
- **[TODO.md](../TODO.md)** — known limitations, deliberately deferred
  follow-ups, and longer-term engine roadmap ideas.

---

See the root [README.md](../README.md) and [AGENTS.md](../AGENTS.md) for the
short overview each of these was extracted from.
