# Why Animator CPU Skinning Is Still Slow Despite the Job System — Analysis & Proposal

**Status:** Problem analysis + proposed solution. No implementation in this
document — see AGENTS.md's "Job System" / "Skeletal Animation Pose
Resolution" sections and `JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md` for the
existing, already-shipped Job System + first-production-consumer work this
proposal builds on top of, not replaces.

**Scope:** CPU-side fix first (this is the actual, provable bottleneck for
the profiled scene below). A brief forward-looking section at the end
sketches the GPU-compute-skinning follow-up, per the request, but it is
explicitly out of scope for the immediate fix — moving the *current*
CPU-bound bottleneck onto the GPU as-is would just move the exact same
architectural waste from one processor to another.

---

## 1. Observed symptom

Profiler capture (attached screenshot), one entity ("Furina", a rigged MMD
import) actively animating, nothing else of note in the scene:

| Scope | Total | Calls |
|---|---|---|
| `Game::Update` | 52.60 ms | 1 |
| `AnimationSystem::Update` | 52.59 ms | 1 |
| `RenderGraph::Execute(Offscreen)` | 0.92 ms | 1 |
| everything else this frame | < 1 ms combined | — |

CPU frame time: **55.04 ms → ~18 FPS.** Draw stats: 35 draw calls, 39,040
triangles (consistent with README's own note that this Furina import has
**32 materials** — i.e. 32 separate mesh "parts" for one model, plus a
couple of incidental draws).

`AnimationSystem::Update` is essentially the *entire* frame budget. Job
System Phase 6 (`AGENTS.md`, "Job System") already dispatches this model's
CPU vertex-skinning blend (`SkinVertexRange()`) across the worker pool via
`gte::Jobs::Dispatch()` — and it still doesn't help. That's the actual
question this document answers: **why doesn't having a job system already
fix this**, and **what should be done about it, CPU-side, before reaching
for GPU compute at all.**

---

## 2. Root-cause analysis: what `AnimationSystem::Update()` actually does, per frame, per animator

Walking `src/Game/Animation/AnimationSystem.cpp`'s per-animator body in
order, with an honest cost model for each stage:

