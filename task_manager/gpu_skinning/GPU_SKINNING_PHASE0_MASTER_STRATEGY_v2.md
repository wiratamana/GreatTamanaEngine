# GPU Vertex Skinning — Master Strategy (Phase 0) — v2

**Orchestrator document — v2.** This supersedes
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v1.md`. It does not implement anything
itself — it defines the goal, the must-have feature set, the phase
breakdown, and the rules every child document must follow.

## Why v2 exists — audit summary

This revision was produced by re-reading every v1 child document
line-by-line against the ACTUAL current source of this engine (not
memory/assumption) — specifically: `Buffer.h/.cpp`, `ComputePipeline.h/.cpp`,
`ComputeDescriptorSet.h/.cpp`, `DescriptorSetLayoutBuilder.h/.cpp`,
`ComputeDispatch.h`, `FormatCapabilities.h/.cpp`, `ShaderModule.h/.cpp`,
`RenderGraph.h/.cpp`, `Jobs/JobSystem.h/.cpp`,
`Game/Animation/AnimationSystem.h/.cpp`, `Game.h/.cpp`, `CMakeLists.txt`,
`AGENTS.md`, `README.md`, `TESTING.md`, `TODO.md`, `BoxBlur.comp` — plus the
actual `task_manager/` folder tree (confirming every cross-campaign
reference the v1 docs make — `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`,
`JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md`,
`GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`,
`MULTITHREAD_CPU_SKINNING_OPTIMIZATION_STRATEGY_v1.md` — genuinely exists at
the exact path claimed).

**The good news first:** the overwhelming majority of v1's technical
assumptions check out exactly against real code. Confirmed accurate,
word-for-word:

- `Buffer`'s constructor already takes a raw `VkBufferUsageFlags` with no
  restriction on which bits may be combined — Phase 1's "combined
  `STORAGE_BUFFER | VERTEX_BUFFER` usage needs no new capability check"
  claim is correct; `Renderer::CreateBuffer()` (a thin wrapper around this
  same constructor) already supports exactly what Phase 4 needs with no
  new Vulkan-level plumbing.
- `ComputePipeline`'s real constructor signature
  (`device, shaderSpirvPath, descriptorSetLayouts, pushConstantRange`) and
  `DescriptorSetLayoutBuilder::AddStorageBuffer(binding)` match Phase 2's
  code sketches exactly.
- `ComputeDispatch.h`'s real `ComputeGroupCount(totalItems, localGroupSize)`
  signature matches Phase 2's dispatch-math usage exactly.
- `RenderGraph.cpp`'s `EnsureBufferResolved()` genuinely has NO import
  branch today (confirmed by its own comment: "Phase 2 has no
  ImportBuffer() counterpart to ImportTexture()") — Phase 3's central
  premise (this gap is real and must be closed) is correct.
- `PassContext::resolveBuffer` **already exists**, already wired up exactly
  as Phase 3 assumed — confirmed directly in `RenderGraph.cpp`.
- `Mesh::VertexBufferIdentity()`, `MeshAssetPart`, and
  `MeshInstantiationSystem::TryGetMeshAssetParts()` are used in
  `AnimationSystem.cpp` exactly the way Phase 4 assumed — the "PendingGroup"
  de-duplication-by-shared-vertex-buffer logic Phase 4 plans to extract
  into a shared free function genuinely exists today, genuinely inline,
  genuinely not yet extracted.
- The Editor's "Jobs" panel (`Panels/JobsPanel.h/.cpp`,
  `JobsPanelData.h/.cpp`) genuinely already exists and is unconditionally
  compiled into the Editor build — Phase 7's plan to put the CPU/GPU
  skinning-mode toggle there is building on real, already-shipped ground.
- `AnimationSystem.cpp` already contains the EXACT "THIS OUTER LOOP MUST
  REMAIN STRICTLY SEQUENTIAL" rule Phase 5 needs, as a real, prominent,
  already-shipped code comment (Job System Phase 6 is done) — not merely a
  README aspiration.

**Two genuine, substantive problems were found, and are fixed in this
revision:**

1. **(V2 Revision Note 1 — the important one.)** Phase 3's and Phase 5's
   claim that "the render graph's barrier planner will correctly serialize
   two compute passes writing the same shared buffer, exactly like it does
   for any other resource" is **not actually verified against the real
   barrier-planning logic**, and a direct reading of `RenderGraph.cpp`'s
   `ApplyUsageBarrierIfNeeded()` shows a concrete, plausible way it could be
   **false**: `RequiredStateFor(access, ...)` is a pure function of the
   `ResourceAccess` enum value alone — it does not know or care whether a
   different PASS wrote the same access kind a moment ago. If
   `RequiresBarrier(oldState, newState)` is implemented as "no barrier
   needed when the state doesn't change" (a very ordinary, very plausible
   way to implement a transition-barrier planner), then TWO consecutive
   `ComputeShaderWrite` accesses to the SAME buffer, from TWO DIFFERENT
   passes, would see `oldState == newState` and get **zero barrier at all**
   between them. That is a genuine, GPU-level write-after-write (WAW)
   data hazard — worse than today's CPU "last write wins" (which is at
   least fully sequential and well-defined) — and is exactly the scenario
   this campaign's own accepted "two entities spawned from the same
   `*.gta` file share one Mesh" limitation creates: TWO SkeletalAnimators,
   TWO compute dispatches, ONE shared output buffer, in the SAME frame.
   See `GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md`'s
   new Step 3.6 for the full analysis and the required mitigation. This
   MUST be verified against the real `RenderGraphBarrierPlanner.cpp`
   source (not shown to this review) before Phase 3 is considered
   implementable as originally written, and a concrete fix is proposed
   there regardless of what that verification finds.
2. **(V2 Revision Note 2.)** Phase 6's plan to land a real, automated,
   `GTEST_SKIP()`-guarded GoogleTest requiring a live `VkDevice` ignores two
   facts this review confirmed directly from `TESTING.md`/`TODO.md`: **(a)**
   no Tier-2, GPU-device-requiring automated test of ANY kind exists in
   this codebase yet — `TESTING.md` states this outright, and `TODO.md`'s
   own "Tier 2 (GPU-backed) integration test fixture" backlog item
   describes it as explicitly NOT started; **(b)** `VulkanDevice::
   PickPhysicalDevice()` today **requires a real `VkSurfaceKHR`** (to query
   present support), which normally only comes from a real OS window —
   `TODO.md` explicitly flags a headless `VK_EXT_headless_surface` fixture
   as the (unstarted) prerequisite for exactly this reason, and further
   notes "the current development machine doesn't even support headless
   mode". Phase 6 as originally written would have quietly assumed a
   test-infrastructure capability that plain does not exist yet, producing
   either a test that can never actually run anywhere in practice (if
   naively `GTEST_SKIP()`-guarded) or a build failure (if not). See
   `GPU_SKINNING_PHASE6_VALIDATION_PARITY_TESTING_STRATEGY_v2.md`'s revised
   plan, which makes the manual, Editor-tool-based validation (already this
   engine's proven pattern for `ComputeBlurValidation`) the PRIMARY,
   REQUIRED deliverable, and demotes the automated GoogleTest to an
   explicitly-optional stretch goal gated behind the separate,
   already-tracked "Tier 2 GPU test fixture" TODO item landing first.

Two smaller, non-blocking precision improvements are also folded in (see
each affected document's own "V2 Revision Note" callouts):

3. **(V2 Revision Note 3.)** Phase 5's illustrative "existing CPU code,
   unchanged" sketch under-represented how much CPU-path machinery already
   exists (Job System Phase 6's `kMinVerticesToParallelize`/
   `kMinVerticesPerBatch` batching, `AnimatorScratchBuffers`, the inline
   `PendingGroup` de-duplication) — corrected to reference the real,
   current shape of `AnimationSystem::Update()` directly, by name, so a
   future implementer isn't surprised by how much bigger that function
   already is than the v1 sketch implied.
4. **(V2 Revision Note 4.)** Every phase's citations of "the strictly
   sequential outer loop rule" now point at the actual, already-shipped
   `AnimationSystem.cpp` code comment (Job System Phase 6) rather than only
   the older `README.md` prose describing the same rule — the code comment
   is the load-bearing, enforced artifact; the README is descriptive only.

Everything else in v1 — the phase breakdown, the must-have feature list's
other nine items, the "What We Will NOT Do" refusals, buffer layouts
(Phase 1), the compute kernel plan (Phase 2), the per-model resource
management plan (Phase 4), and the Editor UX plan (Phase 7) — was
cross-checked and found to be accurate and sufficient as originally
written. **Those four phase documents remain at v1 and are NOT reissued
here** — re-issuing a document that is already correct would just create
two copies to keep in sync for no benefit. Only Phase 0 (this file), Phase
3, Phase 5, and Phase 6 are bumped to v2.

## Document Map (v2)

| # | File | Version | One-line summary |
|---|---|---|---|
| 0 | `GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md` | **v2** | This file — goal, must-haves, phase table, campaign-wide rules, audit findings. |
| 1 | `GPU_SKINNING_PHASE1_DATA_BUFFER_FOUNDATIONS_STRATEGY_v1.md` | v1 (confirmed accurate, unchanged) | GPU-side buffer layouts for bind pose, skin weights, bone matrices, skinned output. |
| 2 | `GPU_SKINNING_PHASE2_COMPUTE_KERNEL_STRATEGY_v1.md` | v1 (confirmed accurate, unchanged) | The `.comp` shaders that blend bones on the GPU, mirroring `VertexSkinning.cpp` exactly. |
| 3 | `GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md` | **v2** | Wiring the skinning dispatch into `gte::rg::RenderGraph` — now with the WAW-hazard finding and its required mitigation. |
| 4 | `GPU_SKINNING_PHASE4_PER_MODEL_RESOURCE_MANAGEMENT_STRATEGY_v1.md` | v1 (confirmed accurate, unchanged) | The GPU-side sibling of `SkeletalRigCache`/`AnimationSystem` — per-model buffer/descriptor lifecycle. |
| 5 | `GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md` | **v2** | The actual on/off switch — now grounded in the real, already-larger `AnimationSystem::Update()`, and correctly conditioned on Phase 3's WAW-hazard fix. |
| 6 | `GPU_SKINNING_PHASE6_VALIDATION_PARITY_TESTING_STRATEGY_v2.md` | **v2** | Proving GPU output is numerically identical to CPU output — now realistic about this repo's actual (currently nonexistent) Tier-2 GPU test infrastructure. |
| 7 | `GPU_SKINNING_PHASE7_EDITOR_PROFILING_UX_STRATEGY_v1.md` | v1 (confirmed accurate, unchanged) | The Editor-facing toggle and profiling UX for the CPU-vs-GPU comparison. |

---

## Step 1: The Goal (Where are we going?)

Unchanged from v1. We are giving the engine a **second, GPU-resident
implementation of vertex skinning** — a compute shader performing the exact
same per-vertex bone blend `Animation/VertexSkinning.cpp`'s
`SkinVertexRange()` already performs on the CPU today, entirely on the GPU,
writing directly into the vertex buffer the very next graphics pass in the
same frame draws from — switchable at runtime against the existing,
already-shipped CPU/Job-System path, specifically so the performance
difference between the two tech stacks can be observed and measured, on
weak hardware as well as capable hardware.

## Step 2: The Situation / The Problem (Where are we now?)

Unchanged in substance from v1 — restated here briefly, now with the audit
confirmations folded in (see "Why v2 exists" above for the full detail):

- A working, job-system-parallelized CPU skinning path already exists and
  is the oracle (`AnimationSystem::Update()`, `VertexSkinning.cpp`) —
  CONFIRMED, and confirmed to already be considerably more sophisticated
  (batched job dispatch, per-model scratch buffers, shared-vertex-buffer
  de-duplication) than a first read of the README's prose alone might
  suggest.
- A first-class render graph with compute as a first-class citizen already
  exists (`gte::rg::RenderGraph`, `ComputeBlurValidation` as its proven
  texture-side end-to-end example) — CONFIRMED.
- A working compute pipeline stack (`ComputePipeline`,
  `ComputeDescriptorSet`, `DescriptorSetLayoutBuilder`, `ComputeDispatch`)
  already exists and matches every signature the v1 phase documents
  assumed — CONFIRMED.
- No GPU buffer layout, no compute kernel, no `ImportBuffer()`/
  `VertexBufferRead`, no GPU-only combined-usage vertex-buffer factory, no
  runtime CPU/GPU switch, and no parity/correctness proof exist yet —
  CONFIRMED still all true, still the real, novel engineering surface of
  this campaign.
- **NEWLY CONFIRMED as part of this v2 audit:** the render graph's
  barrier-planning logic (`RenderGraph.cpp`'s `ApplyUsageBarrierIfNeeded()`)
  computes a write's required `ResourceState` as a pure function of its
  `ResourceAccess` enum value alone, with no visibility into "did a
  DIFFERENT pass already write this same state a moment ago" — meaning a
  same-access-kind write-after-write hazard between two passes is not
  obviously handled by the existing machinery, and must be explicitly
  verified/fixed rather than assumed away. This is exactly the situation
  two SkeletalAnimators sharing one Mesh (an already-accepted, documented
  limitation) would create under GPU mode.
- **NEWLY CONFIRMED as part of this v2 audit:** this repository has NO
  Tier-2 (live-`VkDevice`-requiring) automated test infrastructure of any
  kind today, and building one is nontrivial given `VulkanDevice`'s current
  hard dependency on a real `VkSurfaceKHR` for physical-device selection —
  this is a real, pre-existing gap in the engine's OWN test infrastructure,
  not something this campaign can casually route around by "just adding a
  `GTEST_SKIP()`".

## Step 3: The Plan (How will we get there?)

Unchanged phase table from v1:

```
Phase 1  Data & Buffer Layout Foundations
            v
