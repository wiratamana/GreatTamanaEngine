# Job System — HOTFIX 3 Completion Report

Status: **DONE**. Fixes item 7 of `JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md`
(the code-review hotfix backlog, previously updated by
`JOB_SYSTEM_HOTFIX1_COMPLETION_REPORT.md`/`JOB_SYSTEM_HOTFIX2_COMPLETION_REPORT.md`
for items 1/2). Items 3-6 and the "Minor nits" section of that document
remain OPEN and are explicitly out of scope for this hotfix.

---

## 1. What was wrong

**File:** `src/Jobs/JobContinuation.cpp`, `ScheduleAfter()` /
`PendingContinuation` / `OnDependencyCleared()`.

```cpp
PendingContinuation* continuation = new PendingContinuation{
    fn, payload, handle, std::atomic<std::uint32_t>(static_cast<std::uint32_t>(pendingDependencies.size()))
};

for (JobHandle* dependency : pendingDependencies) {
    WatchDependencyWithFallback(*dependency, continuation);
}
```

`unmetDependencyCount` was initialized to EXACTLY `pendingDependencies.size()`
- the real number of dependencies this continuation waits on - then the loop
above registered a watcher against each one, ONE AT A TIME.
`WatchDependencyWithFallback()`/`AddWatcher()` may invoke
`OnDependencyCleared()` **synchronously, from inside this same loop**, either
because a dependency is already complete by the time it is actually
registered (a real race window - the earlier `!dependency->IsComplete()`
filter is only a snapshot, taken before this loop starts) or because a
background job on another thread completes a still-pending dependency while
this loop is running:

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

**Why it mattered:** if two (or more) of `pendingDependencies` cleared before
the registration loop finished iterating over all of them - entirely
plausible any time `pendingDependencies.size() >= 2` and dependencies are
themselves being completed concurrently by other worker threads, or simply
finish between the pre-loop `IsComplete()` snapshot and their own
registration - `unmetDependencyCount` could reach zero, and `continuation`
could be `delete`d, **while the loop was still running**. Every subsequent
iteration of the loop would then call
`WatchDependencyWithFallback(*dependency, continuation)` with an
already-freed pointer: a textbook use-after-free, reachable from the public
API (`ScheduleAfter()`/`DispatchAfter()`) any time a caller passes 2 or more
dependency handles, with no need for any unusual timing beyond ordinary
concurrent job completion.

---

## 2. The fix

Applied exactly the fix reviewed and proposed under item 7 of
`JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md` - the standard "N+1 latch"
pattern (the same shape of problem `JobHandle::AddPendingUnit()` already
solves for a different counter, `JobHandleState::pending`): initialize
`unmetDependencyCount` to `pendingDependencies.size() + 1` - one extra,
synthetic "registration in progress" unit that only the thread running
`ScheduleAfter()` itself ever owns - run the registration loop exactly as
before, then release that synthetic unit, via one extra
`OnDependencyCleared(continuation)` call, only once every real dependency
has actually been registered:

```cpp
// HOTFIX 3: +1 sentinel unit - represents "this registration loop is
// still in progress", NOT a real dependency. This is what stops
// unmetDependencyCount from EVER being able to reach zero (and
// `continuation` from being deleted) while the loop below is still
// running, no matter how many of pendingDependencies clear out from
// under it concurrently - the count can only actually reach zero once
// (a) every real dependency has cleared AND (b) this function has
// released its own registration-in-progress unit below; those two
// things may happen in either order.
PendingContinuation* continuation = new PendingContinuation{
    fn, payload, handle,
    std::atomic<std::uint32_t>(static_cast<std::uint32_t>(pendingDependencies.size() + 1))
};

for (JobHandle* dependency : pendingDependencies) {
    WatchDependencyWithFallback(*dependency, continuation);
}

// Release the registration-in-progress sentinel now that every
// dependency in pendingDependencies has been safely registered against
// `continuation` - if every one of them ALSO already cleared (possibly
// before this very line runs), this is the call that observes the
// transition to zero and actually fires the deferred job; otherwise it
// just brings the count down to the true "real dependencies still
// outstanding" number, and whichever later OnDependencyCleared() call
// brings that down to zero does the firing, as before this hotfix.
OnDependencyCleared(continuation);
```

This closes the race completely: `unmetDependencyCount` can never drop to
zero before `ScheduleAfter()`'s own registration loop has finished, because
the sentinel unit it holds is not released until after that loop returns -
`continuation` therefore can never be freed while a later loop iteration
still holds and dereferences the same pointer.

### Doc-comment updates (per item 7's own "Review notes / refinements")

Both required doc-comment updates called out in the findings document were
applied:

- `PendingContinuation`'s own struct comment now explicitly documents that
  `unmetDependencyCount` is initialized to `pendingDependencies.size() + 1`,
  not just `pendingDependencies.size()`, and explains the sentinel's
  purpose.
- `OnDependencyCleared()`'s own header comment no longer says "Fired once
  per dependency" - it now documents that it is also called exactly one
  extra time per `ScheduleAfter()` call, for the registration-sentinel
  release.

No other file needed to change - every other call site into
`OnDependencyCleared()`/`WatchDependencyWithFallback()` (the direct-watcher
path and the polling-fallback path, both added under HOTFIX 2) already treat
"a dependency cleared" as an opaque event that decrements the shared counter
by exactly one; neither needed to know the counter's initial value changed.

