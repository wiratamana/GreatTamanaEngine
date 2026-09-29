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

A follow-up bug-fix campaign, `editor-core-separation-20` (three phases,
`task_manager/editor-core-separation-20/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), fixed a confirmed, user-reported bug where disabling built-in
passes via the "Render Graph" panel / `GET /render_graph/set_pass_enabled` could turn the Game/Scene
View solid magenta/pink instead of a sane fallback, and a second, independent bug where 5 real
Atmosphere passes' own "Enabled" checkbox was completely inert. **Root cause 1**: the Game/Scene
View's own `CLEAR` load-op used to be a side effect bolted onto `"RenderOpaque"` - the very pass a
user is most likely to disable first - so disabling it left the view target's color+depth attachment
never cleared that frame (`LOAD_OP_LOAD` on undefined memory, which this GPU/driver combination
happens to display as flat magenta). Fixed by a new, permanently deny-listed (`RenderPassToggleRegistry::
IsDenyListed()`), always-on `"ClearViewTarget"` provider (`Core.cpp`, `RenderPassEvent::BeforeEverything`
- the first real production consumer of that enum value) that owns the one guaranteed clear and
unconditionally appends the view's own target handle to `frame.finalTextureOutputs` every frame, so it
(and the write-after-write chain built on it) always survives `RenderGraphCompiler::Compile()`'s
backward-reachability culling even when every other pass that would otherwise read that resource back
is disabled. `"RenderOpaque"` itself switched its own color/depth attachment writes from `CLEAR` to
`LOAD`, since it no longer owns the clear. **Root cause 2**: `AtmosphereTransmittanceLutPass`/
`AtmosphereMultiScatteringLutPass`/`AtmosphereSkyViewLutPass`/`AtmosphereAerialPerspectiveVolumePass`/
`AtmosphereAerialPerspectiveCompositePass` are all declared via direct `builder.AddRenderPass()` calls
inside `AtmosphereLutRenderer.cpp`, which never consulted `RenderPassToggleRegistry` at all - toggling
them off via the panel/HTTP updated bookkeeping only, with the pass still running every frame
unconditionally. Fixed by a new, pure, Tier-1-tested helper,
`src/Renderer/Atmosphere/AtmospherePassToggleLogic.h`'s `ShouldDeclareAtmospherePassThisFrame()`,
threaded through all 5 methods (plus `AtmospherePassSequence.h/.cpp`'s 3 wrapper functions) with
cascading, `IsValid()`-based upstream-handle skip logic through the whole LUT dependency chain
(Transmittance -> MultiScattering -> SkyView/AerialPerspectiveVolume -> Composite) - a real,
previously-latent engine-crashing hazard (an out-of-bounds `physicalVolumeTextures[0xFFFFFFFF]` access
inside `RenderGraph::ApplyUsageBarrierIfNeeded()`, reachable the instant an upstream LUT pass was
disabled while the Frame-Debugger-only `AddAerialPerspectiveVolumeDebugSlicePass()` call still ran
unconditionally) was found and fixed live during this campaign's own investigation, confirmed via a
dedicated regression check before this campaign shipped. **Explicitly, permanently out of scope**:
`AddAerialPerspectiveVolumeDebugSlicePass()` itself never gained its own toggle-registry consult (only
its caller gained a validity guard); the Plugin Render Feature system (`RenderFeatureCompositor`,
`demo_render_feature*` plugins) was never touched, including its own separately-inert demo-plugin clear
toggles; `src/Application/RenderPasses.cpp`'s confirmed-dead `AddRenderOpaquePass()`/
`AddDrawSkyBackgroundPass()` free functions were never touched. Verified with a full clean build, a full
`ctest` regression pass (1993 tests, 100% of executed tests passing, 8 legitimate environment-gated
skips - this campaign's own attributable contribution is exactly +4 tests over its own actual starting
point, since the prior documented `editor-core-separation-15` baseline of 1934 tests/7 skips had already
drifted upward by +55 tests/+1 skip via undocumented interim work before this campaign even started -
see `CAMPAIGN_COMPLETION_REPORT.md` for the full, honest accounting), and a live, HTTP-driven smoke test
confirming all 5 of this campaign's own success criteria: disabling every content/atmosphere pass shows
a solid, defined dark clear color (never magenta) with `"ClearViewTarget"` confirmed genuinely
un-culled; disabling only `RenderOpaque` still shows a real, visible rendered sky; disabling
`AtmosphereTransmittanceLutPass` alone genuinely removes it and its whole downstream cascade from the
render graph with zero crash; and re-enabling everything returns to the exact prior, byte-identical
baseline image.

A follow-up bug-fix campaign, `editor-core-separation-21` ("The Engine Is Lying", six phases,
`task_manager/editor-core-separation-21/PHASE0_MASTER_STRATEGY.md`, `CAMPAIGN_COMPLETION_REPORT.md`),
fixed a confirmed, user-reported bug where disabling `AtmosphereAerialPerspectiveCompositePass` via the
"Render Graph" panel / `GET /render_graph/set_pass_enabled` left the Frame Debugger's own event tree still
showing it as a REAL, EXECUTED compute-dispatch event, fully populated with live GPU-timing/read/write
data. **Confirmed root cause (PHASE1), mechanically, with log evidence — NOT a toggle-registry
correctness bug**: the render-pass-toggle mechanism itself was already 100% correct every frame; the real
bug is a genuine, reproducible staleness in `FrameDebuggerPanel::CaptureNowFromCommand()` (the handler
behind `GET /frame_debugger/capture`) - it only ARMS a deferred capture trigger and returns immediately,
before the real capture (`TriggerCapture()`, deferred by design since the `frame-debugger-7` campaign)
actually runs on the next engine frame, so the very first `GET /frame_debugger/capture` issued after any
toggle mutation always reports whatever the PREVIOUS, already-completed capture produced. **PHASE2's fix**:
a new, pure `RenderPassToggleChangeDetectionLogic.h`'s `DidRenderPassToggleEnabledStatesChange()` detects a
real enabled-state change across two `RenderPassToggleRegistry::ListAll()` snapshots, wired into a FOURTH
automatic Frame Debugger capture trigger (joining the pre-existing Enable-edge / Step / explicit "Capture"
button) via two mutation paths - `RenderGraphPanel::Build()`'s own checkboxes set a new
`EditorContext::renderPassToggleRegistryChangedThisFrame` flag `FrameDebuggerPanel::Build()` consumes
same-frame, and `GET /render_graph/set_pass_enabled`'s `EditorHost.cpp` handler (which runs BEFORE
`BuildUI()` even starts that frame) calls `IEditorLayer::FrameDebuggerCaptureNow()` directly - so a toggle
mutation now genuinely, automatically triggers a fresh capture, closing the staleness window for good.
**PHASE3's systemic audit found ONE root cause behind almost every other "lie" in the engine**:
`RenderGraphPanel::BuildPassRow()` draws an identical, apparently-functional "Enabled" checkbox for EVERY
pass name in a captured snapshot, with zero knowledge of whether that pass's own declare-time code path
actually consults `RenderPassToggleRegistry` at all - a pass declared via the generic
`RenderPipeline::DeclareOnePhase()` flush loop is honestly gated for free, but a pass declared via a
DIRECT `builder.AddRenderPass()` call bypasses this entirely unless its own call site was hand-written to
separately consult the registry. Six such Confirmed-Lie instances were live-confirmed and ALL SIX fixed by
PHASE4 (per an explicit `ask_questions` decision to fix every one, with zero permanent exceptions carried
over): the already-known `DemoRenderFeaturePlugin_Clear`/`DemoRenderFeatureSecondPlugin_Clear` inert
plugin-demo toggles (`PluginRenderPassBuilderAdapter`), `ComputeBlurValidation`'s and
`GBufferValidation`/`GBufferValidationCopy`'s own per-row checkboxes (previously cosmetic despite each
having a real, separate, ALREADY-honest feature toggle of its own -
`ctx.showBlurredSceneOutput`/`showGBufferValidationOutput` - the registry consult is now an ADDITIONAL,
independent layer of granularity on top of that, with `GBufferValidation::AddPass()` gaining genuinely
independent per-half gating), `AtmosphereAerialPerspectiveVolumeDebugSlicePass`,
`AddGpuSkinningPasses()`'s direct-render-to-swapchain fallback (now sharing the SAME `"GpuSkinning"`
registry entry the offscreen provider already used, so one checkbox honestly gates both reachable code
paths), and `FrameDebuggerReplayStepN`'s dynamically-named per-replay-step passes (gated by a single new
whole-mechanism `"FrameDebuggerReplay"` toggle, mirroring the `AddGpuSkinningPasses()` precedent rather
than inventing one registry entry per ever-growing dynamic name). **PHASE5 made the iron rule
self-enforcing, permanently, in code**: `DetectRenderPassHonestyMismatches()`
(`src/Editor/RenderPassHonestyChecker.h/.cpp`, pure, Tier-1-tested, mirrors `ImGuiIdConflictTracker.h`'s
own precedent) compares a captured `RenderGraphSnapshot`'s own non-culled passes against
`RenderPassToggleRegistry`'s recorded enabled state, and `RenderPassHonestyGuard`
(`src/Editor/RenderPassHonestyGuard.h/.cpp`, mirrors `ImGuiIdConflictGuard`'s log-once-per-new-incident
shape) fires a `GTE_LOG_ERROR("RenderPassHonesty", ...)` the instant a future regression of this exact bug
class reappears, wired into `FrameDebuggerPanel::TriggerCapture()` - the same chokepoint PHASE1's own
diagnostic logging used. Verified live: deliberately reintroducing the original bug made the detector fire
a real, fresh log entry immediately (confirmed via `GET /get_logs?category=RenderPassHonesty`), the
already-fixed production code stayed completely silent (zero false positives), and the temporary
reintroduction hack was fully reverted (confirmed via `git diff --stat` showing zero net change). Verified
with a full clean build (603/603 steps, zero errors), a full `ctest` regression pass (2004 tests, 100% of
executed tests passing, 8 legitimate environment-gated skips - up from `editor-core-separation-20`'s own
1993/8 baseline, a clean +11 from this campaign's own PHASE2 (5 tests) and PHASE5 (6 tests)), and a final,
live, HTTP-driven, end-to-end verification reproducing the FULL original bug report end-to-end one last
time: disabling `AtmosphereAerialPerspectiveCompositePass` now genuinely removes it from the very next
Frame Debugger capture with zero extra manual re-capture needed, re-enabling restores it, the Game View
still renders the exact same byte-identical (158923-byte) sane image throughout, and
`GET /get_logs?category=RenderPassHonesty` stayed completely empty across the whole verification sequence.
**Honest, permanent limitation, restated plainly**: `AddGpuSkinningPasses()`'s direct-render-to-swapchain
fallback branch and `FrameDebuggerReplayStepN`'s own per-step declaration could not be DIRECTLY,
live-exercised through their own dishonest branch this session either before or after the fix (the former
needs both the Game View AND Scene View panels hidden simultaneously - no Editor/HTTP control exists to
force that; the latter's own capture snapshot has already rolled forward past the one frame that declares
these passes by the time any HTTP response is built) - both fixes are proven correct by direct code
reading (the guard is unconditional and runs identically regardless of which caller reaches it) plus their
own registry entry now genuinely existing and mutating cleanly, the same evidentiary standard PHASE3's own
audit already used for these two findings. See `docs/conventions/render-pass-toggle-honesty.md` for the
permanent, standalone reference on how to correctly gate any FUTURE pass that bypasses the generic
`RenderPipeline::DeclareOnePhase()` flush loop, and
`task_manager/editor-core-separation-21/CAMPAIGN_COMPLETION_REPORT.md` for the full six-phase writeup.

Full history: `task_manager/render-pass-1/PHASE0_MASTER_STRATEGY.md`,
`task_manager/render-pass-2/PHASE0_MASTER_STRATEGY.md`,
`task_manager/render-pass-3/PHASE0_MASTER_STRATEGY.md`,
`task_manager/render-pass-4/PHASE0_MASTER_STRATEGY.md`,
`task_manager/render-pass-6/PHASE0_MASTER_STRATEGY.md`,
`task_manager/render-pass-7/PHASE0_MASTER_STRATEGY.md`,
`task_manager/editor-core-separation-20/PHASE0_MASTER_STRATEGY.md`, and
`task_manager/editor-core-separation-21/PHASE0_MASTER_STRATEGY.md`, and each
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

## ImGui Widget ID Uniqueness

`src/Editor/ImGuiUniqueId.h` (`gte::ScopedUniqueId`) is the ONE mandated way
to enter a per-iteration Dear ImGui ID scope for any widget built inside a
loop over runtime data - construct it with the loop's own distinct iteration
index (never a data-derived string alone) plus an optional human-readable
debug key, and let it manage `ImGui::PushID()`/`PopID()` for you. This
exists because a real, live bug shipped from doing exactly the unsafe thing
this class now prevents: `src/Editor/Panels/RenderGraphPanel.cpp`'s
"Offscreen Regime" table legitimately shows the same render-graph pass name
twice in one frame (Game View and Scene View both declare an Atmosphere LUT
pass under the same hard-coded name - a permanent, intentional design
choice, `task_manager/editor-core-separation-10` campaign,
`PHASE0_MASTER_STRATEGY.md` section 2.2), and that file's own checkbox ID
used to be built purely from that (sometimes-duplicate) pass name, so Dear
ImGui's own built-in `io.ConfigDebugHighlightIdConflicts` safety net (left
at its default `true`, still on today, still a final backstop) would flash
a "Programmer error: N visible items with conflicting ID!" red-highlight
the instant either row was hovered. A second, proactive layer,
`src/Editor/ImGuiIdConflictTracker.h`/`ImGuiIdConflictGuard.h` (the former
pure and Tier-1-tested, the latter the real ImGui/`Logger`-aware singleton,
reset once per frame from `ImGuiEditorLayer::NewFrame()`), catches any
future collision the moment it happens and logs it exactly once per new
incident via `GTE_LOG_ERROR("ImGuiIdConflict", ...)` - visible in the "Log"
panel and `GET /get_logs?category=ImGuiIdConflict` - rather than relying on
a human happening to hover the right widget and correctly recognizing what
Dear ImGui's own red highlight means. Every pre-existing `PushID()` call
site under `src/Editor/` was migrated onto `ScopedUniqueId` by this same
campaign, so this is a real, engine-wide, present-day guarantee, not just a
rule for new code.

Full convention: [docs/conventions/imgui-id-uniqueness.md](docs/conventions/imgui-id-uniqueness.md).

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

## Plugin Architecture

A real, dynamic, runtime-loadable `.dll` plugin system (`editor-core-
separation-3` campaign, `task_manager/editor-core-separation-3/
PHASE0_MASTER_STRATEGY.md`) - a feature ships as one or more `.dll`s dropped
into a `plugins/` folder next to the built executable, discovered and used
by the running engine with zero recompilation of the engine itself and zero
per-plugin code inside `gte_core`/`gte_editor`. PHASE1 laid the foundation:
`plugins/gte_plugin_abi/` (`GtePluginAbiFingerprint`, `IPluginModule`, the
three fixed `GTE_*` `extern "C"` exports), a byte-for-byte fingerprint gate
checked first, always (a mismatch is a clean, logged skip, never a crash),
and the reusable `gte_apply_plugin_shared_crt_linkage()` CMake helper
(`cmake/MingwRuntime.cmake`) every target on either side of the plugin ABI
boundary must call. A plugin `.dll` NEVER links or calls a real
`gte_core`/`gte_editor` symbol directly - only small, curated, pure-virtual
wrapper interfaces using solely plain built-in C++ types, implemented
host-side by a thin adapter forwarding to the real internal type; neither
`gte_core.a` nor `gte_editor.a` ever becomes a `SHARED`/`.dll` target.
**Honest, load-bearing caveat**: this repository's own default toolchain (as
of PHASE1) was built `--disable-shared` and cannot produce a shared-CRT-
linked binary at all - `gte_apply_plugin_shared_crt_linkage()` detects this
at configure time and is a clean, honest no-op (fingerprint
`sharedRuntimeLinkage` correctly reads `0`) until a dedicated later decision
actually switches the active toolchain.

A five-phase campaign, `editor-core-separation-9`
(`task_manager/editor-core-separation-9/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), shipped a second, strictly ADDITIVE
render-feature-authoring surface, `IPluginRenderPassBuilder_v3` (paired with a
new `IRenderFeatureModule_v3`), fixing the closed-enumeration problem
`IPluginRenderPassBuilder_v2` had - `_v2` hardcodes exactly 3 fixed C++ methods
(`AddSolidFillPass`/`AddRadialVignettePass`/`AddColorGradePass`), each bound to
one `opCode` inside one host-owned uber compute shader, so a plugin author can
never add a genuinely new visual effect and "a compute pass writes a
texture/buffer, a later pass reads it" is impossible. `_v3` instead gives a
plugin a real, curated, two-phase setup/execute resource-graph builder
(`PluginTextureHandle`/`PluginBufferHandle`, `CreateTexture`/`CreateBuffer`/
`ReadTexture`/`WriteTexture`/`ReadBuffer`/`WriteBuffer`/`WriteColorAttachment`,
mirroring `rg::RenderGraphBuilder::PassBuilder` almost exactly but ABI-safe)
plus a growable, string-keyed, HOST-OWNED `PluginRenderOperationRegistry` a
plugin calls through (`IPluginCommandRecorder::Dispatch(opId, ...)`/
`DrawFullscreenTriangle(opId, ...)`) instead of one fixed method per effect -
a brand-new operation (this campaign shipped two: `gte.builtin.box_blur` and
`gte.builtin.blit_fullscreen`) is now a HOST-SIDE CONTENT ADDITION, never an
ABI change. `_v3` is now the RECOMMENDED path for new plugin authors going
forward - `_v2` (and `_v1`) remain fully supported forever, never touched,
deprecated, or removed. The campaign's own concrete, permanent proof is a real
2-pass GPU downsample-blur demo plugin (`plugins/demo_render_feature_v3/`): a
compute pass reads the new curated `"SceneColor"` named resource and writes a
transient half-res texture via `gte.builtin.box_blur`, then a graphics pass
reads that texture and draws it into the plugin's own private compositing
target via `gte.builtin.blit_fullscreen` - verified with a real, live,
HTTP-driven, mathematically-checked pixel proof (sharp-vs-blurred captures
matching the shader's own hand-computed kernel radius). `_v3` reuses the EXACT
SAME per-plugin private-target + 5-mode blend pipeline `_v2` already has
(`RenderFeatureCompositor`) - it only changes HOW a plugin's own private
target gets filled, never how it gets composited afterward - and every `_v3`
pass is, under the hood, a REAL `rg::PassRecord` produced by the SAME
`RenderGraphBuilder::AddRenderPass()` chokepoint every internal engine pass
uses, so it is already generically visible in `GET /render_graph`/the Editor's
"Render Graph" panel with zero panel/JSON code change (a small, additive
`is_v3` field was added to the separate, per-loaded-plugin "Plugin Render
Features" section only, so a `_v3` row is visually distinguishable from a
`_v2` row at a glance). A new `IPluginBlackboard::Publish`/`Fetch` gives two
independently-loaded `_v3` plugins a generic, ABI-safe, string-keyed
cross-plugin data hand-off, proven by a real, live, VISIBLE demo (one plugin's
published blur strength widens a second, unrelated plugin's own vignette
radius). **Honest, load-bearing caveat, restated plainly**: `Dispatch()`'s
group-count cap (64x64x1 groups per call, host-enforced with a loud warning
and a clean skip, never a crash) has NO device-lost recovery path anywhere in
this engine today - a plugin that somehow drives the GPU into a lost-device
state has no documented recovery story, exactly as true for every other GPU
workload this engine already runs. No plugin-supplied shader bytecode of any
kind is possible yet - `PluginRenderOperationRegistry` remains 100%
host-authored/curated this whole campaign, a separate, future,
security-reviewed campaign's job. Verified with a full clean build, a full
`ctest` regression pass (1882 tests, 100% of executed tests passing, 2
legitimate environment-gated skips - unchanged from this campaign's own PHASE1
baseline), and a live, HTTP-driven smoke test with BOTH `_v2` and `_v3` demo
plugins loaded and rendering simultaneously, exercising resource creation, the
operation registry, and the blackboard all at once with zero unexpected
warnings or errors. See
`task_manager/editor-core-separation-9/CAMPAIGN_COMPLETION_REPORT.md` for the
full five-phase writeup.

Full convention: [docs/conventions/plugin-architecture.md](docs/conventions/plugin-architecture.md).

## Project Assembly System

A per-developer, single-project, `.gitignore`d source tree (`Projects/<Name>/`)
holding REAL, user-authored C++ and GLSL shader source that compiles into two
ordinary Windows `.dll`s (`<Name>_Game.dll`, `<Name>_Editor.dll`) which
`GreatTamanaEditor.exe` loads at its own startup and which then call real,
live, non-ABI-wrapped engine types (`gte::Core&`, real ImGui, real
`rg::RenderGraphBuilder`) directly - zero recompilation of the engine itself
for a content change, and zero new per-feature ABI surface to design/maintain
(unlike `plugins/gte_plugin_abi`, which this system never touches, edits, or
depends on - `editor-core-separation-11` campaign,
`task_manager/editor-core-separation-11/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`).

**Permanent toolchain disclosure (do not lose this again).** Every campaign
through `editor-core-separation-9`/`editor-enchancements-1` ran against this
repo's OLD default toolchain (`scoop/apps/gcc/current`, built
`--disable-shared`), which could not produce a shared-CRT-linked binary at
all - see the "Plugin Architecture" section above's own honest caveat.
Sometime between `editor-core-separation-9` and 2026-09-28, **the active
toolchain was switched, quietly, outside any tracked campaign**, to a
shared-CRT-capable GCC 16.2.0 `mingw-builds-binaries` toolchain
(`scoop/apps/mingw/current`) - confirmed, mechanically, at the start of this
campaign's PHASE1: `build/CMakeCache.txt`'s `CMAKE_CXX_COMPILER` points at
`scoop/apps/mingw/current/bin/c++.exe`,
`GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED` reads `TRUE`, and the generated
plugin ABI fingerprint's `sharedRuntimeLinkage` field reads `1`, not `0`. This
whole "Project Assembly" system is only possible BECAUSE this switch already
happened - a `.dll` that links against `GreatTamanaEditor.exe`'s own import
library and freely passes real `std::string`/`std::vector` values across that
boundary is only safe on a shared-CRT toolchain. **Nobody should ever again
read an older campaign's own "no actual switch happened" caveat and assume it
is still true today** - this section is the permanent, load-bearing
correction. The existing default `build/` tree already IS the shared-CRT
tree; no separate `build-shared-crt` tree was ever created or is ever needed.

