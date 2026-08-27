# Job System — HOTFIX 2 Completion Report

Status: **DONE**. Fixes item 2 of `JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md`
(the code-review hotfix backlog captured on 2026-08-27, previously updated
by `JOB_SYSTEM_HOTFIX1_COMPLETION_REPORT.md` for item 1). Items 3-6 and the
"Minor nits" section of that document remain OPEN and are explicitly out of
scope for this hotfix - item 6 gained a small, honestly-labeled partial
mitigation as a side effect of this fix (see "What remains open" below), but
is NOT considered fixed by this session.

---

## 1. What was wrong

**File:** `src/Jobs/JobContinuation.cpp`, `WatchDependencyWithFallback()` /
`RunPollingFallbackJob()`, plus `src/Jobs/JobQueue.cpp`'s `TryPush()`.

Once a single dependency handle already has `detail::kMaxWatchersPerHandle`
(8) other continuations registered against it, `ScheduleAfter()`/
`DispatchAfter()` fall back to spawning a dedicated, **detached**
`std::thread` that busy-polls the dependency until it completes, then calls
`OnDependencyCleared()` (which may reach `JobSystem::Instance()`):

```cpp
auto* pollContext = new PollingFallbackContext{ dependency, continuation };
std::thread(&RunPollingFallbackJob, pollContext).detach();
```

Nothing tracked or joined this thread, and `JobSystem`'s destructor
(`m_queue.Shutdown()` + join workers) had no knowledge it even existed.

**Why it mattered:**

- If the process exited while such a thread was still spinning (a stuck or
  merely slow dependency), it could call `JobSystem::Instance()`
  during/after the Meyers-singleton's own static destruction - a classic
  static-destruction-order hazard (UB/crash at shutdown).
- Separately, `JobQueue::TryPush()` never checked `m_shuttingDown` at all:

  ```cpp
  bool JobQueue::TryPush(JobEntry entry) {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_count == m_slots.size()) return false;
      ... // pushed unconditionally, even after Shutdown() had been called
  }
  ```

  A late push arriving from a background fallback thread (e.g. the last
  dependency clearing right as `~JobSystem()` was running) could still
  succeed after workers had already exited their `WaitAndPop()` loop and
  been joined - silently stranding that job forever, with no worker left to
  ever run it.

---

## 2. The fix

Both of `JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md` item 2's own suggested
directions were implemented TOGETHER, since each alone left a real gap the
other closes:

### (a) `JobSystem` now tracks and joins its own background fallback threads

`src/Jobs/JobSystem.h` gained a small, always-present (not gated by
`GTE_ENABLE_JOB_SYSTEM` - a background fallback thread can be spawned
regardless of that switch) background-thread registry:

```cpp
void RegisterBackgroundThread(std::thread thread, std::shared_ptr<std::atomic<bool>> completionFlag);
bool IsShuttingDown() const noexcept;

private:
    void JoinAllBackgroundThreads();

    struct BackgroundThreadEntry {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> completionFlag;
    };

    std::atomic<bool> m_shuttingDown{ false };
    std::mutex m_backgroundThreadsMutex;
    std::vector<BackgroundThreadEntry> m_backgroundThreads;
```

`RegisterBackgroundThread()` stores the thread (moved, never detached) plus
a `completionFlag` the thread itself sets right before returning - which
lets the NEXT `RegisterBackgroundThread()` call opportunistically join and
discard any already-finished entry first, so the registry doesn't grow
completely unbounded across a long-running process even though nothing else
ever proactively prunes it. If `RegisterBackgroundThread()` is ever called
after shutdown has already begun (a narrow, expected-rare race -
`JoinAllBackgroundThreads()` may already have collected the registry by
then), the thread is instead joined synchronously, right there, so it can
never be silently dropped from an already-collected registry.

Both `~JobSystem()` bodies (`GTE_ENABLE_JOB_SYSTEM=ON` and `=OFF` - a
background fallback thread can exist in EITHER configuration, since the
overflow condition that spawns one has nothing to do with that switch) now:

1. Set `m_shuttingDown = true` **first**, before tearing down anything else.
2. (ON only) `m_queue.Shutdown()` + join every worker thread, exactly as
   before.
3. Call the new `JoinAllBackgroundThreads()`, which moves the whole
   registry out (under its own mutex) and joins every entry.

By the time `~JobSystem()` returns, every background thread ever registered
against that instance is guaranteed to have already exited - none can ever
touch `JobSystem::Instance()` during/after the singleton's own static
destruction, closing the hazard directly.

### (b) The polling-fallback thread now checks `IsShuttingDown()` and bails out

Simply joining the thread in step 3 above would not be safe on its own: if
the dependency it's polling never actually clears (e.g. it was abandoned -
see item 6), that `join()` would hang `~JobSystem()` forever. `JobContinuation.cpp`'s
`RunPollingFallbackJob()` was rewritten to check `JobSystem::Instance().IsShuttingDown()`
on every loop iteration, alongside its existing `dependency.IsComplete()`
check, and to bail out immediately - WITHOUT calling `OnDependencyCleared()`/
`JobSystem::Instance()` again - the instant it observes shutdown:

