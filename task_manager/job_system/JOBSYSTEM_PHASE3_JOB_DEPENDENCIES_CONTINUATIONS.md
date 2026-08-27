# Job System — Phase 3: Job Dependencies / Continuations

Parent document: `JOBSYSTEM_PHASE0_MASTER_STRATEGY.md` (read first).
Previous phase: `JOBSYSTEM_PHASE2_PARALLEL_FOR_DISPATCH.md` (must be done
— `Dispatch()`/`ComputeBatchRanges()` exist, tested, unused by any real
subsystem).
Next phase: `JOBSYSTEM_PHASE4_THREAD_SAFETY_AUDIT_INTEGRATION_POINTS.md`.

**Definition of Done for this phase (gates Phase 4):** a job (or a whole
`Dispatch()` batch) can be scheduled so that it does not begin running
until one or more OTHER `JobHandle`s have completed, without the main
thread having to call `WaitForJobs()` in between and re-enter the scheduler
by hand; this continuation mechanism is built entirely on top of Phase
1/2's existing primitives (no second scheduler, no second queue); and it
has its own Tier-1 graph-resolution tests plus a real multi-stage
execution test proving strict ordering is honored under real concurrency.

---

## Step 1: The Goal (Where are we going?)

Let calling code express a genuine multi-stage pipeline — the shape Phase
6's real consumer will actually need (sample animation pose → solve IK →
apply append-bone inheritance → compute skinning matrices → skin
vertices — see `Animation/AnimationPoseEvaluator.h`'s own documented
fixed order) — as a small graph of jobs with real data dependencies,
rather than forcing the main thread to manually call `WaitForJobs()`
between every single stage (which would serialize the ENTIRE pipeline back
onto the main thread between stages, throwing away most of the benefit of
having parallelized any of it in the first place, if a future workload ever
has more than one genuinely independent stage of parallel work per frame).

Concretely:

```cpp
namespace gte::Jobs {

// Schedules `fn(payload)` to run only once EVERY handle in `dependencies`
// has completed - never before. If every dependency is ALREADY complete at
// call time (the common case for the first stage of a pipeline, or for a
// dependency that finished earlier in the same frame), this degrades to an
// ordinary Schedule() with no continuation bookkeeping at all.
void ScheduleAfter(JobFunction fn, void* payload, std::span<JobHandle* const> dependencies, JobHandle& handle);

// The Dispatch() equivalent - every batch job in this dispatch waits on
// `dependencies` before it may run.
void DispatchAfter(BatchJobFunction fn, std::uint32_t itemCount, void* payload,
    std::span<JobHandle* const> dependencies, JobHandle& handle, std::uint32_t minItemsPerBatch = 1);

} // namespace gte::Jobs
```

This is deliberately a THIN, general mechanism (arbitrary dependency sets),
not a full authored "job graph" data structure with named nodes/edges a
future Editor tool would visualize as a graph (that is explicitly out of
scope — see "What We Will NOT Do"). It exists purely so the MAIN THREAD can
express "run stage B after stage A finishes, but don't make ME sit and wait
for A before I'm even allowed to queue up B" — the actual, concrete
motivation being that stage B's OWN `Schedule()`/`Dispatch()` call can
happen immediately, back-to-back with stage A's, letting the worker pool
pick up B's jobs the instant A's dependencies clear, with no round-trip
back through the main thread in between.

---

## Step 2: The Situation / The Problem (Where are we now?)

After Phase 2, the ONLY way to sequence two batches of work is:

```cpp
Jobs::JobHandle stageA;
Jobs::Dispatch(RunStageA, count, payload, stageA);
Jobs::WaitForJobs(stageA);       // main thread blocks here, doing nothing else
Jobs::JobHandle stageB;
Jobs::Dispatch(RunStageB, count, payload, stageB);
Jobs::WaitForJobs(stageB);
```

