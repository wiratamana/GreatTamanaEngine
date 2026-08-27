# Job System — Phase 1 (Core Threading Foundation) — Completion Report

Status: **DONE**. This report documents what was actually implemented,
verified, and fixed during this session, for whoever picks up Phase 2
(`JOBSYSTEM_PHASE2_PARALLEL_FOR_DISPATCH.md`) next.

Parent strategy: `JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md`.
Phase strategy followed: `JOBSYSTEM_PHASE1_CORE_THREADING_FOUNDATION_v2.md`.

---

## 1. What was built

A new, always-compiled `src/Jobs/` module (no dependency on
`GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL` — it's a core engine
module, same tier as `src/Profiling/`/`src/Animation/`):

```
src/Jobs/
    JobTypes.h    - JobFunction, detail::JobHandleState, JobHandle
    JobQueue.h    - detail::JobEntry, detail::JobQueue (fixed-capacity ring buffer)
    JobQueue.cpp
    JobSystem.h   - JobSystem (the worker-thread-pool singleton)
    JobSystem.cpp
tests/Jobs/
    JobQueueTests.cpp   - Tier 1, no real threads, always built
    JobSystemTests.cpp  - Tier 1, real threads (or inline fallback), always built
```

### 1.1 — `JobHandle` (`JobTypes.h`)

A cheap-to-copy token backed by `std::shared_ptr<detail::JobHandleState>`
(one atomic `pending` counter). `IsComplete()` is `pending == 0`. Exactly
ONE heap allocation happens — at `JobHandle` construction — never per job
scheduled against it, matching the "zero heap allocation in the
steady-state per-job path" requirement from the strategy document.

### 1.2 — `detail::JobQueue` (`JobQueue.h/.cpp`)

A fixed-capacity (4096 slots by default — see `JobSystem.cpp`'s
`kDefaultQueueCapacity`), mutex + `condition_variable`-guarded ring buffer.
`TryPush()` never blocks/grows — a full queue returns `false` and the
caller (`JobSystem::Schedule()`) falls back to running the job inline.
`WaitAndPop()` blocks until either an entry is available or `Shutdown()`
has been called AND the queue is empty (drains whatever was already queued
first). This class **always compiles**, regardless of
`GTE_ENABLE_JOB_SYSTEM` — it has no dependency on that switch at all, the
same "the class stays available/testable even when gated off" precedent
`SdlMemoryTracker`/`FrameProfiler` already established.

### 1.3 — `JobSystem` (`JobSystem.h/.cpp`)

- `Instance()` — a Meyers singleton (`static JobSystem instance;` inside
  the function), exactly mirroring `Profiling::FrameProfiler::Instance()`.
  Starts lazily, on first call — no worker thread exists until the very
  first real `Schedule()` call happens anywhere in the process. Since
  nothing in production calls `Schedule()` yet (this is Phase 1 — nothing
  is wired up to any real subsystem), a normal run of the engine today
  never spins up a single worker thread; only this module's own tests do.
- `Schedule(fn, payload, handle)` — increments `handle`'s pending counter,
  pushes a `JobEntry` onto the queue. If the queue is full (extremely
  unlikely at the default 4096 capacity for anything this engine does
  today), falls back to running `fn(payload)` immediately, inline,
  right there.
- `WaitForJobs(handle)` — blocks the calling thread until `handle` is
  complete. An already-complete handle returns immediately without
  blocking at all.
