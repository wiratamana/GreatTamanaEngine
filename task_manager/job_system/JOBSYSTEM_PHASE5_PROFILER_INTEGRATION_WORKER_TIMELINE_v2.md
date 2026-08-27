# Job System — Phase 5: Profiler Integration (Worker Timeline) — v2

Parent document: `JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md` (read first).
Previous phase: `JOBSYSTEM_PHASE4_THREAD_SAFETY_AUDIT_INTEGRATION_POINTS_v2.md`
(must be done — including its new §3.2 SDL-clock verification item).
Next phase: `JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md`.

**Definition of Done for this phase (gates Phase 6):** unchanged from v1 —
a job running on any worker thread can safely record its own named CPU
scope, `FrameProfiler`'s internal state is upgraded to make this race-free
without breaking a single existing single-threaded call site or test, the
resulting per-thread history is exposed through a new reshape function
ready for Phase 7, and every existing `Profiling`/`Editor` test still
passes — WITH one addition: the SDL-clock concurrency assumption this
phase depends on (§3.3 below) must have been explicitly verified by Phase
4 before this phase's own multi-threaded test (§3.5) is trusted as
meaningful, rather than merely "passed by luck."

---

## Revision Notes (2nd Iteration / v2)

Re-checked this phase's design against the real `FrameProfiler.h/.cpp`,
`ProfilingTypes.h`, and `ScopeTimer.h` attached to this session. The
overall design (a separate, additive `WorkerJobSample`/`workerJobs` log,
a single atomic-fetch-and-increment write path via a new
`RecordWorkerJobSample()`, and a new `JobScopeTimer`/
`GTE_PROFILE_JOB_SCOPE` mirroring `ScopeTimer`/`GTE_PROFILE_SCOPE`) held
up completely — it composes cleanly with the real `FrameSample`/
`FrameProfiler::BeginFrame()`/`EndFrame()` shown in the attached source,
and correctly avoids touching `RecordCpuScope()`'s existing, genuinely
unsynchronized linear-scan-plus-`strcmp()` logic at all. No design change
was needed. One addition, carried over from Phase 4 v2's new finding:

- **§3.3 gains an explicit dependency on, and citation of, Phase 4 v2's
  new SDL-clock-concurrency verification item.** v1 simply had
  `JobScopeTimer` call `SDL_GetPerformanceCounter()`/
  `SDL_GetPerformanceFrequency()` from a worker thread without ever
  stating that doing so needed checking. This is now made explicit as a
  precondition for this phase, not silently assumed.
- **A small clarification to §3.5's multi-threaded test**: it should
  specifically use MORE than one concurrently-running test thread calling
  `SDL_GetPerformanceCounter()`/`GTE_PROFILE_JOB_SCOPE` at literally the
  same wall-clock moment (not just "eventually, on different threads,
  possibly serialized by scheduling luck") to actually exercise the
  concurrency claim Phase 4 v2 asks to be verified — this is a
  strengthening of an existing test's intent, not a new test.

Nothing else in this phase's design changed. §3.1 (`WorkerJobSample`
data model), §3.2 (the atomic-reservation write path), §3.4 (the
`WorkerTimelineData.h/.cpp` reshape), and the rest of §3.5 remain exactly
as v1 specified them.

---

## Step 1: The Goal (Where are we going?)

Unchanged from v1 — the per-worker timeline picture, the
`GTE_PROFILE_JOB_SCOPE("...")` macro, the thread-safe
`RecordWorkerJobSample()` path, and `Profiling::BuildWorkerTimelinePoints()`.

---

## Step 2: The Situation / The Problem (Where are we now?)