This "works" for a SINGLE animated model (Phase 6's actual initial scope),
but the moment there is more than one independent per-frame parallel
pipeline running in the same frame (e.g., in a hypothetical future: several
different rigged models each running their own sample→IK→append→skin
pipeline, or a future non-animation workload running alongside animation),
forcing every stage boundary through a blocking `WaitForJobs()` call
serializes work that has no real reason to be serialized — model A's stage
2 could easily be running on the worker pool at the same time as model B's
stage 1, but the naive "wait between every stage" pattern above prevents
that entirely, because the MAIN THREAD (the only thing currently able to
queue up the next stage) is blocked waiting rather than free to dispatch
more work.

The actual problem to solve, precisely: **let the main thread describe an
entire multi-stage frame's worth of dependent work UP FRONT, in one
sequence of non-blocking calls, and let the worker pool itself figure out
the correct ordering as dependencies clear — with exactly ONE blocking
`WaitForJobs()` call at the very end of the whole pipeline**, not one per
stage.

---

## Step 3: The Plan (How will we get there?)

### 3.1 — File layout addition

```
src/Jobs/
    JobContinuation.h/.cpp   - ScheduleAfter()/DispatchAfter(), the small continuation-tracking bookkeeping
tests/Jobs/
    JobContinuationTests.cpp - real JobSystem::Instance(); strict-ordering + fan-in/fan-out tests
```

### 3.2 — How a continuation actually works, mechanically

No second scheduler and no second queue are introduced — a continuation is
built ENTIRELY out of Phase 1's existing `JobEntry`/`JobQueue`/worker
threads, using one additional small primitive:

```cpp
// src/Jobs/JobContinuation.h (internal detail, not part of the public API)
struct PendingContinuation {
    JobFunction fn;
    void* payload;
    std::atomic<std::uint32_t> unmetDependencyCount; // starts at dependencies.size()
    JobHandle handle; // the continuation's OWN handle, already incremented once
};
```

`ScheduleAfter(fn, payload, dependencies, handle)`:
1. If `dependencies` is empty, or every one of them is ALREADY complete
   (checked via `JobHandle::IsComplete()` at call time — the common
   "dependency already finished earlier this frame" case), this is
   EXACTLY `Schedule(fn, payload, handle)` — zero extra bookkeeping, zero
   extra allocation, falls straight through to Phase 1's primitive.
2. Otherwise, a `PendingContinuation` is heap-allocated (same documented,
   bounded-per-call exception as Phase 2's `DispatchJobContext` — see that
   phase's own 3.3 for the precedent this follows), its
   `unmetDependencyCount` set to however many of `dependencies` are NOT
   already complete, and a small "continuation watcher" callback is
   attached to EACH of those still-pending dependency handles (see 3.3,
   "how a handle notifies its watchers") — when the LAST of them fires,
   the watcher calls `JobSystem::Instance().Schedule(fn, payload, handle)`
   for real, and the `PendingContinuation` is freed.

### 3.3 — Extending `JobHandle` with watchers (the one real design change)

Phase 1's `JobHandle` is just a `std::shared_ptr<std::atomic<uint32_t>>` —
enough to ask "is it done" (poll `IsComplete()`), but not enough to be
TOLD "the instant it becomes done, please run this". This phase adds a
small, fixed-capacity notification list to the shared state a `JobHandle`
already points at:

```cpp
// src/Jobs/JobTypes.h (extended)
struct JobHandleState {
    std::atomic<std::uint32_t> pendingCount{0};
    // A small, fixed-capacity list of "call this when pendingCount hits
    // zero" callbacks - guarded by its own small spinlock/mutex (contention
    // here is expected to be extremely rare: only a genuine multi-stage
    // dependency chain ever populates this list at all, and only while a
    // dependency handle is still in flight).
    std::array<ContinuationWatcher, kMaxWatchersPerHandle> watchers{};
    std::atomic<std::size_t> watcherCount{0};
    std::mutex watcherMutex;
};
```

`kMaxWatchersPerHandle` (a small fixed constant, e.g. 8 — mirroring this
codebase's existing "small, fixed, generously-sized capacity, not a
growable container" convention yet again) bounds how many DIFFERENT
continuations may depend on the exact same single handle. The worker
thread that performs the LAST decrement of `pendingCount` down to zero
(the one whose `fetch_sub` returned `1`, i.e. it observed the count
transition from 1 to 0 — this is what prevents a race where two different
workers each think they were "the last one") is the one responsible for
firing every registered watcher, synchronously, before moving on to pick
up its next queued job. If `watcherCount` would exceed
`kMaxWatchersPerHandle`, `ScheduleAfter()` falls back to registering a
periodic poll instead (a low-overhead escape hatch, documented as a rare
path — see 3.5), rather than silently dropping a continuation.

### 3.4 — Fan-in (waiting on MULTIPLE handles) and fan-out (multiple
continuations off ONE handle)

Both are supported by construction, not as special cases:

- **Fan-out** (several different continuations all depending on the same
  upstream handle — e.g. several models' stage-2 jobs all waiting on a
  single shared "frame setup" stage) is just several different
  `ContinuationWatcher`s registered against the same `JobHandleState`
  (bounded by `kMaxWatchersPerHandle`, see 3.3).
- **Fan-in** (one continuation waiting on SEVERAL upstream handles — e.g.
  Phase 6's real pipeline waiting on both "pose sampling done" AND "IK
  solving done" before append-inheritance can run) is exactly what
  `PendingContinuation::unmetDependencyCount` exists for: it starts at the
  number of not-yet-complete dependencies, and the continuation only
  actually schedules once EVERY one of them has independently decremented
  it to zero via its own watcher callback.

### 3.5 — Testing

- **`JobContinuationTests.cpp` (real `JobSystem::Instance()`, deterministic
  assertions despite real concurrency)**:
  - **Strict ordering, single dependency**: schedule stage A (writes a
    known "stage A ran" flag into a caller-owned buffer), `ScheduleAfter`
    stage B depending on A's handle (asserts the "stage A ran" flag is
    ALREADY set when B runs — proving B genuinely never starts before A
    finishes, not just "usually" finishes after by luck of scheduling).
    Run this many times in a loop (not just once) — ordering bugs in
    concurrent code are exactly the kind of thing that passes by luck on a
    single run and must be stress-repeated to catch reliably.
  - **Fan-in**: two independent upstream stages, one continuation
    depending on both — asserts the continuation never runs until BOTH
    upstream "ran" flags are set.
  - **Fan-out**: one upstream stage, several independent continuations all
    depending on it — asserts every one of them sees the upstream "ran"
    flag set, and that they may run in any relative order AMONG
    THEMSELVES (never asserts a specific order between siblings, only that
    each individually respects its own single dependency).
  - **Already-complete dependency short-circuit**: call `WaitForJobs()` on
    the upstream handle FIRST (so it's already complete), THEN call
    `ScheduleAfter()` against it — asserts the continuation still runs
    correctly (proving the "falls straight through to `Schedule()`" fast
    path in 3.2, point 1, is correct, not just an unverified optimization).
  - **`kMaxWatchersPerHandle` overflow path**: registers more continuations
    against a single slow-to-complete handle than the fixed capacity, and
    asserts every single one of them STILL eventually runs (via the
    documented polling fallback) — proving the bounded-capacity design
    degrades gracefully rather than silently losing a continuation.

---

## Step 4: What We Will NOT Do (Focus)

- **No general-purpose, arbitrarily-large, named job GRAPH data structure**
  a future tool could load/visualize/edit independently of actually running
  it. This phase is deliberately just "a handle can notify dependents when
  it completes" — the minimum mechanism the master strategy's Phase 6
  target workload (a short, fixed, four-stage animation pipeline) actually
  needs, not a general graph-authoring system.
- **No priorities, no cancellation, no re-queuing/retry of a failed
  dependency.** A dependency handle either completes normally (as every
  job in this engine is expected to, per Phase 1's "no exception handling"
  rule) or the engine has a bug to fix at the source — this phase does not
  add error/cancellation plumbing on top of that.
- **No automatic dependency INFERENCE** (e.g. "figure out that job B reads
  memory job A writes, and insert the dependency automatically"). Every
  dependency in this phase is EXPLICIT, spelled out by the calling code —
  automatic data-flow dependency inference is a research-grade feature this
  engine has no need for at its current scale.
- **No changes to `JobQueue`/`WorkerLoop()` themselves.** Continuations are
  built entirely on the EXISTING scheduling primitive (a continuation
  "fires" by literally calling the existing `Schedule()`) — no second
  execution path, no second thread pool.
- **Still no real engine subsystem wired up.** Exactly like Phases 1–2 —
  this stays proven in isolation until Phase 4's safety audit exists.

---

## Step 5: Their Role (What does this mean for you?)

- Implement `JobHandleState`'s watcher list and the "last decrementer fires
  watchers" rule FIRST, and write a small, direct, single-purpose test
  proving that exactly ONE thread ever fires a given handle's watchers
  (never zero, never more than once) before building `ScheduleAfter()`/
  `DispatchAfter()` on top of it — this is the one piece of genuinely
  tricky concurrent bookkeeping in this whole phase, and it deserves to be
  isolated and proven correct on its own before anything is layered above
  it.
- Stress-repeat the ordering tests (3.5) in a loop (e.g. 100+ iterations
  per test run) specifically BECAUSE concurrent ordering bugs are exactly
  the class of bug that can pass on a single run by luck — this mirrors
  the same "never trust a single passing run for genuinely concurrent
  code" discipline Phase 8's stress-testing work will apply at a larger
  scale later.
- Update `AGENTS.md`'s "Job System" section with: `ScheduleAfter()`/
  `DispatchAfter()`, the "explicit dependencies only, no inference" rule,
  and the `kMaxWatchersPerHandle` bounded-capacity/fallback behavior.
- Once every test in 3.5 is green and has been stress-repeated without a
  single observed ordering violation, move on to
  `JOBSYSTEM_PHASE4_THREAD_SAFETY_AUDIT_INTEGRATION_POINTS.md` — the
  gating phase that decides what any of this machinery is actually allowed
  to touch.