### Stage A — Pose evaluation (`EvaluateAnimatedSkinningPose()`)
Sample keyframes → solve IK chains (CCD, up to `kMaxIkIterations` = 200 per
chain) → apply PMX append/grant inheritance → forward-kinematics to
skinning matrices. Cost is **O(bone count)**, not O(vertex count) — for
this model, ~387 bones (per README's own import-verification numbers).
Runs **entirely on the main thread**, no `Jobs::Dispatch()` involved at
all. For a few hundred bones this is comparatively cheap next to what
follows, but it is worth flagging up front: **nothing in this stage is
parallelized today**, and it's on the main thread's critical path before
skinning can even begin.

### Stage B — CPU vertex skinning blend (`SkinVertexRange()` via `Jobs::Dispatch()`)
This is the part Job System Phase 6 actually parallelizes: split
`[0, vertexCount)` into batches, blend up to 4 bone influences per vertex
per batch, across the worker pool, then one `WaitForJobs()`. Cost is
**O(vertex count)** — for this model, tens of thousands of vertices. This
stage is correctly parallelized and is *not* the problem.

### Stage C — Per-part vertex repacking + GPU re-upload (main thread, unconditional)
This is the actual bottleneck, and it is **not touched by the Job System at
all** (by design — see AGENTS.md's Phase 4 thread-safety audit: GPU
resources/`Mesh`/`Renderer` are a hard **NEVER** for a job body). The loop:

```
for each MeshAssetPart in this model:
    Pack the WHOLE model's skinnedPositions/skinnedNormals into that
    part's own vertex layout (PackMeshVertices / PackMeshVertexUvs)
    -> a brand-new heap-allocated std::vector, full model size
    Mesh::UpdateVertexData(...)  // full memcpy into that part's own GPU buffer
```

The critical detail is in `AnimationSystem.cpp`'s own comment on this loop:
**"each part's own GPU vertex buffer holds a full copy of the whole
model's vertex data"**. In other words, every one of this model's 32
material parts owns an *independent, full-size copy* of every one of the
model's vertices — not just the subset of vertices that part's own
triangles actually reference. Only the **index buffer** differs per part;
the **vertex buffer** is the same data, duplicated 32 times over.

That turns Stage C's true cost into **O(vertexCount × partCount)**, not
O(vertexCount):

- Repacking: 32 full passes over the entire skinned vertex/normal arrays,
  each producing a fresh heap allocation sized to the *whole model*
  (`PackMeshVertices`/`PackMeshVertexUvs` return `std::vector` **by
  value**, allocated fresh every call, every frame).
- Upload: 32 full-size `memcpy`-equivalent writes into 32 separate
  persistently-mapped GPU buffers (`Mesh::UpdateVertexData()`), each sized
  to the *whole model*, not to that part's own vertex subset.

For a model with tens of thousands of vertices and 32 parts, this means
the engine is repacking and re-uploading on the order of a **million+
vertex-struct writes per frame**, plus **32 fresh multi-hundred-KB heap
allocations per frame** (allocated and freed every single frame,
unconditionally) — all of it single-threaded, on the main thread, entirely
outside the one region the Job System was applied to.

### Stage D — Strictly sequential outer loop across animators
`AnimationSystem::Update()`'s outer `for` loop over every live
`SkeletalAnimator` is explicitly, deliberately, and correctly kept
sequential — a full model's own Dispatch()+WaitForJobs()+GPU-upload
sequence must complete before the next animator's begins, because two
instances spawned from the same `*.gta` still share one underlying GPU
`Mesh` (a documented, tracked limitation — see `README.md`, and AGENTS.md's
Phase 4 audit table's own dedicated row for it). **This is not the cause
of the profiled slowdown** (there is exactly one active animator in this
scene) — but it is worth naming here because it is the *next* wall this
campaign will hit the moment a scene has more than one simultaneously
animated character, and any proposal that "just dispatches every
animator's work up front" would reintroduce a real GPU data race, not a
performance-only regression. See §5.5 (deferred).

### Summary table

| Stage | Cost | Parallelized today? | Dominant for this profile? |
|---|---|---|---|
| A. Pose evaluation (sample/IK/append/FK) | O(bones) | No — main thread only | Minor |
| B. Vertex skinning blend | O(vertices) | **Yes** — `Jobs::Dispatch()` | No — this is the part that *is* fast |
| C. Per-part repack + GPU upload | **O(vertices × parts)** | **No** — main thread, unconditional | **Yes — almost certainly the whole 52 ms** |
| D. Cross-animator sequencing | O(animator count) | N/A by design | No (only 1 animator here) |

---

## 3. Why "having a Job System" didn't fix this: Amdahl's Law, concretely

Job System Phase 6 was applied *correctly* to Stage B — but Stage B was
never the expensive part of this pipeline for a multi-material model.
Stage C, which is **structurally the larger cost by a factor equal to the
model's own material/part count (here, ~32×)**, sits entirely outside the
parallelized region and is furthermore *redundant* work, not merely serial
work — it repeats the exact same repack-and-upload operation 32 times over
instead of once.

This is Amdahl's Law in its most literal form: parallelizing a fraction of
a pipeline can only ever speed up *that* fraction; if the un-parallelized
remainder is itself several times *larger* than the parallelized part (as
it is here, since it's O(V×P) against O(V)), the overall frame time barely
moves no matter how many worker threads are thrown at Stage B. The Job
System isn't broken — it was pointed at a real but comparatively small
target while a much bigger, structurally redundant cost was left
completely untouched right next to it.

---

## 4. Proposed resolution — CPU first, staged, each stage independently shippable/testable

The engine's own conventions (AGENTS.md: Tier-1 testability, parity tests
for anything skinning-related, "GPU resources only ever touched from the
main thread") apply directly to every stage below.

### Stage 1 (highest priority, biggest win — architectural, not parallelism): stop duplicating the vertex buffer per part

The real fix for the dominant cost isn't "parallelize the repack loop" —
it's **not doing the repack 32 times in the first place**. This is a
mesh-storage/rendering change, not a Job System change:

- Move from "one full vertex-buffer copy per material part, differing only
  in which index range is drawn" to the standard multi-material mesh
  convention: **one shared vertex buffer for the whole model, plus one
  index buffer (or one shared index buffer with per-part index-range
  offsets) per part**, each part's draw call referencing the *same*
  vertex buffer with its own `firstIndex`/index range.
- This turns Stage C's cost from O(vertices × parts) back down to
  O(vertices) — skin once, pack once, upload once, per model, regardless
  of how many materials it has. For this Furina model that alone is
  roughly a **32× reduction** in the single most expensive stage in the
  profile.
- This is very likely why the mesh-instantiation code took the
  "duplicate everything, keep indices simple" shortcut originally (each
  part's index buffer can stay authored against the *whole* model's
  original vertex numbering, with zero re-indexing work at import time) —
  that shortcut is exactly what needs to be undone. The index buffers
  already reference the full model's own vertex numbering (per
  `MeshMaterialPartitioner`'s partitioning-by-index-range design), so
  switching every part over to point at one shared vertex buffer should
  not require re-deriving per-part vertex subsets or renumbering indices
  at all — only removing the duplication of the *vertex* buffer while
  keeping each part's own *index* buffer/range exactly as-is.
- Net effect: Stage C's per-frame cost collapses to "skin once, format
  once, upload once" for the whole model, independent of material count —
  which is what actually removes the current bottleneck, no additional
  threading required.

### Stage 2: fuse "skin" and "pack" into one job, writing straight into the final upload destination

Once Stage 1 removes the ×32 duplication, there is still a second,
smaller inefficiency worth removing at the same time: today, skinning
writes into a fresh `skinnedPositions`/`skinnedNormals` pair, which is
then *separately* walked a second time by `PackMeshVertices()`/
`PackMeshVertexUvs()` to produce a third, freshly-allocated
`MeshVertex`/`MeshVertexUv` array, which is *then* `memcpy`'d into the GPU
buffer by `Mesh::UpdateVertexData()` — three full passes over the vertex
array, two of them allocating heap memory, for what is conceptually one
operation.

Proposed fusion, still entirely within the Job System's existing,
already-audited boundary rules (AGENTS.md, Phase 4 table: a job body may
only ever be handed a **plain output span resolved by the main thread
first**, never a live `Mesh*`/GPU handle):

- On the main thread, *before* `Dispatch()`, resolve the model's (now
  single, shared) GPU vertex buffer's persistently-mapped pointer once,
  and hand each batch job a plain `MeshVertex*`/`MeshVertexUv*` output
  span into that mapped memory (exactly the same "resolve the handle on
  the main thread, hand job bodies only a disjoint plain span" pattern
  Phase 6 already established for `SkinVertexRange()`'s own
  `outPositions`/`outNormals` — this is a natural, incremental extension
  of it, not a new pattern).
- Each batch job then does skin-blend **and** final-layout packing in one
  pass per vertex, writing the finished `MeshVertex`/`MeshVertexUv`
  directly into that mapped span — no intermediate
  `skinnedPositions`/`skinnedNormals` vectors, no second
  `PackMeshVertices()` pass, no separate `UpdateVertexData()` memcpy
  afterward (the job's own write *is* the upload, since the destination is
  already the mapped GPU buffer).
- This removes two of the three full-array passes and both of the
  per-frame heap allocations for the model's vertex data, and lets the
  "pack" work — which is itself embarrassingly parallel, exactly like the
  blend it's now fused with — actually benefit from the worker pool
  instead of running single-threaded after `WaitForJobs()` returns.
- Constraint to preserve exactly (per Phase 6's own established rule,
  AGENTS.md): still one `Dispatch()` + one `WaitForJobs()` bracket per
  model, still nothing GPU-related touched by a job body except through a
  plain pointer/span resolved main-thread-first, still behavior-identical
  to today's output (a parity test, mirrored on
  `tests/Animation/VertexSkinningParityTests.cpp`'s existing precedent,
  should assert the fused path produces byte-identical `MeshVertex`/
  `MeshVertexUv` output to today's skin-then-pack-then-upload sequence
  before this replaces it).

### Stage 3: stop re-allocating scratch buffers every frame

Independent of Stages 1–2 and safe to land first if convenient: today,
`skinnedPositions`/`skinnedNormals` (and, per-part, the packed
`MeshVertex`/`MeshVertexUv` vectors) are freshly heap-allocated on *every*
`AnimationSystem::Update()` call, every frame, for every animator. Once
Stage 2 removes the packed-vector allocations outright, the remaining
`skinnedPositions`/`skinnedNormals` pair (only needed as an intermediate
at all if Stage 2 isn't done yet, or kept as a fallback for the
below-`kMinVerticesToParallelize` inline path) should be **owned by
`AnimationSystem` per tracked model** (sized once, only ever resized when
a model's own vertex count changes — which never happens after initial
load) instead of being a fresh local `std::vector` every single call. This
is a small, low-risk, purely mechanical change that removes avoidable
allocator churn (and, at 32 allocations × however many frames per second,
non-trivial allocator lock/heap-fragmentation pressure) regardless of how
the two structural stages above land.

### Stage 4: parallelize pose evaluation itself — only if it still shows up after Stages 1–3

Stage A (sample → IK → append → FK) is not parallelized today and *could*
be: `SolveIkChains()` already iterates independent IK chains in a
`for`-loop keyed by `ikBoneIndex`, and different chains never touch each
other's link bones in this model's data (a genuinely shared link bone
across two IK chains would need to be excluded from this parallelization —
worth a defensive check before enabling it), so dispatching one job per
IK chain (or a small batch of chains) is plausible in principle.
**However:** this stage's cost is O(bone count), typically two to three
orders of magnitude smaller than O(vertex count) for a rigged character
model — it is very unlikely to be a measurable contributor once Stages
1–3 remove the O(V×P) duplication. **Recommended action: re-profile after
Stages 1–3 land, with a dedicated `GTE_PROFILE_SCOPE("EvaluateAnimatedSkinningPose")`
around Stage A specifically, before spending effort parallelizing it.**
Do not parallelize speculatively here — this exact "don't over-parallelize
something whose serial cost was never actually shown to matter" caution is
already the Job System's own established design philosophy (see
`ComputeBatchRanges()`'s own `kMinVerticesPerBatch`/`kMinVerticesToParallelize`
floors and their accompanying rationale in AGENTS.md).

