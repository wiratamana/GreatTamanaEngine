# GPU Vertex Skinning — Phase 6: Validation & Parity Testing — v2

Part of the GPU Vertex Skinning campaign — see
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md` for the campaign map. Supersedes
`GPU_SKINNING_PHASE6_VALIDATION_PARITY_TESTING_STRATEGY_v1.md`. Depends on
Phases 1-5 all being functionally complete (including Phase 3 v2's Step 3.6
gate and Phase 5 v2's Rule 4).

## V2 Revision Notes (read this first)

v1's Step 3.4 recommended, as the PREFERRED outcome, wiring the CPU-vs-GPU
epsilon comparison into a real, `GTEST_SKIP()`-guarded GoogleTest requiring
a live `VkDevice`, "if at all achievable within this phase's time budget."
This revision corrects that recommendation using two facts confirmed
directly from this repository's own `TESTING.md` and `TODO.md` (attached in
full to this review), which v1 did not actually check against:

- **`TESTING.md` states outright**, in its own closing paragraph: *"Testing
  `Buffer`/`RenderTexture`/`Pipeline`/`GpuResourceFactory` properly needs a
  real `VkDevice`+`VmaAllocator`... a 'Tier 2' of GPU-backed integration
  tests... is a documented follow-up... rather than implemented yet."*
  There is, right now, **zero** precedent anywhere in this codebase for a
  GoogleTest that spins up a real Vulkan device. v1's Phase 6 plan asked an
  implementer to be "the first one" and merely "say so explicitly" in a
  completion report if that turned out to be true — this revision goes
  further: it is ALREADY confirmed true, right now, without needing Phase 6
  itself to discover it the hard way.
- **`TODO.md`'s own "Tier 2 (GPU-backed) integration test fixture" backlog
  item explains WHY this doesn't exist yet, and it is a real, nontrivial
  obstacle, not a formality**: *"today's `VulkanDevice::PickPhysicalDevice()`
  additionally requires a real `VkSurfaceKHR` (to query present support),
  which normally comes from an actual OS window... A future `GpuTestFixture`
  could build a headless `VkSurfaceKHR` via `VK_EXT_headless_surface`...
  Needs its own fixture/CMake wiring... Explicitly NOT a blocker for any
  other work; the current development machine doesn't even support headless
  mode."* This means a GPU Vertex Skinning GoogleTest that tries to
  construct a `VulkanDevice` the normal way, inside `tests/`, would need a
  real OS window/surface to even get as far as picking a physical device —
  something the test binary (`GreatTamanaEngineTests`) has never needed and
  is not currently set up to create. This is a real, separate, currently-
  unstarted piece of infrastructure work, not something Phase 6 can casually
  bolt on as a side effect of validating vertex skinning.

**The corrected plan**: make the manual, Editor-tool-based validation (the
exact same, already-proven pattern `ComputeBlurValidation` already
established for the texture-side compute-shader campaign) the PRIMARY and
REQUIRED deliverable of this phase. The automated GoogleTest is downgraded
from "preferred, attempt if time allows" to an **explicitly optional
stretch goal, hard-gated behind the separate "Tier 2 GPU test fixture" TODO
item landing first** — which is not this campaign's job to build. This is
not a lowering of QUALITY BAR for Phase 6 — the manual tool still must
produce the exact same rigorous, quantitative "max delta / mean delta"
evidence v1 always required; it is only a correction of WHICH delivery
mechanism (automated CI-runnable test vs. manual Editor tool run by a
human) is realistic to promise given this repository's actual, current
test infrastructure.

## Step 1: The Goal (Where are we going?) — corrected

A documented, repeatable, high-confidence answer to "does the GPU compute
kernel actually produce the same skin as the CPU path?" — a genuine, numeric,
per-vertex comparison against a real rigged model, with a justified
tolerance — delivered as a **manual Editor tool/menu command**, following
`ComputeBlurValidation`'s own already-proven integration pattern EXACTLY
(same bucket, same rigor, same "run it by hand, read the console/log
output" workflow), **not** as an automated GoogleTest — that would require
test infrastructure (a live `VkDevice` reachable from inside
`GreatTamanaEngineTests`) that does not exist in this repository today and
is explicitly out of this campaign's scope to build.

## Step 2: The Situation / The Problem (Where are we now?)

Unchanged from v1 in substance (no analytic shortcut exists for "expected
skinning output" the way `ComputeBlurValidation` has one for a box blur;
the `std430` padding mismatch is the other real risk this phase must
specifically hunt for) — restated here with the corrected framing:

- `ComputeBlurValidation` is a live-`VkDevice`-requiring tool that lives
  under `src/Editor/` (Editor-only, Tier 2, verified manually — see
  `TESTING.md`'s own explicit classification of it and `AssetPreviewMesh`/
  `AssetPreviewTexture` into this same bucket). It is **not** a GoogleTest
  and never has been — it is invoked from inside the running Editor,
  against a real, already-initialized `Renderer`/`VkDevice` the Editor
  itself already stood up (via a real `VulkanSurface`/OS window). This is
  exactly the reachable, already-working way to get a live Vulkan device
  for a validation workload in this codebase TODAY.
- Building a SECOND way to reach a live `VkDevice` — one that works from
  inside the separate `GreatTamanaEngineTests` binary, with no OS
  window/surface — is a real, separate, currently-unstarted piece of
  infrastructure (`TODO.md`'s own "Tier 2 (GPU-backed) integration test
  fixture" item), gated on a headless-surface extension this engine has
  never used and a development machine that, per `TODO.md`'s own words,
  "doesn't even support headless mode" as of today. Phase 6 depending on
  this landing first would make this whole campaign's correctness proof
  hostage to an unrelated, larger, already-independently-tracked
  infrastructure project.
- The `std430`-padding-mismatch risk, and the "which frame of a known real
  motion exercises IK + append inheritance" reasoning from v1, are both
  still completely accurate and unchanged — see below.

## Step 3: The Plan (How will we get there?) — corrected

### 3.1 — The validation tool's shape (PRIMARY deliverable, not a stretch goal)

New file(s): `src/Editor/GpuSkinningValidation.h/.cpp` — same shape, same
location convention, and same Tier-2/manual-verification bucket as
`ComputeBlurValidation.h/.cpp` (both are Editor-only, both need a live
`Renderer`, both are exercised by a human running the Editor, not by
`ctest`). Given one already-loaded, real rigged model (the same
Furina-model fixture used throughout this engine's own animation-runtime
history):

1. Sample the animation to a fixed, deterministic frame chosen specifically
   because it's already proven (via this engine's own prior, hand-built
   diagnostic tooling — see `README.md`'s "IK solving AND PMX append/grant
   bone inheritance" entry) to exercise IK-solved legs and append-inherited
   D-bones — the two most failure-prone parts of pose evaluation, giving
   this validation the best chance of catching a real bug.
2. Compute `skinningMatrices` via `EvaluateAnimatedSkinningPose()` — shared,
   already-tested, identical input for both paths; out of scope for this
   validation to re-check.
3. Run `SkinVertexRange()` (CPU) once — the oracle.
4. Run the real GPU path once, via a one-shot `Renderer::ImmediateSubmit()`
   command buffer (NOT through the full `RenderGraph` — deliberately
   self-contained, so this tool doesn't depend on Phase 3/5's full
   per-frame wiring being live), then read the output buffer back to the
   CPU via a staging buffer + `vkCmdCopyBuffer` + `ImmediateSubmit()`.
5. Compare per-vertex position/normal deltas against a documented epsilon
   (start around `1e-4`), and report max delta, mean delta, and the
   count/list of vertices exceeding the epsilon — the exact same
   quantitative rigor v1 always demanded, unchanged by this revision.

### 3.2 — What "pass" means, and what to do if it doesn't, first try

Unchanged from v1 — do not expect this to pass on the first attempt; a
uniform, enormous delta suggests a layout/binding mismatch (or the
`std430` padding bug); a localized, enormous delta suggests a bone-index/
weight-slot mismatch for a specific body part; a small, roughly
influence-count-proportional delta is legitimate floating-point
accumulation-order noise.

### 3.3 — Multi-part / shared-vertex-buffer parity

Unchanged from v1 — confirm exactly one `GpuModelEntry::OutputGroup`/one
compute dispatch per distinct shared vertex buffer, cross-checked against
the CPU path's own existing `PendingGroup` grouping count for the same
model (the two groupings share one extracted function per Phase 4, so this
also re-proves that extraction stayed correct).

### 3.4 — Automated coverage vs. manual verification, corrected

**This is the section v1 got wrong, and this revision replaces it
entirely.**

- **The manual Editor tool from Step 3.1 above is now the REQUIRED
  deliverable for this phase**, not an acceptable fallback. It must be
  built, run, and its output (max delta / mean delta / exceeding-vertex
  count, for the real Furina-model fixture at the chosen frame) recorded in
  this phase's own completion report — exactly as v1 already required.
- **The automated, `GTEST_SKIP()`-guarded GoogleTest variant is explicitly
  OUT OF SCOPE for this phase**, not merely deprioritized. Do not attempt to
  construct a `VulkanDevice` from inside `tests/` for this purpose — doing
  so would either (a) fail to build/run on the actual development machine
  today (no headless surface support, per `TODO.md`'s own words), or (b)
  require building the separate, currently-unstarted "Tier 2 GPU test
  fixture" (`VK_EXT_headless_surface`-based `GpuTestFixture`,
  `GTE_BUILD_GPU_TESTS` or similar CMake wiring) as an unplanned prerequisite
  — a meaningfully larger, independently-valuable, already-tracked piece of
  engine infrastructure that does not belong inside this campaign's scope
  or completion criteria.
- **If, in the future, that separate Tier 2 GPU test fixture DOES land**
  (tracked in `TODO.md`, not here), THEN — and only then — promoting this
  phase's manual tool into a real, automated,
  `GTEST_SKIP()`-if-fixture-or-model-absent GoogleTest becomes a
  reasonable, cheap follow-up (the actual comparison LOGIC from Step 3.1 is
  already written and reusable; only the "how do I get a `VkDevice`" plumbing
  changes). Note this explicitly as a forward-looking, NOT-currently-planned
  follow-up in `TODO.md` once Phase 6 ships, so it isn't forgotten, but do
  not build toward it speculatively now.
- Per this engine's own established, honest Tier 1 vs. Tier 2 split
  (`TESTING.md`): the *packing* functions from Phase 1
  (`PackBindPoseVertices()`/`PackSkinWeights()`) already have real Tier-1
  unit tests (Phase 1's own deliverable, unaffected by this revision) — that
  part of "automated coverage" was never in question and remains exactly
  as v1 planned it. Only the END-TO-END GPU-vs-CPU numeric comparison is
  affected by this correction.

## Step 4: What We Will NOT Do (Focus)

Unchanged from v1 (no fuzz/property-based testing across random skeletons,
no performance benchmarking in this phase, no visual/screenshot-diff
tooling, no attempt at bit-for-bit-identical floating point results), plus
one addition:

- **No building of a headless/windowless Vulkan device bootstrap inside
  `tests/` as part of this phase**, under any framing ("just for this one
  test", "a minimal version, not the full fixture", etc.). That is
  precisely the separate, larger, already-identified `TODO.md` item this
  revision explicitly keeps out of scope — see Step 3.4 above. If a future
  engineer genuinely believes Phase 6 needs its own bespoke, narrower device
  bootstrap distinct from the general Tier 2 fixture, that is a new,
  separate proposal requiring its own review, not a quiet scope-creep
  addition to this phase.

## Step 5: Their Role (What does this mean for you?)

- Build the readback helper and the full CPU-vs-GPU comparison pipeline
  described in Step 3.1 first, against the real model fixture, run by hand
  from inside the Editor — get a human-readable "max delta: X, mean delta:
  Y" printout you can actually look at and reason about.
- **Stop there.** Do not attempt to also wire this into
  `tests/CMakeLists.txt`/`GreatTamanaEngineTests` as part of this phase —
  see Step 3.4's correction above for exactly why, and confirm in your own
  completion report that you read (or re-read) `TESTING.md`'s and
  `TODO.md`'s own words on this before deciding, rather than re-discovering
  the same gap this revision already found.
- Do not sign off on this phase, and do not let Phase 7's user-facing
  toggle ship, without a specific, written number for "max observed
  per-vertex delta on the real Furina-model fixture at the chosen frame" in
  your own completion report — this bar is unchanged from v1 and is not
  weakened by this revision's correction to HOW that number gets produced.
