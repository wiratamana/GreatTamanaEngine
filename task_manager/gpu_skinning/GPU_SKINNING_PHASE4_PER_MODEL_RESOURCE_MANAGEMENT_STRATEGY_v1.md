# GPU Vertex Skinning — Phase 4: Per-Model Resource Management

Part of the GPU Vertex Skinning campaign — see
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v1.md` for the campaign map. Depends
on Phase 1 (buffer layouts), Phase 2 (compute kernel), and Phase 3
(`ImportBuffer()`/`VertexBufferRead`) all being complete.

## Step 1: The Goal (Where are we going?)

A GPU-side sibling of `SkeletalRigCache`/`AnimationClipCache`/
`ResolvedAnimationBindingCache` — a small, explicitly-owned cache class,
`GpuSkinningRigCache`, that owns, per distinct animated model:

- The three **immutable** GPU buffers (bind pose, skin weights — uploaded
  once, at the moment a model is first registered for GPU skinning) plus
  the **GPU-only, compute-write-only** skinned output buffer(s) — one per
  distinct shared vertex buffer, exactly mirroring `AnimationSystem.cpp`'s
  existing Stage-1 de-duplication (`PendingGroup`/`VertexBufferIdentity()`)
  so a multi-part model with several textured submeshes sharing one
  underlying buffer still gets **exactly one** GPU output buffer, not one
  per part.
- The small, **per-frame-rewritten** bone matrix buffer.
- The `ComputeDescriptorSet` for each distinct output buffer, kept
  up to date (rewritten whenever the physical buffer behind any of its
  four bindings could have changed identity — in practice, only ever once,
  at creation, since every one of these four buffers has a *stable*
  identity for the model's whole lifetime; see Step 3.4).
- A `Mesh` object per shared-buffer group, built from the GPU-only output
  buffer, that `RenderSystem`/`Renderer::Submit()` can draw exactly like
  any other `Mesh` — this is what makes GPU skinning invisible to the
  drawing code, exactly like `Renderer::CreateSkinnedMesh()`'s CPU-mode
  `Mesh` already is today.

## Step 2: The Situation / The Problem (Where are we now?)

The CPU path's existing per-model lifecycle, which this phase mirrors as
closely as possible (deliberately — consistency with an already-proven
design is a virtue here, not a constraint to work around):

- `SkeletalRigCache::Register(absoluteGtaPath, SkinnedMeshData)` — called
  once, from `AnimationSystem::RegisterSkinnedMesh()`, itself called from
  `Game::CreateMeshEntityFromGtaFile()` right after
  `MeshInstantiationSystem::SpawnMeshAsset()` succeeds for a rigged model.
  This is the **one real hand-off point** in the whole engine where
  "a model's CPU-side bind pose/skin weight data becomes available" is an
  explicit, visible function call — not an implicit side effect. GPU
  registration must hook the exact same call site.
- `AnimationSystem::m_scratchBuffers` (a `std::unordered_map<std::string,
  AnimatorScratchBuffers>`, keyed by mesh `*.gta` path) — the precedent for
  "one small struct of per-model resources, looked up by path, lazily
  populated, reused every frame forever after." `GpuSkinningRigCache`
  follows the exact same shape, just holding GPU buffers/descriptor sets
  instead of CPU scratch vectors.
- `AnimationSystem::Update()`'s existing `PendingGroup` de-duplication loop
  (grouping a model's `MeshAssetPart`s by `Mesh::VertexBufferIdentity()`) —
  this exact grouping must be computed **once**, at GPU-registration time,
  not re-derived every frame, since which parts share a buffer never
  changes after a model is loaded. Cache the grouping itself (a
  `std::vector<PendingGroup>`-shaped structure) inside the cache entry.
- `Renderer::CreateStructuredBuffer()` — already exists, already the right
  primitive for the three immutable per-model buffers and the per-frame
  bone matrix buffer. What does **not** exist yet is a factory for the
  fourth, GPU-only, combined `STORAGE_BUFFER | VERTEX_BUFFER` output
  buffer (Phase 1, Step 3.4's requirement) — this phase must add it.
- `Mesh`'s constructors today only cover: non-indexed (`CreateMesh`),
  indexed immutable (`CreateMesh`, indexed overload), indexed
  CPU-writable (`CreateSkinnedMesh`), and shared-vertex-buffer
  (`CreateMeshFromSharedVertexBuffer`). None of these fit "indexed, vertex
  buffer is GPU-only, never touched by the CPU after creation, written
  exclusively by a compute shader." A fifth `Mesh` construction path is
  needed.

## Step 3: The Plan (How will we get there?)

### 3.1 — `Renderer::CreateGpuSkinningTargetBuffer()`

A new, narrowly-scoped factory, alongside `CreateStructuredBuffer()`:

```cpp
// Renderer.h / GpuResourceFactory.h
// Creates a device-local buffer usable BOTH as a compute shader's
// RWStructuredBuffer output AND as a real vertex-input-assembler vertex
// buffer - see GPU_SKINNING_PHASE1_..., Step 3.4 for the full reasoning.
// Never CPU-mappable (BufferMemoryUsage::GpuOnly) - written exclusively by
// a compute dispatch; the CPU never calls Upload()/UpdateVertexData() on
// the result.
Buffer CreateGpuSkinningTargetBuffer(VkDeviceSize size, const char* debugName = nullptr) const;
```

Implemented as a thin wrapper: `CreateBuffer(size,
VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
BufferMemoryUsage::GpuOnly, debugName)` — `CreateBuffer()` already accepts
an arbitrary `VkBufferUsageFlags` and already supports `GpuOnly`, so this
is genuinely a thin convenience wrapper, not new allocation logic. Unlike
`CreateDeviceLocalBuffer()`, there is **no initial staged upload** — the
buffer starts life with undefined contents, and is only ever meaningfully
populated by the first compute dispatch that targets it (which must always
happen before the first draw that reads it — guaranteed by this campaign's
own per-frame ordering, see Phase 3, Step 3.4, and Phase 5's "must skin
before first draw of a newly GPU-enabled model" rule below).

### 3.2 — `Mesh`'s fifth construction path

`Mesh` needs a constructor overload (or a dedicated named factory,
`Renderer::CreateMeshFromGpuSkinningTarget()`) that takes an
already-created `Buffer` (from 3.1 above, ownership shared via
`std::shared_ptr<Buffer>` exactly like `CreateMeshFromSharedVertexBuffer()`
already does — reused verbatim, since the "several parts share one
underlying vertex buffer, each with its own index buffer/range" shape is
*identical* to the existing shared-vertex-buffer CPU case), plus a
per-part index buffer (immutable, uploaded once via
`CreateDeviceLocalBuffer()`, exactly like every other `Mesh`'s index
buffer already is — index data never changes under skinning, only vertex
positions/normals do). This is **not** a sixth new concept — it is the
exact same `CreateMeshFromSharedVertexBuffer()` code path, just handed a
`Buffer` that happens to have been created via 3.1 instead of via
`CreateSharedMeshVertexBuffer()`'s staged-upload path. Confirm at
implementation time whether `CreateMeshFromSharedVertexBuffer()` can be
called completely unmodified with such a buffer, or needs a small,
backward-compatible relaxation (e.g. it may currently assert the shared
buffer was created with a specific usage-flag subset — check and adjust
if so, keeping every existing CPU-path caller's behavior unchanged).

### 3.3 — `GpuSkinningRigCache`

```cpp
// New file: src/Game/Animation/GpuSkinningRigCache.h/.cpp
class GpuSkinningRigCache {
public:
    // Mirrors SkeletalRigCache::Register() - called once, from the SAME
    // Game::CreateMeshEntityFromGtaFile() hand-off site, but ONLY when GPU
    // skinning mode is available/selected (see Phase 5) - registering a
    // model here is what actually allocates its GPU buffers, so a model
    // never spawned/animated in GPU mode never pays this cost at all.
    void Register(Renderer& renderer, const std::string& absoluteGtaPath,
        const SkinnedMeshData& data, const std::vector<MeshAssetPart>& parts);

