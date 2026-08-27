# Job System — Code Review Hotfix Backlog

Status: **PARTIALLY FIXED.** Captured from an ad-hoc code review of
JobContinuation.cpp/.h, JobDispatch.cpp/.h, JobQueue.cpp/.h, JobSystem.cpp/.h,
and JobTypes.h (Phase 3 continuations + underlying Phase 1/2 machinery).
Item 1 (below) was fixed under **HOTFIX 1** - see
`JOB_SYSTEM_HOTFIX1_COMPLETION_REPORT.md` for the full writeup. Item 2 was
fixed under **HOTFIX 2** - see `JOB_SYSTEM_HOTFIX2_COMPLETION_REPORT.md` for
the full writeup. Item 7 - a CRITICAL use-after-free in `ScheduleAfter()` -
was fixed under **HOTFIX 3** - see `JOB_SYSTEM_HOTFIX3_COMPLETION_REPORT.md`
for the full writeup. Item 8 was fixed under **HOTFIX 8** - see
`JOB_SYSTEM_HOTFIX8_COMPLETION_REPORT.md` for the full writeup. Item 9 was
fixed under **HOTFIX 9** - see `JOB_SYSTEM_HOTFIX9_COMPLETION_REPORT.md` for
the full writeup. Items 3-6 and the "Minor nits" remain OPEN - this file
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

## 8. [HIGH] [FIXED — HOTFIX 8] A continuation can fire against the WRONG completion
   cycle — a watcher registered for a brand-new job can be swept into an
   OLD completion's fired-watcher batch

**Files:** `JobTypes.h` (`detail::JobHandleState::AddWatcher()` /
`FireWatchers()`), `JobSystem.cpp` (`WorkerLoop()`, `Schedule()`,
`ScheduleAlreadyPending()`)

