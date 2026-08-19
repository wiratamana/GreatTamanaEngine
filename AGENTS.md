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
  `Renderer::Submit()` takes a plain `Mat4`, never an `Entity`/`Registry`.
  `RenderSystem::CollectRenderables(Registry&)` is the pure ECS -> plain-data
  (`DrawCommand`: `MeshHandle`/`PipelineHandle`/`Mat4`, no live Mesh/Pipeline/
  Renderer involved) step - keep it that way when extending it, and put any
  new Renderer-touching logic in `RenderSystem::Draw()` (or a sibling
  non-pure method) instead, so `CollectRenderables()` stays Tier-1-testable
  (see `tests/Game/RenderSystemTests.cpp`).

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

## Git Workflow

- **Never commit directly to `dev`, `main`, or any `release/*` branch - this
  is an IRON RULE, no exceptions.** Every change lands on its own dedicated
  branch first: `fix/{short-description}` for a bug fix, or
  `feature/{short-description}` for a new feature/enhancement (a docs-only or
  process/governance change - like this very section - counts as a
  `feature/` too). Do the task's actual work (edits, builds, tests) on that
  branch, and commit only once, at the end, when the task is complete and
  verified (matches "Testability & Regression Safety" above) - never commit
  mid-task, and never commit directly on `dev`/`main`/`release/*` under any
  circumstance.
- **Before touching a single file for a new task, check the current branch
  and working tree state first** (`git status`/`git branch`):
  - If already on `dev`/`main`/`release/*` with a CLEAN working tree,
    immediately create and switch to a fresh `fix/{name}` or `feature/{name}`
    branch (`git switch -c ...`) before making any edit - do this
    automatically as part of starting the task, no need to ask first.
  - If the current branch is NOT `dev` (or whatever the task's expected base
    branch is) AND the working tree already has uncommitted changes sitting
    there from unrelated prior work, FAIL IMMEDIATELY - do not switch
    branches, do not commit anything, do not proceed with the new task's
    edits at all. Prompt the user to commit (or stash/discard) those pending
    changes first instead - silently carrying unrelated uncommitted work
    onto a new branch would mix two unrelated changes together.
- **"push to dev"**, said by the user, is a specific, distinct instruction
  with an exact meaning: merge the current working branch into `dev`, push
  `dev` to the remote, then switch back to `dev` locally afterwards -
  resetting to a clean base-branch state so the next task starts fresh. This
  merge-then-push-then-reset sequence is the ONLY sanctioned way a change
  ever actually reaches `dev`.

This document will be extended as more conventions are established.