    struct GpuModelEntry {
        Buffer bindPoseBuffer;      // immutable, uploaded once
        Buffer skinWeightsBuffer;   // immutable, uploaded once
        Buffer boneMatricesBuffer;  // CpuToGpu, rewritten every frame

        struct OutputGroup {
            const void* sourceIdentity;      // groups parts sharing one CPU-side Mesh::VertexBufferIdentity()
            std::shared_ptr<Buffer> outputVertexBuffer; // via CreateGpuSkinningTargetBuffer()
            VkDescriptorSet descriptorSet;
            bool isTextured;
            std::uint32_t vertexCount;
        };
        std::vector<OutputGroup> outputGroups;
    };

    const GpuModelEntry* TryGet(const std::string& absoluteGtaPath) const;

private:
    std::unordered_map<std::string, GpuModelEntry> m_models;
};
```

`Register()` performs, exactly once per model:

1. Pack + upload `bindPoseBuffer`/`skinWeightsBuffer` via Phase 1's
   `PackBindPoseVertices()`/`PackSkinWeights()` + `Renderer::CreateStructuredBuffer()`
   (CPU-to-GPU staged upload, immutable afterward — `BufferMemoryUsage::GpuOnly`
   via `CreateDeviceLocalBuffer()`-style staging, not `CpuToGpu`, since this
   data never changes again).
2. Create `boneMatricesBuffer` sized to `skeleton.bones.size()`, via
   `CreateStructuredBuffer(sizeof(Mat4), boneCount, BufferMemoryUsage::CpuToGpu)`.
3. Group `parts` by `Mesh::VertexBufferIdentity()` (reusing exactly the
   grouping logic `AnimationSystem::Update()` already computes inline today
   — extract it into a small shared free function,
   `GroupMeshAssetPartsBySharedVertexBuffer()`, callable from both this new
   cache AND the existing CPU path, so the two never drift into two
   independently-maintained copies of the same grouping logic).
4. For each group: create one `outputVertexBuffer` via 3.1, sized to
   `vertexCount * sizeof(GpuSkinnedVertexPositionNormal|Uv)` (per Phase 1),
   allocate + `Rewrite()` its `ComputeDescriptorSet` against the four
   buffers (bind pose/weights/bone matrices/this group's own output —
   Phase 2's binding table), and build the actual `Mesh` object via 3.2,
   handed off to whatever owns rendering for this model (see 3.5 below).

### 3.4 — Descriptor sets never need re-`Rewrite()`ing after creation

Unlike the general `ComputeDescriptorSet::Rewrite()` convention (documented
as "safe, and expected, to call every frame" for a resource whose physical
identity may change frame to frame — e.g. a render-graph-pooled texture),
every one of this campaign's four buffers has a **permanently stable**
identity for the model's entire lifetime: the three immutable buffers are
never recreated, and the output buffer (via 3.1/3.2, `shared_ptr`-owned
exactly like the CPU shared-vertex-buffer path) is also never recreated
after `Register()`. So `Rewrite()` is called exactly **once**, at
registration time, and never again — document this explicitly as a
deliberate, correctness-justified deviation from the general convention
(not an oversight), since a future reader familiar with
`ComputeDescriptorSet`'s own "expect to call every frame" framing might
otherwise "fix" this into an unnecessary per-frame rewrite.

### 3.5 — Bone matrix upload is the only genuinely per-frame GPU-side step

Each frame, for a GPU-mode animator, after `EvaluateAnimatedSkinningPose()`
produces its `std::vector<Mat4>` (completely unchanged from today):

```cpp
gpuEntry.boneMatricesBuffer.Upload(skinningMatrices.data(),
    skinningMatrices.size() * sizeof(Mat4));
