# Job System — Phase 3 (Job Dependencies / Continuations) — Completion Report

Status: **DONE**. This report documents what was actually implemented,
verified, and fixed during this session, for whoever picks up Phase 4
(`JOBSYSTEM_PHASE4_THREAD_SAFETY_AUDIT_INTEGRATION_POINTS_v2.md`) next.

Parent strategy: `JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md`.
Phase strategy followed: `JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md`.
Previous phases: `JOB_SYSTEM_PHASE1_COMPLETION_REPORT.md` (Core Threading
Foundation), `JOB_SYSTEM_PHASE2_COMPLETION_REPORT.md` (Parallel-For / Batch
Dispatch).

---

## 1. What was built

Two new files added to the existing, always-compiled `src/Jobs/` module (no
dependency on `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL`, same tier as
Phases 1-2's own files), plus targeted changes to `JobTypes.h`/`JobSystem.h/.cpp`:

```
src/Jobs/
    JobContinuation.h   - ScheduleAfter(), DispatchAfter()
    JobContinuation.cpp
tests/Jobs/
    JobContinuationTests.cpp   - Tier 1, real JobSystem::Instance(), always built
```

### 1.1 — `JobHandleState` gains a watcher list (`JobTypes.h`)

`detail::JobHandleState` (previously just an atomic `pending` counter) now
also carries a small, FIXED-CAPACITY (`detail::kMaxWatchersPerHandle`, 8)
array of `{WatcherFunction, void* context}` pairs, guarded by its own
`watcherMutex` (separate from `JobSystem::m_completionMutex`):

- `AddWatcher(fn, context)` — registers `fn(context)` to run once `pending`
  reaches zero. If `pending` is ALREADY zero, calls `fn` immediately,
  synchronously, right there (never stored). Returns `false` (storing
  nothing) if the array is already full.
- `FireWatchers()` — called by whichever thread performs the decrement of
  `pending` down to zero; fires every registered watcher exactly once,
  then clears the list (so a later reuse of the same handle starts clean).

Both are inline methods (no new `.cpp`), following the header's own
existing all-inline style. The pending-zero check and the watcher-list
mutation happen under the SAME `watcherMutex` `FireWatchers()` also takes —
this is what closes the exact class of lost-wakeup race Phase 1's own
`m_completionMutex`-bracketed decrement already guards against for
`WaitForJobs()`, applied here to watcher registration instead.

`JobHandle` gained two new public methods:
- `AddCompletionWatcher(fn, context)` — thin forwarder to
  `m_state->AddWatcher()`.
- `AddPendingUnit()` — manually increments `pending` by one, representing a
  unit of DEFERRED work accepted but not yet actually scheduled. See §1.3.

### 1.2 — `JobSystem::ScheduleAlreadyPending()` (`JobSystem.h/.cpp`)

A new method, parallel to `Schedule()`, for both `GTE_ENABLE_JOB_SYSTEM`
branches: runs `fn(payload)` against `handle` exactly like `Schedule()`,
EXCEPT it does **not** increment `handle`'s pending counter — the caller
already did that via `AddPendingUnit()`. Still decrements exactly once
(and fires watchers on transition to zero) when the job finishes. This
is what lets a deferred continuation's "placeholder" pending unit and its
eventual real execution net out to exactly one unit of work, with no
double-counting and no transient false-complete window.

### 1.3 — `ScheduleAfter()` / `DispatchAfter()` (`JobContinuation.h/.cpp`)

```cpp
void ScheduleAfter(JobFunction fn, void* payload,
    std::span<JobHandle* const> dependencies, JobHandle& handle);

void DispatchAfter(BatchJobFunction fn, std::uint32_t itemCount, void* payload,
    std::span<JobHandle* const> dependencies, JobHandle& handle,
    std::uint32_t minItemsPerBatch = 1);
```

`ScheduleAfter()`: scans `dependencies`, discarding any already complete.
If none remain pending, falls straight through to an ordinary `Schedule()`
call (zero continuation bookkeeping — the expected common case). Otherwise:
1. Calls `handle.AddPendingUnit()` — `handle` is now incomplete, before this
   function even returns.
2. Heap-allocates a `PendingContinuation` (fn/payload/a **copy** of `handle`/
   an atomic `unmetDependencyCount`) — the same bounded, documented
   exception to Phase 1's zero-allocation rule `DispatchJobContext` (Phase
   2) already established.
3. Registers `OnDependencyCleared` against every still-pending dependency
   (`WatchDependencyWithFallback()` — falls back to a polling thread on
   overflow, see §2.2).
