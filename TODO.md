# TODO / Roadmap

Backlog of known limitations, deliberately deferred follow-ups, and
longer-term ideas for GreatTamanaEngine - moved out of `README.md` to keep
that file focused on describing the architecture/status *as it exists
today*, rather than growing indefinitely with everything that *might* come
next. Nothing here is a forgotten bug; each item below was consciously
deferred (see the reasoning under each one) rather than overlooked.

Organized into three buckets: things already called out as deliberate
follow-ups in the codebase's own comments (Editor / Testing), fresh
follow-ups from the Memory Profiler work, and broader engine-roadmap ideas
that haven't been started yet at all.

## Editor / Debug UI

- **Per-entity Hierarchy context menu (Delete/Rename/Duplicate).**
  "Hierarchy" now has a right-click "Create 3D Object" menu
  (`Game::CreatePrimitiveEntity()`, see `README.md`) via
  `ImGui::BeginPopupContextWindow()`, but it opens the same way whether the
  right-click landed on empty space or an existing entity row - there is no
  separate per-entity menu yet (Unity's own right-click-on-a-GameObject
  menu has "Rename"/"Duplicate"/"Delete" alongside its own "3D Object"
  submenu). "Rename" also needs a Name component first (entities are
  identified purely by numeric index today - see
  `ECS/Components/Transform.h`) - a small, self-contained, likely-useful
  addition on its own (also helpful for a future scene file's
  readability). Deliberately deferred rather than folded into the initial
  "Create 3D Object" work - this engine has no way to destroy an entity's
  GPU-side mesh/pipeline registration mid-session yet either
  (`RenderSystem`'s `ResourcePool<Mesh, MeshHandle>`/
  `ResourcePool<Pipeline, PipelineHandle>` support `Remove()`, but nothing
  calls it today), so "Delete" needs that reasoned through first.
- **Click-to-select via ray casting + a Scene-view outline highlight.**
  Today, picking an entity by clicking directly on it inside "Scene" is not
  implemented - selection only happens via "Hierarchy" (see
  `EditorContext::selectedEntity`). Adding it needs a real ray/triangle
  intersection test against each `MeshRenderer`'s actual mesh data (i.e. this
  engine's first real collider/picking system, since there is no collision/
  physics layer at all yet), plus a Scene-view-only outline post-process
  shader pass to actually highlight whatever gets picked. Deliberately
  deferred as its own follow-up rather than folded into the transform-gizmo
  work (see `README.md`, "Editor / Debug UI") - meaningfully heavier
  (mesh-level intersection math, a new picking/collider abstraction, and a
  new render pass) and orthogonal to it: the gizmo already works fine driven
  purely by a "Hierarchy" selection in the meantime.
- **Long-term: replace ImGuizmo with a homegrown transform gizmo.**
  ImGuizmo (`third_party/imguizmo/`, `cmake/FetchImGuizmo.cmake`) currently
  backs the Scene-view translate/rotate/scale gizmo, and works correctly
  today - `IMGUIZMO_RELEASE_TAG` is pinned to a specific commit SHA (not the
  `master` branch) specifically so this stays true: a moving branch could
  silently change ImGuizmo's public API/behavior underneath this engine on a
  future fetch (a real, separate risk from - and compounding - the two
  hand-diagnosed upstream bugs this integration already had to work around: a
  Vulkan-vs-OpenGL clip-space Y convention mismatch, fixed via
  `EditorCamera::GizmoProjection()`; and HandleScale()/HandleRotation()
  freezing an in-progress drag the instant the cursor left the Scene panel,
  fixed via a source patch applied by `_imguizmo_apply_gte_patches()` in
  `cmake/FetchImGuizmo.cmake`). Pinning removes the "silently changes
  underneath us" risk, but not the underlying reason it came up in the first
  place: depending on a second library with its own coordinate/interaction
  conventions to reconcile with, for something this engine's own
  `Math`/`Renderer` already has every primitive needed for (screen-space
  projection, ray-plane intersection, `ImDrawList` rendering via Dear ImGui,
  which stays either way). Rolling a homegrown gizmo (translate first, then
  scale, then rotate - roughly the increasing order of implementation
  difficulty) would fit the same "own the core data model" philosophy
  already applied to `src/Math/` (no GLM) and `src/ECS/` (no EnTT), and
  permanently remove this entire class of integration bug rather than
  continuing to patch around it. Deliberately NOT undertaken now - real,
  multi-day effort better spent on higher-priority engine work at this early
  a stage.