**System shape:**
- `Projects/<Name>/{Assets,Libraries}` - `Assets/` holds a project's own C++
  (`.cpp`)/shader (`.vert`/`.frag`/`.comp`) source, with an `Assets/Editor/`
  sub-folder for `_Editor.dll`-only source; `Libraries/CMakeLists.txt` calls
  `gte_add_project(<Name>)` (`cmake/GteProject.cmake`), which auto-discovers
  every source file (`CONFIGURE_DEPENDS` globs, never a hand-maintained
  list), builds the shader pipeline (`gte_add_project_shaders()`, a thin
  wrapper around the engine's own unmodified `gte_add_shader()`), applies the
  new, independent `gte_apply_project_assembly_shared_crt_linkage()` CRT
  linkage (`cmake/MingwRuntime.cmake` - never
  `gte_apply_plugin_*_shared_crt_linkage()`, a different, unrelated
  mechanism), and propagates every header search path `gte_core`/`gte_editor`
  themselves declare `PUBLIC` via explicit
  `$<TARGET_PROPERTY:<dep>,INTERFACE_INCLUDE_DIRECTORIES>` generator
  expressions (needed because `GreatTamanaEditor` links `gte_editor`
  `PRIVATE` - CMake usage-requirement propagation does not flow past that).
- **One new CMake option, `GTE_ENABLE_PROJECT_ASSEMBLIES` (default `ON`)**,
  gates the entire system end to end: both the root `CMakeLists.txt`
  auto-discovery `add_subdirectory()` loop over `Projects/*/Libraries/` and
  the runtime `Core::LoadProjectAssemblies()` call site
  (`src/Editor/EditorHost.cpp`, its own constructor) - completely independent
  of `GTE_ENABLE_PLUGINS`, the OTHER, unrelated system's flag.
  `src/Editor/ImGuiEditorLayer.cpp`'s per-frame `EditorPanelRegistry::
  PluginPanels()` `BuildPanel()` call loop is gated by
  `#if GTE_ENABLE_PLUGINS || GTE_ENABLE_PROJECT_ASSEMBLIES` (both systems
  share that one registry) so a Project Assembly Editor panel still draws
  even with `GTE_ENABLE_PLUGINS=OFF`.