---

## 3. Regression test added

`tests/Jobs/JobContinuationTests.cpp` gained
`ScheduleAfterSurvivesConcurrentDependencyCompletionDuringRegistration`, per
item 7's own "Review notes / refinements" recommendation: schedules 4 real
dependency jobs (real work racing to completion concurrently with
`ScheduleAfter()`'s own registration loop when `GTE_ENABLE_JOB_SYSTEM` is
`ON` - each is scheduled onto the real worker pool immediately before
`ScheduleAfter()` is called, so by the time the registration loop iterates
over all 4, any number of them may have already completed on another
thread), then calls `ScheduleAfter()` against all 4 at once and asserts the
continuation and every dependency eventually completes correctly - repeated
300 times (concurrent scheduling races are exactly the class of bug that can
pass by luck on a single run - see `AGENTS.md`, "Job System") to maximize
the chance of actually landing inside the exact registration window a
dependency's own completion used to race against.

This test's real value is simply "this doesn't crash/hang" - before this
fix, hitting the race window here would corrupt/dereference a freed
`PendingContinuation*`, which is undefined behavior (may or may not crash
reliably depending on the allocator/heap state at the time), rather than
something a plain `EXPECT_*` on returned values alone would catch
deterministically. Ran filtered
(`GreatTamanaEngineTests.exe --gtest_filter=JobContinuationTests.*`)
immediately after adding it - all 11 tests in that suite, including the new
one, passed, with no crash.

**Not directly reproducible as a guaranteed, deterministic repro of the
OLD bug:** the underlying race depends on real OS thread scheduling
(a dependency's own worker thread happening to complete it while the main
thread's registration loop is still mid-iteration) - there is no artificial
delay/interleaving hook in this codebase to force that ordering
deterministically, and adding one purely for a hotfix regression test was
judged out of scope (see item 7's own "Review notes" - it suggested this
same style of stress test, not a deterministic hook, as the practical
option). The 300-iteration stress repeat is the same category of evidence
this campaign has already relied on for other concurrency bugs (see HOTFIX 1
completion report's own reentrant-watcher test, and
`JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md`'s own stress-repeat
discipline for `OverflowingWatcherCapacityStillRunsEveryContinuation`, etc.).

---

## 4. Verification performed

- **Fast compile check** (per this session's own instructions - no full
  clean build): `cmake --build build --target gte_core` against the
  existing Ninja/MinGW `build/` directory recompiled exactly the one
  changed file (`src/Jobs/JobContinuation.cpp`) and relinked
  `libgte_core.a` cleanly, with zero warnings/errors.
- `cmake --build build --target GreatTamanaEngineTests` (same existing
  build directory) recompiled the one changed test file
  (`tests/Jobs/JobContinuationTests.cpp`) and relinked
  `GreatTamanaEngineTests.exe` cleanly.
- Ran `GreatTamanaEngineTests.exe --gtest_filter=JobContinuationTests.*`
  directly: **11/11 tests passed**, including the new
  `ScheduleAfterSurvivesConcurrentDependencyCompletionDuringRegistration`
  regression test and every pre-existing test in that file (proving this
  fix did not change any OBSERVABLE behavior of the already-passing tests,
  e.g. `OverflowingWatcherCapacityStillRunsEveryContinuation`,
  `FanInWaitsForEveryDependencyBeforeRunning`,
  `DispatchAfterDefersEveryBatchUntilDependencyClears`).
- Per this session's own explicit instructions, the FULL clean
  `build_joboff`-style cross-configuration rebuild and the full
  `ctest -C Debug --output-on-failure` regression run across the WHOLE
  suite are deliberately deferred to a later pass ("full build and
  regression test will perform later after everything done") - not part of
  this hotfix's own verification.

---

## 5. Files changed this session

- `src/Jobs/JobContinuation.cpp` - `PendingContinuation`'s struct comment
  and `unmetDependencyCount`'s initial value (`+ 1` sentinel);
  `OnDependencyCleared()`'s header comment; `ScheduleAfter()`'s
  continuation-construction/registration-loop/sentinel-release sequence.
- `tests/Jobs/JobContinuationTests.cpp` - new regression test
  `ScheduleAfterSurvivesConcurrentDependencyCompletionDuringRegistration`
  described in §3 above.
- `task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md` - item
  7 marked `[FIXED — HOTFIX 3]` with a pointer to this report; overall
  document status/intro and trailing footer note updated. Items 3-6 and the
  "Minor nits" section are untouched and remain open follow-up work.
- `task_manager/job_system/JOB_SYSTEM_HOTFIX3_COMPLETION_REPORT.md` (this
  file, new).

---

## 6. What remains open

Items 3-6 of `JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md` (the
`kMaxWatchersPerHandle`/unbounded-thread scalability trap, `Schedule()`'s
unsynchronized pending increment, unbounded full-queue recursion depth, and
abandoned-continuation leaks) plus its "Minor nits" are still open and
unpatched - this hotfix was scoped strictly to item 7 per this session's own
instructions ("HOTFIX 7" in the user's original request, mapped onto this
backlog's own next-unfixed item, item 7 / HOTFIX 3). See that document for
full detail on each remaining item.