4. `OnDependencyCleared()` decrements `unmetDependencyCount`; the LAST
   caller to do so calls `JobSystem::ScheduleAlreadyPending(fn, payload,
   continuation->handle)` and frees the continuation.

`DispatchAfter()` reuses `ScheduleAfter()` directly: it heap-allocates a
`DispatchAfterContext` (Dispatch()'s own parameters) and calls
`ScheduleAfter(&RunDispatchAfterTrampoline, context, dependencies, handle)`
against the SAME `handle` the caller will eventually `WaitForJobs()` on.
The trampoline's own placeholder pending unit (added by `ScheduleAfter()`)
nets out correctly once it actually runs `Dispatch()` (which adds its own
N batch-job pending units via ordinary `Schedule()` calls) and returns
(triggering `ScheduleAlreadyPending()`'s own decrement of the placeholder).

`itemCount == 0` is the same immediate, job-free no-op `Dispatch()` itself
already defines — checked before anything else.

---

## 2. Two real correctness bugs found and fixed during this session

Both were caught by actually running the new test suite under a **clean,
separately-configured `GTE_ENABLE_JOB_SYSTEM=OFF` MinGW/GCC build** (per
this project's own established cross-configuration verification discipline
— see Phase 1/2's own completion reports) — neither would have been caught
by the default `GTE_ENABLE_JOB_SYSTEM=ON` build alone, since both are
specific to the OFF configuration's "run everything synchronously, on
whichever thread calls `Schedule()`" contract interacting with Phase 3's
new deferred-completion mechanism.

### 2.1 — `JobSystem::WaitForJobs()`'s OFF-mode no-op was no longer correct

**Symptom:** Several new `JobContinuationTests` (`HandleStaysIncompleteUntil
PendingDependencyClears`, `FanInWaitsForEveryDependencyBeforeRunning`, ...)
failed under `GTE_ENABLE_JOB_SYSTEM=OFF` — a `waiter` thread's
`WaitForJobs(handle)` call returned immediately, `true`, even though the
continuation it was supposed to wait for hadn't run yet.

**Root cause:** the OFF branch of `WaitForJobs()` was a bare `(void)handle;`
no-op, justified (correctly, for Phases 1-2) by the fact that every
`Schedule()`/`Dispatch()` call in that configuration runs its job(s)
synchronously, to completion, before returning — so a handle was always
already complete by the time any caller could reach `WaitForJobs()` at all.
Phase 3's `AddPendingUnit()` breaks that assumption: a handle can be marked
incomplete well BEFORE the work that will eventually complete it is
actually scheduled, and that work may run to completion on a genuinely
DIFFERENT thread (e.g. one concurrently calling `Schedule()`/
`ScheduleAlreadyPending()` for the handle's own dependency).

**Fix:** `JobSystem::WaitForJobs()`'s OFF branch is now a real spin-wait:
```cpp
while (!handle.IsComplete()) {
    std::this_thread::yield();
}
```
(`<thread>` is now included directly and unconditionally in
`JobSystem.cpp`, rather than relying on the ON-only transitive include via
`JobSystem.h`.)

### 2.2 — The overflow polling-fallback path could deadlock under `GTE_ENABLE_JOB_SYSTEM=OFF`

**Symptom:** `JobContinuationTests.OverflowingWatcherCapacityStillRunsEvery
Continuation` (more than `kMaxWatchersPerHandle` dependents on one slow
handle) hung indefinitely under `GTE_ENABLE_JOB_SYSTEM=OFF`.

**Root cause:** `WatchDependencyWithFallback()`'s overflow path originally
scheduled its polling job via an ordinary `JobSystem::Instance().Schedule(
&RunPollingFallbackJob, pollContext, throwawayPollHandle)`. Under
`GTE_ENABLE_JOB_SYSTEM=OFF`, `Schedule()` runs its job IMMEDIATELY,
SYNCHRONOUSLY, on whichever thread calls it — so this call spun forever
(`while (!context->dependency.IsComplete()) { std::this_thread::yield(); }`)
on the SAME thread that was supposed to be free to go on and eventually
cause the dependency to clear (in the test: a dedicated background thread
that itself calls `Schedule()` for the dependency's own holding job — the
overflow poll job stole that same thread and never gave it back).

**Fix:** the overflow fallback now spawns a dedicated, DETACHED
`std::thread` directly (`std::thread(&RunPollingFallbackJob,
pollContext).detach();`) instead of routing through
`JobSystem::Instance().Schedule()`. This guarantees the fallback never
blocks the calling thread, regardless of `GTE_ENABLE_JOB_SYSTEM` — matching
the fallback's own documented "costs a background thread" contract
literally, rather than routing it through a scheduler that may or may not
actually provide one.

**A related test-design lesson (not a production bug, but worth recording
for future `src/Jobs/` tests):** the FIRST version of
`JobContinuationTests.cpp` itself deadlocked under `GTE_ENABLE_JOB_SYSTEM=OFF`
for the identical underlying reason — it called `JobSystem::Instance().Schedule()`
directly, from the main test thread, with a job that spins until a
test-controlled flag is released (to simulate "genuinely pending
dependency"). Fixed by introducing `StartHeldDependency()`/
`WaitUntilPending()` helpers that always spawn a DEDICATED `std::thread` to
make that `Schedule()` call, never the calling thread itself — this is now
the established pattern (documented in the test file's own header comment
and in `AGENTS.md`) for any future `src/Jobs/` test needing a genuinely
long-pending dependency in either build configuration.

---

## 3. Definition of Done — checked against Phase 3's own strategy doc

1. ✅ `ScheduleAfter(fn, payload, dependencies, handle)` exists, matching the
   documented signature (`std::span<JobHandle* const>` instead of a plain
   pointer array, a natural, equivalent C++20 refinement).
2. ✅ `handle` is made incomplete the instant `ScheduleAfter()` returns, even
   with nothing pushed onto the queue yet — verified directly
   (`HandleStaysIncompleteUntilPendingDependencyClears`).
3. ✅ Already-complete dependencies degrade to an ordinary `Schedule()` call
   with zero continuation bookkeeping
   (`AlreadyCompleteDependencyFallsThroughToOrdinarySchedule`,
   `EmptyDependencyListIsAnOrdinarySchedule`).
4. ✅ Fan-in (one continuation, several dependencies) and fan-out (several
   continuations, one shared dependency) both resolve correctly
   (`FanInWaitsForEveryDependencyBeforeRunning`,
   `FanOutRunsEveryDependentOnceTheSharedDependencyClears`).
5. ✅ `kMaxWatchersPerHandle` overflow degrades gracefully via the documented
   polling fallback rather than dropping a continuation
   (`OverflowingWatcherCapacityStillRunsEveryContinuation`).
6. ✅ `DispatchAfter()` exists and defers a real batched `Dispatch()` the
   same way (`DispatchAfterDefersEveryBatchUntilDependencyClears`,
   `DispatchAfterZeroItemCountIsAnImmediateNoOp`).
7. ✅ Built entirely on top of Phases 1-2's `Schedule()`/`WaitForJobs()`/
   `JobHandle`/`Dispatch()` — no second scheduler, no second queue (grepped:
   `JobContinuation.cpp` calls nothing but `JobSystem::Instance().Schedule()`/
   `ScheduleAlreadyPending()` and `Dispatch()` itself).
8. ✅ Strict-ordering stress-repeated (`ContinuationNeverObservesDependencyAsUnfinished`,
   100 iterations per run) and a real cross-thread `WaitForJobs()` block
   proven, not just inferred (`HandleStaysIncompleteUntilPendingDependencyClears`).
9. ✅ Passes identically whether `GTE_ENABLE_JOB_SYSTEM` is `ON` or `OFF` —
   verified after the two fixes in §2, under both the project's normal
   MSVC/Ninja `ON` build and a separate, from-scratch MinGW/GCC `OFF`
   build/configure.
10. ✅ Still used by nothing outside `tests/Jobs/` — grepped the codebase;
    only `tests/Jobs/JobContinuationTests.cpp` references
    `Jobs::ScheduleAfter`/`Jobs::DispatchAfter` at all.

---

## 4. Tests added

`tests/Jobs/JobContinuationTests.cpp` (9 tests):

- `AlreadyCompleteDependencyFallsThroughToOrdinarySchedule`
- `EmptyDependencyListIsAnOrdinarySchedule`
- `HandleStaysIncompleteUntilPendingDependencyClears` (25 iterations,
  spawns a real waiter thread blocked in `WaitForJobs()`)
- `ContinuationNeverObservesDependencyAsUnfinished` (100 iterations, strict
  ordering)
- `FanInWaitsForEveryDependencyBeforeRunning`
- `FanOutRunsEveryDependentOnceTheSharedDependencyClears`
- `OverflowingWatcherCapacityStillRunsEveryContinuation`
  (`kMaxWatchersPerHandle + 3` dependents on one handle)
- `DispatchAfterDefersEveryBatchUntilDependencyClears` (777 items, a real
  batched write-every-index-exactly-once proof)
- `DispatchAfterZeroItemCountIsAnImmediateNoOp`

Added to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` **unconditionally**
(not gated behind any `if(GTE_ENABLE_...)` block) — matching Phases 1-2's
own "always built" bucket, since `JobContinuation.h/.cpp` always compiles
regardless of `GTE_ENABLE_JOB_SYSTEM`.

---

## 5. Verification performed

- Full suite (`ctest -C Debug`) under the project's normal MSVC-equivalent
  MinGW/Ninja `GTE_ENABLE_JOB_SYSTEM=ON` build (`build/`): **687 tests,
  100% passed** (686 passed + 1 pre-existing machine-gated smoke test
  skipped, `PmxLoaderRealModelSmokeTest` — unrelated to this work).
- A second, independent, from-scratch configure+build under MinGW/GCC with
  `-DGTE_ENABLE_JOB_SYSTEM=OFF` (`build_joboff/`, removed after this
  session per the task's own cleanup instructions): **same 687 tests, 100%
  passed**, after the two fixes in §2 — this is what actually caught both
  bugs; the default `ON` build alone never exercised the OFF-mode code
  paths those tests hit.
- `--gtest_filter=JobContinuationTests.*:JobSystemTests.*:JobDispatchTests.*`
  with `--gtest_repeat=30` (ON build) and `--gtest_repeat=15` (OFF build,
  and again isolated to `JobContinuationTests.*` alone on both builds) —
  zero hangs, zero failures across every iteration, following this
  campaign's own "never trust a single passing run for genuinely
  concurrent code" discipline (see `AGENTS.md`, "Job System").

---

## 6. Files changed/added this session

- `src/Jobs/JobTypes.h` — `detail::WatcherFunction`/`kMaxWatchersPerHandle`,
  `JobHandleState::watcherMutex`/`watcherFns`/`watcherContexts`/
  `watcherCount`/`AddWatcher()`/`FireWatchers()`, `JobHandle::
  AddCompletionWatcher()`/`AddPendingUnit()`.
- `src/Jobs/JobSystem.h` — new `ScheduleAlreadyPending()` declaration.
- `src/Jobs/JobSystem.cpp` — `ScheduleAlreadyPending()` (both
  `GTE_ENABLE_JOB_SYSTEM` branches), `FireWatchers()` calls added to
  `WorkerLoop()`/`Schedule()`'s own decrement sites, `WaitForJobs()`'s OFF
  branch changed from a no-op to a real spin-wait (§2.1), unconditional
  `#include <thread>`.
- `src/Jobs/JobContinuation.h` (new)
- `src/Jobs/JobContinuation.cpp` (new) — including the overflow-fallback
  fix in §2.2.
- `tests/Jobs/JobContinuationTests.cpp` (new)
- `CMakeLists.txt` — `src/Jobs/JobContinuation.h/.cpp` added to `gte_core`'s
  source list.
- `tests/CMakeLists.txt` — `Jobs/JobContinuationTests.cpp` entry
  (unconditional), plus a matching documentation comment in the file's own
  test-taxonomy header block.
- `AGENTS.md` — extended the "Job System" section's intro paragraph (now
  Phases 1-3) and added seven new bullets documenting `ScheduleAfter()`/
  `DispatchAfter()`'s design, the `AddPendingUnit()`/`ScheduleAlreadyPending()`
  pairing, the watcher-list mutex discipline, the overflow-fallback fix,
  the `WaitForJobs()` OFF-mode fix, and the new
  "spawn a dedicated thread, never call `Schedule()` with a blocking job
  from the test's own thread" testing convention.

No existing PRODUCTION call site changed — this remains a purely additive
phase from the rest of the engine's point of view, consistent with its own
Definition of Done ("used by nothing else in the engine yet"). The two
fixes in §2 are corrections to Phase 3's OWN new code (`JobSystem::
WaitForJobs()`'s OFF branch, `JobContinuation.cpp`'s overflow fallback),
not regressions in Phase 1/2 behavior — every Phase 1/2 test still passes
unchanged, in both configurations.

Phase 3 is complete. Phase 4
(`JOBSYSTEM_PHASE4_THREAD_SAFETY_AUDIT_INTEGRATION_POINTS_v2.md`) may now
begin.
