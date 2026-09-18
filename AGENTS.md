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
under `"RenderOpaque"` per real draw call it issued that frame - never only
meshes. The Sky Background draw is its own real, separate, individually
selectable `"DrawSkyBackground"` pass/leaf too (`render-pass-1` campaign,
splitting the old monolithic `"GameView"` pass into `"RenderOpaque"` +
`"DrawSkyBackground"` + a scaffolded, currently-always-empty
`"RenderTransparent"` - see "Render Pass System" below), not a fabricated
draw-record hack fused into the mesh-drawing pass like it used to be. Every
real pass leaf in this tree - except `"RenderOpaque"` itself, which keeps its
own per-entity mechanism above - is now a real "v PassName" parent OWNING
exactly one real, independently-selectable CHILD event row describing the
actual GPU operation that pass issues (`"Compute Dispatch"` for a
Compute-kind pass; `"Draw Mesh"`/`"Draw Quad"`/`"Blit"` for a Graphics-kind
pass, chosen purely by that pass's own real, structural `rg::RenderPassDrawKind`
tag - never a pass-name string comparison - `render-pass-2` campaign,
`task_manager/render-pass-2/PHASE0_MASTER_STRATEGY.md`, fixing a confirmed bug
where `"DrawSkyBackground"` rendered as a flat, childless row with no expand
arrow, looking like an orphaned row belonging to no render pass).
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

## Render Pass System

`src/Renderer/RenderGraph/RenderGraphBuilder::AddRenderPass()` is the ONE
official way to declare any render/compute/blit pass in this engine - the
`render-pass-1` campaign (seven phases,
`task_manager/render-pass-1/PHASE0_MASTER_STRATEGY.md`) migrated every real
pass declaration (every Atmosphere LUT/composite pass, the Opaque/Sky-
Background/Transparent draw passes, GPU Skinning, Present, Frame Debugger
Replay, Compute Blur Validation) off the old ad-hoc `AddPass()`/
`AddComputePass()` free-function sprawl and onto this single, uniform
chokepoint. Every real pass declaration stamps two pieces of metadata:
`PassKind` (`Graphics`/`Compute` - RENAMED from the older plain `bool
isComputePass`) and `RenderPassCategory` (`General`/`AtmosphereLut`/
`GpuSkinning`/`Debug`), both surviving into `RenderGraphPassSnapshot` so
downstream consumers (the Frame Debugger's own generic tree-building logic -
see "Frame Debugger" above) can discover and classify every real pass
structurally, never via a hand-maintained name list or a hardcoded string
match. The campaign also split the old monolithic `"GameView"` pass into
three real, separate passes - `"RenderOpaque"`, `"DrawSkyBackground"`, and a
currently-always-empty `"RenderTransparent"` scaffold (a clean drop-in point
for a future real transparency system) - declared back-to-back against the
same render target, in that fixed order (Opaque must run before Sky
Background, since Sky Background relies on an `EQUAL` depth-test against the
depth buffer Opaque just wrote). A follow-up campaign, `render-pass-2`
(`task_manager/render-pass-2/PHASE0_MASTER_STRATEGY.md`), added a third,
purely-descriptive piece of metadata: `rg::RenderPassDrawKind`
(`DrawMesh`/`DrawQuad`/`Blit`), stamped via a new, trailing, defaulted
parameter on both `AddRenderPass()` overloads (every pre-existing call site
compiles unmodified) - `"DrawSkyBackground"` is the one real pass tagged
`DrawQuad` today (a hand-verified fact - it issues a real 3-vertex
full-screen-triangle `vkCmdDraw()`, never a per-object mesh draw); `Blit`
remains a real-but-unused scaffold value, since a genuine Vulkan blit/copy
pass would need `RenderGraph::Execute()` to grow a whole new recording path
outside the `vkCmdBeginRendering`/`vkCmdEndRendering` bracket every
Graphics-kind pass uses today - real orchestrator work, still out of scope.
This tag is what lets the Frame Debugger's own generic tree-building logic
(see "Frame Debugger" above) label a Graphics-kind pass's owned child event
row correctly, purely structurally, never via a pass-name string match.