### Stage 5 (explicitly deferred, not part of this fix): lifting the sequential-animator constraint

Once every spawned model instance owns its own **private** GPU mesh
buffers (a separate, already-tracked, unstarted piece of engine work — see
`README.md`/`TODO.md`'s own documented limitation, and AGENTS.md's Phase 4
audit table row naming it explicitly), the requirement that
`AnimationSystem::Update()`'s outer loop process one animator's entire
`Dispatch()`+`WaitForJobs()`+upload sequence to completion before the next
begins could be relaxed — e.g. issuing every animator's `Dispatch()` calls
up front and waiting on all of them together, or pipelining one model's
upload against the next model's skinning dispatch. This only matters once
a scene has multiple *simultaneously animated* characters (not the case in
the profiled scene, which has exactly one), and depends on a prerequisite
this document does not propose solving. Listed here only so a future
reader doesn't mistake "the loop is sequential" for *this* profile's
bottleneck, and doesn't attempt to parallelize across animators before the
shared-buffer hazard it exists to prevent is actually closed.

---

## 5. Recommended order of work

1. **Instrument first, confirm the hypothesis, don't guess.** Add
   `GTE_PROFILE_SCOPE`s around Stage A (pose evaluation), the existing
   Stage B dispatch+wait bracket, and Stage C (the per-part
   pack-loop) individually inside `AnimationSystem::Update()`, capture one
   more profiler frame against this exact scene, and confirm Stage C is
   in fact the dominant cost (expected, per §2/§3's analysis, but this
   should be verified with real numbers before committing to the Stage 1
   rework — cheap, low-risk, and it directly tells you how much headroom
   each subsequent stage is worth chasing).