```cpp
void RunPollingFallbackJob(void* rawContext)
{
    PollingFallbackContext* context = static_cast<PollingFallbackContext*>(rawContext);

    bool dependencyCleared = false;
    for (;;) {
        if (JobSystem::Instance().IsShuttingDown()) {
            break; // Abandon the PendingContinuation - the process is exiting.
        }
        if (context->dependency.IsComplete()) {
            dependencyCleared = true;
            break;
        }
        std::this_thread::yield();
    }

    if (dependencyCleared) {
        OnDependencyCleared(context->continuation);
    }
    context->completionFlag->store(true, std::memory_order_release);
    delete context;
}
```

`WatchDependencyWithFallback()` now creates a `std::make_shared<std::atomic<bool>>(false)`
completion flag, passes it into the `PollingFallbackContext`, spawns the
thread, and registers it (`std::move`d, never `.detach()`'d) via
`JobSystem::Instance().RegisterBackgroundThread()` instead.

### (b, continued) `JobQueue::TryPush()` now rejects pushes after `Shutdown()`

```cpp
bool JobQueue::TryPush(JobEntry entry)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_shuttingDown) {
        return false; // Reject - even if there's still free capacity.
    }
    if (m_count == m_slots.size()) {
        return false;
    }
    ...
}
```

A push that's rejected this way falls through to `JobSystem::Schedule()`/
`ScheduleAlreadyPending()`'s existing "queue is full" fallback - which
already runs the job immediately, inline, right there - so a late push
during shutdown now runs synchronously instead of being silently stranded
in a queue that's about to be destroyed.

No other file needed to change - every other call site into
`JobSystem::Schedule()`/`ScheduleAlreadyPending()`/`WaitForJobs()` was
already written against the documented (correct) contract; only
`JobContinuation.cpp`'s own fallback-thread lifecycle and `JobQueue`'s own
internal shutdown-awareness were wrong.

---

## 3. Regression tests added

- `tests/Jobs/JobQueueTests.cpp`'s `TryPushFailsAfterShutdownEvenWhenNotFull`
  - constructs a `JobQueue` with plenty of free capacity, calls `Shutdown()`,
    and asserts a subsequent `TryPush()` still returns `false` - proving
    this is a genuine shutdown-awareness check, not merely a "queue happens
    to be full" coincidence. `JobQueue` is a plain, self-contained,
    Tier-1-testable class (no `JobSystem` singleton involved), so this test
    needed no new fixture.
- `tests/Jobs/JobSystemTests.cpp` gained two new tests exercising the
  background-thread registry directly against the real
  `JobSystem::Instance()` singleton:
  - `IsShuttingDownIsFalseDuringNormalOperation` - a sanity check that
    `IsShuttingDown()` never spuriously reports `true` outside of
    `JobSystem`'s own destructor (which, per this file's own header
    comment, is never actually reached during a normal test run, since the
    Meyers singleton has process lifetime).
  - `RegisterBackgroundThreadLetsARealThreadRunToCompletion` - registers a
    real `std::thread` and confirms (via a bounded poll on its completion
    flag) that it actually runs to completion rather than being blocked or
    silently dropped by registration.
  - `RegisterBackgroundThreadPrunesAlreadyFinishedEntriesOnNextCall` -
    registers a quick-finishing thread, waits for it to signal completion,
    then registers a second one, proving the opportunistic pruning pass
    inside `RegisterBackgroundThread()` doesn't hang or crash when it joins
    an already-finished entry as part of a later registration call.
- The pre-existing `JobContinuationTests.cpp::OverflowingWatcherCapacityStillRunsEveryContinuation`
  test (unchanged) continues to exercise the polling-fallback path itself
  end-to-end (more dependents than `kMaxWatchersPerHandle`, forcing the
  overflow thread to spawn) and still passes unmodified - proving the
  rewritten `RunPollingFallbackJob()`/`WatchDependencyWithFallback()` didn't
  change this path's OBSERVABLE behavior, only its lifecycle management.

**Not directly automatable:** actually forcing `JobSystem::Instance()`'s
real destructor to run (to prove the join-at-shutdown sequence itself, or
that a still-spinning fallback thread is correctly joined rather than
crashing at real static destruction) would require tearing down the whole
test binary's process - the Meyers singleton has process lifetime by
design, and nothing in this test suite (or any other) can force it to
destruct early without ending the test run itself. This was instead
verified by REASONING about the destructor's own code (every registered
background thread is guaranteed already-exited by the time `~JobSystem()`
returns, per its own join loop) and by the fact that every existing
concurrency-heavy Job System test (`JobContinuationTests.cpp`,
`JobSystemTests.cpp`) continues to pass, including the specific overflow
path this hotfix touches.