- `src/Core/Plugins/ProjectAssemblyHost.h/.cpp` - a `PluginHost`-shaped
  runtime loader, `LoadLibraryW()`-ing every `<Name>_Game.dll`/
  `<Name>_Editor.dll` under `<exe dir>/project_assemblies/` exactly once, at
  startup, and calling its ONE fixed `GTE_RegisterProject` export
  (`Libraries/ProjectAssemblyExports.h`'s `GTE_DEFINE_PROJECT_EXPORTS_GAME`/
  `_EDITOR` macros) with a real, live `gte::Core&` (and, for `_Editor.dll`, a
  real `gte::EditorHost&`) - never re-scanned, never `FreeLibrary()`'d before
  process exit (no hot reload, ever, by design).
- `src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp` -
  `TriggerProjectAssemblyCompile()`, a real, non-blocking `cmake --build`
  child process on its own dedicated `JobSystem::RegisterBackgroundThread()`
  thread (never `Schedule()`, which would tie up a fixed worker-pool thread
  for a multi-minute build), streaming output into `GTE_LOG_INFO`/
  `WARNING`/`ERROR`. **Known, permanent limitation, not a bug**: a Project
  Assembly `.dll` already loaded by the CURRENTLY RUNNING instance can never
  be successfully recompiled by that same instance (Windows locks a mapped
  DLL image against being overwritten by the linker) - the realistic
  workflow is edit source, close the Editor, rebuild, relaunch.