```

That's it — no packing, no per-vertex CPU work, no `Mesh::UpdateVertexData()`
call. This single `Upload()` call (into an already-mapped `CpuToGpu`
buffer — cheap, a plain `memcpy`) is the *entire* per-frame CPU cost of
GPU-mode skinning for one model, and is exactly the number this campaign's
whole point is to make visible against the CPU path's own, much larger,
per-frame packing+upload cost (Phase 6/7).

## Step 4: What We Will NOT Do (Focus)

- **No lazy/deferred GPU buffer creation triggered from inside
  `AnimationSystem::Update()`'s hot per-frame loop.** `Register()` is
  always called once, up front, from the exact same explicit hand-off
  call site the CPU path already uses (`Game::CreateMeshEntityFromGtaFile()`)
  — never "the first time GPU mode happens to be selected while this model
  is playing." This keeps the per-frame `Update()` loop free of any
  "have I registered this yet?" branch/lazy-init cost, mirroring the CPU
  path's own existing discipline exactly.
- **No attempt to unify `SkeletalRigCache` and `GpuSkinningRigCache` into
  one combined class.** They serve genuinely different resource kinds
  (CPU `std::vector`s vs. GPU `Buffer`/descriptor-set handles) with
  different lifetimes and different owners in mind — keeping them separate
  mirrors this codebase's existing "small, focused, single-responsibility
  cache classes" convention (`AGENTS.md`'s own description of the
  `src/Game/Animation/` refactor: "three small, explicitly-owned caches...
  replacing `Game`'s old anonymous private members").
- **No sharing of GPU buffers across two different `*.gta` files**, even
  if they happen to have identical vertex counts — identity is always
  keyed by absolute path, exactly like every existing cache in this
  module.
- **No attempt to free/evict a `GpuSkinningRigCache` entry when a model
  is no longer on screen.** Matches the CPU caches' own existing
  "load once, cache, never mutate/evict again for the process's lifetime"
  behavior — a genuine eviction policy is out of scope for every cache in
  this module today, GPU or CPU, and not a gap this campaign is asked to
  close.

## Step 5: Their Role (What does this mean for you?)

- Land `Renderer::CreateGpuSkinningTargetBuffer()` + `Mesh`'s new
  construction path first, each with whatever Tier-1 coverage is actually
  possible (the buffer-creation call itself is Tier 2/manual, same bucket
  as every other `Buffer`/`Mesh` factory — but any pure grouping/packing
  logic extracted alongside it, e.g.
  `GroupMeshAssetPartsBySharedVertexBuffer()`, must be Tier-1-tested, per
  `AGENTS.md`'s standing rule).
- Land `GpuSkinningRigCache` next, wired into the existing
  `Game::CreateMeshEntityFromGtaFile()` hand-off site alongside (not
  instead of) the existing `AnimationSystem::RegisterSkinnedMesh()` call —
  both registrations happen unconditionally for a rigged model, regardless
  of which mode is currently active, so switching modes later (Phase 5)
  never needs a lazy "oh, I need to register this now" fallback path.
- **Verify, manually, against a live device**, that a freshly-created
  `GpuSkinningRigCache` entry's `Mesh` renders as pure garbage/undefined
  data if you deliberately skip the very first compute dispatch — this is
  the sharpest edge in this phase's whole design (an uninitialized
  `GpuOnly` buffer has no defined contents until something writes it), and
  confirming you understand exactly when that first write must happen
  (Phase 5's ordering rules) before moving on is worth the five minutes it
  takes.