## Memory Profiler

- **Vulkan `VkAllocationCallbacks` host-memory hook.** Every Vulkan
  `vkCreate*`/`vkDestroy*` call can take a custom allocator callback struct
  (`pfnAllocation`/`pfnReallocation`/`pfnFree`, plus internal-allocation
  notification callbacks), the spec-blessed way to see exactly how much HOST
  (CPU) memory the Vulkan driver spends per scope (`VK_SYSTEM_ALLOCATION_
  SCOPE_INSTANCE`/`DEVICE`/`COMMAND`/`OBJECT`/`CACHE`) - a third CPU bucket
  alongside `SdlMemoryTracker`/`ImGuiMemoryTracker` in the "Memory" panel.
  Deliberately NOT done yet: unlike the SDL/ImGui hooks (a single call each
  before that library's first use), this needs EVERY Vulkan object
  creation/destruction across the whole engine (`VulkanInstance`,
  `VulkanSurface`, `VulkanDevice`, `VulkanSwapchain`, `Buffer`,
  `RenderTexture`, `Pipeline`, command pools/buffers, sync objects, ImGui's
  own Vulkan backend, ...) to consistently pass the exact same
  `VkAllocationCallbacks*` (or consistently `nullptr`) - the Vulkan spec
  treats mixing allocators between create/destroy of the SAME object as
  undefined behavior - making this a real, multi-file refactor rather than a
  quick add. Also not a byte-perfect measurement even once done: driver/
  loader-internal bookkeeping and thread stacks aren't guaranteed to route
  through the callback on every implementation. Investigated live (see the
  "Memory" panel's "GPU Heap Budgets" section) and found that `Driver Usage`
  already matches `VMA Allocated` almost exactly with no large unexplained
  gap - i.e. there's no evidence yet of a real CPU-memory blind spot large
  enough to justify this effort; revisit if that ever changes (e.g. a future
  profiling session shows a process-wide CPU RAM number that
  `SdlMemoryTracker` + `ImGuiMemoryTracker` + the general engine heap can't
  account for).
- **`vmaBuildStatsString()` parser for real per-block detail.** The "GPU Heap
  Budgets" section's "VMA Allocated" column currently shows an aggregate
  block/sub-allocation summary per HEAP (`FormatBlockSummary()`,
  `src/Editor/MemoryPanelData.h`) - e.g. "64.00 MB across 2 blocks (3
  sub-allocations)" - but VMA's stable public statistics API
  (`vmaGetHeapBudgets()`/`VmaStatistics`) only reports heap-level totals, not
  which specific resource landed in which specific block. VMA does expose a
  JSON-formatted dump with real per-block/per-allocation detail via
  `vmaBuildStatsString()`, which would let the panel show something like
  "Block 0: GameView + SceneView (images)" / "Block 1: TriangleMesh
  (buffer)" directly, rather than requiring the Vulkan buffer/image
  `bufferImageGranularity` explanation to be inferred from the aggregate
  counts. Deliberately NOT done yet - parsing/rendering that JSON blob inside
  an ImGui panel is a meaningfully bigger addition than the current
  aggregate summary, and the aggregate view already answers "does
  GpuMemoryTracker account for everything the driver reports" without it.

## Testing

- **Tier 2 (GPU-backed) integration test fixture.** `Buffer`/`RenderTexture`/
  `Pipeline`/`GpuResourceFactory` all need a real `VkDevice`+`VmaAllocator`
  to construct at all, and today's `VulkanDevice::PickPhysicalDevice()`
  additionally requires a real `VkSurfaceKHR` (to query present support),
  which normally comes from an actual OS window (`Window`+`VulkanSurface`).
  A future `GpuTestFixture` could build a headless `VkSurfaceKHR` via
  `VK_EXT_headless_surface` (supported by most desktop drivers, and by
  software implementations like Mesa lavapipe/SwiftShader) instead of a real
  `Window`, then `GTEST_SKIP()` the whole fixture at runtime if instance/
  device creation fails - i.e. those tests would still PASS (skipped, not
  failed) on a GPU-less CI runner, and only actually exercise the GPU on a
  developer machine or a self-hosted runner that has one. Needs its own
  fixture/CMake wiring (`GTE_BUILD_GPU_TESTS` or similar) rather than being
  bolted onto Tier 1 - see `tests/CMakeLists.txt`. Explicitly NOT a blocker
  for any other work; the current development machine doesn't even support
  headless mode.

## Engine Roadmap (not yet started)

Broader, longer-horizon ideas for moving the engine past "tech demo with a
great editor" toward something that can hold a real scene/game - none of
these have any code written yet; listed here in roughly the order they'd
unblock the most follow-on work:

- **Scene serialization (save/load a scene to/from a file, e.g. JSON).**
  Right now the Editor lets you live-edit entities via the gizmo/Inspector,
  and can now also spawn new ones from scratch via "Hierarchy" -> "Create 3D
  Object" (`Game::CreatePrimitiveEntity()`, see `README.md`) - but there is
  still no way to persist or reload the result; `Game.cpp`'s own demo
  entities remain hardcoded in C++. Likely the single highest-leverage next
  engine-level feature: a pure `Registry` <-> text-format read/write, fitting
  this engine's existing Tier-1-testability doctrine, that turns every other
  item below into something you can actually author instead of hardcode.
  "Create 3D Object" is exactly the tool needed to build a non-trivial test
  scene to exercise save/modify/load against once this lands.
- **A minimal asset pipeline: real mesh loading (OBJ/glTF) + textures.**
  Today there is one hardcoded triangle mesh, five procedurally-generated
  built-in primitive shapes (`PrimitiveMeshGenerator` - Cube/Sphere/Capsule/
  Cone/Plane, see `README.md`), and one shader pair
  (`Shaders/Triangle.vert/.frag`) - still no model loader, no texture/
  material system, and no index buffer support (`Mesh`/`GpuResourceFactory`
  only ever build a plain, non-indexed vertex buffer - see `Mesh.h` - which
  is why `PrimitiveMeshGenerator` duplicates vertices across every
  triangle/face rather than sharing them; a real mesh loader would want
  indexing for both memory and CPU-generation-time reasons). Needs a mesh
  loader feeding `Renderer::CreateMesh()` (or a future indexed variant of
  it), and a basic textured pipeline (descriptor set + sampler, following
  the same `Renderer::ColorFormat()`-style "single source of truth"
  discipline already used elsewhere).
- **Transform parenting / hierarchy.** `Transform`
  (`src/ECS/Components/Transform.h`) is flat today - no parent/child
  relationship, which is why ImGuizmo's `LOCAL` space is currently identical
  to `WORLD` (see `README.md`). Adding a parent `Entity` reference (or
  index) with `LocalToWorldMatrix()` walking up the chain is relatively
  contained and unblocks real scene graphs.
- **Basic collision/physics primitives.** No collision/physics layer exists
  at all yet - a prerequisite for click-to-select raycasting (above) and for
  any real gameplay. Start minimal: AABB/sphere colliders + a simple
  broad-phase, rather than reaching for a full physics engine immediately.
- **Someday/maybe:** audio, and a gameplay scripting layer (today, all
  gameplay is hand-authored C++ entities in `Game.cpp`). Not sequenced yet -
  revisit once the items above land.