- Two real, working capabilities are proven end-to-end by one shared,
  permanent smoke-test fixture, `Projects/ProjectAssemblyProbe/` (never
  deleted - this system's own permanent regression fixture, mirroring how
  `plugins/demo_hello_world/` serves that role for `gte_plugin_abi`):
  1. **A custom Editor panel** ("Probe Panel") - `ProjectAssemblyProbe_Editor.dll`
     implements `gte::IEditorPanelModule_v1` directly, ignoring the ABI's
     `ctx` parameter, calling real `ImGui::*` functions, registered through
     the existing, unmodified `EditorPanelRegistry::RegisterPluginPanel()`.
  2. **A custom render-graph pass** ("ProjectAssemblyProbe.FillTexture") -
     `ProjectAssemblyProbe_Game.dll` calls the new public
     `Core::RegisterProjectRenderPassProvider()` (a thin pass-through onto
     `m_offscreenRenderPipeline.Register()`, mirroring `Core::LoadPlugins()`'s
     own "private member, public forwarder" shape), mints its own transient
     256x256 texture via `RenderGraphBuilder::CreateTexture()` (called
     directly inside the provider's lambda, since `PassBuilder` itself has no
     `CreateTexture()`), and fills it with a real compute shader
     (`project_assemblies/shaders/ProbeCompute.comp.spv`) - visible in both
     the Editor's real "Render Graph" panel and `GET /render_graph`'s JSON.
