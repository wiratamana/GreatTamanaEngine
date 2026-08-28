# Job System — Phase 6 (First Production Consumer — Animation / Vertex Skinning) — Completion Report

Status: **DONE** (fast-compile-check verified per this session's own explicit
instructions — no full clean build/`ctest` regression run performed; that is
explicitly deferred to a later session, after every remaining Job System
phase in this campaign is done). This report documents what was actually
implemented and verified this session, for whoever picks up Phase 7
(`JOBSYSTEM_PHASE7_EDITOR_JOBS_PANEL.md`) next.

Parent strategy: `JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md`.
Phase strategy followed: `JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md`.
Previous phase: `JOB_SYSTEM_PHASE5_COMPLETION_REPORT.md` (Profiler
Integration — Worker Timeline).

---

## 1. What this phase actually is

Per its own strategy document, Phase 6 is the campaign's own production
cut-over: move exactly ONE real, already-shipped, already-understood piece
of per-frame CPU work — CPU vertex skinning
(`Animation/VertexSkinning.h`'s `SkinVertices()`, called from
`AnimationSystem::Update()`, `src/Game/Animation/AnimationSystem.cpp`) —
from "always runs serially on the main thread" to "dispatched across the
worker pool, per model, when there's enough work to be worth it, with
identical output to the old single-threaded code path." This is the first
and, as of this session, only real call site anywhere in the engine that
calls `gte::Jobs::Dispatch()`/`WaitForJobs()` in production — everything
built in Phases 1-5 was proven only by this module's own tests until now.

---

## 2. What was built

### 2.1 — `SkinVertexRange()` (`src/Animation/VertexSkinning.h/.cpp`)

A new, pure, always-compiled function — the exact per-vertex blending
`SkinVertices()` already performed, restricted to a single half-open
`[beginIndex, endIndex)` subrange of vertices, writing into a caller-owned,
already-sized pair of output vectors (`outPositions`/`outNormals`) rather
than resizing them itself. This is what lets several disjoint batches write
into disjoint slices of the SAME output vectors with no aliasing and no
risk of one batch's own `resize()` invalidating another batch's
in-progress writes (a batch call never resizes anything — only the caller,
before dispatching any batches, does).

**`SkinVertices()` itself was refactored to be implemented purely in terms
of `SkinVertexRange(0, bindPositions.size(), ...)`** — there is exactly one
copy of the actual blending logic now, not two independently-maintained
copies. This is a behavior-preserving refactor only: every existing
`tests/Animation/VertexSkinningTests.cpp` test (BDEF1/BDEF2/no-weights/
unused-slots/no-valid-influence) passes unchanged against the refactored
code, proving the extraction didn't alter `SkinVertices()`'s own observable
behavior at all (see §4).

`endIndex` is defensively clamped internally against the real vertex/output
vector sizes — never reads/writes out of bounds even if a caller (e.g. a
mis-computed batch range) ever passed a bad range.

### 2.2 — `AnimationSystem::Update()`'s new Dispatch()-based orchestration (`src/Game/Animation/AnimationSystem.cpp`)

For each currently-playing `SkeletalAnimator`, after computing that frame's
`skinningMatrices` (via the unchanged
`Animation/AnimationPoseEvaluator.h::EvaluateAnimatedSkinningPose()`), CPU
vertex skinning is now performed one of two ways:

- **Below `kMinVerticesToParallelize` (512 vertices)**: runs inline,
  serially, on the calling thread via a direct `SkinVertexRange(0,
  vertexCount, ...)` call — scheduling a `Dispatch()` for a genuinely tiny
  model would spend more time on the Job System's own per-`Dispatch()`
  scheduling overhead than the actual skinning work itself (mirrors the
  wider campaign's own Phase 2 batch-vs-item reasoning, applied here at the
  model level).
