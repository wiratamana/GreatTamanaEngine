# Job System — Master Strategy (Phase 0) — v2

> "He who knows when he can fight and when he cannot will be victorious."
> — Sun Tzu. Before we spin up a single worker thread, we must know exactly
> which battle we are fighting, and which ten battles we are deliberately
> not fighting yet.

This is the **orchestrator document** for the Job System campaign, revised
as a 2nd-iteration pass against the engine's ACTUAL current source (not
just its own prior strategy documents). Read this file first, always,
before opening any child file.

---

## Revision Notes (2nd Iteration / v2)

A second pass was made specifically to check this campaign's own strategy
against the real, current `src/` tree (Game.cpp/.h, RenderSystem.cpp/.h,
Application.cpp/.h, RenderPasses.cpp/.h, the whole `Profiling/` module, and
`AGENTS.md`/`README.md`/`TODO.md`'s own up-to-date status) rather than
against assumptions. Five things changed as a result — every one of them
is either a factual correction (the strategy described code that no longer
exists in that shape) or a genuine, previously-unaddressed correctness gap,
never a stylistic rewrite:

1. **Corrected the real production call site for Phase 6's target
   workload.** v1 repeatedly said Phase 6's orchestration change belongs in
   `Game::UpdateSkeletalAnimators()`. That method does not exist anymore —
   it was extracted, along with the rest of `Game`'s old "god object"
   responsibilities, into `AnimationSystem::Update()`
   (`src/Game/Animation/AnimationSystem.cpp`) as part of the refactor
   `README.md`'s own "Status" section describes (`Game.cpp` today is a thin
   composition root whose `Update()` is just
   `m_animationSystem.Update(m_registry, deltaSeconds)`). v1's own Phase 6
   file-layout section (Step 3.1) already correctly named
   `AnimationSystem.h/.cpp` as the file to change — the rest of Phase 6's
   prose (Step 1's "Goal" and Step 2's "BEFORE" code sketch) simply hadn't
   caught up to that. Fixed throughout Phase 6 v2; this master file's own
   Step 2 already used the correct name and needed no change there.
2. **Precisely stated the Job System singleton's startup timing.**
   `gte::Jobs::JobSystem::Instance()` is designed (Phase 1) as a Meyers
   singleton, the exact same pattern `FrameProfiler::Instance()` already
   uses in this codebase. That means it starts **lazily, on first actual
   use** — not "near Application's own construction," as v1's Step 1 goal
   list loosely worded it. In practice the worker pool will not exist until
   the first real `Dispatch()`/`Schedule()` call, which — until Phase 6
   ships — is nothing at all, and after Phase 6 ships is whichever frame
   first plays a skinned model. This is a harmless, deliberate consequence
   of reusing this codebase's existing idiom, not a defect, but it should
   be stated precisely rather than aspirationally.