- **Explicit Non-Goals** (never expand scope onto these without a new,
  separate campaign): no gameplay/"MonoBehaviour"-style scripting bridge, no
  hot reload, no cross-machine/cross-toolchain portability (`Projects/` is
  `.gitignore`d, single-developer, same-build-run only), no separate Player
  executable, no scaffolding/"New Project" wizard (a human creates
  `Projects/<Name>/{Assets,Libraries}` by hand), no UI-design decision for
  where a permanent "Compile" button/menu item lives.
- **Confirmed-unsafe, deliberately deferred future work**: a Project
  Assembly's own render pass writing directly into the real, on-screen Game
  View (via `Core::GetGameViewTargetThisFrame()` + a second
  `ImportTexture()` of that same physical resource) is NOT safe as this
  engine's `RenderGraph` exists today - `RenderGraph::EnsureTextureResolved()`
  tracks resource state PER `TextureHandle`, never per underlying physical
  resource, so a second, independent `ImportTexture()` of an
  already-imported physical image gets NO automatic memory barrier against
  the engine's own internal Game-View-compositing chain's reads/writes of
  that identical image - a real, structural gap (no mechanism exists
  anywhere in this codebase to tell the compiler "this handle aliases that
  other, already-tracked handle"), not merely an untried idea. A Project
  Assembly's render-graph capability is therefore proven ONLY as far as the
  Render Graph panel/HTTP endpoint, never on-screen compositing, until a
  future campaign adds real handle-aliasing support to `RenderGraphBuilder`.

Full convention: [docs/conventions/project-assembly-system.md](docs/conventions/project-assembly-system.md).

### Project Assembly Hot Reload — Live Debug Surface (BIG-STEP 1 of 4 - the whole 4-BIG-STEP effort is now COMPLETE, see "BIG-STEP 4" below)

A four-phase campaign, `editor-core-separation-12`
(`task_manager/editor-core-separation-12/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), gave the Project Assembly system above its
first HTTP visibility - implementing ONLY "BIG-STEP 1" (live debug + compile/
reload triggers) of a larger four-part external master plan - BIG-STEP 2
(teardown safety/registration ledger), BIG-STEP 3 (synchronous compile/atomic
swap orchestrator), and BIG-STEP 4 (state snapshot/restore) were, AT THE TIME
this campaign shipped, deliberately deferred future campaigns - **all three
are now DONE too, see "BIG-STEP 4" below for the closing writeup; this
sentence is kept, historically accurate for what was true when this
campaign itself shipped, not silently updated to imply BIG-STEP 1 shipped
the whole effort**.
A new Bucket B
capability interface, `IHotReloadDebugCapability`
(`src/Core/EditorCapabilities.h`), implemented by `EditorHotReloadDebugCapability`
(`src/Editor/EditorHotReloadDebugCapability.h/.cpp`, wired into `NetworkServer`'s
new 8th constructor parameter exactly like `EditorLogQueryCapability`'s own
precedent), backs 7 new routes: `GET /project_assembly/hot_reload/status`
(always reports `"Idle"` today - a real, new `ProjectAssemblyHotReloadDebugStatus`
push-status singleton nothing yet calls `Set()`/`Finish()` on), `GET
/project_assembly/debug/ledger?name=<X>` and `GET
/project_assembly/debug/loaded_assemblies` (honest, permanent, always-empty
placeholders until BIG-STEP 2 adds a real registration ledger/`ProjectAssemblyHost`
introspection), `GET /project_assembly/debug/component_types` (genuinely
real, live `ComponentTypeRegistry` data today), `GET
/project_assembly/debug/scene_snapshot` (genuinely real, live `SceneDocument`
JSON of the running ECS world - routed through a new
`EngineCommandKind::GetSceneSnapshot` + `EngineCommandBridge`, main-thread-only,
deliberately NOT a direct network-thread `Registry` read, since the live ECS
`Registry` has no thread-safety mechanism of its own), `POST
/project_assembly/debug/compile_only?name=<X>` (a real, already-existing
`cmake --build` trigger - `TriggerProjectAssemblyCompile()`'s return type
changed `void`->`bool` so this route can honestly report
`{"started":false,"reason":"a build for this project is already in
progress"}` for a racing second request), and `POST
/project_assembly/hot_reload?name=<X>` (a stable, permanent `501 Not
Implemented` placeholder contract for a future BIG-STEP 3 campaign to fill in
without ever changing its own shape). A new, tiny, shared
`HotReloadEngineStateMutex` (`src/Core/Plugins/HotReloadEngineStateMutex.h/.cpp`)
protects the three ledger/loaded-assemblies/component-types OBSERVE routes -
any future BIG-STEP 2/3 mutator is REQUIRED to also lock it. Verified with a
full clean build, a full `ctest` regression pass (1903 tests, 100% of
executed tests passing, 2 legitimate environment-gated skips - up from
`editor-core-separation-11`'s own 1888 baseline), and a live, HTTP-driven
10-point verification against a real running `GreatTamanaEditor.exe`,
including a real `compile_only` build of `Projects/ProjectAssemblyProbe`
streaming into `GET /get_logs` and a confirmed in-flight-guard race. See
`task_manager/editor-core-separation-12/CAMPAIGN_COMPLETION_REPORT.md` for
the full four-phase writeup.

### Project Assembly Hot Reload — Teardown Safety & Registration Ledger (BIG-STEP 2)

A five-phase campaign, `editor-core-separation-13`
(`task_manager/editor-core-separation-13/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), implements "BIG-STEP 2" (teardown safety +
registration ledger) of the same four-part external master plan BIG-STEP 1
(above) established; BIG-STEP 3 (synchronous compile/atomic swap
orchestrator) and BIG-STEP 4 (state snapshot/restore) were, AT THE TIME
this campaign shipped, FULLY UNIMPLEMENTED - `POST
/project_assembly/hot_reload` still answered a permanent `501` back then;
**both are now DONE, see "BIG-STEP 4" below - this sentence is kept as an
accurate historical snapshot of this campaign's own moment in time, not
updated to imply otherwise**.
This campaign's own success bar: an already-loaded Project Assembly can now be
safely, cleanly UNLOADED, on command, leaving zero dangling pointers anywhere
in the engine and the process running normally afterward - proven live, not
just by code review. `ComponentTypeRegistry::UnregisterDescriptor(typeName)`
(`src/ECS/Reflection/ComponentTypeRegistry.h/.cpp`) removes a previously
registered component descriptor so the same typeName can be re-registered
without tripping `RegisterDescriptor()`'s own duplicate-registration assert
(Hazard 1). `EditorPanelRegistry::UnregisterPluginPanel(name)`
(`src/Core/EditorPanelRegistry.h/.cpp`) removes a panel from BOTH
`m_pluginPanels` AND `m_allNames` (Hazard 2) - the `m_allNames` half is a
deliberate fix beyond the external plan's own sketch, which left it stale;
leaving it stale would have permanently blocked every reload after the first
one from ever re-registering the same panel name again. A new class,
`ProjectAssemblyRegistrationLedger`
(`src/Core/Plugins/ProjectAssemblyRegistrationLedger.h/.cpp`, its own private
mutex, never `HotReloadEngineStateMutex`), wraps
`Core::RegisterProjectRenderPassProvider()`/`EditorPanelRegistry::
RegisterPluginPanel()`/`ComponentTypeRegistry::RegisterDescriptor()` so every
render-pass/panel/component-type name a given Project Assembly's own
`GTE_RegisterProject` call registers is recorded under that project's own
ledger entry (`BeginRecordingFor()`/`EndRecording()` bracket both
`ProjectAssemblyHost::TryLoadOneAssembly()` call sites), and
`UnregisterEverythingFor(projectName, core)` tears every one of them back
down, in reverse order, in one call.
`ProjectAssemblyHost::UnloadProjectAssembly(projectName, core, renderer)`
(`src/Core/Plugins/ProjectAssemblyHost.h/.cpp`) is the new orchestration
point: `renderer.WaitForGpuIdle()` (Hazard 4) -> ledger teardown -> `FreeLibrary()`
(Editor `.dll` first, then Game), in that exact, non-negotiable order;
`GetLoadedAssemblyFileNames()` is a plain accessor for what is currently
loaded. `ProjectAssemblyBuildRunner` gained
`BackupProjectAssemblyBinaries()`/`RestoreProjectAssemblyBinariesFromBackup()`
(`.hotreload_backup/` next to `project_assemblies/`), purely file-based, no
live engine state, so a future BIG-STEP 3 rollback is physically possible.
`EditorHotReloadDebugCapability::GetLedgerEntry()`/`GetLoadedAssemblyFileNames()`
now return REAL data (previously permanent BIG-STEP 1 placeholders); their
signatures, and the whole `IHotReloadDebugCapability` interface, are
unchanged. `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` gained a
throwaway custom ECS component, `ProbeHotReloadMarker`, specifically so this
campaign's own live isolation test exercises Hazard 1 for real (a genuine
Project Assembly `.dll`, not just an isolated unit test) - it carries no
meaningful runtime value and is never attached to an entity (state
snapshot/restore is BIG-STEP 4's job). Two genuine, previously-latent
dangling-pointer/null-pointer defects were found and fixed live by this
campaign's own isolation test, both now permanently guarded against for any
future Project Assembly, not just this one: (1)
`ComponentTypeRegistry::Instance()`'s lazy `RegisterBuiltinComponentReflections()`
bootstrap could fire from INSIDE a Project Assembly's own registration
bracket if that assembly's own `RegisterComponentType<T>()` call happened to
be the first ever call into that registry in the whole process, silently
misattributing every built-in component type to that assembly's own ledger
entry - fixed by forcing the bootstrap, unconditionally, at the very top of
`ProjectAssemblyHost::LoadProjectAssemblies()`, before any assembly's own
bracket can ever open; (2) `RenderGraphNameSlotTable`
(`src/Renderer/RenderGraph/RenderGraphNameSlotTable.h`) and
`RenderGraph::NamedStats`/its own overflow-report vectors
(`src/Renderer/RenderGraph/RenderGraph.h/.cpp`) used to store a raw,
non-owning `const char*` per pass name, correct only as long as every
debugName is a process-lifetime string literal - a Project Assembly's own
debugName literal lives inside that assembly's own `.dll` image and stops
being valid the instant `FreeLibrary()` runs; since these tables persist a
name PAST any one pass's own single-frame declaration lifetime, the very
next frame after an unload crashed the whole process
(`RenderGraph::FinalizeSynchronousGpuTiming()`'s own per-frame readback loop
dereferencing an already-unmapped pointer) - fixed by making both tables OWN
a `std::string` copy of every name instead. Verified with a full clean
build, a full `ctest` regression pass (1925 tests, 100% passing, 5
legitimate environment-gated skips - up from `editor-core-separation-12`'s
own 1903 baseline; one genuine regression this campaign's own full-suite run
surfaced and fixed - `EditorHotReloadDebugCapability::
GetLoadedAssemblyFileNames()` unconditionally dereferencing a possibly-null
`ProjectAssemblyHost*`, crashing a pre-existing BIG-STEP 1 test fixture that
never calls `SetProjectAssemblyHost()` - now a defensive null check, safe
empty-list fallback), and a live, HTTP-driven 13-point verification against
a real running `GreatTamanaEditor.exe` (register, confirm all three hazards
live/non-empty via the real ledger, unload via a throwaway direct test hook,
confirm empty/gone across every observe route, confirm the process survives
several seconds of continued normal rendering afterward). See
`task_manager/editor-core-separation-13/CAMPAIGN_COMPLETION_REPORT.md` for
the full five-phase writeup.

### Project Assembly Hot Reload — Synchronous Compile & Atomic Swap Orchestrator (BIG-STEP 3)

A five-phase campaign, `editor-core-separation-14`
(`task_manager/editor-core-separation-14/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), implements "BIG-STEP 3" (synchronous
compile/atomic swap orchestrator) of the same four-part external master plan
BIG-STEP 1/BIG-STEP 2 (above) established; BIG-STEP 4 (state snapshot/
restore) was, AT THE TIME this campaign shipped, FULLY UNIMPLEMENTED -
**now DONE, see "BIG-STEP 4" below - this sentence is kept as an accurate
historical snapshot of this campaign's own moment in time**. `POST
/project_assembly/hot_reload?name=<X>`
is no longer a permanent `501` - it is REAL: it freezes the whole engine main
loop, backs up the targeted Project Assembly's current `_Game.dll`/`_Editor.dll`,
cleanly unloads them (`ProjectAssemblyHost::UnloadProjectAssembly()`,
`editor-core-separation-13`'s own BIG-STEP 2 machinery, unchanged), recompiles
them SYNCHRONOUSLY on the calling (main) thread, and either loads the fresh
binaries on success or restores the backup and reloads the old, still-good
pair on ANY failure (a bad compile, or the shared in-flight build guard
rejecting the attempt because an unrelated async `compile_only` for the same
project is already running) - all inside ONE single, non-yielding main-loop
iteration (confirmed live: every phase-transition log line for one cycle
shares the exact same frame number). `GET /project_assembly/hot_reload/status`,
polled from a SECOND, concurrent connection while the first request is still
blocked, genuinely reports live progress through real phase names
(`CapturingState` -> `BackingUpBinaries` -> `Unloading` -> `Compiling` ->
`ReloadingNewCode`/`RollingBack` -> `RestoringState` -> `Idle`), ending
`lastOutcome` = `"Success"`, `"RolledBack"`, or (only in a documented,
low-probability "failure of failure" case, e.g. the backup restore-copy
itself failing) `"CriticalFailure"`. **Zero change to `GreatTamanaEditor.exe`'s
own compiled-in `gte_core`/`gte_editor`** - only the targeted project's own two
`.dll`s are ever touched (LDD-HR2), and `IHotReloadDebugCapability`'s own
method signatures never changed across the whole campaign (LDD-HR3).

New files: `src/Core/Plugins/ProjectAssemblyHotReload.h/.cpp` (the
orchestrator itself, `PerformProjectAssemblyHotReload()`, plus HOOK POINT A/B
- `CaptureProjectAssemblyHotReloadState()`/`RestoreProjectAssemblyHotReloadState()`
- both permanent, logged no-op stubs for BIG-STEP 4 to fill in later, and
`PumpWindowsMessagesDuringHotReloadFreeze()`, a real `PeekMessage`/
`TranslateMessage`/`DispatchMessage` loop keeping Windows from marking the
frozen main window "Not Responding" during a long compile - `hWnd = nullptr`
deliberately, so any ImGui multi-viewport window torn off the main OS window
is serviced too) and `src/Application/ProjectAssemblyHotReloadCommandBridge.h/.cpp`
(a network-thread -> main-thread request bridge mirroring `EngineCommandBridge`'s
proven shape, with ONE deliberate divergence: a timed-out `SubmitAndWait()`
does NOT clear the pending request - only `FulfillPending()` ever does - so a
slow/timed-out HTTP client never causes the main thread to silently skip a
reload cycle it already committed to). `ProjectAssemblyHost` gained two new
public methods, `LoadOneProjectAssemblyFromExactPath()`/
`...IfExistsOnANonExistentPath()` (the latter treats a non-existent path,
e.g. a project with no `_Editor.dll`, as a normal, successful no-op), and both
it and `UnloadProjectAssembly()` now lock `GetHotReloadEngineStateMutex()`
around their whole body - closing an obligation `editor-core-separation-13`'s
own mutex header comment had required but never actually implemented.
`ProjectAssemblyBuildRunner` gained a shared `RunProjectAssemblyBuildAndWait()`/
`BuildOutcome` pair and `TryRunProjectAssemblyBuildSynchronously()` (reusing
the exact same in-flight guard the pre-existing async `TriggerProjectAssemblyCompile()`
already uses), and its build-output read loop became `PeekNamedPipe()`-driven
so an injected idle-tick callback (the message pump above) runs on a fixed
cadence independent of how chatty the child `cmake`/`ninja` process is. A
**genuine `gte_core` -> `gte_editor` layering violation** in the external
plan's own pseudocode (having the orchestrator itself call
`gte::ExecutableDirectory()`, a `gte_editor`-only function, from
`gte_core`-tier code) and a **genuine timeout-semantics bug** in the bridge's
own pseudocode (which would have silently broken every timed-out hot-reload
request) were BOTH found and corrected during this campaign's own PHASE0
double-check pass, before any implementation began - see
`CAMPAIGN_COMPLETION_REPORT.md`'s own "Deliberate deviations" section for the
full detail. Verified with a full clean build, a full `ctest` regression pass
(1934 tests, 100% passing, 7 legitimate environment-gated skips - up from
`editor-core-separation-13`'s own 1925/5 baseline), and a live, HTTP-driven
14-point verification against a real running `GreatTamanaEditor.exe`
(baseline, a real deliberately-broken-compile rollback with live status
polling mid-cycle, a real successful reload with a visibly different output,
a slow-build check proving the main thread stays genuinely frozen without
being force-closed by Windows, and both directions of the shared in-flight
build guard). See `task_manager/editor-core-separation-14/CAMPAIGN_COMPLETION_REPORT.md`
for the full five-phase writeup.

### Project Assembly Hot Reload — State Snapshot, Restore, and Campaign Closeout (BIG-STEP 4 of 4 - THE WHOLE 4-BIG-STEP EFFORT IS NOW DONE)

A five-phase campaign, `editor-core-separation-15`
(`task_manager/editor-core-separation-15/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), implements "BIG-STEP 4" (state
snapshot/restore) of the same four-part external master plan BIG-STEP
1/2/3 (above) established - **the final BIG-STEP of the whole effort.**
HOOK POINT A/B (`CaptureProjectAssemblyHotReloadState()`/
`RestoreProjectAssemblyHotReloadState()`, `src/Core/Plugins/
ProjectAssemblyHotReload.h/.cpp`, permanent, logged no-op stubs since
`editor-core-separation-14`) now have real bodies: capture builds a full
`SceneDocument` snapshot of the live ECS world via the SAME
`Scene/SceneBuilder.h` function `GET /project_assembly/debug/scene_snapshot`
already used (`BuildSceneDocumentFromRegistry()`), and restore reuses a
NEWLY-EXTRACTED, genuinely `gte_core`-tier function,
`Scene/SceneBuilder.cpp`'s `ReconstructSceneFromDocument()` - the SAME
recipe-aware reconstruction algorithm `Editor::LoadScene()`'s own Ctrl+O
already used, moved out of `Editor/SceneIO.cpp` (which is now a thin
wrapper around it) per the user's own explicit direction that "Load Scene
is a core engine feature", mirroring Unity's own `SceneManager.LoadScene()`
being available in a Player build, not just the Editor. `Core` gained a
persistent, engine-owned `AssetDatabase` (`Core::GetAssetDatabase()`),
refreshed exactly once per cycle. `POST /project_assembly/hot_reload?name=<X>`
now genuinely preserves the live ECS world - every entity, every built-in
reflected component, AND every Project-Assembly-defined CUSTOM reflected
component type, including values MUTATED AT RUNTIME - across BOTH a
successful reload and an automatic rollback, proven live for both outcomes
via `Projects/ProjectAssemblyProbe/`'s own permanent `ProbeHotReloadMarker`
fixture and its one new, narrow, testing-only mutation route, `POST
/project_assembly/debug/set_probe_marker_value?value=<N>`
(`IHotReloadDebugCapability::SetProbeHotReloadMarkerValueForTesting()` -
hardcoded to this ONE component/field, permanently, never a generic
mutation surface).

**Two genuine, previously-latent `src/ECS/Registry.h` safety bugs were
found and fixed live by this campaign's own final live-verification
phase** - both confirmed via `gdb`, both required for this campaign's own
mandatory success/rollback tests to pass at all: (1) `detail::ComponentTypeId<T>()`
used to hand out a component type's numeric storage slot from a counter
LOCAL TO EACH BINARY IMAGE (a Meyer's singleton inside a header-only inline
function) - correct only when every caller of a given type is compiled
into the same image, which is false the moment a Project Assembly `.dll`
registers its own custom component type, silently corrupting whichever
real, built-in component's storage happened to already occupy the same
numeric slot in the `.exe`'s own numbering (confirmed, live: this exact
collision landed on the real `Transform` component and crashed the engine
via a corrupted `std::vector` the moment `ClearEntireScene()` walked it
during a real hot-reload cycle) - fixed by resolving every type's slot
through one single, shared, out-of-line, process-wide authority,
`detail::ResolveComponentTypeIdByName()` (new `src/ECS/Registry.cpp`),
keyed by `typeid(T).name()` as a stable string, mirroring
`ComponentTypeRegistry::Instance()`'s own already-correct "real singleton
in a real `.cpp` file" shape; (2) a Project-Assembly-registered custom
component's `ComponentStorage<T>` pool object kept a dangling virtual
function table across that assembly's own `.dll` unload (its vtable is
compiled into the unloading `.dll`'s own image), reliably crashing on the
SECOND consecutive hot-reload cycle in one process session - fixed by a
new `Registry::ResetStoragePool<T>()` (destroys a type's pool entirely),
wired through a new `ComponentTypeDescriptor::destroyPool` callback that
`ProjectAssemblyRegistrationLedger::UnregisterEverythingFor()` now invokes
for every custom component type a project's own ledger entry recorded,
BEFORE that project's `.dll` is `FreeLibrary()`'d. Both fixes are scoped
generically - they protect ANY future Project Assembly's own custom
component type, not merely this one probe fixture, and never touch
built-in component types' own pools at all.

**The one, permanent, honest boundary of this whole feature, restated
here plainly**: everything living in the ECS `Registry` survives a reload
cycle - every entity, every built-in AND custom reflected component;
anything a Project Assembly's own code keeps OUTSIDE the ECS Registry (a
bare C++ global, a non-ECS manager object, GPU resources a render pass
owns opaquely) does NOT survive - it is destroyed and rebuilt from
scratch, exactly like a fresh process start, on every single reload. Two
further, explicitly out-of-scope limitations remain, unchanged from this
campaign's own plan: the engine's persistent `AssetDatabase` is not yet
unified with `ProjectPanel`'s/`SceneIO.cpp`'s own separate instances, and a
reload cycle briefly clears/restores the ENTIRE live world, so every
entity's numeric ID changes for every currently-loaded project, not just
the one being reloaded (accepted - in practice only one project is ever
loaded at a time). See `docs/conventions/project-assembly-system.md`'s own
`## Hot Reload` section (which also corrects that file's own previously
stale `LDD4`, "no hot reload, anywhere, ever") for the complete, permanent,
current picture. Verified with a full clean build, a full `ctest`
regression pass (1934 tests, 100% passing, 7 legitimate environment-gated
skips - byte-for-byte unchanged from `editor-core-separation-14`'s own
baseline, despite this campaign touching the Tier-1-foundational
`src/ECS/Registry.h` every ECS-dependent test transitively exercises), and
a live, HTTP-driven verification covering both the success path (a runtime-
mutated custom-component value AND a genuinely changed compute-shader
render feature both confirmed live from the SAME reload cycle) and the
rollback path (the same mutated value AND the OLD, unchanged render
feature both confirmed intact after a deliberately-broken compile). See
`task_manager/editor-core-separation-15/CAMPAIGN_COMPLETION_REPORT.md` for
the full five-phase writeup.

**This closes the entire, 4-campaign "Project Assembly Hot Reload" effort
(`editor-core-separation-12` through `-15`) for good.**

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
