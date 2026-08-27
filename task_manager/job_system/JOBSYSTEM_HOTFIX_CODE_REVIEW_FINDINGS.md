# Job System — Code Review Hotfix Backlog

Status: **PARTIALLY FIXED.** Captured from an ad-hoc code review of
JobContinuation.cpp/.h, JobDispatch.cpp/.h, JobQueue.cpp/.h, JobSystem.cpp/.h,
and JobTypes.h (Phase 3 continuations + underlying Phase 1/2 machinery).
Item 1 (below) was fixed under **HOTFIX 1** - see
`JOB_SYSTEM_HOTFIX1_COMPLETION_REPORT.md` for the full writeup. Item 2 was
fixed under **HOTFIX 2** - see `JOB_SYSTEM_HOTFIX2_COMPLETION_REPORT.md` for
the full writeup. Item 7 - a CRITICAL use-after-free in `ScheduleAfter()` -
was fixed under **HOTFIX 3** - see `JOB_SYSTEM_HOTFIX3_COMPLETION_REPORT.md`
for the full writeup. Items 3-6 and the "Minor nits" remain OPEN - this file
exists purely so those findings aren't lost before someone circles back to
fix them.

Each item lists: severity, where, what's wrong, why it matters, and a
suggested fix direction. None of these are hypothetical typos - each is a
concrete race/lifetime/scalability issue reachable via the public API as
currently written.

---

## 1. [HIGH] [FIXED — HOTFIX 1] `JobHandleState::AddWatcher()` invokes the callback while still
   holding `watcherMutex` — violates its own no-reentrancy contract, can
   self-deadlock

