# GPU Vertex Skinning — Phase 4: Per-Model Resource Management — Completion Report

Status: **DONE**. Implements
`task_manager/gpu_skinning/GPU_SKINNING_PHASE4_PER_MODEL_RESOURCE_MANAGEMENT_STRATEGY_v1.md`
in full, per the scope fence set by
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md` ("Focus on selected section
only" — this report covers ONLY the "ON GOING" Phase 4 item; Phases 1-3 are
already `[DONE]` and unmodified except for one small, deliberate,
additive extraction described below; Phases 5-7 remain `[TODO]`,
untouched).

## What was built

### 1. `Renderer::CreateGpuSkinningTargetBuffer()` (Phase 1, Step 3.1 of the strategy doc)

New factory method on both `GpuResourceFactory` (`GpuResourceFactory.h/.cpp`)
and `Renderer` (`Renderer.h/.cpp`, a thin one-line forward, mirroring every
other `Renderer` factory method's own shape) — a device-local
(`BufferMemoryUsage::GpuOnly`) buffer created with the combined
`VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT`
usage, per the strategy document's own Step 3.1/3.4 reasoning (this
combination is always legal on any conformant Vulkan implementation, unlike
a storage IMAGE's format-dependent capability check — no
`FormatCapabilities.h`-style runtime probe was needed or added). Never
stages/uploads any initial data — the buffer starts life with undefined
contents, populated only by a future compute dispatch (Phase 5).

### 2. `Mesh`'s "fifth construction path" — confirmed to need NO changes at all

Per the strategy document's own Step 3.2 instruction ("confirm at
implementation time whether `CreateMeshFromSharedVertexBuffer()` can be
called completely unmodified... or needs a small, backward-compatible
relaxation"): read `Mesh.h`'s shared-vertex-buffer constructor and
`GpuResourceFactory::CreateMeshFromSharedVertexBuffer()`'s own
implementation directly — neither asserts or otherwise constrains the
shared buffer's own creation usage flags in any way; the constructor simply
takes a `std::shared_ptr<Buffer>` by value. **This call already works
completely unmodified** against a buffer created via
`CreateGpuSkinningTargetBuffer()` above — confirmed by actually using it
this way in `GpuSkinningRigCache::Register()` (see below), which compiles
and links cleanly. No change to `Mesh.h`/`GpuResourceFactory::
CreateMeshFromSharedVertexBuffer()` was needed or made.

### 3. `MeshAssetPart::indices` — a new, additive field closing a real gap the strategy doc didn't fully anticipate

While implementing Step 3.3 ("build the actual Mesh object... handed off to
whatever owns rendering for this model"), it became clear a GPU-skinned
`Mesh` needs its **own, private** index buffer (per `Mesh.h`'s own
documented rule: "the index buffer is always this ONE Mesh's own private
buffer, never shared, even when the vertex buffer is") — but the only
place this engine had ever kept the raw, CPU-side triangle-index array for
a `MeshAssetPart` was as a local variable inside
`MeshAssetGpuCatalog::EnsureMeshAsset()`, already gone by the time a GPU
mesh needs building later. Reading the raw indices back from the GPU (a
device-to-host readback) was considered and rejected — the CPU-mode index
buffers aren't even created with `VK_BUFFER_USAGE_TRANSFER_SRC_BIT`, so a
GPU-to-GPU copy isn't available either without touching already-shipped,
tested buffer-creation code paths.

The chosen fix: `MeshAssetPart` (`MeshAssetGpuCatalog.h`) gained a new
field, `std::vector<std::uint32_t> indices`, populated by
`MeshAssetGpuCatalog::EnsureMeshAsset()` (`MeshAssetGpuCatalog.cpp`) **only
for a skinned model** (an unskinned model's parts leave it empty — no
extra CPU memory cost for the common case, since a non-skinned model will
never need a GPU-skinned counterpart Mesh at all). Purely additive — no
existing reader of `MeshAssetPart` (Hierarchy/Inspector panels,
`AnimationSystem`'s own grouping) is affected, since every existing field
keeps its exact same meaning/position.

### 4. `GroupMeshAssetPartsBySharedVertexBuffer()` — the shared extraction the strategy doc explicitly called for

New module, `src/Game/Instantiation/MeshAssetPartGrouping.h/.cpp` — the
single, shared home for the "group parts by their live Mesh's own
`VertexBufferIdentity()`" logic that used to be hand-duplicated inline
inside `AnimationSystem::Update()` (`AnimationSystem.cpp`'s own
`PendingGroup` struct/loop). Per the strategy document's own Step 3.3:
*"extract it into a small shared free function... so the two [CPU path and
GPU cache] never drift into two independently-maintained copies of the
same grouping logic."*

`MeshAssetPartGroup` mirrors the old inline `PendingGroup` shape
(`vertexBufferIdentity`/`representativeMesh`/`textured`) but adds one new
field, `partIndices` (indices into the original `parts` vector, in
declaration order) — the CPU path (`AnimationSystem::Update()`) ignores
this new field entirely and is otherwise **behavior-identical** to before
this refactor (same grouping decision, same representative-mesh selection,
same per-group pack/upload loop) — `GpuSkinningRigCache::Register()` is
what actually needs `partIndices`, to build one GPU-skinned `Mesh` **per
part** in a group while still sharing exactly one GPU output buffer per
group.

`AnimationSystem.cpp` was updated to call this shared function instead of
its own inline `struct PendingGroup`/loop — a pure refactor, no behavior
change (same grouping algorithm, same iteration order, same fields
consumed).

### 5. `GpuSkinningRigCache` — the new per-model GPU resource cache (Step 3.3)

New module, `src/Game/Animation/GpuSkinningRigCache.h/.cpp` — the GPU-side
sibling of `SkeletalRigCache`, mirroring its exact "`Register()` once,
`TryGet()` every frame after" shape and this module's own "load once,
cache, never mutate/evict again for the process's lifetime" convention.

`GpuSkinningRigCache::Register(renderer, renderSystem, pipelines,
absoluteGtaPath, data, parts)`:

1. Groups `parts` via `GroupMeshAssetPartsBySharedVertexBuffer()` above —
   a no-op (nothing registered) for a boneless/riggless/empty model, or one
   whose parts don't resolve to any live `Mesh` at all.
2. Calls `pipelines.EnsureInitialized(renderer)` — idempotent; the very
   first `Register()` call across the whole engine session is what
   actually compiles/loads the two skinning compute pipelines (Phase 2),
   not some earlier, unrelated call site.
3. Packs and uploads, **exactly once**, this model's three (four, if any
   group is textured) immutable/semi-immutable per-model buffers, via
   Phase 1's `PackBindPoseVertices()`/`PackSkinWeights()`/`PackUvs()` +
   `Renderer::CreateDeviceLocalBuffer()` (bind pose, skin weights, UV) and
   `Renderer::CreateStructuredBuffer()` (bone matrices — the one genuinely
   per-frame-rewritten input, sized here but left with whatever undefined
   contents that call leaves it in until a future Phase 5 uploads real
   skinning matrices every frame).
4. For each distinct shared-vertex-buffer group: creates one
   `CreateGpuSkinningTargetBuffer()` output buffer sized to
   `vertexCount * sizeof(GpuSkinnedVertexPositionNormal|Uv)` (Phase 1),
   allocates + `Rewrite()`s (**once**, never again — see this class's own
   documented deviation from `ComputeDescriptorSet`'s general "safe/
   expected to call every frame" convention, justified exactly as Phase
   4's strategy document's Step 3.4 anticipated) a `ComputeDescriptorSet`
   against Phase 2's binding table (bind pose / skin weights / bone
   matrices / output, + UV at binding 4 for a textured group), and builds
   one GPU-skinned `Mesh` **per part** in that group (each with its own,
   private index buffer built from `MeshAssetPart::indices` above, all
   sharing that group's one `outputVertexBuffer`), registering each via
   `RenderSystem::RegisterMesh()` and recording the
   `{cpuMeshHandle, gpuMeshHandle}` mapping in
   `OutputGroup::partMeshBindings`.
5. Stores the whole result under `absoluteGtaPath` in `m_models`.

`GpuModelEntry::TryGetGpuMeshHandle(cpuMeshHandle)` is a small,
already-provided convenience lookup — not consumed anywhere yet, but ready
for a future Phase 5 runtime switch to swap a `MeshRenderer`'s `MeshHandle`
onto its GPU-skinned counterpart with **no further GPU work needed at
switch time** (every buffer/descriptor-set/Mesh already exists, built once,
here).

A real, non-obvious C++ structural issue was hit and fixed during
implementation: `GpuModelEntry` (and `OutputGroup`) hold `Buffer`
members directly — `Buffer` has **no default constructor** (see
`Buffer.h`), which means `GpuModelEntry` is **not default-constructible**
either. The original draft (`GpuModelEntry entry; entry.bindPoseBuffer =
...;`) failed to compile for exactly this reason. Fixed by building every
per-model `Buffer` as a plain local first, then constructing `GpuModelEntry`
via a single aggregate-initialization (`GpuModelEntry entry{
std::move(bindPoseBuffer), ... };`) — documented directly in the `.cpp`'s
own comment so a future reader doesn't reintroduce the same mistake.

### 6. Wiring into `AnimationSystem`/`Game::CreateMeshEntityFromGtaFile()`

`AnimationSystem` (`AnimationSystem.h/.cpp`) gained two new private
members — `GpuSkinningPipelines m_gpuSkinningPipelines;` and
`GpuSkinningRigCache m_gpuRigCache;` — plus a new public method,
`RegisterGpuSkinnedMesh(renderer, absoluteGtaPath, data, parts)`, mirroring
`RegisterSkinnedMesh()`'s own inline shape exactly (calls straight into
`m_gpuSkinningPipelines.EnsureInitialized()` + `m_gpuRigCache.Register()`).

`Game::CreateMeshEntityFromGtaFile()` (`Game.cpp`) now calls
`m_animationSystem.RegisterGpuSkinnedMesh(renderer, absoluteGtaPath, *skin,
*parts)` **immediately alongside** (never instead of) the existing
`RegisterSkinnedMesh()` call, at the exact same explicit hand-off site,
gated behind the exact same "this model turned out to be skinned" check —
per the strategy document's own Step 4/5: "both registrations happen
unconditionally for a rigged model, regardless of which mode is currently
active, so switching modes later (Phase 5) never needs a lazy fallback
path."

## Wiring

- `src/Renderer/GpuResourceFactory.h`/`.cpp` — `CreateGpuSkinningTargetBuffer()`.
- `src/Renderer/Renderer.h`/`.cpp` — `CreateGpuSkinningTargetBuffer()` (thin forward).
- `src/Game/Instantiation/MeshAssetGpuCatalog.h`/`.cpp` — `MeshAssetPart::indices` field + population.
- `src/Game/Instantiation/MeshAssetPartGrouping.h`/`.cpp` — new file (`GroupMeshAssetPartsBySharedVertexBuffer()`, `MeshAssetPartGroup`).
- `src/Game/Animation/AnimationSystem.cpp` — refactored to call the shared grouping function instead of its own inline `PendingGroup` logic (pure refactor, no behavior change).
- `src/Game/Animation/GpuSkinningRigCache.h`/`.cpp` — new file (the cache itself).
- `src/Game/Animation/AnimationSystem.h`/`.cpp` — new members + `RegisterGpuSkinnedMesh()`.
- `src/Game/Game.cpp` — new call site alongside `RegisterSkinnedMesh()`.
- `CMakeLists.txt` — added `MeshAssetPartGrouping.h/.cpp` and `GpuSkinningRigCache.h/.cpp` to `gte_core`'s source list.

No shader/`CMakeLists.txt` shader-staging changes were needed — Phase 2's
two `.comp` shaders and their pipeline scaffolding (`GpuSkinningPipelines`)
are consumed as-is, unmodified.

## What was deliberately NOT done (per Phase 4's own "What We Will NOT Do", and the master strategy's phase fence)

- **No lazy/deferred GPU buffer creation triggered from `AnimationSystem::
  Update()`'s hot per-frame loop.** `Register()` is always called once, up
  front, from the exact same explicit hand-off call site the CPU path
  already uses — never "the first time GPU mode happens to be selected
  while this model is playing."
- **No attempt to unify `SkeletalRigCache` and `GpuSkinningRigCache` into
  one combined class** — kept as two small, focused, single-responsibility
  caches, per this module's own established convention.
- **No sharing of GPU buffers across two different `*.gta` files**, even
  with identical vertex counts — identity is always keyed by absolute
  path, exactly like every existing cache in this module.
- **No eviction/free policy for a `GpuSkinningRigCache` entry** — matches
  every other cache in this module's own "load once, cache, never evict"
  behavior.
- **No actual compute dispatch, no `RenderGraph`/`AddComputePass()` call
  site, and no runtime CPU/GPU switch of any kind.** This phase's
  deliverable stops at "the per-model GPU resources exist, are correctly
  laid out/bound, and are ready for Phase 5 to actually dispatch a compute
  pass against and swap a `MeshRenderer`'s `MeshHandle` onto" — see this
  report's own "Notes for future phases" below. `AnimationSystem::Update()`
  itself is completely unmodified in its own skinning-mode behavior (still
  100% CPU path, unconditionally) — `m_gpuRigCache`/`m_gpuSkinningPipelines`
  are populated but never read anywhere yet.
- **No bone-matrix upload.** `GpuModelEntry::boneMatricesBuffer` is created
  at the correct size but left with whatever undefined contents
  `CreateStructuredBuffer()` leaves it in — Phase 5's own per-frame
  `Buffer::Upload()` call is what will first give it real contents.

## Verification performed

Per this session's own instructions — **fast compile check only**, no full
build/regression test/`ctest` run (explicitly deferred to later, after
every phase is done):

- `cmake --build build --target gte_core` — succeeded (after two build-error
  round-trips fixing real bugs caught by the compiler itself — see "What
  was built", item 5's own aggregate-initialization note, and a
  `GpuModelEntry::OutputGroup` vs. plain `OutputGroup` qualification
  mistake, both fixed and re-verified in this same session).
- `cmake --build build --target GreatTamanaEngine` — succeeded, including
  every shader-staging step (unaffected by this phase — Phase 2's two
  `.comp` shaders were already wired in and are unmodified here).
- `cmake --build build --target GreatTamanaEngineTests` — succeeded
  (links cleanly against the updated `gte_core` — no new test file was
  added this phase, see below).
- Did **not** run the full test suite (`ctest`), a clean `build_joboff`
  verification build, or any runtime/GPU-device smoke test — all
  explicitly deferred to "later, after everything done" per this
  session's instructions.

### Why no new automated test file was added this phase

Every genuinely new piece of logic this phase adds is **Tier 2 by
construction** — it needs a live `VkDevice`/real, already-registered
`Mesh` objects to do anything at all (`Renderer::
CreateGpuSkinningTargetBuffer()`, `GpuSkinningRigCache::Register()`,
`GroupMeshAssetPartsBySharedVertexBuffer()`, which calls
`RenderSystem::TryGetMesh()` against real GPU-owning `Mesh` objects). This
mirrors `MeshAssetGpuCatalog`/`RenderSystem::Draw()`'s own existing,
accepted "Tier 2, no automated coverage yet" bucket (see `TESTING.md`) —
none of those have automated tests either, for the exact same reason.
There is no new *pure* logic this phase introduces that isn't already
covered: the grouping decision itself (`GroupMeshAssetPartsBySharedVertexBuffer()`)
is a thin wrapper around calling `RenderSystem::TryGetMesh()` and comparing
`VertexBufferIdentity()` pointers — genuinely impossible to exercise
without a live `Mesh`, unlike e.g. `MeshMaterialPartitioner`'s pure
index-range math (which already has its own dedicated test file).

## Notes for future phases

- **Phase 5 (Runtime CPU/GPU Switch)** is what actually:
  1. Declares a real `AddComputePass("SkinModel:<name>", ...)` per
     `GpuModelEntry::OutputGroup` (per Phase 3's already-landed
     `ImportBuffer()`/`VertexBufferRead` primitives and its own required
     read-before-write mitigation for two writers sharing one output
     buffer — see `GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md`,
     Step 3.6).
  2. Uploads real skinning matrices into `GpuModelEntry::boneMatricesBuffer`
     every frame a model is animated in GPU mode (`Buffer::Upload()` — the
     entire per-frame CPU cost of GPU-mode skinning, per that phase's own
     strategy document).
  3. Uses `GpuModelEntry::TryGetGpuMeshHandle(cpuMeshHandle)` (already
     provided by this phase) to resolve which `MeshHandle` a
     `MeshRenderer` component should point at for the currently-selected
     skinning mode — swapping between the CPU-mode handle
     (`MeshAssetPart::mesh`, already registered by
     `MeshAssetGpuCatalog`) and this phase's own GPU-mode handle needs NO
     further GPU work at switch time, exactly as this phase's own header
     comments promise.
  4. Must respect this campaign's own "STRICTLY SEQUENTIAL" per-animator
     loop rule in `AnimationSystem::Update()` — unaffected by this phase,
     since nothing here touches that loop's own iteration order.
- **`GpuModelEntry::uvBuffer` is genuinely optional** — a future Phase 5
  dispatch call site for an UNTEXTURED group must never attempt to bind
  binding 4 at all (mirrors `SkinVerticesPositionNormal.comp`'s own
  4-binding-only layout, as opposed to `SkinVerticesPositionNormalUv.comp`'s
  5-binding one) — this phase's own descriptor-set `Rewrite()` call already
  only ever includes binding 4's write when `group.textured` is true, so a
  future dispatch call site building a fresh `ComputeDescriptorSet` for a
  *different* purpose against the *same* layout must follow this same rule.
- **A model re-imported/re-spawned from the SAME `*.gta` path a second time
  within one process session will call `GpuSkinningRigCache::Register()`
  again**, which will silently replace (`insert_or_assign`) the earlier
  `GpuModelEntry` — the OLD GPU buffers/descriptor sets/Meshes are simply
  leaked (never explicitly destroyed) in that case, exactly mirroring
  `SkeletalRigCache`/`AnimationClipCache`'s own existing "load once, cache,
  never evict" behavior for the CPU-side caches (this is a pre-existing,
  accepted limitation of this whole module, not something this phase
  introduces or is expected to fix).
