# Job System — Phase 6: First Production Consumer (Animation / Vertex Skinning) — v2

Parent document: `JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md` (read first).
Previous phase: `JOBSYSTEM_PHASE5_PROFILER_INTEGRATION_WORKER_TIMELINE_v2.md`
(must be done — `GTE_PROFILE_JOB_SCOPE` exists, proven race-free, and its
SDL-clock-concurrency precondition is verified).
Next phase: `JOBSYSTEM_PHASE7_EDITOR_JOBS_PANEL.md` (v1, unchanged).

**Definition of Done for this phase (gates Phase 7's "does the panel show
real, meaningful data" claim, and gates the whole campaign's own Step 1
success criterion #3):** `AnimationSystem::Update()`'s per-frame CPU
vertex-skinning work is dispatched through the Job System instead of
running serially on the main thread, produces bit-identical (or
explicitly, narrowly, documented floating-point-tolerance-identical)
output to the old single-threaded code path, is measurably faster on a
real multi-core machine, ships behind its own opt-out, AND — new in v2 —
never allows two different animator entities' own `Dispatch()`/
`WaitForJobs()` sequences to be in flight at the same time within one
`AnimationSystem::Update()` call, for as long as this engine's own
documented cross-instance shared-GPU-buffer limitation exists (see §3.6).

---

## Revision Notes (2nd Iteration / v2)

Two changes, both load-bearing, made after checking this phase against
the real, current source (`Game.cpp`, `Game.h`, `RenderSystem.h/.cpp`,
and `README.md`'s own "Status" section describing the
`MeshInstantiationSystem`/`AnimationSystem` extraction refactor):

1. **Corrected every reference from `Game::UpdateSkeletalAnimators()` to
   `AnimationSystem::Update()` (`src/Game/Animation/AnimationSystem.cpp`).**
   v1's Step 1 "Goal" prose and Step 2 "BEFORE" code sketch both referred
   to a method, `Game::UpdateSkeletalAnimators()`, that no longer exists
   in this codebase — it was extracted into `AnimationSystem` as part of
   the "god object" refactor `README.md`'s own Status section documents
   (`Game.cpp`'s real, attached `Update()` today is just
   `m_animationSystem.Update(m_registry, deltaSeconds)`). v1's own Step
   3.1 file-layout section already correctly targeted
   `AnimationSystem.h/.cpp` — this v2 makes the rest of the document
   internally consistent with that. Nothing about the actual TECHNICAL
   plan changes because of this — it is a naming/reference correction,
   not a design change — but it matters: a future implementer following
   v1 literally would have gone looking for a method that isn't there.
2. **Added §3.6, a new, permanent rule closing a real data-race hazard**
   that v1 never addressed: this engine's own documented limitation (see
   `README.md`'s "Real MMD skinning/animation runtime" status entry) that
   two entities spawned from the SAME `*.gta` file today share one
   underlying GPU `Mesh` (and therefore its CPU-side cached bind-pose
   buffers and output skinning buffers too, per
   `MeshInstantiationSystem`'s own caching-by-path design) is harmless
   TODAY only because `AnimationSystem::Update()` processes every live
   animator strictly one at a time, on one thread. The moment this phase
   parallelizes skinning, if the orchestration loop over "every live
   animator" were ever changed to dispatch ALL animators' work before a
   single, shared `WaitForJobs()` (a natural-looking performance
   optimization once Phase 6 exists — process every model's skinning
   fully in parallel, wait once, then upload everything), any two
   entities sharing the same underlying `*.gta` would have two DIFFERENT
   worker threads writing the SAME memory at the SAME time — an actual
   data race, undefined behavior, strictly worse than today's harmless
   "last write wins, looks a bit wrong" bug. §3.6 makes the fix an
   explicit, permanent rule for this phase (and this whole campaign):
   each model's own `Dispatch()`+`WaitForJobs()` pair must complete in
   full before the next model's begins, always, until a SEPARATE, future,
   explicitly-scoped campaign gives every spawned instance its own private
   GPU buffers.

Everything else in v1 — the trampoline design (§3.2), the size threshold
(§3.3), the parity-testing plan (§3.4), and the manual benchmark
methodology (§3.5) — held up unchanged against the real code, including
one pleasant confirmation: `RenderSystem::TryGetMesh(MeshHandle)` already
exists in the real `RenderSystem.h` specifically for exactly the use this
phase needs ("needed by `Game::UpdateSkeletalAnimators()`... to call
`Mesh::UpdateVertexData()`" per its own doc comment — itself a small,
pre-existing stale reference in the ENGINE's own code, left over from
before the `AnimationSystem` extraction, worth being aware of but not this
campaign's job to fix). This confirms Phase 6's own boundary-enforcement
design (a job body only ever sees plain CPU-side spans, never a `Mesh`/
GPU handle — the GPU upload step stays main-thread-only, called through
this exact accessor after `WaitForJobs()` returns) is realistic and
already has the exact hook it needs in production code today.

---

## Step 1: The Goal (Where are we going?)

Move exactly ONE real, already-shipped, already-understood piece of
per-frame CPU work — CPU vertex skinning (`Animation/VertexSkinning.h`'s
`SkinVertices()`, called today from `AnimationSystem::Update()`
(`src/Game/Animation/AnimationSystem.cpp`) once per currently-playing
`SkeletalAnimator`, per mesh PART) — from "always runs serially on the
main thread" to "dispatched across the worker pool, per mesh part, when
there's enough work to be worth it, and never in parallel across two
DIFFERENT entities' own work at once" (see §3.6).

This is chosen for the same reasons v1 gave: `SkinVertices()` is already a
pure function (Phase 4's audit), already naturally batchable along an
obvious, disjoint axis (a model's own mesh parts, and within a part, its
individual vertices), and already downstream of everything else in the
animation pipeline (no dependency-chain machinery from Phase 3 is needed
for this first migration).

Concretely, `AnimationSystem::Update()`'s per-model skinning loop changes
from:

```cpp
// BEFORE (serial, today - inside AnimationSystem::Update(), one call per
// currently-playing SkeletalAnimator, for that animator's own model):
for (MeshPart& part : model.parts) {
    SkinVertices(part.bindPoseVertices, part.skinWeights, skinningMatrices, /*out*/ part.skinnedVertices);
    part.mesh.UpdateVertexData(part.skinnedVertices);
}
```

to:

```cpp
// AFTER (this phase) - still exactly one call per currently-playing
// SkeletalAnimator, iterated by AnimationSystem::Update()'s own outer loop.
// See 3.6: the outer loop over every live animator MUST remain fully
// sequential model-by-model - this inner block's own Dispatch()+
// WaitForJobs() pair must complete entirely before the outer loop moves
// on to the next animator.
Jobs::JobHandle skinningHandle;
for (MeshPart& part : model.parts) {
    if (part.bindPoseVertices.size() < kMinVerticesToParallelize) {
        SkinVertices(part.bindPoseVertices, part.skinWeights, skinningMatrices, part.skinnedVertices);
        continue;
    }
    Jobs::Dispatch(&SkinVertexBatchTrampoline, static_cast<std::uint32_t>(part.bindPoseVertices.size()),
        &MakeSkinningJobContext(part, skinningMatrices), skinningHandle, kMinVerticesPerBatch);
}
Jobs::WaitForJobs(skinningHandle); // exactly ONE wait, for THIS ONE model's every part, before the next model starts
for (MeshPart& part : model.parts) {
    part.mesh.UpdateVertexData(part.skinnedVertices); // GPU upload stays main-thread-only, unconditionally
}
```

---

## Step 2: The Situation / The Problem (Where are we now?)

Today, `AnimationSystem::Update()` runs, for every live `SkeletalAnimator`,
entirely on the main thread, once per frame: pose evaluation
(`EvaluateAnimatedSkinningPose()` — fast, small bone counts) followed by
`SkinVertices()` per mesh part, where the real cost concentrates for a
model with a non-trivial vertex count (the Furina test model already used
elsewhere in this engine's own test suite has ~31,000 vertices across 32
materials).

The three concrete engineering questions from v1 (batching unit and
whether `Dispatch()`'s own splitting is needed at all, where the job-body/
main-thread boundary falls, and whether a small model is worth
parallelizing at all) are unchanged and still correctly answered by v1's
own reasoning in §3.1-§3.3 below.

A fourth question, missing from v1, is answered by this v2's new §3.6:
**is it safe to let more than one animator's own skinning work be in
flight on the worker pool at the same time?** Today the answer is no,
for a specific, documented, engine-level reason (shared GPU buffers across
instances of the same `*.gta`) — not a Job-System-level reason. This
phase must respect that constraint explicitly rather than silently
assuming "more parallelism is always better."

---

## Step 3: The Plan (How will we get there?)

### 3.1 — Where the new code actually lives

```
src/Game/Animation/
    AnimationSystem.h/.cpp          - EXISTING file, gains the Dispatch()-based
                                       orchestration described in Step 1's "AFTER" sketch,
                                       replacing its existing serial skinning loop.
                                       (Corrected in v2 - this was already accurate in
                                       v1's own file-layout section, only the SURROUNDING
                                       prose elsewhere in v1 had drifted.)
src/Animation/
    VertexSkinning.h/.cpp           - UNCHANGED - SkinVertices() itself is not modified
                                       at all; this migration only changes HOW/WHERE it's
                                       called.
tests/Game/Animation/
    AnimationSystemParityTests.cpp  - NEW - the parity/correctness proof (see 3.4)
```

`SkinVertices()` itself needs zero code changes — unchanged from v1.

### 3.2 — The job-body trampoline (unchanged from v1)

No changes to the `SkinningJobContext`/`SkinVertexBatchTrampoline()`
design from v1 — still the correct shape: a read-only shared
`skinningMatrices` span, disjoint per-batch input/output subspans, and
exactly one `GTE_PROFILE_JOB_SCOPE("SkinVertices")` call and nothing
else touching global state.

### 3.3 — The size threshold and the orchestration loop (unchanged, with
one clarifying addition)

`kMinVerticesToParallelize`/`kMinVerticesPerBatch` and the four-step
per-model orchestration loop from v1 are unchanged. One addition,
consistent with §3.6 below: the OUTER loop in `AnimationSystem::Update()`
that iterates every currently-playing `SkeletalAnimator` and, for each
one, runs this per-model orchestration sequence, must call this whole
sequence (Dispatch every part → `WaitForJobs()` → upload every part) to
full completion for one animator before starting the next animator's own
sequence. This was implicit in v1's own pseudocode (which only ever showed
ONE model's loop) but was never stated as an explicit, permanent rule
governing the OUTER loop across multiple simultaneously-playing animators
— §3.6 below states it explicitly and explains why it must never be
relaxed casually.

### 3.4 — Parity testing: proving zero behavior change (unchanged from v1)

No changes to the parity-test plan itself. One addition: the parity test
should ALSO exercise a scene with more than one simultaneously-playing
`SkeletalAnimator` (even two instances of the SAME model, deliberately
recreating the shared-buffer scenario §3.6 discusses) and assert that the
final GPU-uploaded result for each is bit-identical to running them one at
a time serially the old way — this is what actually proves §3.6's
sequential-dispatch rule is being honored in the real implementation, not
just documented as an intention.

### 3.5 — Measuring the actual win (unchanged from v1)

No changes. Note for the benchmark write-up: measure BOTH the
single-model case (this phase's primary target) and a multi-model case,
so the recorded numbers honestly reflect that the sequential-per-model
rule (§3.6) means multiple SIMULTANEOUSLY-playing animators do NOT get
their OWN work parallelized against each other's — only the work WITHIN
one model's own mesh parts is parallelized. This is an accepted, deliberate
limitation (see §3.6), not a bug in the benchmark.

### 3.6 — Why cross-model parallelism is explicitly deferred (NEW in v2)

This is the single most important addition this revision makes. Read it
before writing a single line of `AnimationSystem::Update()`'s new
orchestration loop.

**The hazard.** This engine's own `MeshInstantiationSystem` caches GPU
mesh upload by SOURCE PATH — spawning two entities from the same `*.gta`
file reuses the SAME underlying `Mesh` (and, for a skinned model, the
same CPU-side cached bind-pose vertex arrays and the same output
skinning/upload buffers), a real, currently-documented limitation
(`README.md`'s own "Real MMD skinning/animation runtime" status entry:
"two SIMULTANEOUSLY-animated instances of the same model file currently
fight over those same buffers"). Today this is tolerable — ugly, but not
dangerous — purely because `AnimationSystem::Update()` runs on ONE thread
and processes every live animator strictly one after another: at any
given instant, at most one animator is ever touching that shared memory.

Once this phase exists, a natural-looking next optimization would be:
"why wait for model A's skinning to fully finish before even starting
model B's? Dispatch every model's jobs up front, then call
`WaitForJobs()` once at the very end, covering everyone." This would
indeed extract more real parallelism — but it also means, for the exact
scenario the shared-buffer limitation already describes, TWO DIFFERENT
WORKER THREADS could end up writing into the SAME shared vertex buffer AT
THE SAME TIME. That is no longer "last write wins, a bit visually wrong
this frame" — it is a textbook data race: undefined behavior, capable of
producing torn/corrupted vertex data, a crash under the right (or wrong)
allocator/memory conditions, or a ThreadSanitizer failure that would
rightly block Phase 8 from ever signing this off as safe to default to
`ON`.

**The rule.** For as long as this shared-buffer limitation exists in
`MeshInstantiationSystem`, `AnimationSystem::Update()`'s outer loop over
every currently-playing `SkeletalAnimator` MUST process one animator's
entire per-model orchestration sequence (every part's `Dispatch()` calls,
followed by that ONE model's `WaitForJobs()`, followed by that model's GPU
uploads) to full completion before beginning the next animator's own
sequence. This is not a temporary implementation shortcut to "optimize
later" — it is a permanent correctness invariant this campaign commits to,
and it must be called out, by name, in a code comment at the exact call
site in `AnimationSystem::Update()` where a future contributor might
otherwise be tempted to "helpfully" batch multiple models' dispatches
together, exactly the way this codebase already comments its other
deliberate, easy-to-accidentally-violate invariants (e.g. `AGENTS.md`'s
own treatment of `RenderGraphBuilder`'s pass-ordering rules, or
`FrameRecorder::RecordFrame()`'s format-matching assert).

**What this costs, and what would remove the cost.** The real performance
cost of this rule is: a scene with several DIFFERENT simultaneously-
animated models gets each model's OWN mesh-part-level parallelism, but
gets ZERO parallelism ACROSS different models — model B's skinning cannot
start until model A's is fully uploaded. For this campaign's own stated
scope (proving out the Job System against one real workload, not chasing
maximum possible speedup), this is an acceptable, honestly-measured
trade-off, consistent with the master strategy's own "bias toward NOT
parallelizing something when in doubt" (see
`JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md`, Step 5). The actual fix that
would allow safely lifting this rule — giving every spawned model
instance its own private GPU mesh buffers instead of sharing one per
source path — is a real, valuable, but SEPARATE piece of engine work
(already implicitly flagged by `README.md`'s own status entry as a
"natural follow-up once that's actually needed"), explicitly out of scope
for this Job System campaign. A future campaign that does that work is
what would then, and only then, make it safe to revisit cross-model
skinning parallelism.

**Testing this rule, not just stating it.** §3.4's parity test addition
(a scene with two simultaneously-playing animators sharing one model) is
what turns this from a documented intention into a verified property —
and Phase 8's ThreadSanitizer pass (`JOBSYSTEM_PHASE8_TESTING_HARDENING_BENCHMARKING.md`,
unchanged) is what gives this the same empirical backstop every other
safety claim in this campaign gets. If a future refactor ever violates
this rule by accident, Phase 8's own soak/TSan pass — run again after any
future change to `AnimationSystem.cpp` — is exactly the mechanism that
should catch it, provided that pass is actually re-run after such changes
land, which this document explicitly asks a future contributor to
remember to do (see Step 5 below).

---

## Step 4: What We Will NOT Do (Focus)

Everything from v1 remains true (no parallelizing pose evaluation, no
overlapping other main-thread work during the wait, no touching
`Mesh::UpdateVertexData()`/Renderer from a job body, no making this
migration mandatory, no skipping the parity test). One addition:

- **We will NOT dispatch more than one animator entity's own skinning
  work onto the worker pool at the same time**, under any circumstances,
  for as long as `MeshInstantiationSystem`'s shared-buffer-per-source-path
  caching exists (see §3.6). This is not a "for now" caveat to be quietly
  dropped the first time someone wants a bit more performance — it is a
  hard rule this whole campaign commits to, and lifting it requires a
  separately-scoped engine change (per-instance GPU buffers), not a
  Job-System-side optimization.

---

## Step 5: Their Role (What does this mean for you?)

Everything from v1 remains true (implement in `AnimationSystem.cpp` only,
write and pass the parity test before considering this phase done, record
the actual before/after timing numbers, update `AGENTS.md`). Two
additions:

- **Add the §3.6 sequential-dispatch rule as an explicit, named comment**
  at the exact point in `AnimationSystem::Update()`'s outer loop where a
  future contributor might be tempted to "improve" it by batching
  multiple models' dispatches together — make the hazard and the rule
  impossible to miss for whoever touches this code next, including your
  own future self.
- **If a future change ever touches `AnimationSystem.cpp`'s orchestration
  loop again** (for a bug fix, a new feature, or a genuine future
  attempt at cross-model parallelism once per-instance GPU buffers exist),
  re-run Phase 8's ThreadSanitizer/soak pass against the changed code
  before merging — this campaign's own safety story for Phase 6 rests
  specifically on the sequential-dispatch rule in §3.6 holding, and that
  is exactly the kind of invariant that silently rots the first time
  someone touches nearby code without rereading why it's there.