3. **Identified and closed a genuine data-race hazard for Phase 6** (see
   Phase 6 v2's new "3.6 — Why cross-model parallelism is explicitly
   deferred" section for the full writeup). Short version: `README.md`'s
   own documented "known limitation" — two simultaneously-animated
   entities spawned from the SAME `*.gta` file today share the same
   underlying GPU mesh buffers, tolerable today only because
   `AnimationSystem::Update()` processes every animator strictly
   sequentially on one thread — silently stops being a merely-cosmetic bug
   the moment Phase 6 parallelizes skinning, UNLESS the campaign explicitly
   commits to keeping each model's own `Dispatch()`/`WaitForJobs()` pair
   fully sequential relative to every other model's. Phase 6 v2 makes this
   an explicit, permanent rule for this campaign (not an implementation
   detail left to chance), and names the real prerequisite (per-instance
   GPU buffers) a future campaign would need before ever lifting it.
4. **Expanded Phase 4's classification table's coverage** to explicitly
   include the Render Graph module (`src/Renderer/RenderGraph/`), the
   Editor/ImGui viewer state, `MeshInstantiationSystem`'s GPU-facing
   catalogs, and — the one genuinely new verification item — an explicit
   requirement to confirm `SDL_GetPerformanceCounter()`/
   `SDL_GetPerformanceFrequency()` (the ONE clock this engine's whole
   `Profiling` module is built on, per `AGENTS.md`) are safe to call
   concurrently from multiple worker threads, since Phase 5's
   `JobScopeTimer` depends on that being true and v1 never stated it as a
   checked assumption. See Phase 4 v2 and Phase 5 v2.
5. **Noted, for context, that this campaign's two biggest sibling
   campaigns are now both fully complete**, not "in progress" as a
   generic engine-status assumption might suggest: the Render Graph
   campaign (`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`, Phases 1-8) and
   the Profiler campaign (`PROFILER_STRATEGY_v2.md`, Phases 0-8, including
   all four Phase-4 GPU-timestamp sub-phases) are both in production today
   — `Application::Run()` genuinely drives every real frame through
   `gte::rg::RenderGraph::Execute()` twice, and `FrameProfiler`/the
   Editor's "Profiler"/"Render Graph" panels are real, shipped, tested
   features. Nothing in this Job System campaign needs to design around
   either one being unfinished; both are simply mature, stable foundations
   to build on, exactly as v1 already (correctly) assumed. No action
   needed here beyond stating it plainly, since a future reader without
   this session's full context might otherwise wonder.

Phases 1, 2, 3, 7, and 8's own strategy files were re-checked against the
real code too. Phase 1 gets a small v2 for finding #2 above. Phases 2, 3,
7, and 8 held up with no factual corrections needed — no v2 was produced
for them; treat their existing (v1) files as still current.

---

## Child documents (read in this order)

| # | File | One-line purpose |
|---|------|-------------------|
| 1 | `JOBSYSTEM_PHASE1_CORE_THREADING_FOUNDATION_v2.md` | Stand up the worker-thread pool + the minimal `Dispatch`/`JobHandle`/`WaitForJobs` API — nothing runs in parallel against ANY engine subsystem yet, purely a self-contained, addressable primitive. |
| 2 | `JOBSYSTEM_PHASE2_PARALLEL_FOR_DISPATCH.md` (v1, unchanged) | The `Dispatch(func, count, jobs)`-shaped batch/parallel-for API from the brief's own example — turns "run one job" into "run 1,000 enemies split across 10 jobs". |
| 3 | `JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md` (v1, unchanged) | Job → job dependencies, so real multi-stage work can be expressed without the main thread manually stitching stages together with `WaitForJobs()` calls. |
| 4 | `JOBSYSTEM_PHASE4_THREAD_SAFETY_AUDIT_INTEGRATION_POINTS_v2.md` | The single most important, least glamorous phase: a full audit of every existing singleton/shared-state object against "what happens if a worker thread touches this", now with an expanded table and the cross-model sequencing rule cross-referenced. |
| 5 | `JOBSYSTEM_PHASE5_PROFILER_INTEGRATION_WORKER_TIMELINE_v2.md` | Extends `src/Profiling/` so a worker thread's own scopes show up as a per-worker timeline, now with an explicit SDL-clock thread-safety verification step. |
| 6 | `JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md` | The campaign's own production cut-over — migrate CPU vertex skinning onto the Job System, end to end, with corrected `AnimationSystem::Update()` references and an explicit, permanent cross-model sequencing rule closing a real data-race hazard. |
| 7 | `JOBSYSTEM_PHASE7_EDITOR_JOBS_PANEL.md` (v1, unchanged) | A new Editor "Jobs" panel rendering the per-worker timeline Phase 5 now collects. |
| 8 | `JOBSYSTEM_PHASE8_TESTING_HARDENING_BENCHMARKING.md` (v1, unchanged) | Stress/soak testing, ThreadSanitizer verification, a benchmark-mode integration, and the final "is this safe to leave running in every build" sign-off. |

The dependency-chain rule from v1 is unchanged: Phase *N* is not allowed to
start until Phase *N-1*'s own Definition of Done is genuinely satisfied,
verified, and merged.

---

## Step 1: The Goal (Where are we going?)

Unchanged from v1: give GreatTamanaEngine a general-purpose, engine-owned
Job System — a small, fixed-size pool of persistent OS worker threads plus
a `Dispatch()`/`JobHandle`/`WaitForJobs()` API that lets the main thread
farm out embarrassingly-parallel, per-frame CPU work, starting with CPU
vertex skinning (`Animation/VertexSkinning.h`'s `SkinVertices()`, invoked
today from `AnimationSystem::Update()`, `src/Game/Animation/
AnimationSystem.cpp` — see Revision Note 1 above for why this reference
now points there instead of `Game.cpp`).

"Done" for the whole campaign means all six conditions from v1 still hold,
with one refinement to condition 1:

1. A `gte::Jobs::JobSystem` singleton (mirroring `FrameProfiler::Instance()`'s
   own precedent — a lazily-initialized Meyers singleton, see Revision
   Note 2) owns a pool of `std::thread`-backed workers, sized from
   `std::thread::hardware_concurrency()`, and shuts down cleanly (RAII, no
   explicit "please remember to call Shutdown()" step anywhere) at process
   exit.
2. Application code can write the exact shape of code the brief's own
   example illustrates (unchanged from v1).
3. At least ONE real, already-shipped engine subsystem — CPU vertex
   skinning, orchestrated from `AnimationSystem::Update()` — is measurably
   faster on a multi-core machine because of this, with identical output
   to the old single-threaded code path, proven by parity tests, AND
   without introducing any new data race across simultaneously-animated
   entities (see Revision Note 3 / Phase 6 v2's new §3.6 — this is a new,
   explicit addition to the Definition of Done that v1 did not state).
4. The Editor's "Profiler" panel family gains a new "Jobs" panel (unchanged
   from v1).
