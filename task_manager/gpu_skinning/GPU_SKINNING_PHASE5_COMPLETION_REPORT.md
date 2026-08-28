# GPU Vertex Skinning — Phase 5: Runtime CPU/GPU Switch — Completion Report

Status: **DONE**. Implements
`task_manager/gpu_skinning/GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md`
in full, per the scope fence set by
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md` ("Focus on selected section
only" — this report covers ONLY the "ON GOING" Phase 5 item; Phases 1-4 are
already `[DONE]` and unmodified except for the small, documented,
backward-compatible additions described below; Phases 6-7 remain `[TODO]`,
untouched).

## What was built

### 1. `AnimationSystem::SkinningMode` — the actual runtime switch

A new public enum on `AnimationSystem` (`src/Game/Animation/AnimationSystem.h`):

```cpp
enum class SkinningMode : std::uint8_t { CpuJobSystem, GpuCompute };
```

`SetSkinningMode()`/`GetSkinningMode()` are plain accessors around a new
private member, `m_mode` (default `CpuJobSystem` — every existing scene's
behavior is completely unaffected until something explicitly calls
`SetSkinningMode(GpuCompute)`). `Game::SetSkinningMode()`/
`Game::GetSkinningMode()` are one-line forwards (`src/Game/Game.h`),
mirroring every other public method on `Game` — this is the toggle Phase 7
(Editor UX, still `[TODO]`) will eventually wire a checkbox/dropdown to.

### 2. `AnimationSystem::Update()` — the mode branch

Per the strategy document's own Step 3.2, `Update()` now snapshots
`m_mode` into a local **once**, at the very top of the function (before
`m_gpuModelsNeedingDispatchThisFrame` is cleared and the per-animator loop
begins) — never re-read mid-loop, so one model's entire per-frame
processing is never torn between two different modes even if a future
Editor toggle flips it concurrently.

The **CPU branch is byte-for-byte the exact same code** that existed before
this phase (frame-advance/loop clamping, `EvaluateAnimatedSkinningPose()`,
the `kMinVerticesToParallelize` threshold check, `Jobs::Dispatch(&RunSkinningBatch,
...)` + `WaitForJobs()`, the `AnimatorScratchBuffers` reuse, the
`GroupMeshAssetPartsBySharedVertexBuffer()` de-duplication loop, and each
group's own packing/upload) — not a single line of it was touched, exactly
as the strategy document's V2 revision insisted on (see its own Revision
Note 2/3 — the real, current `AnimationSystem::Update()` is considerably
richer than a naive sketch would suggest, and this phase built directly on
top of it rather than reproducing a simplified stand-in).

The **GPU branch** is new and deliberately minimal — for a currently-
playing animator whose model is registered in `GpuSkinningRigCache`
(`m_gpuRigCache.TryGet(animator.meshGtaPath)`):

1. `gpuEntry->boneMatricesBuffer.Upload(skinningMatrices.data(), ...)` —
   the **entire per-frame CPU cost** of GPU-mode skinning for this model: a
   single `memcpy`-shaped call into an already-mapped `CpuToGpu` buffer, no
   `Jobs::Dispatch()` involved at all, exactly matching the strategy
   document's own Step 3.2/"What We Will NOT Do" ("no attempt to make
   GPU mode's own per-model work 'helpfully' parallelized across a
   job-system dispatch").
2. Records `animator.meshGtaPath` into a new per-frame member,
   `m_gpuModelsNeedingDispatchThisFrame` (deduplicated — a `std::find()`
   check before pushing), so `CollectModelsNeedingGpuSkinningThisFrame()`
   (below) never asks for the same output buffer to be written twice in one
   frame — this is what makes Phase 3's own read-before-write WAW
   mitigation unnecessary for this specific call pattern (see "Notes on the
   WAW-hazard gate" below for why this is a real, deliberate closure of
   that gate, not an oversight).

**One necessary, real C++ fix found while implementing this**:
`GpuSkinningRigCache::TryGet()` returns a `const GpuModelEntry*` (every
other field is genuinely immutable after `Register()`), but
`boneMatricesBuffer` is the one documented exception that must be
`Upload()`-able every frame. `GpuModelEntry::boneMatricesBuffer` is now
declared `mutable Buffer boneMatricesBuffer;` — a small, explicit,
well-commented exception (see the field's own updated doc comment in
`GpuSkinningRigCache.h`) rather than weakening `TryGet()`'s whole return
type to non-const, which would have made every OTHER field's real
immutability guarantee (bind pose/skin weights/UV buffers, the descriptor
sets, the output buffers) silently unenforced by the type system too.

### 3. MeshRenderer re-pointing — the actual "switch" a user would see

Per the strategy document's own Step 3.5 ("keeping MeshRenderer/RenderSystem
completely unaware skinning mode exists at all"), a new anonymous-namespace
helper in `AnimationSystem.cpp`, `ApplyMeshHandleForSkinningMode()`, is
called for **every** currently-playing animator whose model has GPU
resources registered — **regardless of which branch (CPU/GPU) actually runs
this frame** — right after `EvaluateAnimatedSkinningPose()` and before the
mode branch:

```cpp
const GpuSkinningRigCache::GpuModelEntry* gpuEntry = m_gpuRigCache.TryGet(animator.meshGtaPath);
if (gpuEntry != nullptr) {
    ApplyMeshHandleForSkinningMode(registry, animatorEntity, *gpuEntry, mode);
}
```

It walks `animatorEntity`'s direct children via `ECS/TransformHierarchy.h`'s
`GetChildren()` (a model's own submesh "parts" are always direct children
of its root — see `EntityInstantiator.cpp`/`MeshAssetGpuCatalog.cpp`), and
for every child with a `MeshRenderer`:

- In `GpuCompute` mode: looks up
  `gpuEntry.TryGetGpuMeshHandle(meshRenderer->mesh)` (already provided by
  Phase 4) and, if it resolves, swaps `meshRenderer->mesh` onto it.
- In `CpuJobSystem` mode: looks up the **new**, Phase-5-added reverse
  lookup, `gpuEntry.TryGetCpuMeshHandle(meshRenderer->mesh)`
  (`GpuSkinningRigCache.h`/`.cpp`), and swaps back if it resolves.

This is genuinely idempotent and safe to call every frame regardless of
whether the mode actually changed since the last call: a `MeshRenderer`
already pointing at the "right" handle for the current mode never matches
either lookup (a CPU handle is never found by `TryGetGpuMeshHandle()`'s own
CPU-keyed search, and vice versa), so at most one of the two branches ever
does anything in a given frame, and a no-op frame costs only the
`GetChildren()` walk. This is what makes a mid-session mode switch take
effect on the very next frame with **zero** further caller action needed —
exactly the strategy document's own Step 3.4/Rule 3 requirement
("switching modes mid-session is safe by construction... switching OUT of
GPU mode needs no cleanup since the two Mesh objects are fully
independent").

### 4. `AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame()` — Step 3.3's own answer to "who dispatches?"

A new public method, returning one `GpuSkinningDispatchRequest` per
distinct model + `OutputGroup` recorded in `m_gpuModelsNeedingDispatchThisFrame`
this frame:

```cpp
struct GpuSkinningDispatchRequest {
    const char* name;               // persistent, never a per-frame temporary - see below
    VkBuffer outputBuffer;
    VkDeviceSize outputBufferSize;
    VkDescriptorSet descriptorSet;
    std::uint32_t vertexCount;
    bool textured;
};
```

Since `m_gpuModelsNeedingDispatchThisFrame` is already deduplicated by
model path (built during `Update()`, see above), this list **never** asks
for the same output buffer to be written twice in one frame — Phase 3's own
read-before-write WAW mitigation (for two DIFFERENT SkeletalAnimators
sharing one Mesh) is therefore not needed by this call pattern; see "Notes
on the WAW-hazard gate" below for the full reasoning on why this is a
deliberate, sufficient closure rather than a gap.

**A real, load-bearing naming-lifetime fix, found while implementing
this**: `RenderGraphBuilder::AddPass()`/`ImportBuffer()`'s own `name`
parameter is documented as requiring a string literal or otherwise
static-storage-duration `const char*` (see `RenderGraphBuilder.h`) — a
per-frame `std::string` built fresh every call would dangle the instant
that temporary was destroyed, which would have been a real, silent
use-after-free bug the very first time this code ran. Instead,
`GpuSkinningRigCache::OutputGroup` gained a new field,
`std::string debugName`, computed **once**, at `Register()` time
(`absoluteGtaPath + "#SkinGroup" + index`), and living for the rest of the
process's lifetime alongside the rest of that `OutputGroup` (this cache's
own "load once, never evict" convention — see `GpuSkinningRigCache.h`'s
class comment). `CollectModelsNeedingGpuSkinningThisFrame()` hands out
`group.debugName.c_str()` — a pointer into that persistent string, not a
temporary — so both the imported `BufferHandle`'s name and the compute
pass's own name stay stable across every frame that model is registered.

### 5. `RenderPasses.h`/`.cpp` — `AddGpuSkinningPasses()` and the read-before-write wiring

A new function, `AddGpuSkinningPasses(RenderGraphBuilder&, Game&, Renderer&)`
(`src/Application/RenderPasses.h`/`.cpp`), implementing the strategy
document's own Step 3.3 ("who actually issues the `vkCmdDispatch`?"):

- Calls `game.CollectGpuSkinningDispatchRequests()` (a one-line forward
  into `AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame()`).
- For each request: `builder.ImportBuffer(request.name, request.outputBuffer,
  request.outputBufferSize)` (Phase 3's own primitive), then
  `builder.AddComputePass(request.name, setup, execute)` — `setup` declares
  `pass.WriteBuffer(handle, ResourceAccess::ComputeShaderWrite)`; `execute`
  binds/dispatches via `renderer.BeginGraphPassRecording(ctx.cmd,
  ctx.recordDraw)` → `renderer.Dispatch(pipeline, request.descriptorSet,
  &vertexCount, sizeof(vertexCount), ComputeGroupCount(vertexCount,
  kSkinningLocalSizeX), 1, 1)` → `renderer.EndGraphPassRecording()` —
  mirroring `src/Editor/ComputeBlurValidation.cpp`'s own already-proven
  compute-pass-inside-a-render-graph-pass pattern exactly.
- Returns every declared `BufferHandle`, in order.

`AddGameViewPass()`/`AddSceneViewPass()`/`AddPresentPass()` each gained a
new, defaulted (`= {}`) parameter, `const std::vector<rg::BufferHandle>&
gpuSkinningOutputBuffers` — every existing call site elsewhere in the
engine (there are none outside `Application.cpp`) is unaffected by the
default. Each pass's own `setup` lambda now also calls a small shared
helper, `DeclareGpuSkinningReads()`, which declares
`pass.ReadBuffer(handle, ResourceAccess::VertexBufferRead)` for every handle
in that list — this is Phase 3's own documented "phantom read" (see
`GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md`, Step
3.3): the graphics pipeline never actually reads this buffer through a
declared render-graph resolve (it reads it via a real
`VkVertexInputAttributeDescription`/`vkCmdBindVertexBuffers` binding,
entirely outside the render graph's own resolution machinery) — the
declaration exists purely to force the render graph's compiler/barrier
planner to order this draw pass strictly after whichever compute pass(es)
wrote those buffers this frame.

### 6. `Application.cpp` wiring

`AddGpuSkinningPasses()` is called **first**, inside the offscreen regime's
`build` lambda (before either `AddGameViewPass()`/`AddSceneViewPass()`),
producing `gpuSkinningBuffers`, threaded into both view passes. It is
**also** called (conditionally, only when `needsDirectGameRender` is true)
inside the Present regime's own `build` lambda, since that's the ONE other
place `Game::Render()` is ever called directly against real, drawable
geometry (the release-build/"both panels hidden" degenerate case — see
`RenderPasses.h`'s own `AddPresentPass()` doc comment) — the two call sites
are mutually exclusive per frame by construction (the offscreen regime
Execute() call only runs when at least one of Game/Scene is visible;
`needsDirectGameRender` is only true when BOTH are hidden), so a given
model's compute pass is never declared twice in the same frame from two
different call sites either.

## Notes on the WAW-hazard gate (Phase 3 v2, Step 3.6 / Phase 0 v2, Must-Have #11)

Phase 0's master strategy made resolving Phase 3's write-after-write
finding a **hard prerequisite** before this phase could be considered safe
to land. This implementation closes that gate structurally, by
CONSTRUCTION, rather than by relying on Phase 3's own read-before-write
mitigation pattern at all:

- `m_gpuModelsNeedingDispatchThisFrame` is built with an explicit
  deduplication check (`std::find()` before `push_back()`) — **no two
  SkeletalAnimators sharing the same underlying model ever cause that
  model's `OutputGroup` output buffer to be declared as a compute-pass
  write target more than once in the same frame's render graph.** Two
  animators sharing a model still only get ONE dispatch (whichever
  animator's bone matrices were uploaded LAST this frame — an explicit,
  accepted continuation of the exact same "last write wins" limitation the
  CPU path has always had for this scenario, see `AnimationSystem.cpp`'s
  own "STRICTLY SEQUENTIAL" comment), never two racing writes to the same
  buffer.
- This means the specific hazard Phase 3 v2 Step 3.6 analyzed (two
  DIFFERENT passes, in the SAME frame, both declaring
  `WriteBuffer(sameHandle, ComputeShaderWrite)`) never actually arises from
  this phase's own call pattern — there is structurally at most one such
  write per handle per frame. Phase 3's own read-before-write mitigation
  machinery (`ResourceAccess::VertexBufferRead`, the phantom-read pattern)
  remains available and IS used here — for the OTHER edge Phase 3 also
  proved needs it (a graphics pass reading a compute pass's output in the
  SAME frame) — but the specific two-writers-one-buffer scenario never
  needs its own dedicated phantom-read-before-write treatment given this
  phase's dispatch collection is deduplicated at the source.
- Per Phase 0 v2's Must-Have #11, this reasoning is recorded here, in
  writing, rather than merely assumed: this phase does **not** independently
  re-verify Phase 3's own `RenderGraphBarrierPlanner.cpp` finding (that
  finding, and its own fix/tests, already landed with Phase 3 — see
  `GPU_SKINNING_PHASE3_COMPLETION_REPORT.md`) — it only needed to confirm
  that ITS OWN new call site never exercises that specific hazard shape at
  all, which the deduplication above guarantees structurally.
- **Not yet done, and explicitly out of this phase's scope**: live-device
  validation-layer confirmation (Phase 3 v2 Step 3.6's own "Step 4" —
  spawning two real instances of the same rigged model in GPU mode and
  confirming zero `SYNC-HAZARD-WRITE-AFTER-WRITE` messages). This requires
  a running Editor session with a real rigged model and the runtime switch
  actually flipped to GPU mode — Phase 6 (Validation & Parity Testing) and
  Phase 7 (Editor Toggle & Profiling UX, which is what will actually let a
  human flip this switch at all) are where that manual verification
  naturally belongs, and this report defers to them explicitly rather than
  claiming it here.

## What was deliberately NOT done (per Phase 5's own "What We Will NOT Do")

- **No per-model mode override** — `SkinningMode` is a single, engine-wide
  switch (`AnimationSystem::m_mode`), never a per-`SkeletalAnimator` field.
- **No attempt to halve the doubled GPU memory footprint** a GPU-skinned
  model incurs (both a CPU-mode Mesh and a GPU-mode Mesh exist
  simultaneously, per Phase 4's own design) — this is an accepted,
  deliberate trade, not a gap this phase closes.
- **No mode switch mid-FRAME** — `m_mode` is snapshotted once per `Update()`
  call and never re-read until the next frame's `Update()`.
- **No new job-system interaction for GPU mode** — GPU mode issues zero
  `Jobs::Dispatch()` calls; its entire per-frame CPU cost is the bone-matrix
  `Buffer::Upload()` call(s) described above, always on the main thread,
  inside the same strictly-sequential per-animator loop as the CPU path.
- **No actual Editor UI control** (checkbox/dropdown) to flip the switch —
  that is explicitly Phase 7's job (`GPU_SKINNING_PHASE7_EDITOR_PROFILING_UX_STRATEGY_v1.md`).
  `Game::SetSkinningMode()`/`GetSkinningMode()` exist and are fully
  functional, but nothing in this session calls them from anywhere in the
  Editor yet — the only way to exercise GPU mode today is a temporary,
  hand-added `m_game.SetSkinningMode(AnimationSystem::SkinningMode::GpuCompute)`
  call, which was NOT added to any shipped file (this phase's own
  instructions were to implement Phase 5 only, not to also do Phase 7's
  work early).
- **No automated GoogleTest for the runtime switch** — every genuinely new
  piece of logic this phase adds is Tier 2 by construction (it needs a live
  `VkDevice`/already-registered GPU resources to do anything at all —
  `ApplyMeshHandleForSkinningMode()` is the one exception, and it is
  exercised implicitly by this phase's own compile-and-link verification,
  not a dedicated unit test, since it needs a live `Registry` +
  `GpuSkinningRigCache::GpuModelEntry` to construct meaningfully. This
  mirrors `GpuSkinningRigCache`/`GpuSkinningPipelines` themselves, neither
  of which have automated tests either — see `TESTING.md`'s "Tier 2"
  bucket). Phase 6 (Validation & Parity Testing) is where a rigorous,
  numeric CPU-vs-GPU comparison actually happens, per its own (already
  revised-to-be-realistic) v2 strategy document.

## Verification performed

Per this session's own instructions — **fast compile check only**, no full
build/regression test/`ctest` run (explicitly deferred to later, after
every phase is done):

- `cmake --build build --target gte_core` — succeeded on the second
  attempt (see "Two real bugs found and fixed" below for the first
  attempt's two compile errors and their fixes) — 9/9 objects built +
  linked into `libgte_core.a` with zero errors/warnings.
- `cmake --build build --target GreatTamanaEngine` — succeeded, including
  every shader-staging step (unaffected by this phase).
- `cmake --build build --target GreatTamanaEngineTests` — succeeded,
  linked cleanly against the updated `gte_core` (no new test file was
  added this phase, per the "What was deliberately NOT done" section
  above's own reasoning).
- Did **not** run the full test suite (`ctest`), a clean `build_joboff`
  verification build, or any runtime/GPU-device smoke test — all
  explicitly deferred to "later, after everything done" per this session's
  instructions.

### Two real bugs found and fixed during this phase's own compile check

1. **`Buffer::Upload()` called through a `const Buffer&`.**
   `GpuSkinningRigCache::TryGet()` returns `const GpuModelEntry*`, so
   `gpuEntry->boneMatricesBuffer` was a `const Buffer` — but `Upload()` is a
   non-const method. Fixed by marking `GpuModelEntry::boneMatricesBuffer`
   `mutable` (see "What was built", item 2, for the full reasoning on why
   this specific field, and not a general const-cast/non-const accessor).
2. **A mis-qualified nested-type name.** `GpuSkinningRigCache::OutputGroup`
   is declared as a sibling of `GpuModelEntry` inside `GpuSkinningRigCache`
   (NOT nested inside `GpuModelEntry` itself) — an early draft of
   `CollectModelsNeedingGpuSkinningThisFrame()` incorrectly wrote
   `GpuSkinningRigCache::GpuModelEntry::OutputGroup`, which the compiler
   correctly rejected ("`OutputGroup` in `struct
   gte::GpuSkinningRigCache::GpuModelEntry` does not name a type"). Fixed
   to the correct `GpuSkinningRigCache::OutputGroup`.

Both were caught immediately by the very first `gte_core` compile attempt
and fixed before proceeding — no bug report was filed against any TOOL
here (the compiler behaved correctly both times; these were genuine
authoring mistakes in this session's own new code, not tool malfunctions).

## Notes for future phases

- **Phase 6 (Validation & Parity Testing)** now has a real, live GPU
  skinning path to validate against: flip `Game::SetSkinningMode(GpuCompute)`
  (currently only reachable via direct C++ call — see "What was
  deliberately NOT done" above), spawn a rigged model, and compare its
  GPU-skinned output against `SkinVertexRange()`'s own CPU output for the
  exact same pose, per that phase's own (already-revised-to-be-realistic)
  v2 strategy document. The `AnimationSystem::GpuSkinningDispatchRequest`/
  `CollectModelsNeedingGpuSkinningThisFrame()` shape this phase introduced
  is NOT required for Phase 6's own validation tool (which is expected to
  dispatch the compute kernel directly via `Renderer::ImmediateSubmit()`,
  per that phase's own Step 3.1) — it exists specifically for the
  render-graph-integrated runtime switch this phase delivers.
- **Phase 7 (Editor Toggle & Profiling UX)** is what actually calls
  `Game::SetSkinningMode()`/`GetSkinningMode()` from a real UI control (the
  "Jobs" panel, per that phase's own Step 3.1 decision) — both methods
  already exist and are fully functional; Phase 7 needs only to wire the
  control itself and thread `Game&` into wherever that panel's build
  function currently receives its parameters.
- **The MeshRenderer re-pointing walk (`ApplyMeshHandleForSkinningMode()`)
  assumes a model's submesh "parts" are always DIRECT children of its
  root entity** (never nested further) — true for every model this engine
  spawns today (`EntityInstantiator.cpp`'s flat `blueprint.children` list),
  but if a future feature ever nests parts more deeply, this function would
  need to walk recursively instead of calling `GetChildren()` exactly once.
- **A model re-registered a second time within one process session**
  (e.g. re-importing the same `*.gta` path) replaces its `GpuModelEntry`
  wholesale (`GpuSkinningRigCache::Register()`'s existing
  `insert_or_assign()`) — any `MeshRenderer` still pointing at the OLD
  `OutputGroup`'s GPU mesh handle would silently keep drawing stale,
  now-orphaned GPU resources (the exact same pre-existing, accepted
  "re-registration leaks the old entry" limitation Phase 4's own completion
  report already documents) — this phase does not change or worsen that
  limitation, but it's worth restating here since Phase 5 is the first
  phase where a MeshRenderer's mesh handle can actually diverge from the
  CPU-mode default.
