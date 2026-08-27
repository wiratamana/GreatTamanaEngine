# Job System — Phase 1: Core Threading Foundation — v2

Parent document: `JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md` (read first).
Next phase: `JOBSYSTEM_PHASE2_PARALLEL_FOR_DISPATCH.md` (v1, still current).

**Definition of Done for this phase (gates Phase 2):** unchanged from v1 —
a `gte::Jobs::JobSystem` exists, boots a real worker-thread pool, can run a
single arbitrary job to completion observably via `WaitForJobs()`, has zero
heap allocation in its steady-state per-job path, compiles and passes its
own Tier-1 test suite with `GTE_ENABLE_JOB_SYSTEM` both `ON` and `OFF`, and
is used by nothing else in the engine yet.

---

## Revision Notes (2nd Iteration / v2)

Only one substantive change from v1, a precision fix, not a design
change: **the singleton's startup timing is now stated exactly, instead of
loosely.** v1's own master strategy said the `JobSystem` "starts up once
near Application's own construction." That phrasing doesn't match what
Phase 1 itself actually designs (§3.4 below, unchanged): a Meyers
singleton (`static JobSystem instance;` inside `Instance()`), the exact
pattern `FrameProfiler::Instance()` already uses in this codebase. A
Meyers singleton is, by construction, lazily initialized on its FIRST call
— there is no eager "near Application's own construction" step anywhere
unless something explicitly calls `JobSystem::Instance()` that early, and
nothing in this campaign's own plan ever does that deliberately.

This matters for one concrete, practical reason: until Phase 6 ships and
some real call site (`AnimationSystem::Update()`) actually calls
`Dispatch()`/`Schedule()` for the first time, the worker pool simply does
not exist yet, even after Phases 1-5 are otherwise "done" and merged. That
is completely fine and intended — Phases 1-5 are explicitly scoped to be
provable in isolation via their OWN test code (which DOES call
`JobSystem::Instance()`, and so DOES cause the pool to spin up, for the
duration of the test binary's own process). A future reader should not be
surprised that a build with Phases 1-5 merged but Phase 6 not yet started
never actually creates a single worker thread during normal gameplay.

No other part of Phase 1's design (§3.1 through §3.7 below) needed any
change — the file layout, `JobQueue`, `JobSystem::Schedule()`/
`WaitForJobs()`, the `GTE_ENABLE_JOB_SYSTEM` switch, and the testing
strategy all held up unchanged against the real codebase's own
`GTE_ENABLE_PROFILER`/`FrameProfiler` precedent this phase is modeled on.

---

## Step 1: The Goal (Where are we going?)

Unchanged from v1: the smallest possible, fully-correct, fully-tested
threading primitive — `JobHandle`, `JobFunction`, `JobSystem::Schedule()`/
`WaitForJobs()`/`WorkerCount()`. See v1's own code sketch; it is
reproduced below verbatim since nothing in it changed.

```cpp
namespace gte::Jobs {

class JobHandle {
public:
    JobHandle();
    bool IsComplete() const;
private:
    std::shared_ptr<std::atomic<std::uint32_t>> m_pending;
    friend class JobSystem;
};

using JobFunction = void(*)(void* payload);

class JobSystem {
public:
    static JobSystem& Instance(); // Meyers singleton - lazy, first-call init (see Revision Notes above).

    void Schedule(JobFunction fn, void* payload, JobHandle& handle);
    void WaitForJobs(JobHandle& handle);
    std::size_t WorkerCount() const;
};

} // namespace gte::Jobs
```

`Dispatch()` remains explicitly out of scope for this phase (Phase 2's
job).

---

## Step 2: The Situation / The Problem (Where are we now?)

Unchanged from v1. Nothing in this engine creates a `std::thread` anywhere
today; `Application::Run()` remains a single, synchronous loop end to end
(confirmed directly against the real `Application.cpp` attached to this
session — `PollEvents → Game::Update → two RenderGraph::Execute() calls →
Present`, all issued from one thread, with only Vulkan's own GPU-side
pipelining, never CPU threading, providing any overlap). The three design
questions from v1 (worker count sizing, job payload shape, single shared
queue vs. work-stealing) are unchanged and still correctly answered by
v1's own reasoning.

---

## Step 3: The Plan (How will we get there?)

Unchanged from v1 in every particular — file layout (§3.1), `JobQueue`
(§3.2), `kMaxQueuedJobs` sizing (§3.3), `JobSystem`'s worker
threads/`Schedule()`/`WaitForJobs()` (§3.4), the `GTE_ENABLE_JOB_SYSTEM`
switch (§3.5), the full-queue fallback behavior (§3.6), and the testing
strategy (§3.7). Re-verified each of these against the real `CMakeLists.txt`
(confirming the `GTE_ENABLE_PROFILER`/`GTE_ENABLE_EDITOR` `PUBLIC` compile-
definition pattern this phase's §3.5 models itself on is accurately
described) and against `FrameProfiler.h`'s real singleton shape (confirming
the Meyers-singleton pattern this phase's §3.4 models itself on is
accurately described). No changes needed to any of it. Refer to the
original `JOBSYSTEM_PHASE1_CORE_THREADING_FOUNDATION.md` for the full text
of §3.1-§3.7 — it remains accurate and is not reproduced a second time here
to avoid two documents silently drifting apart over time; only this file's
Revision Notes section above is new/authoritative over it.

---

## Step 4: What We Will NOT Do (Focus)

Unchanged from v1.

---

## Step 5: Their Role (What does this mean for you?)

Unchanged from v1, with one addition: when documenting this phase's own
new `AGENTS.md` "Job System" section (last bullet of v1's Step 5), state
the lazy-singleton-startup fact from this file's Revision Notes explicitly
— a future contributor debugging "why doesn't a profiling capture ever
show any worker activity before Phase 6 lands" should be able to find the
answer in `AGENTS.md` directly, not have to re-derive it from first
principles.