- **At or above that threshold**: dispatched via `gte::Jobs::Dispatch()`
  (a `SkinningBatchContext` — five plain pointers into this call's own
  read-only input data plus its own output vectors — handed through as the
  opaque payload, and `RunSkinningBatch()` — the job-body trampoline,
  wrapped in `GTE_PROFILE_JOB_SCOPE("SkinVertices")`, per Phase 5's own
  "the ONE sanctioned way to profile code running inside a job body" rule
  — calling `SkinVertexRange()` for that one batch's own `[begin, end)`
  slice), floored at `kMinVerticesPerBatch` (256) so `Dispatch()` never
  splits smaller than that many vertices into their own batch. Exactly ONE
  `JobSystem::Instance().WaitForJobs(skinningHandle)` call follows, for
  this one model's entire dispatch, before this loop iteration's GPU
  upload runs.

The GPU upload step itself (`Mesh::UpdateVertexData()` via
`RenderSystem::TryGetMesh()`) is completely unchanged, and stays
main-thread-only, unconditionally — exactly matching the Phase 4
thread-safety audit's own `Renderer`/`Mesh` row (NEVER for a job body to
touch).

### 2.3 — The §3.6 sequential-dispatch rule, made explicit in code

Per the strategy document's own Section 3.6 (a real, load-bearing
correctness rule, not a performance nice-to-have): `AnimationSystem::
Update()`'s OUTER loop over every live `SkeletalAnimator` remains, and MUST
remain, strictly sequential — one animator's entire per-model sequence
(skinning dispatch + wait + every part's GPU upload) must run to full
completion before the next animator's own sequence begins. This is
required because two entities spawned from the SAME `*.gta` file share one
underlying GPU `Mesh` (a real, pre-existing, documented engine limitation —
see `README.md`'s own "two SIMULTANEOUSLY-animated instances... currently
fight over those same buffers" note) — overlapping two animators' own
`Dispatch()`/`WaitForJobs()` work on the worker pool at the same time would
turn today's harmless "last write wins" visual bug into a genuine,
unsynchronized data race on that shared buffer.

A long, explicit, impossible-to-miss comment block was added directly at
the top of this outer `for` loop in `AnimationSystem::Update()` — not just
in this report or the strategy document — spelling out the hazard and the
rule, specifically so a future contributor tempted to "helpfully" batch
multiple animators' dispatches together (a natural-looking next
optimization once this phase exists) sees the warning at the exact point
they would make that mistake.

---

## 3. Tests added

`SkinVertices()` itself needed no test changes — its existing test suite
(`tests/Animation/VertexSkinningTests.cpp`) is unaffected and unchanged,
and continues to pass exactly as before (proving the `SkinVertexRange()`
extraction is a pure refactor — see §4).

A new file, `tests/Animation/VertexSkinningParityTests.cpp` (Tier 1, real
`gte::Jobs::JobSystem::Instance()`, no live Renderer/GPU/ECS involved —
`AnimationSystem::Update()` itself is Tier 2 today, per this codebase's own
established "needs a live Renderer for `Mesh::UpdateVertexData()`" bucket,
so this test instead proves the actual new PURE logic Phase 6 adds, at the
same Tier-1 level this whole module has always been tested at):

- `SkinVertexRangeFullRangeMatchesSkinVertices` — `SkinVertexRange(0,
  count, ...)` called directly produces identical output to
  `SkinVertices()` — the direct proof the refactor in §2.1 changed nothing
  observable.
- `ParallelDispatchProducesIdenticalResultsToSerialSkinVertices` — a
  synthetic, deterministic 4,001-vertex/11-bone model (a deliberately
  large, non-evenly-divisible vertex count, mirroring
  `JobDispatchTests.cpp`'s own 1009-item precedent) skinned via a real
  `Dispatch()`+`WaitForJobs()` round-trip across several concurrent
  batches, compared vertex-for-vertex against the same model skinned via
  the original, serial `SkinVertices()` call — every position/normal must
  match exactly (`ApproximatelyEqual`).
- `RepeatedParallelDispatchesStayConsistentAcrossIterations` — the same
  parity check repeated 25 times in a row against fresh `JobHandle`s (a
  2,003-vertex/7-bone model), per this campaign's own "never trust a
  single passing run for genuinely concurrent code" discipline.

Added to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` unconditionally (no
`GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL` dependency), matching every
other `Jobs`/`Animation` test file's own "always built" bucket.

---

## 4. Verification performed

Per this session's own explicit instructions ("do not perform a full
build, just a fast compile check; if it compiles, commit directly — full
build and regression test will happen later"):

- `cmake -S . -B build` (no `require_internet_connection`) against the
  existing Ninja/MinGW `build/` directory — picked up the new test source
  file, configured cleanly.
- `cmake --build build --target gte_core` — `src/Animation/VertexSkinning.cpp`
  and `src/Game/Animation/AnimationSystem.cpp` (the only two changed
  production files) compiled cleanly, zero warnings/errors, and relinked
  `libgte_core.a` successfully.
- `cmake --build build --target GreatTamanaEngineTests` — the new
  `tests/Animation/VertexSkinningParityTests.cpp` compiled cleanly and
  linked into `GreatTamanaEngineTests.exe` successfully.
- `cmake --build build --target GreatTamanaEngine` — the real executable
  (which links `gte_core`) still builds and links cleanly, confirming the
  `AnimationSystem.cpp` changes (new `#include`s of `Jobs/JobDispatch.h`/
  `Jobs/JobSystem.h`/`Profiling/JobScopeTimer.h`) don't break the shipped
  binary.
- As an extra sanity check beyond the requested "compile check" (running
  the already-built binary, not a rebuild):
  `tests\GreatTamanaEngineTests.exe --gtest_filter=VertexSkinningParityTests.*:VertexSkinningTests.*`
  — **all 8 tests passed** (3 new parity tests + all 5 pre-existing
  `VertexSkinningTests` tests, confirming the `SkinVertexRange()` refactor
  changed nothing observable about `SkinVertices()`'s own behavior).
- Per this session's own instructions, the full clean, cross-configuration
  rebuild (`build_joboff`-style) and the full `ctest -C Debug
  --output-on-failure` regression run across the whole suite are
  deliberately deferred to a later pass, once every remaining Job System
  work for this session is done.

---

## 5. Definition of Done — checked against Phase 6's own strategy doc

1. ✅ `AnimationSystem::Update()`'s per-frame CPU vertex-skinning work is
   dispatched through the Job System (for a model at/above
   `kMinVerticesToParallelize`) instead of always running serially on the
   main thread.
2. ✅ Produces identical output to the old single-threaded code path —
   proven by `VertexSkinningParityTests.cpp`'s parity tests (§3), and by
   every pre-existing `VertexSkinningTests.cpp` test still passing
   unchanged against the refactored `SkinVertices()` (§4).
3. ⚠️ "Measurably faster on a real multi-core machine" — NOT independently
   re-measured this session (this session's own scope was explicitly "fast
   compile check only, full regression/benchmark later"). The underlying
   mechanism (splitting a model's own vertex array into
   `WorkerCount()`-bounded batches via the already-proven `Dispatch()`
   primitive) is identical to what Phases 2/3's own tests already
   established works correctly and in parallel; an actual wall-clock
   benchmark against a real rigged model is explicitly Phase 8's own
   "automated benchmark" deliverable (`JOBSYSTEM_PHASE8_TESTING_HARDENING_BENCHMARKING.md`,
   §3.3) — this phase intentionally does not duplicate that effort with an
   ad hoc manual measurement.
4. ✅ Ships behind its own opt-out — a model below `kMinVerticesToParallelize`
   never touches the Job System at all, and the whole mechanism degrades
   correctly under `GTE_ENABLE_JOB_SYSTEM=OFF` (per Phase 1's own contract,
   `Dispatch()`/`WaitForJobs()` run every batch inline, synchronously, with
   identical observable results — not independently re-verified under a
   dedicated `GTE_ENABLE_JOB_SYSTEM=OFF` build THIS session, per the
   explicit "fast compile check only" scope, but no code path introduced
   here is conditional on that switch in a way that would behave
   differently from every other already-proven `Dispatch()` call site).
5. ✅ Never allows two different animator entities' own `Dispatch()`/
   `WaitForJobs()` sequences to be in flight at the same time within one
   `AnimationSystem::Update()` call — the outer loop's sequencing is
   completely unchanged from before this phase (still a plain, ordinary
   `for` loop, one iteration fully completing before the next begins), and
   is now protected by an explicit, prominent code comment (§2.3) warning
   against ever changing that.

---

## 6. Files changed/added this session

- `src/Animation/VertexSkinning.h` — new `SkinVertexRange()` declaration;
  `SkinVertices()`'s own doc comment updated to note it now delegates to it.
- `src/Animation/VertexSkinning.cpp` — `SkinVertexRange()` implementation
  (the extracted per-vertex blending loop); `SkinVertices()` now just
  resizes its output vectors and delegates to `SkinVertexRange(0,
  vertexCount, ...)`.
- `src/Game/Animation/AnimationSystem.cpp` — new includes
  (`Jobs/JobDispatch.h`, `Jobs/JobSystem.h`, `Profiling/JobScopeTimer.h`);
  new anonymous-namespace `kMinVerticesToParallelize`/
  `kMinVerticesPerBatch` constants, `SkinningBatchContext`/
  `RunSkinningBatch()`; `Update()`'s skinning step now branches between
  inline `SkinVertexRange()` and a real `Dispatch()`+`WaitForJobs()`
  round-trip; the outer loop gained the explicit §3.6 sequential-dispatch
  warning comment.
- `tests/Animation/VertexSkinningParityTests.cpp` (new) — §3.
- `tests/CMakeLists.txt` — new `Animation/VertexSkinningParityTests.cpp`
  entry (unconditional).
- `task_manager/job_system/JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md` (this
  file, new).

No existing PUBLIC API changed shape — `SkinVertices()`'s signature is
byte-for-byte unchanged, and `AnimationSystem::Play()`/`Update()`'s own
public signatures are unchanged too. This phase's only externally-visible
effect is that a sufficiently large rigged model's per-frame CPU vertex
skinning now genuinely runs across the worker pool instead of serially on
the main thread, with identical results either way.

---

## 7. What remains open

- **Phase 6's own "measurably faster" claim is not independently
  benchmarked in this session** (see §5, item 3) — Phase 8's own dedicated,
  repeatable benchmark (`JOBSYSTEM_PHASE8_TESTING_HARDENING_BENCHMARKING.md`,
  §3.3) is the intended place this gets proven with real numbers, plugged
  into this engine's own future benchmark-mode infrastructure rather than
  a one-off manual measurement here.
- **A dedicated multi-animator parity test exercising the actual §3.6
  hazard scenario** (two simultaneously-playing `SkeletalAnimator`s sharing
  the same underlying `*.gta`/GPU mesh, proving the sequential-dispatch
  rule is honored end-to-end through real `AnimationSystem::Update()` calls)
  was NOT added this session — `AnimationSystem::Update()` itself is Tier 2
  (needs a live Renderer for `Mesh::UpdateVertexData()`), so this would
  need either a real GPU-backed test fixture (not yet built anywhere in
  this engine — see `TESTING.md`'s own "Tier 2" note) or a refactor to make
  the orchestration loop itself testable independent of the GPU upload
  step. Left as a follow-up; the sequential-loop STRUCTURE itself is
  unchanged from before this phase (still a plain, ordinary `for` loop),
  so no new risk was introduced relative to the pre-Phase-6 baseline, but a
  dedicated regression test proving this explicitly would be valuable
  before Phase 8's own ThreadSanitizer pass, per that phase's own
  discipline.
- Phase 7 (Editor "Jobs" Panel) and Phase 8 (Testing, Hardening &
  Benchmarking) remain entirely unstarted, per the master strategy's own
  phase-by-phase gating.
- The full clean cross-configuration rebuild and the full
  `ctest -C Debug --output-on-failure` regression run across the whole
  suite (both `GTE_ENABLE_JOB_SYSTEM` configurations) are deferred to a
  later session, per this session's own explicit scope.
