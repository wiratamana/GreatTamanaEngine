# Job System — Phase 2: Parallel-For / Batch Dispatch

Parent document: `JOBSYSTEM_PHASE0_MASTER_STRATEGY.md` (read first).
Previous phase: `JOBSYSTEM_PHASE1_CORE_THREADING_FOUNDATION.md` (must be
done — `Schedule()`/`WaitForJobs()`/`JobHandle` exist, tested, unused by
any real subsystem).
Next phase: `JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md`.

**Definition of Done for this phase (gates Phase 3):** the exact
`Dispatch(JobFunc, count, handle)`-shaped API from the original brief
exists, correctly splits `count` units of work into a bounded number of
batches (never one job per element for a large `count`), is built entirely
as a thin layer on top of Phase 1's `Schedule()`/`WaitForJobs()` (no
parallel bookkeeping duplicated), has its own Tier-1 batch-splitting-math
tests plus a Tier-2-ish real-execution correctness test, and — same rule as
Phase 1 — is still used by nothing outside `tests/Jobs/` yet.

---

## Step 1: The Goal (Where are we going?)

Turn Phase 1's "run one arbitrary job" primitive into the actual
ergonomic API the original brief's own example calls for:

```cpp
void GameUpdateLoop() {
    Jobs::JobHandle enemyHandle;

    // 1,000 enemies split across roughly 10 jobs.
    Jobs::Dispatch(UpdateEnemyGroup, /*itemCount=*/1000, enemyHandle);

    UpdateAudio();
    UpdateUI();

    Jobs::WaitForJobs(enemyHandle);

    RenderFrame();
}
```

Concretely, this phase delivers:

```cpp
namespace gte::Jobs {

// A batch-shaped job function: told which half-open [beginIndex, endIndex)
// range of the original `itemCount` it is responsible for, plus whatever
// user context it needs (via the same raw-payload convention as Phase 1).
using BatchJobFunction = void(*)(std::uint32_t beginIndex, std::uint32_t endIndex, void* payload);

// Splits `itemCount` items into a bounded number of batches (see 3.2 for
// the exact splitting rule) and Schedule()s one job per batch, each
// invoking `fn(batchBegin, batchEnd, payload)`. Every batch shares the
// SAME `payload` pointer (read-only sharing) and the SAME `handle` (so a
// single WaitForJobs(handle) call waits for the whole dispatch, not one
// call per batch).
void Dispatch(BatchJobFunction fn, std::uint32_t itemCount, void* payload, JobHandle& handle,
    std::uint32_t minItemsPerBatch = 1);

} // namespace gte::Jobs
```

---

## Step 2: The Situation / The Problem (Where are we now?)

Phase 1 gives us `Schedule(fn, payload, handle)` — exactly one job per
call. Naively re-implementing the brief's own "1,000 enemies split across
10 jobs" example on top of that primitive by calling `Schedule()` 1,000
times (once per enemy) would technically work, but for the wrong reason
and at the wrong granularity:

1. **Per-job overhead is real and must be amortized across enough work to
   be worth it.** Every `Schedule()` call touches the shared, mutex-guarded
   `JobQueue` (Phase 1) — contended by up to `WorkerCount()` other threads
   — plus an atomic increment on the handle's counter. For genuinely tiny
   per-item work (say, updating one enemy's simple position/AI state, on
   the order of tens of nanoseconds), scheduling one job PER ITEM would
   spend more total time on scheduling overhead than on the actual work —
   the classic "over-parallelized until it's slower than serial" trap.
   Batches, not items, must be the unit of scheduling.