---

## 4. Verification performed

- **Clean rebuild** of the existing Ninja/MinGW `build/` directory
  (`cmake --build build`) - compiled cleanly, including every changed/new
  file.
- **Full regression suite** (`ctest -C Debug --output-on-failure` from
  `build/`): **694 tests, 693 passed + 1 pre-existing machine-gated skip**
  (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  unrelated to this fix), including all of this session's new tests
  (`JobQueueTests.TryPushFailsAfterShutdownEvenWhenNotFull`,
  `JobSystemTests.IsShuttingDownIsFalseDuringNormalOperation`,
  `JobSystemTests.RegisterBackgroundThreadLetsARealThreadRunToCompletion`,
  `JobSystemTests.RegisterBackgroundThreadPrunesAlreadyFinishedEntriesOnNextCall`)
  and the pre-existing `JobContinuationTests.OverflowingWatcherCapacityStillRunsEveryContinuation`
  (still passing, proving the fallback path's observable behavior is
  unchanged).
- **A second, independent, from-scratch configure+build** under MinGW/GCC
  with `-DGTE_ENABLE_JOB_SYSTEM=OFF` (`build_joboff/`, configured, built,
  tested, then removed after this session - mirroring HOTFIX 1's own
  precedent of using a completely separate configuration as an extra
  cross-check): **same 694 tests, same 693 passed + 1 pre-existing skip** -
  confirming the fix (and the whole existing suite) behaves identically
  whether the real worker-thread pool is compiled in or not. This
  cross-check specifically matters for this hotfix, since a background
  fallback thread - and therefore both halves of this fix
  (`RegisterBackgroundThread()`/`IsShuttingDown()` and `TryPush()`'s
  shutdown-awareness) - can be exercised in EITHER `GTE_ENABLE_JOB_SYSTEM`
  configuration, unlike some earlier phases' worker-pool-specific code.

---

## 5. Files changed this session

- `src/Jobs/JobSystem.h` - `RegisterBackgroundThread()`/`IsShuttingDown()`
  (public) and `JoinAllBackgroundThreads()` (private) declared, plus the new
  `BackgroundThreadEntry`/`m_shuttingDown`/`m_backgroundThreadsMutex`/
  `m_backgroundThreads` members (defined unconditionally, not gated by
  `GTE_ENABLE_JOB_SYSTEM`).
- `src/Jobs/JobSystem.cpp` - implementations of the three methods above
  (common to both `GTE_ENABLE_JOB_SYSTEM` configurations), and both
  `~JobSystem()` bodies updated to set `m_shuttingDown` first and call
  `JoinAllBackgroundThreads()` as their last step.
- `src/Jobs/JobContinuation.cpp` - `PollingFallbackContext` gained a
  `completionFlag`; `RunPollingFallbackJob()` now checks
  `JobSystem::Instance().IsShuttingDown()` every loop iteration and bails
  out without firing the continuation if shutdown has begun;
  `WatchDependencyWithFallback()` now registers the spawned thread via
  `JobSystem::Instance().RegisterBackgroundThread()` instead of calling
  `.detach()`.
- `src/Jobs/JobQueue.h`/`.cpp` - `TryPush()` now rejects a push once
  `Shutdown()` has been called, even with free capacity remaining; doc
  comments on `TryPush()`/`Shutdown()` updated to describe this.
- `tests/Jobs/JobQueueTests.cpp` - new regression test
  `TryPushFailsAfterShutdownEvenWhenNotFull`.
- `tests/Jobs/JobSystemTests.cpp` - three new tests described in §3 above.
- `task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md` - item
  2 marked `[FIXED — HOTFIX 2]` with a pointer to this report; item 6
  annotated with an honest "partially mitigated, still open" note; overall
  document status/intro updated. Items 3-5 and the "Minor nits" section are
  untouched and remain open follow-up work.
- `task_manager/job_system/JOB_SYSTEM_HOTFIX2_COMPLETION_REPORT.md` (this
  file, new).

---

## 6. What remains open

Items 3-6 of `JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md` (the
`kMaxWatchersPerHandle`/unbounded-thread scalability trap, `Schedule()`'s
unsynchronized pending increment, unbounded full-queue recursion depth, and
abandoned-continuation leaks) plus its "Minor nits" are still open and
unpatched - this hotfix was scoped strictly to item 2 per this session's own
instructions ("HOTFIX 2"). Item 6 specifically gained a small, honestly-
labeled partial mitigation as a side effect of this fix (the polling
fallback thread no longer leaks *undetected by `JobSystem`*, and no longer
risks running into/past static destruction) but is explicitly NOT considered
fixed: an abandoned dependency's `PendingContinuation` allocation is still
never freed, and its polling thread still spins for the remaining life of
the process (until either the dependency clears or shutdown begins) with no
timeout/cancellation mechanism of any kind. See that document for full
detail on each remaining item.