Unchanged from v1. Confirmed directly against the attached
`FrameProfiler.cpp`: `RecordCpuScope()` is indeed a plain, unsynchronized
linear scan (`for (std::size_t i = 0; i < m_current.cpuScopeCount; ++i)`)
plus a non-atomic `++m_current.cpuScopeCount` — exactly the kind of code a
second concurrent caller would corrupt, validating v1's own reasoning for
why a SEPARATE, genuinely thread-safe write path is required rather than
wrapping the existing one in a lock (which would also, per v1's own
argument, produce the wrong DATA SHAPE — a flat aggregate instead of a
per-worker timeline — even if it were made safe).

---

## Step 3: The Plan (How will we get there?)

### 3.1 — A separate, additive data model: `WorkerJobSample` (unchanged)

No changes to the `WorkerJobSample`/`kMaxWorkerJobSamplesPerFrame`/
`FrameSample::workerJobs` design from v1.

### 3.2 — Making the WRITE path thread-safe, narrowly (unchanged)

No changes to the atomic-fetch-and-increment `RecordWorkerJobSample()`
design from v1. Confirmed this composes correctly with the real
`FrameProfiler::BeginFrame()`/`EndFrame()` shown in the attached source:
`m_current` is fully reset (`m_current = FrameSample{}`) only inside
`BeginFrame()`, which — per this campaign's own caller obligations
(Phase 4) — never runs while a job from a PRIOR dispatch could still be
mid-flight, since every dispatch is required to be fully drained via
`WaitForJobs()` before the frame it belongs to ends. No race between a
worker thread's write and the main thread's `BeginFrame()`/`EndFrame()`
reset is possible under this campaign's own rules.

### 3.3 — `GTE_PROFILE_JOB_SCOPE`: the job-body-facing macro/RAII helper

Unchanged from v1's design, with the explicit precondition (new in v2):
**before relying on this in production, Phase 4 v2's §3.2 SDL-clock
concurrency verification must have been performed and recorded.**
`JobScopeTimer`'s constructor/destructor call
`SDL_GetPerformanceCounter()` from whatever worker thread is currently
running the job body wrapped in `GTE_PROFILE_JOB_SCOPE(...)` — this is
the ONE place in this whole campaign where SDL's own timer API is called
from a thread other than the main thread, and it is worth this phase's own
document saying so explicitly, not leaving it to be discovered by
inference from Phase 4's table alone.

The rest of §3.3 (the `WorkerIndexForCurrentThread()` accessor added to
`JobSystem`, `JobScopeTimer`'s two behavioral differences from
`ScopeTimer`, and the "never call `GTE_PROFILE_SCOPE` from a job body,
never call `GTE_PROFILE_JOB_SCOPE` from the main thread" rule) is
unchanged from v1.

### 3.4 — The reshape: `WorkerTimelineData.h/.cpp` (unchanged)

No changes from v1.

### 3.5 — Testing (one strengthening, rest unchanged)

All of v1's testing plan stands. One addition: the multi-threaded test in
`tests/Profiling/JobScopeTimerTests.cpp` should specifically arrange for
several test-owned threads to call `GTE_PROFILE_JOB_SCOPE`-recorded work
that OVERLAPS in wall-clock time (e.g. each thread does a short, real
amount of CPU work inside its own scope, started at approximately the
same moment via a shared start barrier/condition variable, rather than
threads that happen to run one after another due to OS scheduling) —
this is what actually exercises "does `SDL_GetPerformanceCounter()`
behave correctly when called concurrently from multiple threads at once,"
the exact claim Phase 4 v2 asks to be verified, rather than a weaker test
that could pass even if concurrent calls were subtly unsafe but simply
never happened to occur at the exact same instant during a given test
run.

---

## Step 4: What We Will NOT Do (Focus)

Unchanged from v1, in full.

---

## Step 5: Their Role (What does this mean for you?)

Unchanged from v1's list, plus: confirm Phase 4 v2's SDL-clock
verification item is actually done (not just theoretically true) before
treating this phase's multi-threaded test as sufficient proof of
correctness — a test that happens to pass on today's specific SDL3
build/platform is not the same as a documented, intentional confirmation
of the underlying contract this phase depends on.