A follow-up campaign, `render-pass-3` (five phases,
`task_manager/render-pass-3/PHASE0_MASTER_STRATEGY.md`), added a NEW,
GENERIC declaration layer, `RenderPipeline`/`RenderPassDesc`/
`RenderPassProvider`/`RenderPassBlackboard`
(`src/Renderer/RenderGraph/RenderPipeline.h/.cpp`), that sits STRICTLY ABOVE
the `AddRenderPass()` chokepoint above and NEVER replaces it - every feature
now registers a `RenderPassProvider` once at startup (`Register()`) instead
of a hand-written free function called by name from `Application.cpp`, a
`RenderPassBlackboard` gives one provider (GPU Skinning) a generic,
opaque-keyed way to hand a value (its output buffers) to another, otherwise
unrelated provider (`"RenderOpaque"`) with zero shared/hand-threaded
parameter, and `Application.cpp`'s previously hand-duplicated Game View/Scene
View `if` blocks collapsed into ONE generic per-view loop
(`RenderPassViewData`, `ProviderScope::PerActiveView`) that both views now
share - Scene View's own Opaque/Sky/Transparent draws are therefore real,
separate passes today (tagged `ViewScope::SceneView`), mirroring Game View's
own already-shipped shape, instead of the old single fused `"SceneView"`
pass. **Every production pass this campaign covers (Atmosphere x6, Opaque,
Sky, Transparent, GPU Skinning, Present, both views) is declared through this
new provider system; `AddFrameDebuggerReplayPasses()`'s replay steps and
`ComputeBlurValidation.cpp`'s own pass are DELIBERATELY, PERMANENTLY left on
the OLD, direct `AddRenderPass()` call style** - they exist purely to feed
Frame Debugger tooling that itself never moved off `ViewScope`/
`RenderPassCategory`/`RenderPassDrawKind`, so migrating them would add
dual-maintenance cost for zero benefit. **LOUD, DELIBERATE DEVIATION from
this feature's own original design brief
(`task_manager/render-pass-3/GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`)**: that
document's own Section 0/12 describe the render-graph BUILDER's pass-adding
entry point itself eventually accepting only the new opaque types, with the
OLD `ViewScope`/`RenderPassCategory`/`RenderPassDrawKind` enums deleted once
migration completes - **this did NOT happen, by explicit user decision, and
never will under this campaign's own scope**: `RenderGraphBuilder::AddRenderPass()`'s
two overloads, and `ViewScope`/`RenderPassCategory`/`RenderPassDrawKind`
themselves, remain permanently unchanged and permanently in place: the new
`RenderPipeline` layer is the ONLY thing that ever sees the new opaque
`RenderPassId`/`RenderPassTagMask`/`RenderViewId`/`RenderPassEvent` types, and
it internally TRANSLATES them into the old, unchanged enum values before
calling the exact same, byte-for-byte-unchanged `AddRenderPass()` chokepoint
- see `task_manager/render-pass-3/CAMPAIGN_COMPLETION_REPORT.md`'s own
dedicated "Deviations from the original design doc" section for the full,
itemized list (six Locked Design Decisions in total) so a future reader of
the original design doc is never misled into thinking it shipped exactly as
originally written.

Full history: `task_manager/render-pass-1/PHASE0_MASTER_STRATEGY.md`,
`task_manager/render-pass-2/PHASE0_MASTER_STRATEGY.md`, and
`task_manager/render-pass-3/PHASE0_MASTER_STRATEGY.md`, and each
`PHASEn_COMPLETION_REPORT.md`/`CAMPAIGN_COMPLETION_REPORT.md` in those same
folders.

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
