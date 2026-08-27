# Job System — Phase 2 (Parallel-For / Batch Dispatch) — Completion Report

Status: **DONE**. This report documents what was actually implemented,
verified, and fixed during this session, for whoever picks up Phase 3
(`JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md`) next.

Parent strategy: `JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md`.
Phase strategy followed: `JOBSYSTEM_PHASE2_PARALLEL_FOR_DISPATCH.md` (v1,
unchanged — held up completely against the real Phase 1 code with no
revision needed).
Previous phase: `JOB_SYSTEM_PHASE1_COMPLETION_REPORT.md` (Core Threading
Foundation — `JobHandle`/`Schedule()`/`WaitForJobs()`/`WorkerCount()`).

---

## 1. What was built

Two new files added to the existing, always-compiled `src/Jobs/` module
(no dependency on `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL`, same tier
as Phase 1's own `JobQueue.h/.cpp`/`JobSystem.h/.cpp`):

```
src/Jobs/
    JobDispatch.h   - BatchRange, BatchJobFunction, ComputeBatchRanges(), Dispatch()
    JobDispatch.cpp
tests/Jobs/
    JobDispatchMathTests.cpp   - Tier 1, pure math, zero real threads, always built
    JobDispatchTests.cpp       - Tier 1, real JobSystem::Instance(), always built
```

### 1.1 — `ComputeBatchRanges()` (`JobDispatch.h/.cpp`)

A pure, deterministic function with no `JobSystem`/thread dependency at
all: splits `[0, itemCount)` into a bounded number of contiguous,
non-overlapping, gap-free `BatchRange`s.

- `itemCount == 0` → an empty result (no batches at all).
- `itemCount < minItemsPerBatch` (or `== `) → collapses to exactly ONE
  batch covering the whole range — never splits smaller than the caller's
  own stated per-batch cost floor.
- Otherwise → `batchCount = clamp(workerCount, 1, itemCount /
  max(1, minItemsPerBatch))`, then `itemCount` is distributed as evenly as
  possible across `batchCount` batches (the first `itemCount % batchCount`
  batches get exactly one extra item — the same "as even as possible"
  splitting convention this engine's primitive-mesh generators already use
  elsewhere).
- A degenerate `workerCount == 0` or `minItemsPerBatch == 0` is silently
  treated as `1` rather than dividing by zero or producing zero batches
  for a non-zero `itemCount`.

### 1.2 — `Dispatch()` (`JobDispatch.h/.cpp`)

```cpp
void Dispatch(BatchJobFunction fn, std::uint32_t itemCount, void* payload,
    JobHandle& handle, std::uint32_t minItemsPerBatch = 1);
```

Built entirely on top of Phase 1's `JobSystem::Schedule()`/`WaitForJobs()`/
`JobHandle` — no second scheduler, no second queue, no parallel bookkeeping
duplicated. For each `BatchRange` `ComputeBatchRanges()` produces (using
`JobSystem::Instance().WorkerCount()` as the batch-count ceiling),
`Dispatch()` heap-allocates a small `DispatchJobContext` (function pointer +
user payload pointer + that batch's own range) and calls `Schedule()` with
a trampoline (`RunBatchJobTrampoline()`) that invokes `fn(beginIndex,
endIndex, payload)` and then frees the context. `itemCount == 0` is an
immediate, job-free no-op.

**The one necessary, explicitly-documented exception to Phase 1's "zero
heap allocation in the steady-state per-job path" guarantee**: each
batch's `DispatchJobContext` is a real `new`/`delete` pair, required
because each batch needs its own distinct `[begin, end)` range that must
outlive `Dispatch()` returning (it does not block). This is bounded —
at most `WorkerCount()` allocations per `Dispatch()` call, never one per
array element — and is commented at the exact `new` call site plus in
`AGENTS.md`'s "Job System" section, per the strategy document's own
instruction to make this impossible for a future reader to mistake for an
accidental violation.

---

## 2. Definition of Done — checked against Phase 2's own strategy doc

1. ✅ The exact `Dispatch(BatchJobFunction, count, payload, handle,
   minItemsPerBatch)`-shaped API from the original brief exists.
2. ✅ Correctly splits `count` units of work into a bounded number of
   batches — never one job per element for a large `count` (verified by
   `JobDispatchMathTests.cpp`'s `NeverProducesMoreBatchesThanWorkerCount`
   and the fuzz test).
3. ✅ Built entirely as a thin layer on top of Phase 1's `Schedule()`/
   `WaitForJobs()` — no parallel bookkeeping duplicated (grepped: `Dispatch()`
   calls nothing but `JobSystem::Instance().Schedule()`/`WorkerCount()`).
4. ✅ Has its own Tier-1 batch-splitting-math tests
   (`JobDispatchMathTests.cpp`, 11 tests including a fuzz sweep across
   `(itemCount, workerCount, minItemsPerBatch)` triples) plus a real-execution
   correctness test (`JobDispatchTests.cpp`, 6 tests, using a real
   `JobSystem::Instance()`).
5. ✅ Still used by nothing outside `tests/Jobs/` — grepped the codebase;
   only the two new test files reference `Jobs::Dispatch`/
   `Jobs::ComputeBatchRanges` at all.

---

## 3. Tests added

- `tests/Jobs/JobDispatchMathTests.cpp` (11 tests) — zero `itemCount`;
  `itemCount` smaller than or exactly equal to `minItemsPerBatch`
  (collapses to one batch either way); evenly-divisible and
  non-evenly-divisible splits (checked for full coverage, no gaps/overlap,
  and batch sizes never differing by more than 1); never more batches
  than `workerCount`; `workerCount == 1`; `itemCount == 1`; degenerate
  zero `workerCount`/`minItemsPerBatch` inputs; and a fuzz-style sweep
  across 11 `itemCount` values × 6 `workerCount` values × 6
  `minItemsPerBatch` values (396 combinations), asserting the three core
  invariants (contiguous/gap-free, no overlap, exact `[0, itemCount)`
  coverage) hold for every single one.
- `tests/Jobs/JobDispatchTests.cpp` (6 tests) — a zero-`itemCount` call is
  an immediate no-op that never runs the supplied function; a real
  `Dispatch()`+`WaitForJobs()` round-trip against an evenly-divisible
  workload (1024 items) and a deliberately prime, non-evenly-divisible
  workload (1009 items) both write every index exactly once with no
  missed/double-written slot; a workload smaller than a deliberately large
  `minItemsPerBatch` exercises the "collapses to one batch" path for real,
  under genuine concurrent execution (not just the pure-math test); a
  single-item workload; and a 25-iteration stress-repeat of independent
  dispatches (777 items each), matching this campaign's own "never trust
  a single passing run for genuinely concurrent code" discipline.

Both files are added to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`
**unconditionally** — `JobDispatch.h/.cpp` always compiles (no
`GTE_ENABLE_JOB_SYSTEM` dependency of its own; it only calls into
`JobSystem`, whose OWN internals are what that switch gates), matching
Phase 1's own "always built" bucket for `JobQueueTests.cpp`/
`JobSystemTests.cpp`.

---

## 4. Verification performed

- `tests\GreatTamanaEngineTests.exe --gtest_filter=JobDispatchTests.* --gtest_repeat=100`
  — 100/100 iterations passed, zero hangs, zero failures (the stress-repeat
  discipline this campaign's own `AGENTS.md` section calls for whenever a
  test exercises real cross-thread interaction).
- Full suite (`ctest -C Debug`) — **678 tests, 100% passed** (677 passed +
  1 pre-existing machine-gated smoke test skipped,
  `PmxLoaderRealModelSmokeTest` — unrelated to this work), under the
  project's normal MSVC/Ninja build with `GTE_ENABLE_JOB_SYSTEM=ON`
  (the default).
- A second, independent configure+build under MinGW/GCC (Ninja generator,
  `-DGTE_ENABLE_JOB_SYSTEM=OFF`) — clean build, and the SAME full suite
  (678 tests, 677 passed + 1 pre-existing skip) — confirming `Dispatch()`
  behaves identically (falls through to `Schedule()`'s inline-synchronous
  path) whether the real worker-thread pool is compiled in or not, exactly
  as Phase 1's own two-branch contract requires.

---

## 5. A tooling note (not a Job System bug)

While editing `CMakeLists.txt`/`tests/CMakeLists.txt` by 0-based line index
during this session, a 1-based/0-based line-numbering mismatch (`findstr`
reports 1-based line numbers; this session's `edit_line` tool takes a
0-based `line_index`) caused two transient, self-inflicted edit mistakes:
the root `CMakeLists.txt`'s `add_library(gte_core STATIC ...)` briefly lost
its own closing `)` (silently swallowing every subsequent CMake command
into one giant, never-closed argument list — caught immediately by a
`CMake Error: ... Function missing ending ")"` at the next configure), and
`tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list briefly lost its
`Input/InputStateTests.cpp` entry. Both were caught and fixed within this
same session — a fresh `read_file`/`read_line` confirmed the final state of
both files line-for-line before building, and the subsequent clean
configure+build+678-tests-passing run (both toolchains) is the real proof
nothing was left broken. Recorded here only as a caution for future editing
of these two files by line index — always re-verify the line-number
convention (1-based vs. 0-based) a given tool actually reports before
trusting a computed `line_index`.

---

## 6. Files changed/added this session

- `src/Jobs/JobDispatch.h` (new)
- `src/Jobs/JobDispatch.cpp` (new)
- `tests/Jobs/JobDispatchMathTests.cpp` (new)
- `tests/Jobs/JobDispatchTests.cpp` (new)
- `CMakeLists.txt` — `src/Jobs/JobDispatch.h/.cpp` added to `gte_core`'s
  source list.
- `tests/CMakeLists.txt` — `Jobs/JobDispatchMathTests.cpp`/
  `JobDispatchTests.cpp` entries (unconditional), plus matching
  documentation comments in the file's own test-taxonomy header block.
- `AGENTS.md` — extended the "Job System" section with `Dispatch()`'s API,
  the batch-vs-item scheduling-unit rule and why, the documented
  `DispatchJobContext` heap-allocation exception, and the
  math-vs-execution test split.

No existing production call site changed — this remains a purely
additive phase, consistent with its own Definition of Done ("used by
nothing else in the engine yet").

Phase 2 is complete. Phase 3
(`JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md`) may now begin.
