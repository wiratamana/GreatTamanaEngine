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

A follow-up campaign, `render-pass-4` (three phases,
`task_manager/render-pass-4/PHASE0_MASTER_STRATEGY.md`), fixed a confirmed
structural gap in `RenderPassEvent` itself: the enum LOOKED like it decided
real pass execution order, but `RenderGraphCompiler::Compile()`'s own RAW/WAW
dependency-edge construction never read it at all - ordering was 100% a
function of raw C++ declaration order, a fact that already caused one real,
live, confirmed production bug (`AtmosphereComposite` silently culling
`RenderOpaque`/`DrawSkyBackground`, patched narrowly by `render-pass-3`'s own
`ProviderTiming` two-phase split, not fixed at the root). PHASE1 shipped a
pure, Tier-1-tested safety net first, changing zero scheduling behavior: a new
`DetectRenderPassEventContradictions()` function wired into `Compile()` that
reports - via an unconditional `stderr` message plus a debug-build `assert()`
- the exact moment a pass's declared `RenderPassEvent` contradicts what its
real, compiler-enforced resource dependencies say must happen. **PHASE2 then
shipped this campaign's ONE genuine, deliberate behavior change**:
`RenderGraphCompiler::Compile()` now computes an "effective order" - every
pass stable-sorted by `(RenderPassEvent, original declaration index)` - and
walks passes in THAT order, instead of raw declaration order, for both its
RAW/WAW dependency-edge scan and its Kahn's-algorithm ready-set tie-break.
`RenderPassEvent` is therefore real, load-bearing ordering input for the
first time; it keeps its exact original name (never renamed), and
`RenderGraphBuilder::AddRenderPass()`'s own two overloads remain byte-for-byte
unchanged - only what order `Compile()` processes already-declared passes in
changed. A real production-code fix landed alongside it, found by PHASE2's
required audit and confirmed via `ask_questions`:
`src/Editor/ComputeBlurValidation.cpp`'s pass is now explicitly tagged
`RenderPassEvent::AfterTransparents` instead of the implicit default
`Opaques`, since the new effective order would otherwise have silently walked
it before `DrawSkyBackground`/`RenderTransparent` (Scene View) - the exact
"orphan read" bug shape this campaign exists to prevent, this time freshly
introduced by the reorder itself rather than fixed by it. **What remains true
about `RenderPassEvent` today: it is a REAL ordering key `Compile()` actually
uses (not just a sort hint for `RenderPipeline`'s own deferred-pass list), but
it is still NOT a dependency mechanism** - a pass with a genuinely WRONG
`RenderPassEvent` tag relative to its real resource reads/writes remains a
real hazard, now caught by PHASE1's detector instead of silently
miscompiling. Verified with a full clean build, a full `ctest` regression
pass (1626 tests, 100% passing, one pre-existing environment-gated skip - up
from `render-pass-3`'s own 1616 baseline), and a live, HTTP-driven Frame
Debugger + Scene View smoke test confirming the real production pass tree's
execution order (Atmosphere LUTs, then Opaque, then Sky, then the Post-Game-
View Composite pass) is visually unchanged from every prior campaign's own
final screenshot. See `task_manager/render-pass-4/CAMPAIGN_COMPLETION_REPORT.md`
for the full three-phase writeup.

A further campaign, `render-pass-6` (seven phases,
`task_manager/render-pass-6/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), reworked this system's own internals for
scale and per-frame speed with **zero behavior change and zero public API
change** - implementing exactly the P0/P1 items of a prior outside-in code
review (P2's `PassDesc` value type and P3's three opportunistic items were
explicitly left out of scope). `RenderGraphNameSlotTable`'s previously-silent
GPU-timing-slot-budget overflow now produces a real, one-time
`GTE_LOG_WARNING` plus a `RenderGraphSnapshot::timingSlotBudgetExhausted`
flag the Editor's "Render Graph" panel can surface (PHASE1); `RenderGraph::
ExecuteCompiledGraph()`'s six previously-inline concerns became named private
methods (`BuildPassContext()`/`BuildColorAttachmentInfos()`/
`BuildDepthAttachmentInfo()`/`RegisterDebugTextureSnapshots()`/
`RegisterDebugVolumeTextureSnapshots()`, PHASE2); `PassContext`'s six
`std::function` fields (freshly constructed, per pass, per `Execute()` call)
became plain non-owning pointers plus ordinary member functions - except
`recordDraw`/`recordIndirectDraw`, which became small non-owning callable
struct fields (`RecordDrawFn`/`RecordIndirectDrawFn`) since real call sites
pass them by value/truthiness-check into `Renderer::BeginGraphPassRecording()`
- with zero change to any pass author's own call-site syntax (PHASE3);
`RenderGraphCompiler::Compile()`'s dependency-graph construction moved from an
`O(P^2)` adjacency matrix to `O(P+E)` adjacency lists, with
`DetectRenderPassEventContradictions()`'s own standalone implementation left
byte-for-byte unchanged and a new inline fast path reusing `Compile()`'s own
last-writer bookkeeping - byte-identical `executionOrder` verified against
the full pre-existing `RenderGraphCompilerTests.cpp` suite (PHASE4); the 9
parallel builder/`CompiledGraphInput` vectors (`textureDescs`/`textureNames`/
`textureImportInfo`, etc.) collapsed into 3 per-kind `TextureSlot`/
`BufferSlot`/`VolumeTextureSlot` vectors, eliminating a whole class of "forgot
to push to array #3" silent-misalignment bug (PHASE5); and all nine (not the
original estimate of seven - two more were introduced by PHASE4 itself)
hand-duplicated `switch (usage.kind)`/`switch (a.kind)` blocks scattered
across `RenderGraph.cpp`/`RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp`
collapsed into one generic `DispatchByKind()` dispatcher - still a real,
`default:`-less exhaustive switch internally with a hard-fail unreachable
tail, though PHASE6 discovered and honestly documented a genuine pre-existing
gap: this project's build enables no `-Wswitch`/`-Wall`/`-Werror` for its own
code, so an unhandled `ResourceKind` enumerator does not actually fail to
compile anywhere in this codebase today, before or after this campaign - the
"no `default:`, ever" discipline remains a code-review convention, not a
compiler-enforced one (PHASE6). Verified with a full clean build, a full
`ctest` regression pass (1736 tests, 100% passing, one pre-existing
environment-gated skip - up from `logger-1`'s own 1673 baseline), and a live,
HTTP-driven smoke test confirming rendering and the "Render Graph" panel are
visually unchanged. See `task_manager/render-pass-6/CAMPAIGN_COMPLETION_REPORT.md`
for the full seven-phase writeup.

A follow-up campaign, `render-pass-7` (five phases,
`task_manager/render-pass-7/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), de-hardcoded `RenderPassCategory` - a Core
(Layer 1) file no longer needs to name a specific Layer-2 feature, ever.
Previously `RenderPassCategory` baked `AtmosphereLut`/`GpuSkinning`
enumerators directly into a Core render-graph type; it now has exactly two
enumerators left, `General`/`Debug`, both genuinely Core-level concepts. The
previously-designed-but-never-wired `RenderPassTag`/`RenderPassTagMask`
mechanism (`RenderPipeline.h`) was relocated into `RenderGraphTypes.h`
(mirroring `RenderPassEvent`'s own precedent) and threaded end-to-end for the
first time: `PassRecord`/`RenderGraphPassSnapshot` gained a real `tags` field,
both `RenderGraphBuilder::AddRenderPass()` overloads gained a new trailing
`tags` parameter, and a real, PRE-EXISTING dead-field bug was found and fixed
along the way (PHASE1) - `RenderPipeline::DeclareOnePhase()`'s own
`builder.AddRenderPass(...)` call never actually passed `desc.tags` through,
silently dropping it every frame for every provider, meaning the tag
mechanism was completely inert for anything routed through `RenderPipeline`
before this fix. A brand-new, generic Core facility,
`RenderPassGroupRegistry.h/.cpp` (`RegisterPassGroupLabel()`/
`FindPassGroupIndexForTags()`, PHASE2), lets any Layer-2 module register its
own human-readable Frame Debugger tree heading for its own tag, with Core
itself never learning or caring what any tag or heading means. PHASE3 then
did the actual de-hardcoding: two new Layer-2 tag headers
(`AtmosphereRenderPassTags.h`'s `kAtmosphereLutPassTag` = bit 0,
`GpuSkinningRenderPassTags.h`'s `kGpuSkinningDispatchPassTag` = bit 1, both
exactly the bit values this campaign's own strategy doc suggested) replaced
the two deleted enumerators at all 7 real production call sites (5 Atmosphere
LUT passes, the `"GpuSkinning"` `RenderPipeline` provider,
`AddGpuSkinningPasses()`'s direct-render-only fallback), and
`AtmosphereLutRenderer`'s own constructor now self-registers the
`"Compute LUT"` heading from its own file. PHASE4 rewrote
`FrameDebuggerData.cpp`'s pre-GameView compute-dispatch grouping logic to
consume the registry generically - one bucket per currently-registered
`(tag -> heading)` pair, in registration order, looked up via
`FindPassGroupIndexForTags(pass.tags)` instead of a hardcoded
`RenderPassCategory::AtmosphereLut` check - proven genuinely generic by a new
Tier-1 test that registers a synthetic, test-only tag/heading pair with zero
relationship to any real feature and confirms it is correctly bucketed by
code that never mentions it. Verified with a full clean build (both
`GTE_ENABLE_EDITOR` configs), a full `ctest` regression pass (1753 tests,
100% passing, one pre-existing environment-gated skip - up from
`render-pass-6`'s own 1736 baseline), and a live, HTTP-driven smoke test
confirming the Game View Frame Debugger tree's `"Compute LUT"`/
`"RenderOpaque"`/`"DrawSkyBackground"`/`"Compute Dispatches (Post-GameView)"`
grouping and per-pass Inspector data are visually and structurally identical
to every prior campaign's own documented baseline. See
`task_manager/render-pass-7/CAMPAIGN_COMPLETION_REPORT.md` for the full
five-phase writeup.

Full history: `task_manager/render-pass-1/PHASE0_MASTER_STRATEGY.md`,
`task_manager/render-pass-2/PHASE0_MASTER_STRATEGY.md`,
`task_manager/render-pass-3/PHASE0_MASTER_STRATEGY.md`,
`task_manager/render-pass-4/PHASE0_MASTER_STRATEGY.md`,
`task_manager/render-pass-6/PHASE0_MASTER_STRATEGY.md`, and
`task_manager/render-pass-7/PHASE0_MASTER_STRATEGY.md`, and each
`PHASEn_COMPLETION_REPORT.md`/`CAMPAIGN_COMPLETION_REPORT.md` in those same
folders.

## Multi-Render-Target (MRT) / G-Buffer Support

The `mrt-1` campaign (five phases, `task_manager/mrt-1/PHASE0_MASTER_STRATEGY.md`)
gave the Render Graph the ability for a single pass to write **more than one
color attachment at once** - the foundational mechanism a real G-buffer/
deferred-shading pass needs - and proved it end-to-end with a small, additive,
debug-only consumer pass, mirroring `ComputeBlurValidation`'s own established
proof-of-mechanism precedent. `RenderGraphBuilder::PassBuilder::
WriteColorAttachment()` is now callable **more than once per pass**, appending
an ordered `PassRecord::colorAttachments` list (`ColorAttachmentDesc{ handle,
clearColor }`, `RenderGraphTypes.h`) instead of the old "last write silently
overwrites the previous one" behavior - **attachment index in that list ==
shader `layout(location = N) out`** - capped at `kMaxColorAttachments = 8`
(matching typical `VkPhysicalDeviceLimits::maxColorAttachments`), enforced by
a debug-only `assert()`. `RenderGraph::ExecuteCompiledGraph()` records a real
`vkCmdBeginRendering` with `colorAttachmentCount` built from that list's real
size (`FindMismatchedColorAttachmentExtent()` throws `std::runtime_error` if
two declared attachments on the same pass ever disagree on extent - a real,
reachable failure mode for a genuinely misconfigured MRT pass, not just a
defensive assert). `Pipeline` gained a real N-format PSO path - a new
`std::span<const VkFormat> colorFormats` constructor overload (one
`VkPipelineColorBlendAttachmentState` per target) - while its original
single-`VkFormat` constructor is preserved as a genuinely callable overload,
now just a thin delegating forwarder into the new one; `GpuResourceFactory::
CreatePipeline()`/`Renderer::CreatePipeline()` each gained a parallel
`std::span`-taking overload, with every pre-existing single-format call site
(`MeshAssetGpuCatalog.cpp`'s `Mesh`/`TexturedMesh` pipelines,
`PrimitiveGpuCatalog.cpp`'s default/Triangle primitive pipeline) compiling and
behaving completely unmodified. **`RenderGraphCompiler.cpp`/
`RenderGraphSnapshot.cpp` needed ZERO changes** for any of this - both already
looped over `pass.writes`/a pass's write list completely generically, with no
"at most one color write" assumption anywhere, proven by dedicated Tier-1
tests (`RenderGraphBuilderTests.cpp`/`RenderGraphCompilerTests.cpp`/
`RenderGraphSnapshotTests.cpp`) rather than merely asserted. The campaign's
first real consumer, `src/Editor/GBufferValidation.h/.cpp` (`GTE_ENABLE_EDITOR`-
only, mirroring `ComputeBlurValidation`'s exact shape), declares a real
"GBufferValidation" graphics pass writing two color targets
(`outAlbedo`/`outNormal`, `Shaders/GBufferValidation.vert/.frag`) in one draw,
plus a second, compute "GBufferValidationCopy" pass
(`Shaders/GBufferCopy.comp`) that reads one of those two outputs back and
copies it into a third, ImGui-visualizable output - proving both halves of
the mechanism (N targets written by one pass in one draw; a later pass
reading one of N outputs, cross-pass, barrier-synchronized automatically by
the existing, unmodified `RenderGraphBarrierPlanner`) with a single real,
additive, opt-in workload. Toggled by a "Show GBuffer Validation (debug)"
checkbox in the Scene panel's toolbar (`EditorContext::
showGBufferValidationOutput`), `ViewScope::SceneView` + the existing
`RenderPassCategory::Debug` (no new enumerator was added) - **never on by
default, and never affecting the Game View's own rendered output in any way**.
`IEditorLayer` gained two new pure-virtual methods,
`AddGBufferValidationPass()`/`FinalizeGBufferValidationForSampling()`
(`EditorLayer.h`), with matching no-op stubs in `NullEditorLayer.cpp` so a
`GTE_ENABLE_EDITOR=OFF` release build keeps compiling. See
`task_manager/mrt-1/CAMPAIGN_COMPLETION_REPORT.md` for the full five-phase
writeup, including the corrected `Renderer::CreatePipeline()` call-site
enumeration and the depth-attachment/vertex-binding wrinkles this campaign's
own first real N-format `Pipeline` consumer had to work around.

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
`/delete_entity`/`/set_entity_trs`/`/instantiate_light`), Editor UI
control (`GET /activate_tab`/`/list_tabs`, letting an external caller bring
a specific named Editor panel/tab to the front and enumerate every known
panel name), and engine log retrieval (`GET /get_logs`/`POST /clear_logs`,
the ONE documented, narrow exception to this section's own "reach engine
state only through a reviewed bridge" rule - see "Logging" below).

Full convention: [docs/conventions/networking.md](docs/conventions/networking.md).

## Logging

`src/Editor/Logger.h/.cpp` (`gte::Logger`) is the engine's Editor-only,
thread-safe, in-memory log store - callable from any thread via the
GTE_LOG_DEBUG/INFO/WARNING/ERROR macros, which compile to a true empty
no-op (not even evaluating their arguments) whenever GTE_ENABLE_EDITOR is
OFF. A bounded 2000-entry ring buffer, filterable by level/category/
keyword/frame range/an incremental since_id cursor, surfaced by the
Editor's "Log" panel and by GET /get_logs / POST /clear_logs.

Full convention: [docs/conventions/logging.md](docs/conventions/logging.md).

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

## GPU-Driven Rendering (Frustum Culling + Indirect Draw)

A seven-phase campaign, `render-pass-5`
(`task_manager/render-pass-5/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), replaced this engine's old "walk every
`MeshRenderer` and issue one `vkCmdDrawIndexed` per entity, unconditionally,
no culling of any kind" behavior with a real, always-on, PRODUCTION
GPU-driven path for the common case where several entities share the exact
same `(MeshHandle, PipelineHandle)` pair (a "batch"): `RenderSystem::
CollectGpuDrivenBatches()` (`src/Game/RenderBatching.h`) groups this frame's
draw commands and applies a fixed, four-condition eligibility rule (`>=
kMinInstancesForGpuDrivenBatch` = 4 instances; `Mesh::HasIndexBuffer()`;
`Pipeline` built with EXACTLY `VertexLayout::PositionNormal` - untextured,
non-instanced; not part of this frame's GPU-skinning output-buffer set) -
every OTHER `MeshRenderer` (every primitive, every textured submesh, every
GPU-skinned model, every group below the threshold) keeps drawing through the
byte-for-byte-unchanged per-entity `Renderer::Submit()` path, forever. For
each eligible batch, a real compute shader (`Shaders/FrustumCull.comp`, one
thread per instance, mirroring `GpuSkinningPipelines`' own shape via the new
`src/Renderer/Culling/CullingPipelines.h/.cpp`) tests each instance's
world-space AABB (`Mesh::LocalBounds()`, computed once at load time by
`MeshAssetGpuCatalog.cpp`, transformed per-instance per-frame) against the
real camera's 6 frustum planes and writes exactly one
`VkDrawIndexedIndirectCommand` per surviving instance into a per-batch
indirect-command buffer (`src/Renderer/Culling/GpuDrivenBatchCache.h/.cpp`),
plus an atomic visible count into a companion count buffer (reset to `0` via
a real `vkCmdFillBuffer` pass every frame - the shader can never safely do
this itself, since GLSL compute has no cross-workgroup ordering guarantee
within one dispatch). A new `"GpuDrivenBatches"` `rg::RenderPassProvider`
(`Application::RegisterOffscreenRenderPipelineProviders()`, registered
strictly between `"RenderOpaque"` and `"DrawSkyBackground"`'s own
`Register()` calls - this ordering is real, load-bearing, PROVIDER
REGISTRATION ORDER, since `DetectRenderPassEventContradictions()` cannot
catch a pure write-after-write hazard between two same-tier passes) declares,
per eligible batch, a reset pass, the culling compute pass, and a graphics
pass that reads the resulting buffer back
(`ReadBuffer(indirectCommandHandle, ResourceAccess::IndirectCommandRead)`)
and issues **exactly one** `vkCmdDrawIndexedIndirectCount` (or, on a device
without `drawIndirectCount` - probed once via `Renderer::
SupportsDrawIndirectCount()`/`VulkanDevice::QueryDrawIndirectCountSupport()`,
never assumed - a `vkCmdDrawIndexedIndirect` fallback against a
degenerate-padded command array, always sized to THIS FRAME'S real instance
count, never the cache's own monotonically-growing buffer capacity) via the
new `Renderer::SubmitIndirect()`. Each surviving instance's own model matrix
is read by a genuinely NEW vertex-shader/pipeline variant,
`VertexLayout::PositionNormalInstanced` + `Shaders/MeshInstanced.vert`
(reusing `Shaders/Mesh.frag` unmodified), indexed by `gl_InstanceIndex` from
the SAME per-instance input buffer the culling shader read from - this
engine's existing 128-byte push-constant model-matrix convention is
fundamentally one-draw-one-object and cannot do this, which is why a new
`ResourceAccess::VertexShaderStorageRead` enumerator (distinct from the
existing fragment-only `ShaderRead`) was added specifically for this
buffer's vertex-stage read. **This cutover is GAME VIEW ONLY** - Scene View
and the rare direct-render-to-swapchain fallback both keep rendering EVERY
entity, including every batch-eligible one, through the fully unmodified
per-entity path, forever (confirmed via `ask_questions` during this
campaign's own dedicated PHASE5 pre-implementation review), which is exactly
why the per-batch resource cache is safely keyed by `(MeshHandle,
PipelineHandle)` alone with no view dimension. A new "instances culled this
frame" readout (the Editor's "Render Graph" panel, plus
`POST /spawn_gpu_driven_test_batch` for HTTP-driven validation) surfaces the
GPU-computed visible/culled count live. **Deliberately out of scope**: no
occlusion culling, no hierarchical/two-phase culling, no LOD selection, no
textured/bindless batching, no primitive-shape (`VertexLayout::PositionColor`)
batching, no async compute - see `CAMPAIGN_COMPLETION_REPORT.md`'s own "what
remains genuinely open" section for the full, honest restatement of every one
of these Non-Goals.

Full convention: [docs/conventions/gpu-driven-rendering.md](docs/conventions/gpu-driven-rendering.md).

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

The in-engine Editor module lives under `src/Editor/`, compiled into its own real,
separately-linked static library, `gte_editor` (see "`gte_core`/`gte_editor` Library
Separation" below - `GTE_ENABLE_EDITOR` no longer exists anywhere in this codebase) -
`ImGuiEditorLayer` as its composition root, a shared `EditorContext`, a single
`Selection` gate-keeper for Hierarchy/Project selection changes, `DockLayout`, and a
fixed set of `Panels/*.cpp` builder functions (deliberately not a polymorphic panel
registry).

Full convention: [docs/conventions/editor-module-structure.md](docs/conventions/editor-module-structure.md).

## `gte_core` / `gte_editor` Library Separation

A 19-phase campaign, `editor-core-separation-1`
(`task_manager/editor-core-separation-1/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), turned the one CMake target that used to contain
BOTH the engine and the entire Editor/debug-tooling surface (gated by one
`if(GTE_ENABLE_EDITOR)` block) into **two real, separately-linked static libraries**:
`gte_core.a` (the engine - Renderer, ECS, Game, Render Graph, Time/EngineContext - a
new `gte::Core` class is its public facade) and `gte_editor.a` (depends on
`gte_core.a`, one-way, never the reverse - owns ImGui, gizmos, the Frame Debugger,
every panel, and the authoring window's own SDL/main-loop glue). A new `EditorHost`
class replaced the old `Application` (deleted outright) as the real composition root:
it owns `SdlContext`/`Window`, constructs `Core` by injecting `Window` as an
`ISurfaceProvider&`, constructs the concrete Editor UI, and owns every automation
bridge (`EngineCommandBridge`/`FrameCaptureBridge`/`EditorUiCommandBridge`/
`FrameDebuggerCommandBridge`/`AssetImportCommandBridge`) plus the embedded
`Network::NetworkServer` - `Core` itself stays a pure engine facade with zero HTTP/
automation-bridge knowledge, ever. The built executable was renamed
`GreatTamanaEngine` -> **`GreatTamanaEditor`**, reflecting what it actually is (the
authoring tool). A manually-invocable, CI-only standalone-core probe
(`tools/ci/gte_core_standalone_probe/`) configures ONLY `gte_core` - no `gte_editor`,
no SDL, no ImGui - proving it compiles/archives on its own; a headless
`ISurfaceProvider` test fixture (`tests/Fakes/HeadlessSurfaceProvider.h`, a real
`VK_EXT_headless_surface`-based surface, never a fake pointer) proves `Core` can be
constructed and driven with no real `Window`/SDL involved (it self-skips, correctly,
on any machine whose Vulkan driver lacks that extension). Verified with a full clean
build (497/497 steps) and a full `ctest` regression pass (1773 tests, 100% of executed
tests passing, 2 legitimate environment-gated skips - up from `render-pass-7`'s own
1753 baseline).

**Honest, load-bearing caveat, restated plainly (not silently smoothed over)**: the
campaign's own closeout phase (PHASE19) found, and mechanically reproduced with a
real link attempt, that the design's own "Four Hard Rules" are only PARTIALLY met
today - `RenderSystem.cpp` (`gte_core`) still carries a real, unresolved reference to
`gte::RecordFrameDebuggerDraws()` (`gte_editor`-only), and `Network/NetworkServer.cpp`/
`NetworkRoutes.cpp`/`.h` (`gte_core`) still directly `#include "../Editor/Logger.h"`
and call the real `Logger` class (a pre-existing, already-documented exception, see
"Logging" below) - meaning a thin Player-build host linking `gte_core.a` alone would
NOT yet get a running, renderable engine. Both gaps are pre-existing since early in
the campaign, honestly disclosed at the time, and never assigned to any of the 19
phases to close - see `CAMPAIGN_COMPLETION_REPORT.md`'s own dedicated "Four Hard
Rules" section for the full, itemized evidence and what closing this for real would
require. **The Player Build Pipeline itself remains explicitly, permanently OUT OF
SCOPE** - no `<ProjectName>.exe` generation, no per-project build system, no Player
host template beyond what the standalone-core probe already needs - this campaign
proves `gte_core.a` is heading in the right direction for that future initiative, it
does not build it.

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