2. **Stage 1 — shared vertex buffer / per-part index ranges.** This is the
   architectural fix that removes the O(V×P) duplication outright; expect
   this alone to recover the large majority of the 52 ms, independent of
   any further threading work.
3. **Stage 2 — fuse skin+pack into one parallel pass writing straight into
   the mapped GPU buffer.** Removes the remaining redundant CPU passes and
   heap churn, and finally puts the *entire* per-vertex cost (not just the
   blend math) under the Job System's parallelization.
4. **Stage 3 — reuse scratch buffers across frames.** Small, mechanical,
   safe to land alongside either of the above.
5. **Re-profile.** Confirm Stage A (pose evaluation) is or isn't worth
   parallelizing (Stage 4) with real data, rather than assuming.
6. **Stage 5 is explicitly out of scope** for this pass — tracked
   separately, dependent on per-instance GPU buffers.

Every stage above must preserve the existing, hard-won correctness
guarantees this codebase already documents and tests for: identical
skinning output to today's serial path (parity-tested, per
`VertexSkinningParityTests.cpp`'s precedent), the "resolve GPU
handles/pointers on the main thread first, hand job bodies only a plain
span" boundary rule (AGENTS.md Phase 4 audit), and one `Dispatch()` +
exactly one `WaitForJobs()` bracket per model per frame.

