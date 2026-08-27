# Job System — HOTFIX 8 Completion Report

Status: **DONE**. Fixes item 8 of `JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md`
(the code-review hotfix backlog, previously updated by
`JOB_SYSTEM_HOTFIX1_COMPLETION_REPORT.md`/`JOB_SYSTEM_HOTFIX2_COMPLETION_REPORT.md`/
`JOB_SYSTEM_HOTFIX3_COMPLETION_REPORT.md` for items 1/2/7). Items 3-6 and the
"Minor nits" section of that document remain OPEN and are explicitly out of
scope for this hotfix. Item 9 was fixed in the SAME session - see
`JOB_SYSTEM_HOTFIX9_COMPLETION_REPORT.md`.

---

## 1. What was wrong

**Files:** `src/Jobs/JobTypes.h` (`detail::JobHandleState::FireWatchers()`),
`src/Jobs/JobSystem.cpp` (`WorkerLoop()`, `Schedule()`,
`ScheduleAlreadyPending()`, in both the `GTE_ENABLE_JOB_SYSTEM=ON` and `=OFF`
configurations).

The transition of a `JobHandleState`'s `pending` counter to zero, and the
extraction of its watcher list to actually fire, were two SEPARATE,
unsynchronized steps guarded by two DIFFERENT mutexes:

```cpp
// JobSystem::WorkerLoop() (ON configuration)
std::uint32_t previousPending;
{
    std::lock_guard<std::mutex> lock(m_completionMutex);
    previousPending = entry.state->pending.fetch_sub(1, std::memory_order_acq_rel);
}

if (previousPending == 1) {
    entry.state->FireWatchers();   // takes watcherMutex ONLY here, well after the decrement
}
```

Between those two steps, `pending` had already publicly reached zero (any
concurrent `AddWatcher()`/`IsComplete()` caller could observe it), but the
watcher list hadn't been captured/cleared yet - a race window was open. A
concurrent `Schedule()` call against the SAME handle, landing inside that
window (bumping `pending` back up to 1 for a brand-new, unrelated job),
followed by a `ScheduleAfter()` call treating this handle as a dependency,
would register its own watcher normally via `AddWatcher()` (since `pending`
was non-zero again at that instant) - but then, once the ORIGINAL worker
finally got around to calling `FireWatchers()`, it would capture and fire
EVERY watcher currently in the list, including the one just registered for
the brand-new job, even though that job was still in flight. This directly
broke `ScheduleAfter()`'s core documented promise ("...run only once EVERY
handle in `dependencies` has completed - never before").

---

## 2. The fix

Applied the reviewed/refined fix from item 8 of the findings document: merge
the pending-count decrement-to-zero check with the watcher-list extraction
into ONE operation, both guarded by the SAME `watcherMutex` `AddWatcher()`
already takes, so the two can never observe each other's state
inconsistently.

`JobTypes.h`'s `detail::JobHandleState` gained
`CompleteOneAndTakeWatchers()`, replacing the old, separately-locked
`FireWatchers()`:

```cpp
bool CompleteOneAndTakeWatchers(std::array<WatcherFunction, kMaxWatchersPerHandle>& outFns,
    std::array<void*, kMaxWatchersPerHandle>& outContexts, std::size_t& outCount)
{
    std::lock_guard<std::mutex> lock(watcherMutex);
    const std::uint32_t previous = pending.fetch_sub(1, std::memory_order_acq_rel);
    if (previous != 1) {
        outCount = 0;
        return false;
    }
    outCount = watcherCount;
    outFns = watcherFns;
    outContexts = watcherContexts;
    watcherCount = 0;
    return true;
}
```

Since `AddWatcher()` and this new method are now fully serialized against
each other by `watcherMutex`, whichever one runs first is completely
finished (list captured-and-cleared, or a new watcher safely stored) before
the other can observe any state at all - a watcher registered for a NEW
completion cycle (bumped `pending` back up) can never be swept into an OLD
cycle's fired batch, because either it registers strictly before the
decrement+capture (and is legitimately part of that batch, correctly, since
it was genuinely a dependent of the completion that already happened) or
strictly after (and lands in a freshly-cleared, empty list for the new
cycle).

### All five call sites updated, with the outer-lock nesting the review notes required

Per item 8's own "Review notes / refinements", the merged operation had to
be applied at every place that previously did a raw `pending.fetch_sub()`
followed by a conditional `FireWatchers()` call, and — critically — the
pre-existing `m_completionMutex` bracket around the decrement (which exists
for a DIFFERENT, independent reason: closing a classic
`condition_variable` lost-wakeup race against `WaitForJobs()`, documented at
length in `WorkerLoop()`'s own comment) had to remain the OUTER lock, with
`CompleteOneAndTakeWatchers()`'s own internal `watcherMutex` nested INSIDE
it — never replaced by it:

1. **`JobSystem::WorkerLoop()`** (`GTE_ENABLE_JOB_SYSTEM=ON`) - the decrement
   is now `entry.state->CompleteOneAndTakeWatchers(...)`, called inside the
   same `m_completionMutex` lock_guard the old `fetch_sub()` was; the
   returned watchers are fired after that lock is released, exactly
   mirroring the old `FireWatchers()` call's position relative to the lock.
2. **`JobSystem::Schedule()`'s full-queue fallback** (ON configuration) -
   same pattern.
3. **`JobSystem::ScheduleAlreadyPending()`'s full-queue fallback** (ON
   configuration) - same pattern.