**File:** `JobTypes.h`, `detail::JobHandleState::AddWatcher()`

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
`watcherMutex` *before* invoking any callback, specifically so a watcher body
can safely call back into the same handle (e.g. register another watcher)
without risking a self-deadlock (see that method's own comment). The
"already complete" immediate-fire branch of `AddWatcher()` does not follow
that same rule.

**Why it matters:** `ScheduleAfter()` snapshots each dependency's
`IsComplete()` before registering a watcher. If a dependency completes in
the window between that snapshot and the actual
`AddCompletionWatcher()`/`AddWatcher()` call, the immediate-fire branch runs
`OnDependencyCleared()` synchronously **with the dependency's own
`watcherMutex` still held**. If that firing is the last dependency, it calls
`JobSystem::ScheduleAlreadyPending()` — and if the job queue happens to be
full at that moment, `ScheduleAlreadyPending()` runs the *real deferred job
body* inline, still nested inside the locked `AddWatcher()` call. If that
job body ever touches the same dependency handle again (e.g. registers
another `ScheduleAfter()` against it — plausible for any shared "barrier"
handle), it will try to re-lock the same non-recursive `watcherMutex` on the
same thread → **deadlock**. Even without hitting the full-queue path, this
silently turns an "async continuation" into synchronous execution of
arbitrary job code on whatever thread happened to call `ScheduleAfter()`.

**Suggested fix:** Mirror `FireWatchers()`'s pattern — under the lock, only
decide whether to fire immediately (`pending == 0`); release the lock, then
call `fn(context)` outside the lock.

**FIXED (HOTFIX 1):** `AddWatcher()` now only DECIDES "already complete"
under `watcherMutex`; `fn(context)` is invoked after the lock is released -
exactly mirroring `FireWatchers()`'s own pattern, per the suggested fix
above. Regression test:
`tests/Jobs/JobContinuationTests.cpp`'s
`ReentrantWatcherRegistrationOnAlreadyCompleteHandleDoesNotDeadlock` (a
watcher fired on an already-complete handle re-enters
`AddCompletionWatcher()` on that SAME handle from inside its own callback -
this would hang forever under the old locked-fire behavior). See
`JOB_SYSTEM_HOTFIX1_COMPLETION_REPORT.md` for full verification details.

---

## 2. [HIGH] [FIXED — HOTFIX 2] Detached polling-fallback threads have no
   lifecycle tie to `JobSystem`'s shutdown — possible crash/UB at process
   exit

**File:** `JobContinuation.cpp`, `WatchDependencyWithFallback()` /
`RunPollingFallbackJob()`

The overflow fallback spawns a raw, **detached** `std::thread` that busy-
yields until a dependency completes, then calls `OnDependencyCleared()`,
which may reach `JobSystem::Instance()`. Nothing tracks or joins these
threads, and `JobSystem`'s destructor (`m_queue.Shutdown()` + join workers)
has no knowledge of them.

**Why it matters:**
- If the process exits while such a thread is still spinning (dependency
  stuck, or just slow), it can call `JobSystem::Instance()` during/after the
  Meyers-singleton's own static destruction — a classic static-destruction-
  order hazard (UB/crash at shutdown).
- `JobQueue::TryPush()` never checks `m_shuttingDown`:
  ```cpp
  bool JobQueue::TryPush(JobEntry entry) {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_count == m_slots.size()) return false;
      ... // pushes unconditionally, even after Shutdown() was called
  }
  ```
  A late push from a background fallback thread can succeed after workers
  have already been joined, silently stranding a job (or racing the queue's
  own destruction).

**Suggested fix:** Either (a) have `JobSystem` track/join outstanding
fallback threads before it finishes destructing (e.g. a registry + join in
`~JobSystem()`), or (b) make `TryPush()` reject pushes once
`m_shuttingDown` is set, and have the fallback thread bail out gracefully
(without touching `JobSystem::Instance()`) if it observes shutdown.

**FIXED (HOTFIX 2):** Implemented BOTH suggested directions together, since
each alone left a gap the other closes. (a) `JobSystem` now owns a small
background-thread registry (`RegisterBackgroundThread()`/
`JoinAllBackgroundThreads()`, `JobSystem.h`/`.cpp`) - the polling fallback
thread (`JobContinuation.cpp`'s `WatchDependencyWithFallback()`) is
registered instead of detached, and `~JobSystem()` (both the
`GTE_ENABLE_JOB_SYSTEM=ON` and `=OFF` bodies) sets a new `m_shuttingDown`
flag and joins every registered thread before finishing destruction - by the
time `~JobSystem()` returns, no background thread can still be running, so
none can ever touch `JobSystem::Instance()` during/after the singleton's own
static destruction. A new `JobSystem::IsShuttingDown()` is what
`RunPollingFallbackJob()`'s poll loop checks on every iteration, bailing out
immediately (without calling `OnDependencyCleared()`/`JobSystem::Instance()`
again) the instant shutdown begins - without this half, the destructor's own
join would otherwise risk hanging forever on a dependency that never clears
(e.g. an abandoned dependency - see item 6). (b) `JobQueue::TryPush()`
(`JobQueue.cpp`) now also rejects any push once `Shutdown()` has been
called, even when the queue still has free capacity - closing the "late
push silently stranded after workers already joined" half of this finding
directly. See `JOB_SYSTEM_HOTFIX2_COMPLETION_REPORT.md` for full
verification details.

---

## 3. [MEDIUM] `kMaxWatchersPerHandle == 8` + unbounded raw-thread fallback
   is a scalability trap, not just a rare-corner-case cost

**Files:** `JobTypes.h` (`kMaxWatchersPerHandle`), `JobContinuation.cpp`
(`WatchDependencyWithFallback`)

A single shared "barrier" handle (e.g. "frame start complete", "asset load
complete") with more than 8 downstream continuations is an ordinary
pattern, not a corner case. Each watcher beyond the 8th spawns its own
dedicated OS thread that spins via `std::this_thread::yield()` (no
backoff/sleep) for the entire lifetime of the dependency. Realistic fan-out
turns into N-8 permanently busy-spinning threads, one core each — a real
thread/CPU-exhaustion risk, not the "rare" cost the current comments assume.

**Suggested fix:** Either raise `kMaxWatchersPerHandle` to a more realistic
ceiling, make it configurable per-handle, or replace the per-overflow raw
thread with a single shared/pooled poller that can watch multiple overflowed
dependencies at once instead of one thread per overflow.

---

## 4. [MEDIUM] Plain `Schedule()`'s pending-count increment is not
   linearized with the completion-decrement/notify path — possible
   transient "false complete" observed by a concurrent `WaitForJobs()`

**File:** `JobSystem.cpp`, `JobSystem::Schedule()`

```cpp
void JobSystem::Schedule(JobFunction fn, void* payload, JobHandle& handle)
{
    handle.m_state->pending.fetch_add(1, std::memory_order_acq_rel); // NOT under m_completionMutex
    ...
}
```

The decrement-to-zero path is carefully bracketed by `m_completionMutex`
specifically to avoid a lost-wakeup / false-complete race (see the long
comment in `WorkerLoop()`, and the whole rationale behind
`JobHandle::AddPendingUnit()`). The **increment** side has no equivalent
protection. Given two threads racing on the same handle:

1. A worker decrements pending 1→0 (the *previously* last tracked job for
   this handle finishes), fires watchers, notifies — a concurrent
   `WaitForJobs()` caller wakes and observes `IsComplete() == true`.
2. A second thread's concurrent `Schedule()` call then does its
   `fetch_add`, bumping pending 0→1 for a brand-new job.

A waiter can conclude "all done" moments before new work is actually
attached to the same handle. This is exactly the class of bug
`AddPendingUnit()` exists to prevent for `ScheduleAfter()`/`DispatchAfter()`,
but ordinary concurrent `Schedule()` calls against a shared handle aren't
documented as unsafe — this looks like an unaddressed gap rather than an
intentional restriction.

**Suggested fix:** Either document "only ever `Schedule()` against a handle
from a single owning thread, never concurrently with a `WaitForJobs()` on
it elsewhere" as a hard API rule, or move the increment under
`m_completionMutex` too (symmetric with the decrement) to fully close the
window.

---

## 5. [LOW] Full-queue fallback can recurse synchronously to unbounded depth

**File:** `JobSystem.cpp`, `Schedule()` / `ScheduleAlreadyPending()` full-queue
fallback paths

When the queue is saturated, the job runs **inline**, including on a worker
thread that is already executing a job. A job that itself schedules further
jobs while the queue stays full will recurse on the same call stack with no
bound. Unlikely with a 4096-slot queue, but not impossible (e.g. deep
`ScheduleAfter()` continuation chains under sustained overload) — worth a
depth guard or at least a debug assertion/metric if this is ever seen in
practice.

---

## 6. [LOW] No cancellation path for pending continuations — abandoned
   dependencies leak permanently, including a spinning thread in the
   fallback case

**Files:** `JobContinuation.cpp` (`PendingContinuation`,
`DispatchAfterContext`, `PollingFallbackContext`)

These are only ever freed once their dependency actually completes. If a
dependency handle is abandoned (never scheduled against again by mistake),
the associated allocation - and, in the polling-fallback case, an entire
spinning OS thread - leaks for the life of the process, with no way to
detect or cancel it.

**Suggested fix:** Consider an optional timeout/cancellation token for
`ScheduleAfter()`/`DispatchAfter()`, or at minimum a debug-build counter/
assertion for "continuations still pending after N seconds" to make stuck
dependencies observable instead of silently leaking.

**Partially mitigated by HOTFIX 2 (still OPEN):** the polling-fallback
thread no longer leaks *undetected by `JobSystem`* - it is now registered
(`RegisterBackgroundThread()`) and will be joined by `~JobSystem()`, and it
bails out cleanly (via `IsShuttingDown()`) the instant process shutdown
begins, rather than running forever into/past static destruction. This does
NOT fix the underlying issue this item describes: an abandoned dependency's
`PendingContinuation` is still never freed, and its polling thread still
spins for the entire remaining life of the process (right up until shutdown
begins) if the dependency genuinely never clears - there is still no
timeout/cancellation mechanism. See
`JOB_SYSTEM_HOTFIX2_COMPLETION_REPORT.md`'s own "What remains open" section.

---

## 7. [CRITICAL] [FIXED — HOTFIX 3] Use-after-free race
   in `ScheduleAfter()`'s dependency-registration loop

**File:** `JobContinuation.cpp`, `ScheduleAfter()` / `PendingContinuation` /
`OnDependencyCleared()`

```cpp
PendingContinuation* continuation = new PendingContinuation{
    fn, payload, handle, std::atomic<std::uint32_t>(static_cast<std::uint32_t>(pendingDependencies.size()))
};

for (JobHandle* dependency : pendingDependencies) {
    WatchDependencyWithFallback(*dependency, continuation);
}
```

`unmetDependencyCount` is initialized to EXACTLY `pendingDependencies.size()`
- the real number of dependencies this continuation is waiting on - then the
loop above registers a watcher against each one, ONE AT A TIME.
`WatchDependencyWithFallback()`/`AddWatcher()` may invoke
`OnDependencyCleared()` **synchronously, from inside this same loop**, either
because a dependency is already complete by the time it is actually
registered (a real race window - the earlier `!dependency->IsComplete()`
filter is only a snapshot, taken before this loop starts) or because a
background job on another thread completes a still-pending dependency while
this loop is running.

`OnDependencyCleared()` decrements `unmetDependencyCount` and, the moment it
observes the transition to zero, immediately `delete`s `continuation`:

```cpp
void OnDependencyCleared(void* rawContinuation)
{
    PendingContinuation* continuation = static_cast<PendingContinuation*>(rawContinuation);
    if (continuation->unmetDependencyCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        JobSystem::Instance().ScheduleAlreadyPending(continuation->fn, continuation->payload, continuation->handle);
        delete continuation;
    }
}
```

**Why it matters:** if two (or more) of `pendingDependencies` clear before
the registration loop has finished iterating over all of them - entirely
plausible any time `pendingDependencies.size() >= 2` and dependencies are
themselves being completed concurrently by other worker threads, or simply
finish between the pre-loop `IsComplete()` snapshot and their own
registration - `unmetDependencyCount` can reach zero, and `continuation` can
be `delete`d, **while the loop is still running**. Every subsequent
iteration of the loop then calls
`WatchDependencyWithFallback(*dependency, continuation)` with an
already-freed pointer: a textbook use-after-free, reachable from the public
API (`ScheduleAfter()`/`DispatchAfter()`) any time a caller passes 2 or more
dependency handles, with no need for any unusual timing beyond ordinary
concurrent job completion. This is a crash/memory-corruption bug, not a
theoretical one.

**Proposed fix (reviewed below - considered correct and sufficient):**
initialize `unmetDependencyCount` to `pendingDependencies.size() + 1` - one
extra, synthetic "registration in progress" unit that only the calling
thread itself ever owns - run the registration loop exactly as today, then
release that synthetic unit, via one extra `OnDependencyCleared(continuation)`
call, only once every real dependency has actually been registered:

```cpp
// +1 sentinel: represents "ScheduleAfter()'s own registration loop is still
// in progress", NOT a real dependency. This is what stops
// unmetDependencyCount from EVER being able to reach zero (and `continuation`
// from being deleted) while the loop below is still running, no matter how
// many of pendingDependencies clear out from under it concurrently - the
// count can only actually reach zero once (a) every real dependency has
// cleared AND (b) this function has released its own registration-in-progress
// unit below; those two things may happen in either order.
PendingContinuation* continuation = new PendingContinuation{
    fn, payload, handle,
    std::atomic<std::uint32_t>(static_cast<std::uint32_t>(pendingDependencies.size() + 1))
};

for (JobHandle* dependency : pendingDependencies) {
    WatchDependencyWithFallback(*dependency, continuation);
}

// Release the registration-in-progress sentinel now that every dependency in
// pendingDependencies has been safely registered against `continuation` - if
// every one of them ALSO already cleared (possibly before this very line
// runs), this is the call that observes the transition to zero and actually
// fires the deferred job; otherwise it just brings the count down to the
// true "real dependencies still outstanding" number, and whichever later
// OnDependencyCleared() call brings that down to zero does the firing, as
// today.
OnDependencyCleared(continuation);
```

This is the standard "N+1 latch" pattern for exactly this class of bug (the
same shape of problem `JobHandle::AddPendingUnit()` already exists to solve
for a different counter, `JobHandleState::pending` - see that method's own
comment) and closes the race completely: `unmetDependencyCount` can never
drop to zero before this function's own registration loop has finished,
because the sentinel unit this function itself holds is not released until
after that loop returns.

**Review notes / refinements beyond the original proposal:**
- The struct comment on `PendingContinuation::unmetDependencyCount` ("only
  ever decremented... atomically guards against more than one caller
  observing the transition to zero") remains true after this fix, but should
  gain a note that its initial value is `pendingDependencies.size() + 1`, not
  `pendingDependencies.size()`, and why (the sentinel).
- `OnDependencyCleared()`'s own header comment ("Fired once per dependency...")
  should be updated to note it is also called exactly one extra time per
  `ScheduleAfter()` call, for the registration-sentinel release - it is no
  longer strictly "once per dependency".
- Recommend a regression test mirroring HOTFIX 1/2's own verification style
  (see `tests/Jobs/JobContinuationTests.cpp`): schedule `ScheduleAfter()`
  with several (3+) dependency handles, complete them concurrently from
  other threads DURING the registration window (e.g. a small artificial
  delay/interleaving hook, or a stress/`--gtest_repeat` loop under ASan/
  TSan), and confirm no crash/UB - this is exactly the kind of race that can
  pass silently for a long time without such a test.
- The same theoretical truncation caveat already listed under "Minor nits"
  below applies one integer wider here (`pendingDependencies.size() + 1`
  overflowing `uint32_t` requires `size() == UINT32_MAX`) - still purely
  theoretical at current scale, not worth a special-case fix, just noting it
  is unchanged in severity by this proposal.

**Status:** FIXED (HOTFIX 3). The proposed fix above was applied to
`JobContinuation.cpp` exactly as reviewed (the "N+1 latch" sentinel unit,
released via one extra `OnDependencyCleared(continuation)` call right after
the registration loop finishes), plus every "Review notes / refinements"
item above (both doc-comment updates, and the recommended concurrent-
completion-during-registration regression test). See
`JOB_SYSTEM_HOTFIX3_COMPLETION_REPORT.md` for the full writeup.

---

## Minor nits (not urgent)

- `ScheduleAfter()`: `static_cast<std::uint32_t>(pendingDependencies.size())`
  truncates silently if `dependencies.size() > UINT32_MAX`. Purely
  theoretical at current scale, but a `assert`/guard would be free insurance.
- `PollingFallbackContext`'s busy-loop uses `std::this_thread::yield()` with
  no backoff; combined with item 3 above, consider a small sleep escalation
  instead of a tight yield loop.

---

_Generated from an AI code-review pass on 2026-08-27. No code was changed as
part of this pass — this file is tracking/backlog only._

_Item 7 added, and its proposed fix reviewed/refined, during a follow-up pass
on 2026-08-27 (same day). No code was changed as part of THAT follow-up pass
either - `JobContinuation.cpp` still contained the bug at that point._

_HOTFIX 3 (2026-08-27, same day): item 7's reviewed fix was applied to
`JobContinuation.cpp` as-is, plus a new regression test
(`tests/Jobs/JobContinuationTests.cpp`'s
`ScheduleAfterSurvivesConcurrentDependencyCompletionDuringRegistration`). See
`JOB_SYSTEM_HOTFIX3_COMPLETION_REPORT.md` for the full writeup._
