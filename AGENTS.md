# AGENTS.md

Instructions for LLM/AI agents working on this codebase.

## Documentation

This file covers universal coding guidelines and testability rules, plus a
short summary + link for every subsystem-specific convention. Full detail
for each subsystem lives under [`docs/conventions/`](docs/README.md).

## Coding Guidelines

- **Clean Architecture**: Write clean architecture code. Keep clear
  separation of concerns between layers (e.g. SDL -> Application -> Window
  and Renderer -> Game). Lower-level/core layers must not depend on
  higher-level or framework-specific details. Only the `Application` layer
  should know about SDL directly; other layers must go through the custom
  abstraction objects (Window, Renderer, etc.).
- **RAII**: Every resource-owning piece of code must use RAII (Resource
  Acquisition Is Initialization). Resources (SDL handles, memory, file
  handles, GPU objects, etc.) must be acquired in constructors and released
  in destructors, so lifetime is tied to object scope and cleanup is
  automatic and exception/error-safe. Avoid manual/explicit cleanup calls
  scattered through the code — wrap raw resources in owning types instead.
- **Namespace**: Every new script (every class/function/type this project
  defines) must live inside the `gte` namespace (short for Great Tamana
  Engine), e.g. `namespace gte { class Window { ... }; }`. This keeps engine
  symbols from colliding with SDL's or third-party globals.

## GPU Resource Memory Tracking

Every GPU resource type (`Buffer`, `RenderTexture`, and any future type) must
register with `GpuMemoryTracker` (`src/Renderer/Memory/GpuMemoryTracker.h`),
identified by a cheap, generational `GpuResourceHandle` (never a pointer or
string), so the engine always has an accurate, live picture of exactly what
GPU memory is allocated, of what kind, and where.

Full convention: [docs/conventions/gpu-resource-memory-tracking.md](docs/conventions/gpu-resource-memory-tracking.md).

## CPU Dependency Memory Tracking

Alongside `GpuMemoryTracker` (above), the engine also tracks how much CPU
(host) memory its own third-party dependencies use - `SdlMemoryTracker`
(SDL) and `ImGuiMemoryTracker` (Dear ImGui, Editor-only) - both surfaced by
the Editor's "Memory" panel. The tracking allocator must be installed before
the dependency's first call of any kind, and `Install()` must be idempotent.

Full convention: [docs/conventions/cpu-dependency-memory-tracking.md](docs/conventions/cpu-dependency-memory-tracking.md).

## Profiling

`src/Profiling/` (`ProfilingTypes.h`, `FrameProfiler.h/.cpp`, `ScopeTimer.h`)
is the engine's always-compiled CPU scope-timer instrumentation module,
separately gated at runtime by `GTE_ENABLE_PROFILER`. `GTE_PROFILE_SCOPE`
is the only way to add a new CPU profiling call site; draw-call/triangle
counts (`DrawStats.h`), GPU memory snapshots (`MemorySnapshotBuilder.h`),
and real Vulkan GPU timestamp queries (`GpuTiming.h`/`GpuTimingService`) are
all wired to genuine production data, reshaped for the Editor "Profiler"
panel by `FrameGraphData.h`.

Full convention: [docs/conventions/profiling.md](docs/conventions/profiling.md).

## Time and Playback Pause

`src/Core/Time.h/.cpp` (`gte::Time`) plus `src/Core/EngineContext.h`
(`gte::EngineContext`) are the engine's dedicated, explicit (never
singleton) per-frame time-keeping objects - `Application` owns the one
`EngineContext` instance, advances its `Time` once per frame
(`Time::Advance()`), and passes it by `const&` into `Game::Update()`,
which uses `Time::IsFrozenThisFrame()` to skip Animation/Physics/GPU-
skinning work entirely on a paused (non-stepping) frame - Unity-style
Pause, driven by a small Pause/Resume/Step toolbar in the Editor
(`src/Editor/PlaybackControls.h/.cpp`, `EditorContext::playbackPaused`/
`stepOneFrameRequested`, `IEditorLayer::IsPlaybackPaused()`/
`TryConsumeStepRequest()`). Rendering, the Editor UI, and the
independently-orbitable Scene-view camera all keep running normally
regardless of pause - only gameplay simulation freezes.