4. **`JobSystem::Schedule()`** (`GTE_ENABLE_JOB_SYSTEM=OFF`) - no
   `m_completionMutex` exists in this configuration at all (see that class's
   own header comment), so `CompleteOneAndTakeWatchers()` is called directly,
   with no outer lock - safe, since `JobHandleState`'s own internal
   `watcherMutex` fully guards the operation against a concurrent
   `AddWatcher()` call regardless of which thread calls either one.
5. **`JobSystem::ScheduleAlreadyPending()`** (`GTE_ENABLE_JOB_SYSTEM=OFF`) -
   same as #4.

No new deadlock risk was introduced by the `m_completionMutex`-outer/
`watcherMutex`-inner nesting in cases 1-3: nothing else in the codebase ever
locks `watcherMutex` and then tries to acquire `m_completionMutex` in the
reverse order (`AddWatcher()`/the old `FireWatchers()` never touched
`m_completionMutex` at all), so the lock order
(`m_completionMutex` outer, `watcherMutex` inner) is consistent everywhere
it is taken.

`FireWatchers()` itself was removed entirely (not left alongside the new
method as dead/second code path) once every call site above was migrated -
every doc comment that referenced it (`JobHandleState`'s own struct
comment, `AddWatcher()`'s HOTFIX 1 comment, `JobHandle::AddCompletionWatcher()`'s
comment) was updated to point at `CompleteOneAndTakeWatchers()` instead.

---

## 3. Regression test

Per this session's explicit "fast compile check only, no full build/test
run - full regression test will run later" instructions, no new automated
regression test was added in this pass. The existing
`tests/Jobs/JobContinuationTests.cpp`/`JobSystemTests.cpp` suites already
exercise `ScheduleAfter()`/`Schedule()` against shared handles under
concurrent load (see `AGENTS.md`, "Job System", on this module's own
stress-repeat discipline) and will be re-run in full once the deferred full
regression pass happens - a dedicated regression test reproducing this
EXACT race (a `Schedule()` call landing inside the old window between
decrement and watcher-capture) would need an artificial delay/interleaving
hook this codebase does not currently have, mirroring the same
"not directly reproducible as a guaranteed, deterministic repro" caveat
`JOB_SYSTEM_HOTFIX3_COMPLETION_REPORT.md` already notes for the analogous
item 7 fix - left as a follow-up for whoever revisits this area next,
consistent with this session's own scope.

---

## 4. Verification performed

- **Fast compile check** (per this session's own instructions - no full
  clean build): using the existing Ninja/MinGW `build/` directory, `ninja`
  was invoked directly against the specific changed object files -
  `CMakeFiles/gte_core.dir/src/Jobs/JobSystem.cpp.obj`,
  `.../JobContinuation.cpp.obj`, `.../JobDispatch.cpp.obj`,
  `.../JobQueue.cpp.obj` (the whole `src/Jobs/` translation unit set, since
  `JobTypes.h` is a shared header all of them include) - all four compiled
  cleanly, with the project's own `-Wall -Wextra -Wpedantic -Werror -Wshadow
  -Wdouble-promotion` flags, zero warnings/errors.
- The existing `tests/Jobs/*.cpp` test translation units
  (`JobQueueTests.cpp`, `JobSystemTests.cpp`, `JobDispatchMathTests.cpp`,
  `JobDispatchTests.cpp`, `JobContinuationTests.cpp`) were also compiled
  (object-file level only, not linked/run) against the updated headers, to
  confirm nothing in the existing test suite's own usage of
  `JobHandle`/`JobSystem`'s public API broke source-compatibility - all
  compiled cleanly.
- Per this session's own explicit instructions, the FULL clean
  `build_joboff`-style cross-configuration rebuild, linking/running
  `GreatTamanaEngineTests.exe`, and the full
  `ctest -C Debug --output-on-failure` regression run across the whole suite
  are deliberately deferred to a later pass - not part of this hotfix's own
  verification.

---

## 5. Files changed this session

- `src/Jobs/JobTypes.h` - `FireWatchers()` replaced by
  `CompleteOneAndTakeWatchers()`; every doc comment referencing the old
  method updated.
- `src/Jobs/JobSystem.cpp` - all five `pending.fetch_sub()` +
  conditional-`FireWatchers()` call sites (`WorkerLoop()`, both
  `GTE_ENABLE_JOB_SYSTEM=ON` full-queue fallbacks, both
  `GTE_ENABLE_JOB_SYSTEM=OFF` bodies) migrated to
  `CompleteOneAndTakeWatchers()`, with `m_completionMutex` kept as the outer
  lock where it previously existed.
- `task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md` - item
  8 marked `[FIXED — HOTFIX 8]` with a pointer to this report; overall
  document status/intro and trailing footer note updated. Items 3-6 and the
  "Minor nits" section are untouched and remain open follow-up work.
- `task_manager/job_system/JOB_SYSTEM_HOTFIX8_COMPLETION_REPORT.md` (this
  file, new).

---

## 6. What remains open

Items 3-6 of `JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md` (the
`kMaxWatchersPerHandle`/unbounded-thread scalability trap, `Schedule()`'s
unsynchronized pending increment, unbounded full-queue recursion depth, and
abandoned-continuation leaks) plus its "Minor nits" are still open and
unpatched - this hotfix was scoped strictly to item 8. See that document for
full detail on each remaining item.