2. **The batch count itself needs a sane default, not a caller-supplied
   magic number every call site has to guess at.** The brief's own example
   says "10 jobs" for 1,000 items, but a caller migrating a REAL engine
   workload (Phase 6: CPU vertex skinning across however many mesh PARTS
   a given rigged model happens to have — anywhere from 1 to several dozen,
   see `Game::CreateMeshEntityFromGtaFile()`'s own per-material submesh
   splitting) should not have to hand-tune a batch count per call site.
   `Dispatch()` should derive a sensible batch count from
   `JobSystem::Instance().WorkerCount()` automatically, with an optional
   `minItemsPerBatch` floor so a caller CAN prevent over-splitting
   fine-grained work if they know better (e.g. "never split fewer than 8
   vertices' worth of work into their own batch" for vertex skinning),
   without ever being FORCED to compute the batch count by hand.
3. **A degenerate `itemCount` (0, or smaller than `minItemsPerBatch`) must
   be handled without spinning up work for nothing.** `Dispatch()` with
   `itemCount == 0` must be an immediate, job-free no-op — `handle`
   completes instantly, nothing is scheduled at all.

---

## Step 3: The Plan (How will we get there?)

### 3.1 — File layout addition

```
src/Jobs/
    JobDispatch.h/.cpp   - Dispatch() itself, batch-splitting math
tests/Jobs/
    JobDispatchMathTests.cpp   - Tier 1: PURE batch-splitting math, zero real threads/jobs
    JobDispatchTests.cpp       - real JobSystem::Instance(), correctness across a real Dispatch()+WaitForJobs()
```

`JobDispatchMathTests.cpp` existing SEPARATELY from `JobDispatchTests.cpp`
is deliberate, mirroring `DrawStats.h`'s own `AccumulateDrawStats()`/
`CountDrawStats()` split (see `AGENTS.md`, "Profiling") — the actual
"how many batches, and what are their [begin, end) ranges" arithmetic is
100% pure, deterministic, and testable with zero threads involved at all,
and must be pulled out as its own small, directly-callable function rather
than buried inline inside `Dispatch()` where only a real multi-threaded
test could ever exercise it.

### 3.2 — The batch-splitting function (pure, Tier 1)

```cpp
// src/Jobs/JobDispatch.h
namespace gte::Jobs {

struct BatchRange {
    std::uint32_t beginIndex = 0;
    std::uint32_t endIndex = 0; // half-open: [beginIndex, endIndex)
};

// Pure function - no JobSystem, no threads, no scheduling. Splits
// [0, itemCount) into a bounded number of contiguous, non-overlapping,
// gap-free BatchRanges, honoring `minItemsPerBatch` as a floor (never
// produces a batch smaller than this UNLESS itemCount itself is smaller
// than minItemsPerBatch, in which case exactly one batch covering
// everything is produced) and `workerCount` as a ceiling on how many
// batches to aim for (never more than workerCount batches, since more
// batches than available workers cannot run any more in parallel and only
// adds scheduling overhead - see Step 2, point 1). Returns an empty vector
// only when itemCount == 0.
std::vector<BatchRange> ComputeBatchRanges(
    std::uint32_t itemCount, std::uint32_t workerCount, std::uint32_t minItemsPerBatch);

} // namespace gte::Jobs
```

Exact algorithm: `batchCount = std::clamp<std::uint32_t>(workerCount, 1, itemCount / std::max(1u, minItemsPerBatch))`,
then distribute `itemCount` as evenly as possible across `batchCount`
batches (the first `itemCount % batchCount` batches get one extra item,
exactly the "as even as possible" splitting `PrimitiveMeshGenerator`-style
generators already use elsewhere in this codebase for evenly distributing
counts) — this is what `JobDispatchMathTests.cpp` verifies exhaustively:
`itemCount == 0` → empty result; `itemCount < minItemsPerBatch` → exactly
one batch `[0, itemCount)`; `itemCount` exactly divisible by `workerCount`
→ perfectly even batches; `itemCount` NOT evenly divisible → every batch's
size differs from every other by at most 1, and every batch's ranges are
contiguous and gap-free covering exactly `[0, itemCount)` with no overlap
(this last property — full, non-overlapping coverage — is the single most
important invariant a test must assert here, since Phase 6's real
consumer, CPU vertex skinning, would silently corrupt or skip vertices if
this were ever wrong).

### 3.3 — `Dispatch()` itself

```cpp
// src/Jobs/JobDispatch.cpp
void Dispatch(BatchJobFunction fn, std::uint32_t itemCount, void* payload, JobHandle& handle,
    std::uint32_t minItemsPerBatch)
{
    if (itemCount == 0) {
        return; // handle stays "already complete" - nothing to wait for.
    }

    const auto ranges = ComputeBatchRanges(
        itemCount, static_cast<std::uint32_t>(JobSystem::Instance().WorkerCount()), minItemsPerBatch);

    // One heap-allocated (see below) DispatchJobContext per batch, freed by
    // the job itself right before it returns - NOT stack-allocated in
    // Dispatch()'s own frame, since Dispatch() returns immediately (it does
    // not block) and every batch's context must outlive that return.
    for (const BatchRange& range : ranges) {
        auto* context = new DispatchJobContext{fn, payload, range};
        JobSystem::Instance().Schedule(&RunBatchJobTrampoline, context, handle);
    }
}
```

**A necessary, explicitly-acknowledged exception to the "zero heap
allocation" rule**: each batch's own small `DispatchJobContext` (the
function pointer + user payload pointer + this batch's own `BatchRange` —
three-ish words) is heap-allocated via `new`, freed by
`RunBatchJobTrampoline()` itself right after calling `fn(...)`. This is
DELIBERATE and DOCUMENTED, not an oversight: Phase 1's own `Schedule()`
primitive is zero-allocation because it hands the caller's own long-lived
`payload` pointer straight through — but `Dispatch()` needs to hand each
batch its OWN distinct `[begin, end)` range, and that per-batch range has
to live somewhere between `Dispatch()` returning and the job actually
running. The number of these allocations per `Dispatch()` call is bounded
by `workerCount` (never more — see 3.2's `batchCount` ceiling), so this is
a small, BOUNDED number of allocations per dispatch call (a handful, not
one per array element), not an unbounded/hot-path allocation — an
acceptable, explicit trade for API ergonomics, and something Phase 8's
benchmark pass should specifically measure to confirm it never actually
shows up as a bottleneck. If it ever does, the fix is a small fixed-size
pool of reusable `DispatchJobContext` slots (mirroring `JobQueue`'s own
fixed-capacity philosophy) — deliberately NOT built preemptively here,
per this whole campaign's own "prove the need first" discipline.

### 3.4 — Testing

- **`JobDispatchMathTests.cpp` (Tier 1, exhaustive, zero threads)**: every
  case enumerated in 3.2 above, plus a fuzz-style test iterating many
  `(itemCount, workerCount, minItemsPerBatch)` triples (including edge
  values: `workerCount == 1`, `minItemsPerBatch` larger than `itemCount`,
  `itemCount == 1`) asserting the three universal invariants every single
  time: (a) ranges are sorted and contiguous with no gaps, (b) ranges
  never overlap, (c) the union of all ranges is exactly `[0, itemCount)`.
- **`JobDispatchTests.cpp` (real `JobSystem::Instance()`)**: a real
  `Dispatch()` call with a batch job function that, for each index in its
  assigned `[begin, end)` range, writes that index's value into a
  caller-owned `std::vector<int>` slot (sized `itemCount`, zero-initialized
  before the dispatch) — after `WaitForJobs()`, assert every slot equals
  its own index exactly once, no slot untouched, no slot double-written.
  This is the real-world proof that batch splitting AND parallel execution
  compose correctly, run at a few different `itemCount` values (including
  one deliberately not evenly divisible by any plausible worker count, and
  one smaller than a deliberately large `minItemsPerBatch` to exercise the
  "collapses to one batch" path for real).

---

## Step 4: What We Will NOT Do (Focus)

- **No automatic, caller-invisible sizing that ignores `minItemsPerBatch`
  entirely.** A caller with real knowledge of their own per-item cost
  (Phase 6's vertex-skinning migration will have exactly this knowledge)
  must be able to floor the batch size — `Dispatch()` never second-guesses
  that floor by splitting smaller anyway.
- **No dynamic load-balancing/work-stealing across batches once
  dispatched.** Each batch is a fixed, pre-computed `[begin, end)` range,
  handed to exactly one job, run start-to-finish by whichever worker picks
  it up. A batch that happens to be more expensive than another (e.g. one
  mesh part with far more vertices than another) is accepted as a known
  v1 limitation, not solved here — see Phase 0's explicit refusal of
  work-stealing for the whole campaign.
- **No nested/recursive `Dispatch()` calls from inside a batch job body in
  this phase.** A batch job calling `Dispatch()` again (fork-join style)
  is not forbidden by the type system, but is explicitly UNTESTED and
  UNSUPPORTED as of this phase — Phase 3 (dependencies/continuations) is
  the natural place to reason about whether/how that should work, not
  here.
- **No change to `Schedule()`/`WaitForJobs()`/`JobHandle` themselves.**
  `Dispatch()` is purely additive, built entirely on Phase 1's existing,
  already-tested primitives.
- **Still no real engine subsystem wired up.** Same rule as Phase 1 — this
  is proven in isolation first.

---

## Step 5: Their Role (What does this mean for you?)

- Implement `ComputeBatchRanges()` first, as a completely standalone, pure
  function, and get `JobDispatchMathTests.cpp` fully green BEFORE writing
  `Dispatch()` itself — this is the same "test the pure math in isolation
  before wiring it into anything stateful" discipline this codebase already
  applies everywhere (`DrawStats.h` before `FrameRecorder`, `GpuTiming.h`
  before `GpuTimingService`).
- When implementing `Dispatch()`, explicitly comment the `new
  DispatchJobContext` allocation the same way this codebase already
  comments its OTHER deliberate, bounded exceptions to a stated rule (e.g.
  `AGENTS.md`'s own treatment of ImGuizmo/third-party integration
  exceptions) — a future reader must immediately understand this is a
  reviewed trade-off, not an accidental violation of Phase 1's
  zero-allocation guarantee.
- Update `AGENTS.md`'s "Job System" section (started in Phase 1) with:
  the `Dispatch()` API itself, the batch-vs-item scheduling-unit rule and
  why, and the explicit, bounded, documented exception to the
  zero-allocation rule described in 3.3.
- Once `JobDispatchMathTests.cpp` and `JobDispatchTests.cpp` are both fully
  green, move on to `JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md`.
