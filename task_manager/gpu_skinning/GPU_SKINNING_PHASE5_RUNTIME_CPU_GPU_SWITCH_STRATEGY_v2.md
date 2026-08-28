# GPU Vertex Skinning — Phase 5: Runtime CPU/GPU Switch — v2

Part of the GPU Vertex Skinning campaign — see
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md` for the campaign map. Supersedes
`GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v1.md`. Depends on
Phases 1-4 all being complete, **and on Phase 3 v2's Step 3.6 WAW-hazard
item being resolved** (new hard dependency added by this revision — see
below).

## V2 Revision Notes (read this first)

- **V2 Revision Note 1 — the "GPU mode is structurally safer" argument in
  v1 was incomplete, and is corrected here.** v1's Step 2 argued that GPU
  mode is actually SAFER than the CPU path's own "strictly sequential" rule
  because "everything the render graph records goes into one linear command
  buffer... Vulkan's own execution model guarantees commands within a
  single queue submission execute... in program order, GIVEN THE BARRIERS
  PHASE 3 ALREADY INSERTS" (emphasis added to the load-bearing assumption).
  This v2 audit's re-reading of the real `RenderGraph.cpp` found good
  reason to doubt "the barriers Phase 3 already inserts" are actually
  guaranteed for the specific write-after-write case two same-model
  instances create (see `GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md`'s
  new Step 3.6 for the full analysis). This document's Step 2 below
  restates the safety argument CORRECTLY — conditioned on Phase 3 v2's fix,
  not assumed for free.
- **V2 Revision Note 2 — the illustrative "existing CPU code, unchanged"
  sketch is now grounded in the REAL, current `AnimationSystem.cpp`**,
  which (thanks to Job System Phase 6 already having landed) is
  considerably more sophisticated than v1's simplified sketch implied:
  real `kMinVerticesToParallelize`/`kMinVerticesPerBatch` batching via
  `Jobs::Dispatch()`/`WaitForJobs()`, real per-model `AnimatorScratchBuffers`
  reuse, and a real, inline `PendingGroup` de-duplication-by-shared-vertex-
  buffer loop — all of which the GPU branch must sit ALONGSIDE, not
  reproduce or interfere with.
- **V2 Revision Note 3 — the "STRICTLY SEQUENTIAL" rule is now cited from
  its real, load-bearing home**: an actual, already-shipped, prominent code
  comment in `AnimationSystem.cpp` itself (not merely descriptive
  `README.md` prose) — this is the artifact a future maintainer will
  actually see and must not violate, so it's the one this document should
  point at directly.

## Step 1: The Goal (Where are we going?)

Unchanged from v1: `AnimationSystem::Update()` gains a genuine, per-session,
runtime-toggleable branch — CPU mode (today's exact, unmodified behavior)
or GPU mode (upload this frame's bone matrices, let the render graph's
compute pass do the rest) — switchable at any point, including mid-session,
with zero crashes and zero visual corruption.

## Step 2: The Situation / The Problem (Where are we now?) — corrected

`AnimationSystem::Update()`'s real, current structure (quoted in full in
this campaign's shared context, `AnimationSystem.cpp`) is considerably
richer than a first pass at v1 implied, and every design decision below
must sit correctly alongside ALL of it, not a simplified mental model of
it:

- **The real "STRICTLY SEQUENTIAL" rule** is not just documented in
  `README.md`/`AGENTS.md` — it is a real, prominent, already-shipped
  comment directly inside `AnimationSystem::Update()`'s own source, reading
  in part: *"THIS OUTER LOOP MUST REMAIN STRICTLY SEQUENTIAL, ONE ANIMATOR
  AT A TIME - NEVER 'HELPFULLY' RESTRUCTURED TO FIRE OFF EVERY ANIMATOR'S
  OWN Dispatch() CALL UP FRONT AND WAIT ON ALL OF THEM TOGETHER."* This
  rule was written for, and is currently enforced for, the CPU path's own
  `Jobs::Dispatch()`/`WaitForJobs()` calls. **This document's job is to
  make sure GPU mode's own per-model work (bone-matrix upload plus, later,
  the render-graph-recorded compute dispatch) never violates the SPIRIT of
  this rule either** — even though, as explained below, GPU mode's actual
  risk profile is different in an important way from the CPU path's.
- **Why GPU mode's risk profile is different — and why v1's confidence was
  premature.** Under CPU mode, "touching shared memory" means a worker
  thread's `Jobs::Dispatch()` batch writing into a `std::vector`/host-mapped
  buffer — real, unsynchronized shared mutable state, which is exactly why
  the strictly-sequential rule exists at all. Under GPU mode, the CPU side
  of the equivalent work is just one `Buffer::Upload()` call per animator
  (cheap, main-thread-only, already provably safe) — the ACTUAL shared
  resource is the GPU-side output buffer, and whether writes to it are
  safely ordered is now entirely `gte::rg::RenderGraph`'s
  responsibility, not `AnimationSystem`'s. v1 asserted this was
  automatically fine "because Vulkan guarantees program order, given the
  barriers Phase 3 already inserts" — but per
  `GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md`'s new
  Step 3.6, whether those barriers are actually emitted for a
  same-access-kind write-after-write pair needs verification, and a
  concrete mitigation is provided there regardless. **This document's own
  "GPU mode is structurally safer" claim is therefore now CONDITIONAL, not
  free**: it is only true once Phase 3 v2's Step 3.6 fix has actually
  landed (or its Step 1 verification has confirmed the existing barrier
  planner already handles this correctly).
- **The mode must be read once, consistently, for one model's entire
  per-frame processing** — unchanged from v1, still correct, still
  required: snapshot `m_mode` into a local at the very top of `Update()`,
  never re-read the global flag mid-loop.
- **The CPU branch, when the doc says "copied verbatim" / "not a single
  line touched", now means: the REAL, current body** — the frame-advance
  logic, the `EvaluateAnimatedSkinningPose()` call, the
  `kMinVerticesToParallelize` branch between inline `SkinVertexRange()` and
  `Jobs::Dispatch(&RunSkinningBatch, ...)` + `WaitForJobs()`, the
  `AnimatorScratchBuffers` lookup/reuse, the `PendingGroup`
  shared-vertex-buffer de-duplication loop, and the per-group packing
  dispatch (also job-batched above `kMinVerticesToParallelize`) followed by
  `Mesh::UpdateVertexData()`. GPU mode's branch sits as a genuine
  **alternative tail** to this whole sequence, starting right after
  `EvaluateAnimatedSkinningPose()` returns `skinningMatrices`, not as a
  simplified stand-in for it. Implementers should open the real
  `AnimationSystem.cpp` side-by-side with this document while writing the
  branch, not work from memory of the file's shape.

## Step 3: The Plan (How will we get there?)

### 3.1 — The mode enum and where it lives

Unchanged from v1 — `SkinningMode` (`CpuJobSystem`/`GpuCompute`), owned by
`AnimationSystem` as a plain member, default `CpuJobSystem`.

### 3.2 — `Update()`'s new branch — corrected against the real function shape

```cpp
void AnimationSystem::Update(Registry& registry, double deltaSeconds)
{
    GTE_PROFILE_SCOPE("AnimationSystem::Update");
    const SkinningMode mode = m_mode; // snapshotted ONCE - unchanged from v1

    ComponentStorage<SkeletalAnimator>& animators = registry.Storage<SkeletalAnimator>();

    // *** THIS OUTER LOOP MUST REMAIN STRICTLY SEQUENTIAL *** - see the
    // REAL comment already in this function (AnimationSystem.cpp) for the
    // full text this document is not repeating in full here. GPU mode's own
    // per-model work below must respect the exact same discipline: no
    // animator's own GPU-mode work may be "fired off" ahead of the
    // previous animator's own work completing, for the exact same
    // "two instances may share one Mesh" reason the CPU path already
    // documents - see this document's own Step 2 above for how GPU mode's
    // risk profile differs (and what still needs to be true regardless).
    for (std::size_t i = 0; i < animators.Size(); ++i) {
        SkeletalAnimator& animator = animators.ComponentAt(i);
        if (!animator.playing || animator.animationGtaPath.empty()) {
            continue;
        }

        const SkinnedMeshData* skinData = m_rigCache.TryGet(animator.meshGtaPath);
        if (skinData == nullptr) {
            continue;
        }
        const MotionData* motion = m_clipCache.TryGet(animator.animationGtaPath);
        if (motion == nullptr) {
            continue;
        }

        // ... existing frame-advance / binding-resolution /
        // EvaluateAnimatedSkinningPose() code, COMPLETELY UNCHANGED - see
        // the real AnimationSystem.cpp for its exact current shape,
        // including binding-cache lookups and loop/frame-clamping math ...
        const std::vector<Mat4> skinningMatrices =
            EvaluateAnimatedSkinningPose(skinData->skeleton, binding, animator.frame);

        if (mode == SkinningMode::CpuJobSystem) {
            // ... existing CPU skinning branch, COMPLETELY UNCHANGED: the
            // kMinVerticesToParallelize threshold check, inline
            // SkinVertexRange() OR Jobs::Dispatch(&RunSkinningBatch, ...) +
            // WaitForJobs(), the AnimatorScratchBuffers lookup/reuse, the
            // PendingGroup shared-vertex-buffer de-duplication loop, and
            // each group's own packing (also job-batched above threshold)
            // + Mesh::UpdateVertexData() call - NOT a single line of this
            // is touched by this phase. See the real AnimationSystem.cpp
            // for its current, full shape - this document deliberately
            // does not re-derive it, to avoid two independently-maintained
            // copies of the same logic drifting apart.
            continue;
        }

        // GPU mode:
        const GpuSkinningRigCache::GpuModelEntry* gpuEntry = m_gpuRigCache.TryGet(animator.meshGtaPath);
        if (gpuEntry == nullptr) {
            continue; // Never registered for GPU skinning (see Phase 4).
        }
        gpuEntry->boneMatricesBuffer.Upload(skinningMatrices.data(), skinningMatrices.size() * sizeof(Mat4));
        // Entire GPU-mode per-frame CPU cost for this model: one Upload()
        // call, main-thread-only, no Jobs::Dispatch() involved at all (see
        // this document's own "What We Will NOT Do" below). The actual
        // compute dispatch is recorded later, from RenderPasses.cpp's own
        // build lambda, per Phase 3 - see Step 3.3 below.
    }
}
```

The structural point unchanged from v1: the CPU branch's own body is never
touched by this phase, at all — this revision only corrects the DOCUMENT's
own illustration of what "unchanged" actually contains, so a future
implementer isn't surprised mid-implementation by how much of
`AnimationSystem::Update()` already exists.

### 3.3 — Who actually issues the `vkCmdDispatch`?

Unchanged from v1 — `AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame()`,
called from `RenderPasses.cpp` after `Update()` has already run and before
the render graph's `build` lambda executes.

### 3.4 — Switching modes mid-session is safe by construction, if two rules hold — corrected

Rules 1-3 from v1 remain accurate and are carried over unchanged (snapshot-once
timing; GPU dispatch always ordered after registration and before the
consuming draw, per Phase 3's render-graph ordering; switching OUT of GPU
mode needs no cleanup since the two `Mesh` objects are fully independent —
see Step 3.5 below). **A fourth rule is added by this revision:**

4. **(NEW) The render-graph-level write-ordering guarantee this whole
   design depends on (rule 2 above, and the "GPU mode is safer" framing in
   Step 2) is only actually true once
   `GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md`'s Step
   3.6 has been resolved** (verified-safe, or fixed via the read-before-write
   dependency-edge mitigation). Do not enable the runtime switch for real
   users, even behind an Editor-only toggle, until that item's own
   completion-report entry (Phase 3 v2, Step 3.6) is filled in. This is a
   **hard gate**, not a suggestion — flipping the switch live, with two
   entities sharing a rigged model on screen, is precisely the scenario
   that would silently exercise an unresolved WAW hazard.

### 3.5 — Two separate `Mesh` objects per model, not one shared, mode-swapped `Mesh`

Unchanged from v1 — see that document's Step 3.5 for the full reasoning
(doubled GPU memory is an accepted, deliberate trade for keeping
`MeshRenderer`/`RenderSystem` completely unaware skinning mode exists at
all).

## Step 4: What We Will NOT Do (Focus)

Unchanged from v1 (no per-model mode override, no attempt to halve the
doubled GPU memory footprint in this phase, no mode switch mid-frame, no
new job-system interaction — GPU mode issues zero `Jobs::Dispatch()`
calls, its whole per-frame CPU cost is one `Buffer::Upload()` per animated
model). One clarification added:

- **No attempt to make GPU mode's own per-model work "helpfully"
  parallelized across a job-system dispatch, even though it would be
  technically possible** (uploading N models' bone matrices from N worker
  threads). Every GPU-mode animator's own `Buffer::Upload()` call stays on
  the main thread, inside the same strictly-sequential loop as the CPU
  path, for the exact same reason the CPU path's own loop must stay
  sequential (Step 2 above) — this is cheap enough (a handful of small
  `memcpy`s per frame) that there is no performance case for
  parallelizing it, and doing so would reopen exactly the kind of
  cross-thread-shared-resource reasoning this phase's own Step 3.4/Rule 4
  works hard to keep closed.

## Step 5: Their Role (What does this mean for you?)

Unchanged from v1's staged rollout (land the enum/accessors as a no-op
first; land the `Update()` branch + collector next, verified with a
temporarily-hardcoded mode switch; land the `MeshHandle` re-pointing logic
last, manually tested through a live CPU->GPU->CPU transition), **with one
addition**: before landing the `MeshHandle` re-pointing logic (the step
that makes the switch actually live/user-flippable), confirm — and record
in your own completion report — that Phase 3 v2's Step 3.6 gate (this
document's new Rule 4) has actually been closed. Do not consider this
phase done, and do not hand off to Phase 6/7, while that gate is still
open.