5. Every existing Tier-1 test still passes with `GTE_ENABLE_JOB_SYSTEM=OFF`
   (unchanged from v1).
6. `AGENTS.md` gains a new "Job System" section (unchanged from v1).

---

## Step 2: The Situation / The Problem (Where are we now?)

Unchanged from v1 in substance. GreatTamanaEngine is completely
single-threaded by explicit, repeated design choice (`GpuMemoryTracker`,
`FrameProfiler`, `Registry`/ECS, `AssetDatabase`, `SdlMemoryTracker` all say
so directly, in code comments). The genuine per-frame CPU cost that
justifies this campaign is skeletal animation — bone pose evaluation, IK
solving, append-bone inheritance, CPU vertex skinning — run once per frame,
entirely on the main thread, via `AnimationSystem::Update()` (already
correctly named this way in v1's own master-doc Step 2 text; only Phase 6's
own child document had drifted).

One addition worth stating explicitly here, since it changes how "the
problem" should be read: this engine's ECS/Renderer/Animation architecture
was built with real, deliberate reuse discipline (shared caches, shared GPU
buffers per unique asset path) rather than one-buffer-per-instance
everywhere. That discipline is exactly why `Animation/VertexSkinning.h`'s
own logic is pure and parallelizable (Phase 4's audit) — but it also means
some of this engine's existing SHARING (e.g., `MeshInstantiationSystem`
caching one GPU `Mesh` per unique `*.gta` path, reused by every entity that
imports the same file — see `README.md`'s own documented limitation) was
only ever safe because everything ran on one thread. Phase 6 must inherit
that awareness explicitly, not discover it mid-implementation.

---

## Step 3: The Plan (How will we get there?)

Unchanged from v1 — the eight-phase shape, the diagrams, and the
must-have/nice-to-have/non-goal table all still hold. One addition to the
must-have list, direct consequence of Revision Note 3:

**11. (new) No parallel dispatch may ever create a genuine data race across
two different entities/animators that share underlying CPU or GPU memory.**
Concretely for Phase 6: two different `SkeletalAnimator` entities' own
`Dispatch()`/`WaitForJobs()sequences must never overlap in time within the
same `AnimationSystem::Update()` call, full stop, for as long as this
engine's own mesh-instance-sharing limitation exists (see Phase 6 v2,
§3.6). This is a hard rule, not a performance nice-to-have — a subtle,
load-bearing safety property this campaign must never regress, even in a
future phase that revisits animation parallelism for a performance win.

---

## Step 4: What We Will NOT Do (Focus)

Unchanged from v1, with one clarification: "we will not attempt automatic/
implicit parallelization" already covered the general case; Revision Note 3
adds a SPECIFIC instance of this rule that must never be relaxed by
accident — never let a future optimization pass "fan out" multiple models'
animation dispatches before a single shared wait, no matter how tempting
the performance upside looks, until the shared-GPU-buffer-per-instance
limitation is fixed as its own, separately-scoped campaign.

---

## Step 5: Their Role (What does this mean for you?)

Unchanged from v1, plus: when implementing Phase 6, treat §3.6 of that
phase's v2 document as load-bearing, not optional color commentary — it is
the difference between "a visually wrong but harmless bug" (today) and "a
memory-corrupting data race" (a naive parallel implementation). Read it
before writing a single line of `AnimationSystem::Update()`'s new
orchestration loop.