The transition of `pending` to zero and the extraction of the watcher list
to fire are two SEPARATE, unsynchronized steps. `WorkerLoop()`'s decrement
is bracketed only by `m_completionMutex` (for `WaitForJobs()`'s own
lost-wakeup reasons - see that method's own comment); `FireWatchers()`
acquires the handle's OWN, DIFFERENT `watcherMutex` only afterward, once the
worker actually calls it:

```cpp
std::uint32_t previousPending;
{
    std::lock_guard<std::mutex> lock(m_completionMutex);
    previousPending = entry.state->pending.fetch_sub(1, std::memory_order_acq_rel);
}

if (previousPending == 1) {
    entry.state->FireWatchers();   // <-- takes watcherMutex ONLY here, well after the decrement
}
```

Between those two lines, `pending` has already publicly reached zero (any
concurrent `AddWatcher()`/`IsComplete()` caller can observe it), but the
watcher list hasn't been captured/cleared yet - a window is open.

**Concrete failing sequence** (as originally reported):
1. Dependency handle has `pending == 1`.
2. A worker finishes the old job: `fetch_sub` takes `pending` 1 → 0.
   `previousPending == 1` is recorded, but `FireWatchers()` hasn't run yet.
3. Before that worker calls `FireWatchers()`, another thread calls
   `Schedule()` against the SAME handle, incrementing `pending` 0 → 1 for a
   brand-new, unrelated job.
4. A third thread calls `ScheduleAfter()` treating this handle as a
   dependency: it sees `IsComplete() == false` (pending is 1 again) and
   calls `AddCompletionWatcher()` → `AddWatcher()`, which locks
   `watcherMutex`, sees `pending != 0`, and stores the new watcher normally
   (not fired immediately - this is the CORRECT outcome for the NEW job).
5. The original worker now finally calls `FireWatchers()`. It locks
   `watcherMutex`, and - having no idea a new job/watcher exist for a
   completely different completion cycle - captures and fires EVERY
   watcher currently in the list, INCLUDING the one just registered in
   step 4.
6. The continuation from step 4 now runs even though the job scheduled in
   step 3 is still in flight - it fires far too early.

**Why it matters:** this directly breaks `ScheduleAfter()`'s core, documented
promise ("...run only once EVERY handle in `dependencies` has completed -
never before"). `FireWatchers()` never checks whether `pending` is *still*
zero at the moment it actually acquires `watcherMutex` and captures the
list - it unconditionally fires whatever is present, regardless of whether
that list now belongs to a newer completion cycle than the one that
triggered the call.

**Original proposed fix (reviewed and considered directionally correct, but
incomplete as written):**

```cpp
bool CompleteOneAndTakeWatchers(...)
{
    std::lock_guard<std::mutex> lock(watcherMutex);

    const auto previous = pending.fetch_sub(1, std::memory_order_acq_rel);
    if (previous != 1) {
        return false;
    }

    // Move/copy current watchers here while watcherMutex is locked.
    // Clear watcherCount here.
    return true;
}
```

The core idea - make the decrement-to-zero and the watcher-list extraction
one atomic, `watcherMutex`-guarded operation, mirroring `AddWatcher()`'s own
`pending == 0` check under that same lock - is correct and sufficient to
close the race described above: since `AddWatcher()` and this merged
operation are now serialized by the same `watcherMutex`, whichever one runs
first is fully observed by the other (either the new watcher lands in a
freshly-cleared, empty-for-this-cycle list because it registered strictly
after the merged op released the lock, or it's legitimately part of the
batch actually being fired because it registered strictly before). Verified
by re-tracing the exact failing sequence above with the merged operation in
place: the captured/fired list in step 5 is snapshotted and `watcherCount`
cleared BEFORE step 3's increment even matters, so step 4's `AddWatcher()`
call in step 4 necessarily lands in the fresh (post-clear) list, not the one
about to be fired.

**Review notes / refinements needed before this is applied:**
- **The proposal only takes `watcherMutex`, silently dropping the
  `m_completionMutex` bracket `WorkerLoop()` currently relies on.** That
  bracket exists for a DIFFERENT, independent reason (documented at length
  in `WorkerLoop()`'s own comment): to avoid a classic
  `condition_variable` lost-wakeup race against `WaitForJobs()`. Simply
  moving the decrement to be guarded by `watcherMutex` alone would silently
  regress that already-fixed bug. The two concerns must BOTH be satisfied
  simultaneously - `m_completionMutex` must remain the OUTER lock, with the
  new `watcherMutex`-guarded operation nested INSIDE it, e.g.:
  ```cpp
  bool completedToZero;
  std::array<detail::WatcherFunction, detail::kMaxWatchersPerHandle> firedFns{};
  std::array<void*, detail::kMaxWatchersPerHandle> firedContexts{};
  std::size_t firedCount = 0;
  {
      std::lock_guard<std::mutex> lock(m_completionMutex);
      completedToZero = entry.state->CompleteOneAndTakeWatchers(firedFns, firedContexts, firedCount);
  }
  m_completionCondition.notify_all();
  if (completedToZero) {
      for (std::size_t i = 0; i < firedCount; ++i) {
          firedFns[i](firedContexts[i]);
      }
  }
  ```
  No new deadlock risk is introduced by this nesting: nothing else in the
  codebase ever locks `watcherMutex` and then tries to acquire
  `m_completionMutex` in the reverse order (`AddWatcher()`/`FireWatchers()`
  never touch `m_completionMutex` at all), so the lock order
  (`m_completionMutex` outer, `watcherMutex` inner) is consistent
  everywhere it is taken.
- **This must be applied at ALL FIVE places that currently do a raw
  `pending.fetch_sub()` followed by a conditional `FireWatchers()` call**,
  not just conceptually inside `JobHandleState`: `JobSystem::WorkerLoop()`,
  the `GTE_ENABLE_JOB_SYSTEM=ON` full-queue fallback tails of both
  `Schedule()` and `ScheduleAlreadyPending()`, AND the
  `GTE_ENABLE_JOB_SYSTEM=OFF` bodies of `Schedule()`/`ScheduleAlreadyPending()`
  (which have no `m_completionMutex` at all, but are just as vulnerable to
  the underlying `watcherMutex`-only race if called concurrently from
  multiple threads - they should call the new merged operation directly,
  with no outer lock needed since none exists in that configuration).
- `FireWatchers()` itself becomes dead code once every call site above is
  migrated to the new merged operation and should be removed (or repurposed
  as a private helper the merged operation calls internally) rather than
  left alongside it as a second, still-racy way to fire watchers.
- Recommend a regression test that deliberately interleaves (via an
  artificial hook/delay, or a stress/`--gtest_repeat` loop) a `Schedule()`
  call against a handle in the narrow window between its `pending` reaching
  zero and its watchers being fired, then asserts the newly-registered
  watcher for the NEW job is NOT included in the old completion's fired
  batch (e.g. a counter/flag the new job's watcher sets, checked to still be
  false immediately after the old batch fires, and only becomes true once
  the new job itself actually completes).

**Status:** FIXED (HOTFIX 8). The reviewed/refined fix above was applied:
`FireWatchers()` was replaced by
`detail::JobHandleState::CompleteOneAndTakeWatchers()` (`JobTypes.h`),
merging the decrement-to-zero check with the watcher-list extraction into
one `watcherMutex`-guarded operation, and every one of the five call sites
identified above (`JobSystem::WorkerLoop()`; both
`GTE_ENABLE_JOB_SYSTEM=ON` full-queue fallbacks in `Schedule()`/
`ScheduleAlreadyPending()`; both `GTE_ENABLE_JOB_SYSTEM=OFF` bodies of
`Schedule()`/`ScheduleAlreadyPending()`) was migrated to it, with
`m_completionMutex` kept as the OUTER lock (nesting the new
`watcherMutex`-guarded operation inside it) wherever it previously existed.
`FireWatchers()` itself was removed rather than left alongside the new
method. Per this session's own "fast compile check only, full regression
test later" instructions, the recommended regression test was NOT added in
this pass. See `JOB_SYSTEM_HOTFIX8_COMPLETION_REPORT.md` for the full
writeup.

---

## 9. [HIGH] [FIXED — HOTFIX 9] `ScheduleAfter()`/`DispatchAfter()` deadlock permanently
   when the output handle is also (directly, or via a shared copy) one of
   its own dependencies

**File:** `JobContinuation.cpp`, `ScheduleAfter()`

```cpp
JobHandle handle;
JobSystem::Instance().Schedule(SomeJob, payload, handle);

std::array<JobHandle*, 1> dependencies = { &handle };
ScheduleAfter(NextJob, payload, dependencies, handle);
```

**Traced against the actual current code:** `handle` starts with
`pending == 1` (one job in flight). `ScheduleAfter()`'s initial filter loop
sees `dependency == &handle`, `IsComplete()` is false (`pending == 1`), so
it is pushed into `pendingDependencies`. Since that list isn't empty,
`handle.AddPendingUnit()` runs, bumping `pending` to 2. A
`PendingContinuation` is created (post-HOTFIX-3, `unmetDependencyCount =
pendingDependencies.size() + 1 = 2`) and a watcher is registered against
`handle` itself via `WatchDependencyWithFallback()`. The loop's own
sentinel release (`OnDependencyCleared(continuation)`) brings
`unmetDependencyCount` down to 1 - not zero, so nothing fires yet.
`SomeJob` then finishes: `WorkerLoop()` decrements `handle`'s `pending` from
2 to 1 - NOT to zero, so `FireWatchers()` never runs, so the watcher
registered for `NextJob` never fires, so `NextJob` is never scheduled via
`ScheduleAlreadyPending()`, so `pending` can never be decremented the rest
of the way to zero. `handle` is now permanently stuck at `pending == 1`:
`WaitForJobs(handle)` blocks forever, and `NextJob` never runs. The same
deadlock is reachable with a *copy* of `handle` inside `dependencies`, since
copies share the same underlying `JobHandleState`.

**Why it matters:** this is a very easy, silent API misuse (no compiler or
runtime diagnostic today) that causes a permanent hang rather than an
immediately-visible error - exactly the kind of bug that is trivial to
introduce by accident (e.g. reusing a "stage complete" handle as both a
dependency and the next stage's own output handle) and painful to diagnose
after the fact (it manifests as an indefinite `WaitForJobs()` hang far away
from the actual `ScheduleAfter()` call site that caused it).

**Original proposed fix (reviewed - directionally correct, refined below):**
add an identity-comparison helper and reject the self-dependency case before
`handle.AddPendingUnit()`:

```cpp
bool SharesStateWith(const JobHandle& other) const noexcept
{
    return m_state == other.m_state;
}
```
```cpp
for (JobHandle* dependency : dependencies) {
    if (dependency != nullptr && dependency->SharesStateWith(handle)) {
        // assert, log error, or return an error result
    }
}
```

**Review notes / refinements:**
- `SharesStateWith()` itself is correct as proposed: `JobHandle::m_state` is
  a `std::shared_ptr<detail::JobHandleState>`, and comparing two
  `shared_ptr`s with `==` compares the stored pointer identity directly (not
  a deep/value compare) - exactly what's needed here, and it requires no
  locking since `m_state` is only ever set once, at construction (or via
  ordinary copy, itself the caller's own responsibility to not race).
- **"Assert, log, or return an error result" isn't directly actionable as
  written, because `ScheduleAfter()` returns `void`.** Silently rejecting
  the whole call (returning without scheduling anything) would mean `fn`
  never runs at all - arguably a WORSE outcome than today's hang, since it
  fails silently instead of loudly. The recommended concrete behavior
  instead: **treat the self-referencing dependency as already satisfied and
  skip it** (never add it to `pendingDependencies`, regardless of its
  current `IsComplete()` state - see next point for why "regardless of
  current state" matters), while still surfacing the misuse loudly via an
  assert/log in every build so it gets fixed at the actual call site. This
  guarantees `fn` still eventually runs (once every OTHER, legitimate
  dependency clears) instead of deadlocking OR silently vanishing.
- **The check must run unconditionally for every entry in `dependencies`,
  not only for ones that already look "pending"** - i.e. compare identity
  regardless of the dependency's current `IsComplete()` value. A
  self-referencing dependency that happens to be complete (`pending == 0`)
  at the exact moment of the check is still a latent bug: a concurrent
  `Schedule()` call against that same shared state from another thread,
  landing between the check and `handle.AddPendingUnit()`, would still be
  perfectly capable of re-triggering the exact same deadlock. Checking
  identity unconditionally (not gated behind the pre-existing
  `!IsComplete()` filter) closes that TOCTOU-shaped gap entirely rather than
  narrowing it.
- Concretely, this folds into `ScheduleAfter()`'s existing filter loop:
  ```cpp
  for (JobHandle* dependency : dependencies) {
      if (dependency == nullptr) {
          continue;
      }
      if (dependency->SharesStateWith(handle)) {
          // Self-dependency: honoring this would deadlock `handle` against
          // itself (see JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md, item 9).
          // Never register a watcher for it - treat it as already
          // satisfied instead of silently dropping the whole continuation,
          // so `fn` still eventually runs once every OTHER dependency
          // clears. Loud in every build, not just debug, since this is a
          // caller bug that otherwise manifests as a silent, hard-to-trace
          // permanent hang far from this call site.
          GTE_JOBS_LOG_ERROR("ScheduleAfter()/DispatchAfter(): a dependency "
              "must never be (or share underlying state with) its own "
              "output handle - ignoring this dependency to avoid a "
              "permanent deadlock.");
          assert(false && "ScheduleAfter()/DispatchAfter(): self-dependency on output handle");
          continue;
      }
      if (!dependency->IsComplete()) {
          pendingDependencies.push_back(dependency);
      }
  }
  ```
  (`GTE_JOBS_LOG_ERROR`/`assert` are placeholders - use whichever
  logging/assert facility this codebase actually standardizes on; the
  important part is that the self-reference is skipped rather than either
  silently honored or silently dropping the whole call.)
- This fix lives entirely inside `ScheduleAfter()`, so `DispatchAfter()` -
  which simply forwards to `ScheduleAfter()` with the same `handle` - is
  automatically covered too; no separate change is needed there.
- Recommend a regression test mirroring HOTFIX 1/2/3's own style: construct
  a handle, schedule an ordinary job against it, then call `ScheduleAfter()`
  with `dependencies` containing a pointer to (or a copy of) that SAME
  handle, and assert that `WaitForJobs(handle)` still returns within a
  bounded time (i.e. does NOT hang) once the original job and the
  continuation both finish - this is exactly the scenario that hangs
  forever under the current, unfixed code.

**Status:** FIXED (HOTFIX 9). The proposed fix above was applied to
`JobTypes.h` (`JobHandle::SharesStateWith()`) and `JobContinuation.cpp`
(`ScheduleAfter()`'s filter loop) as refined by the "Review notes /
refinements" above - the self-dependency check runs unconditionally for
every entry in `dependencies` (not gated behind `!IsComplete()`), and a
matching dependency is skipped (never registered as a watcher) rather than
honored or silently dropping the whole call, with a loud `std::fprintf` +
`assert()` diagnostic in every build. Per this session's own "fast compile
check only, full regression test later" instructions, the recommended
regression test was NOT added in this pass. See
`JOB_SYSTEM_HOTFIX9_COMPLETION_REPORT.md` for the full writeup.

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

_Items 8 and 9 added during a further follow-up pass on 2026-08-27 (same
day), evaluating two externally-proposed fixes against the actual current
code. Both proposals identified genuine, reachable bugs, but neither was
applied to the code as originally written - item 8's proposal omitted the
required `m_completionMutex` nesting (and missed 4 of the 5 call sites that
need updating); item 9's proposal specified an unavailable `void`-returning
error path and didn't handle the TOCTOU case where a self-dependency looks
already-complete at check time. Both items' proposed fixes were refined
above to close those gaps. No code was changed as part of this pass either -
`JobTypes.h`/`JobSystem.cpp`/`JobContinuation.cpp` still contain both bugs as
of this writing._

_HOTFIX 8 (2026-08-27, same day): item 8's reviewed fix was applied to
`JobTypes.h`/`JobSystem.cpp` as refined (`FireWatchers()` replaced by
`JobHandleState::CompleteOneAndTakeWatchers()`, merging the decrement and
watcher-list capture under `watcherMutex`, applied at all five call sites
with `m_completionMutex` kept as the outer lock where it previously
existed). See `JOB_SYSTEM_HOTFIX8_COMPLETION_REPORT.md` for the full
writeup. Per this session's own "fast compile check only, full regression
test later" instructions, no new automated regression test was added for
this item in this pass - see that report's own "Regression test" section._

_HOTFIX 9 (2026-08-27, same day): item 9's reviewed fix was applied to
`JobTypes.h`/`JobContinuation.cpp` as refined (`JobHandle::SharesStateWith()`
added; `ScheduleAfter()`'s dependency filter loop now unconditionally skips
- with a loud `fprintf`/`assert` diagnostic - any dependency that shares
underlying state with its own output `handle`, rather than honoring it and
deadlocking). See `JOB_SYSTEM_HOTFIX9_COMPLETION_REPORT.md` for the full
writeup. Per this session's own "fast compile check only, full regression
test later" instructions, no new automated regression test was added for
this item in this pass - see that report's own "Regression test" section.