Full convention: [docs/conventions/time-and-playback-pause.md](docs/conventions/time-and-playback-pause.md).

## Frame Debugger

`src/Editor/FrameDebuggerData.h/.cpp` (pure data model, real snapshot builder),
`src/Editor/FrameDebuggerCapture.h/.cpp` (the per-frame capture context
threaded through `Renderer::Submit()`/`RenderSystem::Draw()`, zero overhead
when disarmed), `src/Editor/FrameDebuggerHistory.h/.cpp` (the class itself is
`gte::FrameDebuggerCurrentCapture` - exactly ONE captured frame is ever held
in memory, no multi-frame history, `frame-debugger-7` campaign - an explicit,
user-approved BREAKING CHANGE replacing the old 8-slot ring buffer),
`src/Editor/FrameDebuggerPreviewProcessing.h/.cpp` (a real Channels/Levels
preview-compositing CPU oracle + compute shader), and `src/Editor/Panels/
FrameDebuggerPanel.h/.cpp` (the on-demand floating "Frame Debugger" window,
opened via Window > Frame Debugger or `GET /frame_debugger/open`) together
give the Editor a genuinely working, Unity-Frame-Debugger-style tool for the
Game View render target: enabling it captures one real rendered frame's worth
of real Render Graph passes (every real compute-shader dispatch is a
first-class, automatically discovered tree citizen, never a hand-maintained
special case) PLUS one real, individually selectable per-entity child leaf
under `"GameView"` per real draw call it issued that frame, including the Sky
Background pass itself (`frame-debugger-8` campaign) - never only meshes.
**Selecting ANY
leaf - a compute pass or a per-object draw alike - now shows a real, correct
"accumulated Game View as of this exact step" preview image** (`frame-debugger-7`
campaign, an explicit, user-approved BREAKING CHANGE replacing the older
per-compute-pass-distinct-texture preview outright): N debug-only, self-
contained Render Graph passes redraw objects `[0..i]` from scratch into their
own dedicated destination textures on every explicit capture trigger (Enable-
edge / Step / "Capture" button), deferred by exactly one frame so the very
first capture after "Enable" is never missing objects. The entire feature is
drivable end-to-end over the embedded HTTP server (`GET
/frame_debugger/open|enable|capture|select_event|set_channel|set_levels|state`),
with the window forced onto the main ImGui viewport whenever opened this way.
True per-pass "stop"/breakpoint execution control (pausing the GPU mid-frame
at a specific compute dispatch boundary) is a still-deferred future item -
see `TODO.md`'s "Frame Debugger" section.

Full convention: [docs/conventions/frame-debugger.md](docs/conventions/frame-debugger.md).

## Job System

`src/Jobs/` (`JobTypes.h`, `JobQueue.h/.cpp`, `JobSystem.h/.cpp`,
`JobDispatch.h/.cpp`, `JobContinuation.h/.cpp`) is the engine's general-purpose
worker-thread pool - `JobHandle`/`Schedule()`/`WaitForJobs()`, the
batch/parallel-for `Dispatch()` API, job dependencies/continuations
(`ScheduleAfter()`/`DispatchAfter()`), a reviewed thread-safety
classification (NEVER/READ-SAFE/JOB-SAFE) of every shared engine subsystem a
job body might touch, thread-safe worker-timeline profiling
(`GTE_PROFILE_JOB_SCOPE`), and its first production consumer,
`AnimationSystem::Update()`'s CPU vertex skinning dispatch.

Full convention: [docs/conventions/job-system.md](docs/conventions/job-system.md).

## Networking