---

## 6. Looking ahead: GPU compute skinning (brief, deliberately not the focus of this pass)

Once the CPU-side pipeline above is fixed — specifically, once Stage 1
removes the O(V×P) duplication — the natural next step for further
headroom is moving the per-vertex blend itself (`SkinVertexRange()`'s
math) onto a compute shader: bind the model's bind-pose
position/normal/skin-weight buffers plus a small per-frame skinning-matrix
uniform/storage buffer, dispatch one compute invocation per vertex, and
write the result directly into the vertex buffer used for rendering —
eliminating the CPU cost of Stage B (and, if fused the GPU way instead of
the CPU way, Stage C's packing too) entirely, at the cost of a GPU-GPU
synchronization point (compute-write → vertex-read barrier) instead of a
CPU `WaitForJobs()`.

**This is explicitly a follow-up, not part of this proposal**, for two
reasons directly tied to the analysis above:

- It would be a mistake to port today's O(V×P) duplicated-vertex-buffer
  design onto the GPU as-is — the exact same 32× redundant work would
  simply move from CPU cycles to GPU compute cycles/memory bandwidth
  instead of being removed. Stage 1's shared-vertex-buffer fix is a
  prerequisite for a *good* GPU-compute-skinning design, not an
  alternative to it.
- Per AGENTS.md's own Job System Phase 4 thread-safety audit, GPU
  resources are strictly main-thread-only in this engine's current
  architecture (`Renderer`/`Vulkan/*` — NEVER for a job body); a
  compute-skinning path is a `Renderer`/render-graph-level feature
  (likely its own render-graph pass, alongside the existing box-blur
  compute pass precedent — see `AGENTS.md`'s Render Graph/compute-shader
  references), not a `Jobs::Dispatch()` consumer at all. It deserves its
  own dedicated design pass once the CPU-side data layout it will consume
  (one shared vertex buffer per model) actually exists.

---

## 7. Summary

The Job System was applied correctly to CPU vertex skinning — but only to
the one stage (the per-vertex bone-blend math) that was never the actual
bottleneck for a multi-material model. The real cost is a structural,
O(vertexCount × partCount) redundancy: every material part re-packs and
re-uploads a full copy of the *entire* model's vertex data, unconditionally,
on the main thread, completely outside the parallelized region. Fixing
that duplication (one shared vertex buffer per model, indexed differently
per part) is the single highest-leverage change available, and should be
done *before* any further multithreading work — parallelizing redundant
work still leaves the redundancy. Once that's fixed, fusing skin+pack into
one job writing directly into the shared, mapped GPU buffer removes the
remaining CPU overhead and heap churn, and finally puts the model's entire
per-vertex cost — not just the blend math — under real parallelism. Only
after that should pose-evaluation parallelism or GPU compute skinning be
considered, and only if re-profiling still shows them as worthwhile.