Phase 2  Compute Kernel(s)
            v
Phase 3  RenderGraph Synchronization        <- v2: WAW-hazard fix required here
            v
Phase 4  Per-Model Resource Management
            v
Phase 5  Runtime CPU/GPU Switch              <- v2: depends on Phase 3's fix landing first
            v
Phase 6  Validation & Parity Testing         <- v2: realistic Tier-2 test-infra plan
            v
Phase 7  Editor Toggle & Profiling UX
```

**A new hard dependency edge, added by this revision:** Phase 5 (the
runtime switch actually going live) must not be considered safe to ship
until Phase 3's WAW-hazard finding has been EITHER verified to be a
non-issue against the real `RenderGraphBarrierPlanner.cpp` source, OR
fixed per Phase 3 v2's Step 3.6 mitigation. Do not skip straight from "the
compute kernel dispatches correctly for one model" to "the runtime switch
is safe for a multi-instance scene" without this check — a single-instance
smoke test would never exercise the shared-buffer WAW scenario at all and
could pass cleanly while masking a real, only-sometimes-reproducible GPU
race.

### Must-Have Features (campaign-wide Definition of Done) — v2

All ten of v1's must-have items remain, verbatim, unless noted. One item
is added:

1-10. (Unchanged from v1 — bit-for-bit-equivalent math; pose evaluation
stays CPU-only; zero CPU readback in the render path; fully automatic
render-graph-synthesized synchronization; a genuine mid-session runtime
switch; existing profiling surfaces show the difference automatically;
correctness proven via a real numeric comparison; multi-part/shared-buffer
models keep working; graceful loud failure on unsupported hardware; the
Editor exposes the switch and the comparison.)

11. **(NEW, V2.) GPU-side write ordering across two passes sharing one
    output resource must be PROVEN correct, not assumed.** Before Phase 5
    ships the runtime switch, whoever implements Phase 3 must have either
    (a) read the real `RenderGraphBarrierPlanner.cpp`/`RenderGraphTypes.cpp`
    source and confirmed a same-access-kind write-after-write hazard
    between two passes IS already correctly barriered, with a short written
    note explaining exactly which code path guarantees it, or (b) applied
    Phase 3 v2's Step 3.6 mitigation (forcing a real barrier between the
    two writes) and demonstrated — with RenderDoc/Vulkan validation layers,
    or a targeted manual multi-instance test — that two SkeletalAnimators
    sharing one Mesh under GPU mode no longer produce validation-layer WAW
    warnings/errors. "It rendered without crashing" is explicitly NOT
    sufficient evidence for this item — a WAW race is exactly the kind of
    bug that renders "fine" nine times out of ten and corrupts data on the
    tenth, or only on a different GPU/driver.

## Step 4: What We Will NOT Do (Focus)

Unchanged from v1 — see that document's own list (no GPU-side pose
evaluation, no morph/physics, no automatic mode selection, no per-instance
private GPU mesh buffers as part of this campaign, no tie-in with the
separate GPU-driven-rendering campaign, no shader permutation/bindless/
reflection, no change to the CPU skinning math, no LOD/bone-count
reduction). This v2 revision does not relax or add to any of these
refusals.

## Step 5: Their Role (What does this mean for you?)

Same core discipline as v1 (read phases in order, each phase independently
shippable/testable, write a completion report per phase, update
`AGENTS.md`/`README.md`/`TODO.md` only at the end, never let the CPU path
regress) — with two additions specific to this revision:

- **Before starting Phase 3's implementation, read this file's "Why v2
  exists" section and Phase 3 v2's Step 3.6 in full.** The WAW-hazard
  finding is the single most important correction this revision makes —
  treat verifying it as a hard prerequisite, not an optional nice-to-have,
  before writing a single line of Phase 5 code.
- **Before starting Phase 6's implementation, read Phase 6 v2's revised
  plan in full**, and do not attempt to wire an automated,
  live-`VkDevice`-requiring GoogleTest into `tests/CMakeLists.txt` without
  first confirming the separate "Tier 2 GPU test fixture" TODO item has
  actually landed — building that fixture is explicitly NOT part of this
  campaign's scope, and Phase 6 v2 explains exactly what to build instead
  in the meantime.