`src/Network/` is the engine's embedded, loopback-only HTTP server
(`gte::Network::NetworkServer`), gated by `GTE_ENABLE_NETWORK`. Every route
handler runs on its own background thread and must be a pure function of
its own request data, reaching engine state only through a small set of
dedicated, reviewed cross-thread bridges (`FrameCaptureBridge`,
`EngineCommandBridge`, `EditorUiCommandBridge`) - never directly. Endpoints
today include frame/texture capture (`GET /get_swapchain`/`/get_game_view`/
`/get_texture`, including "Named Texture Capture" volume-texture support),
ECS-mutating commands (`POST /instantiate_primitive`/
`/delete_entity`/`/set_entity_trs`/`/instantiate_light`), and Editor UI
control (`GET /activate_tab`/`/list_tabs`, letting an external caller bring
a specific named Editor panel/tab to the front and enumerate every known
panel name).

Full convention: [docs/conventions/networking.md](docs/conventions/networking.md).

## Render Target Format Matching

Vulkan pipelines are built against an exact color format
(`VkPipelineRenderingCreateInfo::pColorAttachmentFormats`) - never hardcode a
`VkFormat` literal; always read it from `Renderer::ColorFormat()`/
`Renderer::DepthFormat()`, which `FrameRecorder::RecordFrame()` asserts every
render target actually matches (debug builds only).

Full convention: [docs/conventions/render-target-format-matching.md](docs/conventions/render-target-format-matching.md).

## Skeletal Animation Pose Resolution

Every per-frame MMD skeletal-animation pose evaluation lives under
`src/Animation/` - never hand-roll a new cycle-guarded bone-ancestor-chain
walk (use `BoneChainResolver.h`), the bind-relative local-transform formula
lives in exactly one place (`BonePoseMath.h`), and the per-frame execution
order (sample -> IK -> append -> forward-kinematics) has exactly one home,
`AnimationPoseEvaluator.h`'s `EvaluateAnimatedSkinningPose()`.

Full convention: [docs/conventions/skeletal-animation-pose-resolution.md](docs/conventions/skeletal-animation-pose-resolution.md).

## GPU Vertex Skinning

An eight-phase campaign gave the engine a SECOND, GPU-resident implementation
of vertex skinning - a compute-shader mirror of `Animation/VertexSkinning.h`'s
CPU path, switchable at runtime via `AnimationSystem::SkinningMode` and the
Editor "Jobs" panel's "Skinning Mode" toggle. The CPU path remains the
permanent oracle the GPU kernel is checked against.

Full convention: [docs/conventions/gpu-vertex-skinning.md](docs/conventions/gpu-vertex-skinning.md).

## Atmosphere Scattering

A nine-phase campaign gave the engine a physically-based, real-time
atmosphere-scattering + aerial-perspective system, hand-ported from a
cloned (never vendored, never committed) reference implementation,
`hoffstadt/pl-sky`. `src/Renderer/Atmosphere/AtmosphereMath.h/.cpp` is the
PERMANENT CPU ORACLE every atmosphere shader is checked against; the
Aerial Perspective froxel volume is a genuine THIRD Render Graph resource
kind (`VolumeTexture`); this feature is always compiled in, with no
`GTE_ENABLE_ATMOSPHERE` switch.

Full convention: [docs/conventions/atmosphere-scattering.md](docs/conventions/atmosphere-scattering.md).

## Entity-Component-System (ECS)

The engine's Scene/World data model lives under `src/ECS/` (`Entity`,
`EntityManager`, `ComponentStorage<T>`, `Registry`), hand-rolled rather than
a third-party library - identify entities by handle, keep components plain
data, and only `RenderSystem`/`MeshInstantiationSystem`/`AnimationSystem` are
allowed to depend on both the ECS world and `Renderer`.

Full convention: [docs/conventions/ecs.md](docs/conventions/ecs.md).

## Scene Serialization

The `scene-serialization-1` campaign gave the engine its first real Save/Load
loop (root-only, `PrimitiveSource`/`MeshAssetSource`-only, a hand-rolled TEXT
format). The `scene-serialization-2` campaign (six phases) replaced that
wholesale with a genuinely generic system: a new, always-compiled
`src/ECS/Reflection/` field-reflection layer (`ComponentTypeRegistry`,
`GTE_REFLECT_FIELD`/`GTE_REFLECT_ENUM_FIELD`, `BuiltinComponentReflection.cpp`)
a future component registers its own fields into ONCE - `src/Scene/`
(`SceneDocument.h`, `SceneJsonFormat.h/.cpp` - JSON, replacing the deleted
`SceneTextFormat.h/.cpp`, `SceneBuilder.h/.cpp`) now walks EVERY entity in the
Registry (full parent/child hierarchy), not just tagged roots; plus
`src/Editor/SceneIO.h/.cpp`'s recipe-spawn-reconciliation `LoadScene()`, wired
into `File > Save Scene`/`File > Open Scene`, and two new HTTP endpoints,
`POST /save_scene`/`POST /load_scene` (see the "Networking" section below).

