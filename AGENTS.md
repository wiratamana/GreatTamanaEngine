# AGENTS.md

Instructions for LLM/AI agents working on this codebase.

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

Every GPU resource type (`Buffer`, `RenderTexture`, and any future type -
vertex/index/uniform buffers, textures, etc.) must register with
`GpuMemoryTracker` (`src/Renderer/Memory/GpuMemoryTracker.h`) so the engine
always has an accurate, live picture of exactly what GPU memory is
allocated, of what kind, and where - a Unity-Memory-Profiler-style live
object registry, not just an aggregate byte counter. Follow these rules
whenever touching GPU resource lifetime code:

- **Identify resources by handle, never by pointer or string.**
  `GpuResourceHandle` is a cheap 8-byte POD (index + generation), generated
  automatically by `GpuMemoryTracker::Track()` - calling code never
  invents/assigns its own id. Handles are meant to be copied/compared/
  stored by the thousands with no real cost, unlike a `std::string`, which
  is comparatively large and unpredictable memory-wise.
- **The tracked record must always reflect the CURRENT actual allocation -
  never a stale, construction-time snapshot.** Any lifecycle method that
  destroys and recreates a resource's underlying VMA allocation (e.g.
  `RenderTexture::Resize()`, which internally does `Destroy()` +
  `Create()`) is creating a genuinely new allocation, and MUST `Untrack()`
  the old handle and `Track()` a fresh one reflecting the new size/location
  as part of that same operation. A resource's `Handle()` is therefore NOT
  guaranteed stable across its lifetime - only guaranteed valid for
  whatever the resource's CURRENT allocation actually is. Never assume a
  handle captured once stays correct after a resize/recreate; always read
  `Handle()` again afterwards if you need it. This was verified with a
  dedicated runtime test (create -> resize -> confirm the old handle is
  gone, the new one is tracked, and the byte count reflects the new size,
  with no duplicate/leaked entry) - re-verify this way whenever this code
  path changes.
- **Track the size VMA actually gave you, not the size you requested.**
  Use the `VmaAllocationInfo::size` returned by `vmaCreateBuffer`/
  `vmaCreateImage` (VMA may allocate more than requested due to alignment),
  and classify the real memory location via `ClassifyGpuMemoryLocation()`
  (reads the allocation's actual `VkMemoryPropertyFlags` from VMA) rather
  than assuming it matches whatever `BufferMemoryUsage` was requested -
  VMA's actual choice can legitimately differ (e.g. falling back to plain
  host-visible system RAM instead of a shared device-local+host-visible
  heap).
- **Human-readable debug names are Editor-only and live in a completely
  separate table from the hot resource record.** Pass names as a plain
  `const char*` (never `std::string`) through an optional `debugName`
  parameter, and only ever store/attach them via
  `GpuMemoryTracker::SetDebugName()`, which is guarded by
  `#if GTE_ENABLE_EDITOR` in `GpuMemoryTracker.h` - this compiles the name
  table out ENTIRELY (not just unused) in a non-Editor/release build, so a
  shipped game carries zero string cost for this. Never add a name/string
  field to `GpuResourceRecord` itself. If a resource's debug name must
  survive a resize/recreate (see above), store the `const char*` on the
  resource itself and re-apply it via `SetDebugName()` every time it
  re-tracks - this requires the caller-supplied string to have static
  storage duration (e.g. a string literal), since only the pointer is kept,
  not a copy.
- **Own the tracker via `std::shared_ptr`, never a raw pointer/reference.**
  `Renderer` owns the one `GpuMemoryTracker` and hands a `shared_ptr` copy
  to every `Buffer`/`RenderTexture` it creates, so tracking stays valid no
  matter how `Renderer`/`VulkanAllocator` get moved later - a raw
  pointer/reference into `VulkanAllocator` or `Renderer` itself would risk
  dangling after a move (the underlying Vulkan handles survive moves fine,
  but the C++ wrapper objects can relocate). Any new GPU resource type
  added later should follow this same pattern, not invent its own.

## CPU Dependency Memory Tracking