- `WorkerCount()` — real worker count when `GTE_ENABLE_JOB_SYSTEM` is ON
  (from `std::thread::hardware_concurrency()`, floored at 1), always
  exactly `1` when OFF. Never zero either way, so a future caller (Phase
  2's `Dispatch()`) can always safely divide work by it.
- **`GTE_ENABLE_JOB_SYSTEM`** — new CMake option, `ON` by default, exposed
  as a `PUBLIC` compile definition (`GTE_ENABLE_JOB_SYSTEM=$<BOOL:...>`)
  the same way `GTE_ENABLE_PROFILER`/`GTE_ENABLE_EDITOR` already are.
  `JobSystem`'s class/method signatures are IDENTICAL in both branches —
  only the two `.cpp`-level implementations differ (real worker threads
  vs. immediate-inline-synchronous execution). This is the same
  "genuinely zero std::thread creation in the OFF build, not just a
  runtime no-op" discipline `ScopeTimer`'s own `#if GTE_ENABLE_PROFILER`
  split already establishes.
- Also added: `find_package(Threads REQUIRED)` +
  `target_link_libraries(gte_core PUBLIC Threads::Threads)` in the root
  `CMakeLists.txt`, so `<thread>`/`<mutex>`/`<condition_variable>` link
  correctly on every toolchain (needed on MinGW/GCC; a no-op on MSVC).

---

## 2. A real bug found and fixed during this session

**Symptom:** `JobSystemTests.ManyJobsAgainstOneSharedHandleAllCompleteWithoutCorruption`
(256 jobs scheduled against one shared `JobHandle`, then `WaitForJobs()`)
hung intermittently — roughly 1 run in 4 under `--gtest_repeat` — never on
its own in isolation, only when run as part of a larger suite/repeated
run. This is exactly the kind of concurrency bug that can pass by luck on
a single test run, which is why Phase 3's own stress-repeat discipline
matters even this early.

**Root cause:** a classic `std::condition_variable` lost-wakeup race. The
original code decremented `JobHandleState::pending` via a bare atomic
`fetch_sub()` and then called `m_completionCondition.notify_all()`,
neither of which held `JobSystem::m_completionMutex` — the exact mutex
`WaitForJobs()` holds while checking its own predicate
(`handle.IsComplete()`) before calling the condition variable's blocking
`wait()`. Per `cppreference`'s own documented caveat: *"even though the
shared variable is atomic, it must be modified while owning the mutex to
correctly publish the modification to the waiting thread."* Without that,
a worker's decrement-then-notify could complete entirely in the window
between a waiter checking `IsComplete()` (still false) and that same
waiter actually registering itself with the condition variable — the
notification fires, finds no registered waiter, and is lost; the waiter
then blocks forever.

**Fix:** bracket the `pending.fetch_sub()` call with
`std::lock_guard<std::mutex> lock(m_completionMutex)` in BOTH places a
worker/the calling thread can decrement a handle's pending count:
`JobSystem::WorkerLoop()` (the real worker path) and `JobSystem::Schedule()`'s
own full-queue fallback path (the rare synchronous-inline path). The
`notify_all()` call itself does not need the lock (and is deliberately
kept outside it, after the `lock_guard` releases, to avoid the woken
thread immediately blocking on a mutex we're still holding).

**Verification:**
- Before the fix: `--gtest_repeat=50` on `JobSystemTests.*` hung inside
  ~15–25% of iterations (confirmed reproducible across several runs).
- After the fix: `--gtest_repeat=100` on `JobSystemTests.*:JobQueueTests.*`
  — 100/100 iterations passed, zero hangs, zero failures.
- Full suite (all 661 tests) passes cleanly after the fix, both with
  `GTE_ENABLE_JOB_SYSTEM=ON` (default MSVC/Ninja build) and
  `GTE_ENABLE_JOB_SYSTEM=OFF` (a completely separate configure+build using
  MinGW/GCC instead of the project's usual MSVC toolchain, as an extra
  cross-toolchain sanity check specifically because this is the engine's
  first genuinely multi-threaded code) — 660 passed + 1 pre-existing
  machine-gated smoke test skipped (`PmxLoaderRealModelSmokeTest`, skips
  when the real MMD test model isn't present on the machine — unrelated to
  this work).

This fix, and the reasoning behind it, is now documented in `AGENTS.md`'s
new "Job System" section so it isn't silently reintroduced by a future
edit to `WorkerLoop()`/`Schedule()`.

---

## 3. Tests added

- `tests/Jobs/JobQueueTests.cpp` (5 tests) — FIFO push/pop order, the
  full-queue `TryPush()` failure case, slot reuse after draining,
  `Shutdown()`'s "drain what's already queued, then report empty"
  behavior. No real threads involved — pure single-threaded exercise of
  the ring buffer.
- `tests/Jobs/JobSystemTests.cpp` (6 tests) — `WorkerCount()` is always
  ≥ 1; a default-constructed `JobHandle` is already complete;
  `Schedule()`+`WaitForJobs()` actually runs the job; 20 iterations × 256
  jobs against one shared handle all complete with no missed/corrupted
  writes (this is the test that caught the lost-wakeup bug above); a
  copied `JobHandle` observes the same completion state as the original;
  two independent handles don't interfere with each other.

Both files are added to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`
**unconditionally** (not gated behind any `if(GTE_ENABLE_...)` block) —
`JobQueue`/`JobSystem` always compile regardless of
`GTE_ENABLE_JOB_SYSTEM`, so their tests always build too, the same
"always built" bucket `Profiling/FrameProfilerTests.cpp`/`ScopeTimerTests.cpp`
already occupy.

---

## 4. Definition of Done — checked against Phase 1's own strategy doc

1. ✅ `gte::Jobs::JobSystem` exists, boots a real worker-thread pool (when
   `GTE_ENABLE_JOB_SYSTEM=ON`).
2. ✅ Can run a single arbitrary job to completion observably via
   `WaitForJobs()`.
3. ✅ Zero heap allocation in its steady-state per-job path (the queue is
   fixed-capacity; `JobHandle`'s one allocation is at construction, not
   per-`Schedule()`-call).
4. ✅ Compiles and passes its own Tier-1 test suite with
   `GTE_ENABLE_JOB_SYSTEM` both `ON` and `OFF` (verified above, including
   under a second, independent toolchain for the OFF configuration).
5. ✅ Used by nothing else in the engine yet (no production call site —
   confirmed by grepping the codebase; only `tests/Jobs/*.cpp` reference
   `Jobs::JobSystem`/`Jobs::JobHandle` at all).

Phase 1 is complete. Phase 2 (`JOBSYSTEM_PHASE2_PARALLEL_FOR_DISPATCH.md`)
may now begin.

---

## 5. Files changed/added this session

- `src/Jobs/JobTypes.h` (new)
- `src/Jobs/JobQueue.h` (new)
- `src/Jobs/JobQueue.cpp` (new)
- `src/Jobs/JobSystem.h` (new)
- `src/Jobs/JobSystem.cpp` (new)
- `tests/Jobs/JobQueueTests.cpp` (new)
- `tests/Jobs/JobSystemTests.cpp` (new)
- `CMakeLists.txt` — new `GTE_ENABLE_JOB_SYSTEM` option, new `src/Jobs/*`
  sources added to `gte_core`, new `GTE_ENABLE_JOB_SYSTEM` compile
  definition, new `find_package(Threads REQUIRED)` +
  `Threads::Threads` link.
- `tests/CMakeLists.txt` — new `Jobs/JobQueueTests.cpp`/`JobSystemTests.cpp`
  entries (unconditional), plus matching documentation comments in the
  file's own test-taxonomy header block.
- `AGENTS.md` — new "Job System" section (lazy-singleton-startup fact,
  the `GTE_ENABLE_JOB_SYSTEM` ON/OFF contract, the zero-allocation
  guarantee and its one documented exception, the lost-wakeup bug and its
  fix, the "tests are always built" rule, and the stress-repeat
  discipline for future concurrent tests in this module).

No existing file's behavior changed — this is a purely additive phase,
consistent with its own Definition of Done ("used by nothing else in the
engine yet").