Full convention: [docs/conventions/scene-serialization.md](docs/conventions/scene-serialization.md).

## Editor Module Structure

An optional in-engine Editor module lives under `src/Editor/`, compiled only
when `GTE_ENABLE_EDITOR` is ON - `ImGuiEditorLayer` as its composition root,
a shared `EditorContext`, a single `Selection` gate-keeper for
Hierarchy/Project selection changes, `DockLayout`, and a fixed set of
`Panels/*.cpp` builder functions (deliberately not a polymorphic panel
registry).

Full convention: [docs/conventions/editor-module-structure.md](docs/conventions/editor-module-structure.md).

## Testability & Regression Safety

- **Design new logic to be Tier-1-testable whenever the underlying problem
  allows it.** Follow the split already established in `tests/CMakeLists.txt`:
  "Tier 1" code is pure logic that operates on plain data/enums/structs and
  needs no live `VkDevice`/`VmaAllocator`/`VkSurfaceKHR`/SDL window - see
  `EventTranslator` (takes a plain `SDL_Event` struct), `InputState` (takes
  plain `gte::Event` values), and `GpuMemoryTracker` (takes plain enums + a
  byte count, never a real `VmaAllocation`) for the pattern to copy. Before
  wiring new logic directly into a GPU/SDL-owning class, ask whether it can
  instead be extracted as a small pure function/class that takes
  already-resolved plain values - if it can, do that, then add its test under
  `tests/<Layer>/` (mirroring the folder it lives in under `src/`), not "if
  there's time".
- **Every change to Tier 1 code must come with a matching test change.**
  Adding a new branch/case to `EventTranslator`, `InputState`,
  `GpuMemoryTracker`, `GpuResourceHandle`, `Vertex`, or any future Tier 1
  module must add or update the corresponding file in `tests/` in the same
  change - never leave a new code path with zero coverage in a module that
  already has a test file. Fixing a bug in one of these files must add a
  regression test that fails before the fix and passes after it, not just a
  code change.
- **Run the actual test suite before considering any change to `gte_core`
  done - a successful build is not enough.** Build `GreatTamanaEngineTests`
  and run it (e.g. `ctest` from the build directory, or the built `.exe`
  directly) after any change under `src/` - a change can compile cleanly
  while still silently breaking `InputState`'s held/pressed/released
  semantics, `EventTranslator`'s mappings, or `GpuMemoryTracker`'s
  bookkeeping. Treat any newly-failing test as a real regression to fix, not
  something to work around by loosening the test's expectation without
  understanding why it failed.
- **GPU-dependent ("Tier 2") code - `Buffer`, `RenderTexture`, `Pipeline`,
  `GpuResourceFactory`, `FramePresenter`, `FrameRecorder`, everything under
  `Renderer/Vulkan/` - has no automated test coverage yet** (see the "Tier 2"
  note in `tests/CMakeLists.txt`). A headless-surface `GpuTestFixture`
  (`VK_EXT_headless_surface`) is noted there as a possible future addition,
  but it is a backlog/TODO item only, NOT a prerequisite or gate for
  anything else - the current development machine doesn't support headless
  mode anyway, so this must never be treated as a blocker for adding new
  features or landing changes under `Renderer/Vulkan/` or elsewhere. When
  it's convenient, build and run against a real GPU/window as a sanity
  check for changes here, and extracting more logic into Tier-1-testable
  pure functions (per the point above) is always welcome - but the absence
  of automated Tier 2 coverage should never itself slow down or stop
  feature work.

This document will be extended as more conventions are established.