Alongside `GpuMemoryTracker` (above), the engine also tracks how much CPU
(host) memory its own third-party dependencies are using - `SdlMemoryTracker`
(`src/Memory/SdlMemoryTracker.h`, always compiled) for SDL, and
`ImGuiMemoryTracker` (`src/Editor/ImGuiMemoryTracker.h`, Editor-only) for Dear
ImGui - both surfaced by the Editor's "Memory" panel
(`src/Editor/Panels/MemoryPanel.cpp`) as their own named CPU buckets,
alongside `Renderer::GetVmaHeapBudgets()` (the real, driver-reported GPU heap
usage/budget, distinct from `GpuMemoryTracker`'s own tally). Follow these
rules whenever touching this code or adding a tracker for a future
dependency:

- **Install the tracking allocator before the dependency's first call of any
  kind, not just before some "main" entry point.** Both SDL
  (`SDL_SetMemoryFunctions()`) and Dear ImGui
  (`ImGui::SetAllocatorFunctions()`) document this same constraint: swapping
  allocators after the library has already allocated something risks a later
  free using a DIFFERENT allocator than whatever alloc call originally served
  that pointer. `SdlMemoryTracker::Install()` is called at the very top of
  `Application::SdlContext`'s constructor (`Application.cpp`), before
  `SDL_Init()`; `ImGuiMemoryTracker::Install()` is called at the very top of
  `ImGuiEditorLayer`'s constructor (`ImGuiEditorLayer.cpp`), before
  `ImGui::CreateContext()`. A future tracker for a new dependency must find
  and hook that same "first call" point, not an approximate/later one.
- **The production `Install()` call site must itself be gated behind
  `#if GTE_ENABLE_EDITOR`, even if the tracker CLASS compiles in every
  build.** These trackers exist purely to feed the Editor's "Memory" panel -
  a release/shipped build has no panel to display them and must not pay
  their real per-allocation cost (an extra header write + atomic increment
  on EVERY single alloc/free of that dependency, for the rest of the
  process's lifetime) for nothing. `ImGuiMemoryTracker::Install()` gets this
  for free (its call site, `ImGuiEditorLayer`'s constructor, is only ever
  compiled when `GTE_ENABLE_EDITOR` is ON in the first place - see "Editor
  Module Structure"). `SdlMemoryTracker::Install()` does NOT get this for
  free, since `Application.cpp` compiles in every build - its call site in
  `Application::SdlContext`'s constructor is explicitly wrapped in
  `#if GTE_ENABLE_EDITOR` for exactly this reason. A future tracker for a
  dependency used outside `src/Editor/` (i.e. one whose install call site
  isn't naturally Editor-only compiled) must add this same explicit guard at
  its call site - don't assume "the class only matters to the Editor" is
  enough on its own to keep it out of a release build's runtime behavior.
- **`Install()` must be idempotent.** Both trackers guard themselves with a
  local `static bool installed` - calling `Install()` more than once (e.g.
  from a test, or if a future call site is added) is always a safe no-op, so
  no caller ever needs to guard its own call site.
- **These are necessarily static/process-global, not instance-based like
  `GpuMemoryTracker`.** `SDL_malloc_func`/`SDL_free_func` and
  `ImGuiMemAllocFunc`/`ImGuiMemFreeFunc` are plain C function pointers with
  no (or, for ImGui, an engine-unused) userdata slot to stash a `this` in -
  there is nowhere else the byte/count totals could live. This matches a
  constraint SDL's own `SDL_GetNumAllocations()` already has.
- **A hidden per-allocation header carries the LOGICAL size, since free-side
  callbacks are only ever handed the pointer, never a size.** Both trackers
  use the same fixed 16-byte header trick (see `SdlMemoryTracker.cpp`'s
  `AllocHeader`/`HeaderEncode()`/`HeaderDecode()`) - 16 bytes because that is
  this Windows target's guaranteed allocation alignment (the smaller of
  `alignof(std::max_align_t)` or `2*sizeof(void*)`), so offsetting the
  underlying allocator's own aligned block by exactly that many bytes
  preserves its alignment guarantee for the pointer handed back to the
  caller. A future tracker copying this pattern must keep the header size a
  multiple of that alignment, not just `sizeof(size_t)`.
- **Both trackers are genuinely Tier-1-testable despite touching a
  third-party library's own allocator.** Neither `SDL_malloc()`/`SDL_free()`
  nor `ImGui::MemAlloc()`/`MemFree()` need `SDL_Init()`, a live window, or an
  `ImGuiContext` to be called safely - see
  `tests/Memory/SdlMemoryTrackerTests.cpp`/
  `tests/Editor/ImGuiMemoryTrackerTests.cpp` for the pattern: capture
  `LiveBytes()`/`LiveAllocationCount()` BEFORE each test's own alloc/free
  calls and assert on the DELTA, never an assumed absolute baseline, since
  `Install()`'s process-global state persists across every test in the same
  binary.

## Render Target Format Matching

Vulkan pipelines are built against an exact color format
(`VkPipelineRenderingCreateInfo::pColorAttachmentFormats`, since this engine
uses dynamic rendering - no `VkRenderPass`/`VkFramebuffer`) - binding a
pipeline built for one format to a target that actually has a different
format is invalid per the spec, and can silently misrender or crash
depending on the driver instead of failing loudly. Follow these rules
whenever adding a real graphics pipeline or a new render target:

- **`Renderer::ColorFormat()`** (`src/Renderer/Renderer.h/.cpp`) is the
  single source of truth for "the" color format this engine renders with -
  whatever `VulkanSwapchain` actually negotiated at runtime (see
  `ChooseSurfaceFormat` in `VulkanSwapchain.cpp`), which can legitimately
  differ across GPUs/drivers. Never hardcode a `VkFormat` literal (e.g.
  `VK_FORMAT_B8G8R8A8_UNORM`) into a pipeline's
  `VkPipelineRenderingCreateInfo` or into a `RenderTexture` you expect to
  share a pipeline with the swapchain - read it from `Renderer::ColorFormat()`
  instead.
- **`Renderer::CreateRenderTexture()`'s `format` parameter defaults to
  `VK_FORMAT_UNDEFINED`**, meaning "match `ColorFormat()` exactly" (resolved
  internally in `Renderer.cpp`, not baked into the default argument as a
  literal) - this is what lets a single pipeline built once against
  `ColorFormat()` legally draw into either the swapchain or a default-format
  `RenderTexture` (e.g. the Editor's "Game" view). Only pass an explicit
  format when a target is deliberately different (e.g. a future HDR
  intermediate or a shadow map) - that target needs its own dedicated
  pipeline variant built for its exact format, never the default pipeline.
- **`FrameRecorder::RecordFrame()` asserts (debug builds only) that
  every target it's given has `target.format == ColorFormat()`.** This is
  the one recording path shared by `Present()` and `RenderOffscreen()`, so
  it's the natural place a future pipeline-bound draw call (recorded via
  `recordExtra`) runs - the assert exists to catch a format mismatch loudly,
  right there, instead of a confusing validation-layer warning (or silent
  misrendering on a driver that happens to tolerate it). A deliberately
  different-format target (see above) needs its own recording path rather
  than going through this assert unmodified - don't weaken or delete the
  assert to make a special case fit.
- **This same discipline applies to DEPTH, not just color.**
  `Renderer::DepthFormat()` (`VulkanDevice::PickDepthFormat()`, queried once
  from the physical device rather than hardcoded) is depth's equivalent of
  `Renderer::ColorFormat()` - every `Pipeline` is built with
  `VkPipelineRenderingCreateInfo::depthAttachmentFormat` set to it, and every
  render target (the swapchain's own per-image `DepthBuffer`s in
  `FramePresenter`, or a `RenderTexture`'s own companion `DepthBuffer` - see
  `src/Renderer/DepthBuffer.h`) is created at that exact same format.
  `FrameRecorder::RecordFrame()` asserts `target.depthFormat ==
  DepthFormat()` right alongside its existing color-format assert, for
  exactly the same reason. This was added specifically because the engine's
  original hardcoded triangle demo was always flat/coplanar (no real
  occlusion to get wrong), so a genuinely 3D, depth-tested render target
  (needed once the built-in primitive shapes - `Renderer/Primitives/
  PrimitiveMeshGenerator.h` - introduced real overlapping-in-screen-space
  geometry) never existed until now - don't reintroduce a render target or
  pipeline that skips a depth attachment/depth test, even for something that
  "looks flat," without a specific reason.

## Skeletal Animation Pose Resolution

Every per-frame MMD skeletal-animation pose evaluation lives under
`src/Animation/` (`BoneLocalOffset.h`, `MotionSampler.h/.cpp`,
`IkSolver.h/.cpp`, `AppendBoneSolver.h/.cpp`, `SkeletonPose.h/.cpp`,
`VertexSkinning.h/.cpp`, `AnimationPoseEvaluator.h/.cpp` - see `README.md`,
"Status", for the full history of how this runtime was built up). Follow
these rules whenever touching bone-hierarchy-walking code in this module:

- **Never hand-roll a new cycle-guarded bone-ancestor-chain walk - use
  `Animation/BoneChainResolver.h`'s `ResolveBoneChain()`/
  `ResolveSingleBoneChain()` instead.** `SkeletonPose.cpp`'s whole-skeleton
  world-matrix pass, `AppendBoneSolver.cpp`'s append/grant-source
  resolution, and `IkSolver.cpp`'s per-CCD-iteration single-bone world-
  matrix query all build on these two generic, Tier-1-tested primitives
  (`tests/Animation/BoneChainResolverTests.cpp`) rather than each
  maintaining its own copy of the same cycle-guard/memoization logic - three
  independent, subtly different hand-rolled versions of this exact pattern
  is what this code used to be, before it was pulled out into one place.
  Pick `ResolveBoneChain()` (memoized, one pass over the WHOLE skeleton)
  when every bone genuinely needs resolving and the underlying pose data
  won't change mid-walk; pick `ResolveSingleBoneChain()` (recomputed fresh,
  no caching across calls) only when it might - e.g. a CCD solve mutating
  the very pose being queried between successive single-bone lookups (see
  `IkSolver.cpp`'s own comment on why it can't use the memoized flavor).
- **The bind-relative local-transform formula lives in exactly one place:
  `Animation/BonePoseMath.h`'s `ComputeBoneLocalMatrix()`.** Both
  `SkeletonPose.cpp` and `IkSolver.cpp` compose a bone's world matrix as
  `parentWorld * ComputeBoneLocalMatrix(...)` - never reintroduce a second
  copy of `bone.position - parentBindPosition` plus `Mat4::TRS(...)`
  anywhere else; add a parameter to this one function instead if a future
  caller needs a variation on it.
- **The animation pipeline's per-frame execution ORDER (sample -> IK ->
  append -> forward-kinematics) has exactly one home:
  `Animation/AnimationPoseEvaluator.h`'s `EvaluateAnimatedSkinningPose()`.**
  This order is correctness-critical (an append source that's also an IK
  link must already carry its IK-solved rotation - see
  `Animation/AppendBoneSolver.h`'s own file comment) and used to be
  reproduced by hand inside `Game::UpdateSkeletalAnimators()`
  (`src/Game/Game.cpp`) - the only call site at the time. Any new call site
  that needs a fully-resolved animated pose (e.g. the Bone Viewer's planned
  live-pose overlay - see `TODO.md`) must call this one function rather than
  re-inlining the same four-call sequence; if the pipeline itself ever needs
  a new stage (e.g. future morph blending), add it here, once, not at every
  caller. Covered by a genuine ordering-regression test
  (`tests/Animation/AnimationPoseEvaluatorTests.cpp`'s
  `AppendedBoneInheritsIkSolvedRotationNotRawBindPose`) that fails if a
  future edit ever swaps IK solving and append inheritance.

## Entity-Component-System (ECS)

The engine's Scene/World data model lives under `src/ECS/`: `Entity`
(`src/ECS/Entity.h`), `EntityManager` (`src/ECS/EntityManager.h/.cpp`),
`ComponentStorage<T>` (`src/ECS/ComponentStorage.h`), and `Registry`
(`src/ECS/Registry.h`), which owns one of each. This was deliberately rolled
by hand (not via a third-party library like EnTT) so the engine keeps
ownership of its core gameplay data model, the same way its math library
(`src/Math/`) was written from scratch rather than depending on GLM (see
`MathTypes.h`). Follow these rules whenever touching entity/component
lifetime code:

- **Identify entities by handle, never by pointer or string.** `Entity` is a
  cheap 8-byte POD (index + generation), generated automatically by
  `EntityManager::Create()` - calling code never invents/assigns its own id.
  This is the exact same shape and rationale as `GpuResourceHandle` (see
  "GPU Resource Memory Tracking" above): cheap to copy/store/compare by the
  thousands, and the `generation` field guards against a stale `Entity`
  silently referring to a different entity that was later created in the
  same (reused) slot - `EntityManager::Create()`/`Destroy()` use the exact
  same slot + free-list + generation-bump pattern as
  `GpuMemoryTracker::Track()`/`Untrack()`, on purpose, so there is only one
  such pattern in the codebase to understand, not two subtly different ones.
- **Components are plain data, never GPU/SDL-resource-owning types, and
  never carry virtual behavior of their own.** `Transform`
  (`src/ECS/Components/Transform.h`) is the pattern to copy: fields only,
  plus at most small pure-math helper methods (`LocalToWorldMatrix()`). A
  component that needs a live GPU resource - `MeshRenderer`
  (`src/ECS/Components/MeshRenderer.h`) is the first one - must reference it
  by handle/value data (`MeshHandle`/`PipelineHandle`,
  `src/Renderer/MeshHandle.h`/`PipelineHandle.h`), never by embedding a
  `Buffer`/`RenderTexture`/`Mesh`/`Pipeline`/raw Vulkan handle directly - the
  RAII-owning object stays behind a `ResourcePool<T, HandleT>`
  (`src/Renderer/ResourcePool.h`, owned by `RenderSystem` - see below),
  exactly as GPU resources are already addressed by `GpuResourceHandle`
  rather than a raw pointer.
- **A component that references ANOTHER entity (e.g. `Transform::parent`)
  stays plain data too - the logic that actually WALKS that reference lives
  in a separate free-function module, never on the component itself.**
  `Transform::parent` (an `Entity`, `kInvalidEntity` by default) plus
  `Transform::siblingIndex` are exactly this: plain fields, no different in
  kind from `position`/`rotation`/`scale` above. Resolving a full WORLD
  transform by walking the parent chain, cycle-safe reparenting, and sibling
  reordering all live in `src/ECS/TransformHierarchy.h/.cpp` instead
  (`ComputeWorldMatrix()`/`ComputeWorldTransform()`, `SetParent()`,
  `GetChildren()`/`SetSiblingIndex()`) - free functions that take a
  `Registry&` plus plain `Entity` values, same shape as `RenderSystem`'s own
  ECS-bridging functions below, just bridging ECS-to-ECS instead of
  ECS-to-Renderer. This keeps `Transform` itself trivially copyable/
  Tier-1-testable-by-construction while still allowing genuinely non-trivial
  hierarchy logic (cycle detection, world-position-preserving reparenting) to
  exist somewhere sensible - never add a Registry-dependent method to
  `Transform` (or any other component) directly, follow this same
  free-function pattern instead.
- **`ComponentStorage<T>` is a sparse set, addressed by `Entity::index`
  directly - never a hash lookup.** Adding/removing/querying a component is
  O(1) array indexing (`m_sparse`/`m_dense`), and `Remove()` uses
  swap-with-last to keep the dense array packed for cache-friendly
  iteration - dense iteration order is therefore NOT stable across a
  `Remove()` call, never rely on it. `Registry` picks each component type's
  numeric id via a per-type function-local static counter
  (`detail::ComponentTypeId<T>()`), not `std::type_index`/RTTI, so
  `Registry::Storage<T>()`/`AddComponent<T>()`/etc. stay a plain array
  lookup rather than a hash on every call - the same "no hashing on the hot
  path" philosophy as `GpuMemoryTracker`'s handle-indexed slot array.
- **`Registry::DestroyEntity()` must remove the entity from EVERY pool it
  has ever touched, not just the ones a caller happens to think of.** This
  is why `Registry` keeps a homogeneous `std::vector<std::unique_ptr<IComponentPool>>`
  and calls `IComponentPool::Remove()` (the type-erased virtual, not the
  typed `ComponentStorage<T>::Remove()`) on every pool before destroying the
  entity itself - an entity is never left with a dangling/orphaned component
  in some pool this forgot about. Any new component-holding structure added
  later must go through this same `IComponentPool` path, not invent a
  separate destroy-time cleanup step.
- **A `Registry`/`EntityManager`/`ComponentStorage<T>` is Tier-1-testable by
  construction, and must stay that way.** None of them touch a live
  `VkDevice`/`VmaAllocator`/SDL window - see `tests/ECS/` (`EntityManagerTests.cpp`,
  `ComponentStorageTests.cpp`, `RegistryTests.cpp`) for the pattern to copy
  when adding a new component type or Registry method: hand-built `Entity`
  values and plain component structs are enough, following the same
  Tier-1-testability rule already established below.
- **Only `RenderSystem` (`src/Game/RenderSystem.h/.cpp`) is allowed to
  depend on both the ECS world (`Registry`/`Transform`/`MeshRenderer`) AND
  `Renderer`/`Mesh`/`Pipeline` - the same "only one layer crosses this
  boundary" rule this file already applies to SDL (see "Coding Guidelines",
  Clean Architecture: only `Application` touches SDL directly). `Renderer`
  itself must never gain a dependency on ECS in either direction -
  `Renderer::Submit()` takes plain `Mat4`s, never an `Entity`/`Registry`.
  `RenderSystem::CollectRenderables(Registry&)` is the pure ECS -> plain-data
  (`DrawCommand`: `MeshHandle`/`PipelineHandle`/`Mat4`, no live Mesh/Pipeline/
  Renderer involved) step - keep it that way when extending it, and put any
  new Renderer-touching logic in `RenderSystem::Draw()` (or a sibling
  non-pure method) instead, so `CollectRenderables()` stays Tier-1-testable
  (see `tests/Game/RenderSystemTests.cpp`).
- **`Camera` (`src/ECS/Components/Camera.h`) never bakes an aspect ratio
  into itself.** `ProjectionMatrix(aspectWidthOverHeight)` always takes the
  aspect ratio as a parameter, resolved fresh by whoever is about to draw
  (`RenderSystem::ResolveActiveCameraViewProjection(Registry&,
  aspectWidthOverHeight)`), because the SAME `Camera` entity can legitimately
  render into multiple differently-sized/shaped targets in the same frame
  (the Editor's "Game" and "Scene" panels, each with their own
  `RenderTexture` - see "Editor Module Structure" below). Never cache a
  `Camera`'s resolved projection matrix keyed only by the component itself -
  always re-resolve it per render target/aspect ratio. `ViewMatrix()` is
  built from a plain `Transform` (via `Mat4::LookAtLH`, looking down its
  rotated `Vec3::Forward()`) rather than a bespoke eye/target/up API, so a
  camera entity is edited exactly like any other entity (Transform in the
  Inspector) - don't add a separate eye/target/up field set to `Camera`
  itself. `RenderSystem::ResolveActiveCameraViewProjection()` picks the
  FIRST entity (in `ComponentStorage<Camera>` order) with `active == true`
  and falls back to `Mat4::Identity()` if none exists - this is what
  preserves the engine's original "vertices already authored directly in
  clip space" behavior for a scene that hasn't added a `Camera` yet; don't
  change this fallback without checking `Shaders/Triangle.vert`'s
  `pc.viewProj * pc.model * ...` still makes sense for it. `Pipeline`'s one
  push constant range now carries a `model` `Mat4` immediately followed by a
  `viewProj` `Mat4` (128 bytes total - the guaranteed minimum
  `maxPushConstantsSize` on every conformant Vulkan implementation, see
  "Render Target Format Matching" above for the same "match the GPU side
  exactly" philosophy applied here) - grow this only by moving to a uniform
  buffer/descriptor set instead of growing the push constant range further,
  since 128 bytes is the only size guaranteed to fit everywhere without a
  per-GPU limit check.

## Editor Module Structure

`src/Editor/` is the Editor/Debug UI seam described in "Coding Guidelines"
(Clean Architecture) - the same boundary role `EventTranslator` plays for
SDL in the Application layer, but for ImGui. The boundary is the **folder,
compiled only under `GTE_ENABLE_EDITOR`** - not a single file. Only
`EditorLayer.h` (the pure `IEditorLayer` interface) and
`NullEditorLayer.cpp` (the release-build no-op implementation) must stay
completely free of ImGui/SDL/Vulkan-beyond-forward-declares; every other
file under `src/Editor/` is compiled exclusively when `GTE_ENABLE_EDITOR` is
ON (see `CMakeLists.txt`'s `target_sources(gte_core PRIVATE ...)` inside
that `if()` block) and is just as free to include ImGui/SDL headers
directly as `ImGuiEditorLayer.cpp` itself:

- **`ImGuiEditorLayer.cpp`** is the Editor's composition root, not a
  monolith holding every panel: it owns the ImGui context, the SDL3/Vulkan
  backend lifecycle, TWO `RenderTexture`s (`m_gameView`/`m_sceneView` - one
  per panel, never shared), and the shared `EditorContext` (below) -
  `BuildUI()` just calls out, in a fixed, deliberate order, to
  `DockLayout.cpp` and each `Panels/*.cpp` builder.
- **`EditorContext.h`** is a small plain-data struct (no behavior of its
  own, same philosophy as ECS components - see "Entity-Component-System"
  below) holding everything that needs to be shared across panels/frames:
  the Game-view/Scene-view ImGui descriptors, each panel's own desired
  render-texture extent (`desiredExtent`/`desiredSceneExtent`) and visibility
  flag (`gameViewVisible`/`sceneViewVisible`), the current Hierarchy/
  Inspector selection (`EditorContext::selection`, see `Selection.h` below),
  the exit-requested flag, and the dock-layout-ensured latch. Passed by
  reference into every panel/dock-layout function.
- **`Selection.h/.cpp`** is the single gate-keeper for every Hierarchy-entity
  / Project-asset selection change - `HierarchyPanel`/`ProjectPanel` never
  assign `EditorContext::selection`'s fields directly; they only ever call
  `Selection::SelectEntity()`/`SelectAsset()`/`ClearAssetIfPath()`, and every
  reader (`InspectorPanel`, `ScenePanel`'s gizmo) goes through its
  `Kind()`/`SelectedEntity()`/`SelectedAssetAbsolutePath()`/etc. accessors
  rather than reading a raw field. Deliberately pure logic with zero ImGui/
  SDL/Vulkan dependency (Tier-1-testable - see `tests/Editor/
  SelectionTests.cpp` - and "Testability & Regression Safety" below), and
  deliberately just a plain gate-keeper with no history/undo of its own -
  this is what gives a future Command-pattern implementation (undo-able
  selection changes, then edits in general) exactly one choke point to route
  through, instead of several panels each writing selection state directly
  (see `TODO.md`, "Editor / Debug UI"). Any future selectable "thing" (e.g. a
  multi-select set) should extend this same class rather than adding a new
  ad hoc field to `EditorContext` directly.
- **`gameViewVisible`/`sceneViewVisible` are written from `ImGui::Begin()`'s
  own return value** (`Panels/GamePanel.cpp`/`ScenePanel.cpp`) - `false`
  whenever that panel is an inactive/hidden dock tab (or collapsed), not
  just "exists somewhere" - and read by
  `ImGuiEditorLayer::GameViewTarget()`/`SceneViewTarget()` at the START of
  the NEXT frame to return `nullptr` outright for a currently-invisible
  panel, which is what makes `Application::Run()` skip that view's
  `Renderer::RenderOffscreen()` pass entirely (real GPU savings, not just a
  cosmetic skip) whenever "Scene"/"Game" are tabbed together and only one is
  actually on screen. A future panel with its own `RenderTexture` should
  follow this exact same pattern rather than always rendering unconditionally.
- **`DockLayout.h/.cpp`** builds the top menu bar + full-viewport DockSpace
  and the one-shot default Unity-style layout (Hierarchy left, Inspector
  right, Scene/Game tabbed center) - see its own comments for why rebuilding
  that layout must stay strictly one-shot, never re-checked every frame,
  or the user could never drag a panel loose from the default arrangement.
- **`Panels/HierarchyPanel.*`, `InspectorPanel.*`, `ScenePanel.*`,
  `GamePanel.*`** are each a single free function (`BuildXPanel(...)`)
  taking `EditorContext&` (plus `Registry&` where a panel needs the ECS
  world) - not classes, and NOT implementations of any common
  `IEditorPanel` interface. There is deliberately no polymorphic
  panel list/registry here: the dock layout above already addresses each
  panel by its literal, hardcoded name, so nothing ever needs to iterate
  over "the panels" generically - `ImGuiEditorLayer::BuildUI()` calls each
  one explicitly, by name, in a fixed order. Don't introduce an
  `IEditorPanel` abstraction preemptively; only reach for one if a genuine,
  stated requirement for runtime-registered/plugin panels shows up later.
- A **future panel that genuinely needs its own persistent state across
  frames** (e.g. a Console's scrollback buffer) may become a small class
  instead of a free function - it still gets called explicitly by name from
  `ImGuiEditorLayer::BuildUI()`, exactly like the stateless ones, with no
  interface needed for it either.
- **Vulkan types (e.g. `EditorContext::gameViewDescriptor`,
  `VkExtent2D`) are fine to use directly anywhere in this folder** - this is
  not an architectural leak. `Renderer`'s own public API
  (`Renderer::GetVulkanContextInfo()`, `RenderTexture::Extent()`/`View()`/
  `Sampler()`) already hands out plain Vulkan handles on purpose, precisely
  so "an external Vulkan-based rendering backend... owned by the Editor
  module" (see `Renderer.h`) - i.e. Dear ImGui's own Vulkan backend - can
  use them directly; there is exactly one rendering backend in this engine
  and no plan to swap it, so wrapping these handles in a fake neutral type
  would add indirection with no real decoupling benefit. The boundary that
  actually matters and must stay intact is that `Renderer`'s *internal*
  RAII wrapper types (`VulkanInstance`, `VulkanDevice`, `VulkanSwapchain`,
  `VulkanAllocator`, `FramePresenter`, `FrameRecorder`, `GpuResourceFactory`
  - everything under `Renderer/Vulkan/` plus Renderer's private
  collaborators) never leak outside `Renderer`, and that `Game`/ECS never
  see a Vulkan type in either direction (see `RenderSystem`'s rule below).
- **`IEditorLayer::WantsCaptureMouse()`/`WantsCaptureKeyboard()` gate every
  translated mouse/keyboard `Event` before it ever reaches
  `InputState`/`Game::OnEvent()`.** `Application::Run()` checks these
  (backed by `ImGuiIO::WantCaptureMouse`/`WantCaptureKeyboard` in
  `ImGuiEditorLayer`, always `false` in `NullEditorLayer`) so
  clicking/dragging/typing into the Editor's own ImGui panels never ALSO
  registers as gameplay input underneath them - the classic
  ImGui-in-a-game-engine "click-through" problem. This is deliberately NOT
  solved with a separate Editor-side event broadcaster/receiver system:
  Dear ImGui already does all the hard part itself (per-frame, internal
  topmost-window-wins hit-testing/focus/modal-exclusivity across every one
  of its own panels, via the one `ImGuiContext` `ProcessEvent()`/
  `NewFrame()` already feed) - there is nothing left for engine code to
  arbitrate between ImGui's own panels. The only genuinely missing piece
  was ImGui-vs-gameplay leakage, and two `bool` query methods (mirroring
  the existing `WantsExit()` pattern) are enough to close that gap; don't
  reintroduce a broadcaster/registry to solve a problem ImGui already owns.
  `Quit`/`WindowResized` events bypass this check entirely (see
  `Application::Run()`) - they aren't gameplay input in this sense, and
  Renderer/the Editor's own resize handling must always see them regardless
  of what the Editor UI currently wants captured.

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
