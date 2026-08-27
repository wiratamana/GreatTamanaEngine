# Job System — HOTFIX 1 Completion Report

Status: **DONE**. Fixes item 1 of `JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md`
(the code-review hotfix backlog captured on 2026-08-27). Items 2-6 and the
"Minor nits" section of that document remain OPEN and are explicitly
out of scope for this hotfix.

---

## 1. What was wrong

**File:** `src/Jobs/JobTypes.h`, `detail::JobHandleState::AddWatcher()`.

```cpp
bool AddWatcher(WatcherFunction fn, void* context)
{
    std::lock_guard<std::mutex> lock(watcherMutex);
    if (pending.load(std::memory_order_acquire) == 0) {
        fn(context);              // <-- called WHILE HOLDING watcherMutex
        return true;
    }
    ...
}
```

`FireWatchers()` deliberately copies the watcher list out and releases
`watcherMutex` *before* invoking any callback, specifically so a watcher
body can safely call back into the same handle (e.g. register another
watcher) without risking a self-deadlock (see that method's own comment).
The "already complete" immediate-fire branch of `AddWatcher()` did not
follow that same rule — it called `fn(context)` synchronously, still
holding `watcherMutex`.

**Why it mattered:** `ScheduleAfter()` snapshots each dependency's
`IsComplete()` before registering a watcher. If a dependency completed in
the window between that snapshot and the actual `AddCompletionWatcher()`
call, the immediate-fire branch ran `OnDependencyCleared()` synchronously
*with the dependency's own `watcherMutex` still held*. If that firing was
the last dependency, it called `JobSystem::ScheduleAlreadyPending()` — and
if the job queue happened to be full at that moment,
`ScheduleAlreadyPending()` runs the *real deferred job body* inline, still
nested inside the locked `AddWatcher()` call. If that job body ever touched
the same dependency handle again (e.g. registered another `ScheduleAfter()`
against it — plausible for any shared "barrier" handle), it would try to
re-lock the same non-recursive `watcherMutex` on the same thread →
**deadlock**. Even without hitting the full-queue path, this silently
turned an "async continuation" into synchronous execution of arbitrary job
code on whatever thread happened to call `ScheduleAfter()`.

---

## 2. The fix

`AddWatcher()` now only *decides* whether the handle is already complete
while holding `watcherMutex` (setting a local `alreadyComplete` flag,
exactly mirroring the overflow/registration branch's own "decide under the
lock" shape), releases the lock, and only then calls `fn(context)` if the
handle was already complete — the exact same "decide under the lock, fire
after releasing it" discipline `FireWatchers()` already used. No other
observable behavior of `AddWatcher()` changed: it still returns `true`
immediately for an already-complete handle, `true` for a successful
registration, and `false` once `kMaxWatchersPerHandle` is exceeded.

```cpp
bool AddWatcher(WatcherFunction fn, void* context)
{
    bool alreadyComplete = false;
    {
        std::lock_guard<std::mutex> lock(watcherMutex);
        if (pending.load(std::memory_order_acquire) == 0) {
            alreadyComplete = true;
        } else if (watcherCount >= kMaxWatchersPerHandle) {
            return false;
        } else {
            watcherFns[watcherCount] = fn;
            watcherContexts[watcherCount] = context;
            ++watcherCount;
        }
    }
    if (alreadyComplete) {
        fn(context); // Invoked OUTSIDE watcherMutex.
    }
    return true;
}
```

No other file needed to change — `JobContinuation.cpp`/`JobSystem.cpp`'s
own call sites into `AddCompletionWatcher()`/`AddWatcher()` were already
written against the documented (correct) contract; only the
implementation's own internal locking discipline was wrong.

---

## 3. Regression test added

`tests/Jobs/JobContinuationTests.cpp` gained
`ReentrantWatcherRegistrationOnAlreadyCompleteHandleDoesNotDeadlock`:

1. A dependency handle is scheduled and `WaitForJobs()`'d to completion, so
   it is genuinely already-complete.
2. `handle.AddCompletionWatcher(&OuterWatcherFn, ...)` is called from a
   dedicated worker thread (not the test's main thread, so a hang doesn't
   wedge the whole test binary).
3. `OuterWatcherFn` — invoked synchronously by `AddWatcher()`'s
   immediate-fire branch — itself calls
   `handle->AddCompletionWatcher(&InnerWatcherFn, ...)` on the *same*
   already-complete handle, from *inside* the first callback. Before the
   fix, this second call would try to re-lock the same non-recursive
   `watcherMutex` already held by the outer call on the same thread and
   hang forever.
4. The test polls (bounded, ~10 seconds) for the outer call to return
   rather than doing an unconditional `join()` — if it never returns, the
   test explicitly `FAIL()`s with a descriptive message and detaches the
   stuck thread (avoiding a `std::terminate()` crash on a still-joinable
   thread's destructor), so a future regression shows up as a clean, loud
   test failure instead of hanging the whole suite or crashing it.
5. On success, asserts the inner watcher actually ran too — proving the
   fix doesn't just avoid deadlocking, it still delivers both callbacks
   correctly.

This test fails (hangs, then times out/asserts) against the pre-fix code
and passes cleanly against the fix.

---

## 4. Verification performed

- **Clean rebuild** of the existing Ninja/MinGW `build/` directory
  (`cmake --build build`) — compiled cleanly, including the new test file.
- **Full regression suite** (`ctest -C Debug --output-on-failure` from
  `build/`): **690 tests, 689 passed + 1 pre-existing machine-gated skip**
  (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  unrelated to this fix), including the new
  `JobContinuationTests.ReentrantWatcherRegistrationOnAlreadyCompleteHandleDoesNotDeadlock`
  test (passed, 0.09s).
- **A second, independent, from-scratch configure+build** under
  MinGW/GCC with `-DGTE_ENABLE_JOB_SYSTEM=OFF` (`build_joboff/`,
  configured, built, tested, then removed after this session — mirroring
  the Job System campaign's own precedent of using a completely separate
  configuration as an extra cross-check, e.g.
  `JOB_SYSTEM_PHASE3_COMPLETION_REPORT.md`/`JOB_SYSTEM_PHASE4_COMPLETION_REPORT.md`):
  **same 690 tests, same 689 passed + 1 pre-existing skip** — confirming
  the fix (and the whole existing suite) behaves identically whether the
  real worker-thread pool is compiled in or not (`JobTypes.h` — and
  therefore `AddWatcher()` — always compiles regardless of
  `GTE_ENABLE_JOB_SYSTEM`).

---

## 5. Files changed this session

- `src/Jobs/JobTypes.h` — `detail::JobHandleState::AddWatcher()` fixed per
  §2 above, plus an updated doc comment explaining the hazard and the fix.
- `tests/Jobs/JobContinuationTests.cpp` — new regression test described in
  §3 above (`ReentrantWatcherRegistrationOnAlreadyCompleteHandleDoesNotDeadlock`,
  plus its small `ReentrantWatcherContext`/`InnerWatcherFn`/`OuterWatcherFn`
  helpers).
- `task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md` —
  item 1 marked `[FIXED — HOTFIX 1]` with a pointer to this report; overall
  document status updated from "OPEN" to "PARTIALLY FIXED". Items 2-6 and
  the "Minor nits" section are untouched and remain open follow-up work.
- `task_manager/job_system/JOB_SYSTEM_HOTFIX1_COMPLETION_REPORT.md` (this
  file, new).

---

## 6. What remains open

Items 2-6 of `JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md` (detached
polling-fallback thread lifecycle, `kMaxWatchersPerHandle`'s
unbounded-thread scalability trap, `Schedule()`'s unsynchronized pending
increment, unbounded full-queue recursion depth, and abandoned-continuation
leaks) plus its "Minor nits" are still open and unpatched — this hotfix was
scoped strictly to item 1 per this session's own instructions ("HOTFIX
1"). See that document for full detail on each remaining item.
