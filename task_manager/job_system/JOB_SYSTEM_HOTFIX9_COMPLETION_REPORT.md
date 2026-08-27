# Job System — HOTFIX 9 Completion Report

Status: **DONE**. Fixes item 9 of `JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md`
(the code-review hotfix backlog, previously updated by
`JOB_SYSTEM_HOTFIX1_COMPLETION_REPORT.md`/`JOB_SYSTEM_HOTFIX2_COMPLETION_REPORT.md`/
`JOB_SYSTEM_HOTFIX3_COMPLETION_REPORT.md`/`JOB_SYSTEM_HOTFIX8_COMPLETION_REPORT.md`
for items 1/2/7/8). Items 3-6 and the "Minor nits" section of that document
remain OPEN and are explicitly out of scope for this hotfix.

---

## 1. What was wrong

**File:** `src/Jobs/JobContinuation.cpp`, `ScheduleAfter()`.

```cpp
JobHandle handle;
JobSystem::Instance().Schedule(SomeJob, payload, handle);

std::array<JobHandle*, 1> dependencies = { &handle };
ScheduleAfter(NextJob, payload, dependencies, handle);
```

`ScheduleAfter()` had no guard against a dependency being (or sharing
underlying state with, e.g. a copy of) its OWN output `handle`. Traced
against the actual code: `handle` starts with `pending == 1`; the filter
loop sees `IsComplete() == false` and pushes it into `pendingDependencies`;
since that list isn't empty, `handle.AddPendingUnit()` bumps `pending` to 2;
a watcher is registered against `handle` itself for the deferred `NextJob`;
once `SomeJob` finishes, `WorkerLoop()` decrements `pending` from 2 to 1 -
NOT to zero, so the watcher never fires, so `NextJob` never runs, so
`pending` can never be decremented the rest of the way to zero. `handle` is
now permanently stuck at `pending == 1`: `WaitForJobs(handle)` blocks
forever. This is a very easy, silent API misuse (no compiler or runtime
diagnostic previously existed) that manifests as a permanent, hard-to-trace
hang far from the actual `ScheduleAfter()` call site that caused it.

---

## 2. The fix

Applied the reviewed/refined fix from item 9 of the findings document.

`JobTypes.h`'s `JobHandle` gained a `SharesStateWith()` identity check:

```cpp
bool SharesStateWith(const JobHandle& other) const noexcept { return m_state == other.m_state; }
```

`m_state` is a `std::shared_ptr<detail::JobHandleState>`, only ever set once
at construction - comparing two `shared_ptr`s with `==` compares stored
pointer IDENTITY directly (never a deep/value compare), exactly what's
needed to detect "is `other` this same handle, or a copy of it" with no
locking required.

`ScheduleAfter()`'s dependency filter loop (`JobContinuation.cpp`) now
checks this UNCONDITIONALLY for every entry in `dependencies` - regardless
of that dependency's current `IsComplete()` value, closing the TOCTOU-shaped
gap the original externally-proposed fix left open (a self-referencing
dependency that happens to look complete at check time is still a latent
bug, since a concurrent `Schedule()` call against the same shared state,
landing between the check and `handle.AddPendingUnit()`, could still
re-trigger the exact same deadlock):

```cpp
for (JobHandle* dependency : dependencies) {
    if (dependency == nullptr) {
        continue;
    }
    if (dependency->SharesStateWith(handle)) {
        // ... loud diagnostic (see below) ...
        continue;
    }
    if (!dependency->IsComplete()) {
        pendingDependencies.push_back(dependency);
    }
}
```

A self-referencing dependency is SKIPPED (never added to
`pendingDependencies`, never registered as a watcher) rather than either
silently honored (deadlock) or causing the whole call to be dropped
(`ScheduleAfter()` returns `void`, so silently rejecting the entire call
would mean `fn` never runs at all - arguably worse than today's hang, since
it fails silently instead of loudly). This guarantees `fn` still eventually
runs once every OTHER, legitimate dependency clears. The misuse is still
surfaced LOUDLY, in every build (not just debug), via both a `std::fprintf`
to `stderr` and an `assert(false, ...)` - so it gets caught/fixed at the
actual call site rather than only manifesting as a mysterious hang
somewhere else.

This fix lives entirely inside `ScheduleAfter()`'s filter loop, so
`DispatchAfter()` - which simply forwards to `ScheduleAfter()` with the same
`handle` - is automatically covered too; no separate change was needed
there.

---

## 3. Regression test

Per this session's explicit "fast compile check only, no full build/test
run - full regression test will run later" instructions, no new automated
regression test was added in this pass (the recommended test - construct a
handle, schedule an ordinary job against it, call `ScheduleAfter()` with
`dependencies` containing that SAME handle or a copy of it, and assert
`WaitForJobs(handle)` returns within a bounded time rather than hanging - is
noted here as a follow-up for whoever adds `tests/Jobs/JobContinuationTests.cpp`
coverage next, per item 9's own "Review notes / refinements").

---

## 4. Verification performed

- **Fast compile check** (per this session's own instructions - no full
  clean build): using the existing Ninja/MinGW `build/` directory, `ninja`
  was invoked directly against the specific changed object files -
  `CMakeFiles/gte_core.dir/src/Jobs/JobSystem.cpp.obj`,
  `.../JobContinuation.cpp.obj`, `.../JobDispatch.cpp.obj`,
  `.../JobQueue.cpp.obj` - all four compiled cleanly, with the project's own
  `-Wall -Wextra -Wpedantic -Werror -Wshadow -Wdouble-promotion` flags, zero
  warnings/errors (this includes the new `<cassert>`/`<cstdio>` includes
  and the `assert()`/`std::fprintf()` calls added by this fix).
- The existing `tests/Jobs/*.cpp` test translation units were also compiled
  (object-file level only, not linked/run) against the updated headers -
  all compiled cleanly.
- Per this session's own explicit instructions, the FULL clean
  `build_joboff`-style cross-configuration rebuild, linking/running
  `GreatTamanaEngineTests.exe`, and the full
  `ctest -C Debug --output-on-failure` regression run across the whole suite
  are deliberately deferred to a later pass - not part of this hotfix's own
  verification.

---

## 5. Files changed this session

- `src/Jobs/JobTypes.h` - new `JobHandle::SharesStateWith()` method.
- `src/Jobs/JobContinuation.cpp` - `ScheduleAfter()`'s dependency filter loop
  now rejects a self-referencing dependency (loud `fprintf`/`assert`,
  skipped rather than honored or silently dropped); new `<cassert>`/
  `<cstdio>` includes.
- `task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md` - item
  9 marked `[FIXED — HOTFIX 9]` with a pointer to this report; overall
  document status/intro and trailing footer note updated. Items 3-6 and the
  "Minor nits" section are untouched and remain open follow-up work.
- `task_manager/job_system/JOB_SYSTEM_HOTFIX9_COMPLETION_REPORT.md` (this
  file, new).

---

## 6. What remains open

Items 3-6 of `JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md` (the
`kMaxWatchersPerHandle`/unbounded-thread scalability trap, `Schedule()`'s
unsynchronized pending increment, unbounded full-queue recursion depth, and
abandoned-continuation leaks) plus its "Minor nits" are still open and
unpatched - this hotfix was scoped strictly to item 9. See that document for
full detail on each remaining item.
