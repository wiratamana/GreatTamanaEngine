# Job System

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

`src/Jobs/` (`JobTypes.h`, `JobQueue.h/.cpp`, `JobSystem.h/.cpp`,
`JobDispatch.h/.cpp`, `JobContinuation.h/.cpp`) is the engine's general-purpose
worker-thread pool - see `task_manager/job_system/JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md`
for the full multi-phase campaign this is Phases 1-6 of,
`task_manager/job_system/JOB_SYSTEM_PHASE1_COMPLETION_REPORT.md` for Phase
1's own detailed writeup, `task_manager/job_system/JOB_SYSTEM_PHASE2_COMPLETION_REPORT.md`
for Phase 2's, `task_manager/job_system/JOB_SYSTEM_PHASE3_COMPLETION_REPORT.md`
for Phase 3's, `task_manager/job_system/JOB_SYSTEM_PHASE4_COMPLETION_REPORT.md`
for Phase 4's (the Thread-Safety Audit),
`task_manager/job_system/JOB_SYSTEM_PHASE5_COMPLETION_REPORT.md` for Phase
5's (Profiler Integration - Worker Timeline), and
`task_manager/job_system/JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md` for Phase
6's (First Production Consumer - Animation / Vertex Skinning). As of Phase
6, this module provides the minimal `JobHandle`/`Schedule()`/`WaitForJobs()`/
`WorkerCount()` primitive (Phase 1), the batch/parallel-for API, `Dispatch()`/
`ComputeBatchRanges()` (Phase 2), job dependencies/continuations,
`ScheduleAfter()`/`DispatchAfter()` (Phase 3 - see this section's own
dedicated Phase 3 bullets further below), a written, reviewable
thread-safety classification (NEVER/READ-SAFE/JOB-SAFE) of every existing
shared/global/singleton piece of engine state a future job body could reach
into (Phase 4 - see this section's own dedicated "Phase 4 - Thread-Safety
Audit" bullets further below), a genuinely thread-safe way for a job
body to record its own CPU scope into `Profiling::FrameProfiler`,
`GTE_PROFILE_JOB_SCOPE`/`Profiling::JobScopeTimer` (Phase 5 - see this
section's own dedicated Phase 5 bullets further below), and its first real
production consumer - `AnimationSystem::Update()`
(`src/Game/Animation/AnimationSystem.cpp`) now dispatches CPU vertex
skinning (`Animation/VertexSkinning.h`'s `SkinVertexRange()`) across the
worker pool for a sufficiently large rigged model (Phase 6 - see this
section's own dedicated Phase 6 bullets further below). Nothing else in
the engine calls `Schedule()`/`Dispatch()`/`ScheduleAfter()`/`DispatchAfter()`
yet - `AnimationSystem::Update()` is still the only real, non-test call site.
Follow these rules whenever touching this module or building a later phase on top of it:

- **`gte::Jobs::JobSystem::Instance()` is a Meyers singleton that starts
  lazily, on its FIRST call from anywhere in the process** - the exact same
  pattern `Profiling::FrameProfiler::Instance()` already uses (see
  [Profiling](profiling.md) above). This means the worker pool - real OS `std::thread`s -
  does not exist at all, and none are ever created, until the first genuine
  `Schedule()` call happens to run somewhere in the engine. Since nothing
  calls `Schedule()` in production yet (only this module's own tests do),
  a plain build/run of the engine today never spins up a single worker
  thread - don't be surprised if a profiling capture shows zero job/worker
  activity before Phase 6 lands; that's expected, not a bug.
- **`JobSystem` (and `JobQueue`) always compile, unconditionally, regardless
  of `GTE_ENABLE_JOB_SYSTEM`** - the same "the class stays available/
  testable even when its production behavior is gated off" precedent
  `SdlMemoryTracker`/`FrameProfiler` already established. Only the
  *internal* behavior differs per that switch: `GTE_ENABLE_JOB_SYSTEM=ON`
  (the default) runs `Schedule()`'d work on a real worker-thread pool sized
  from `std::thread::hardware_concurrency()` (falling back to 1 if that
  returns 0); `=OFF` runs `Schedule()`'d work IMMEDIATELY, synchronously, on
  the calling thread, with no `std::thread` ever created - the public API's
  observable contract (a `JobHandle` eventually becomes complete;
  `WaitForJobs()` returns once it is) is identical either way. This is the
  exact same two-branch, ODR-safe "gate at the .cpp level, never at the
  call site" convention `GTE_ENABLE_EDITOR`'s `ImGuiEditorLayer.cpp`/
  `NullEditorLayer.cpp` split already established.
- **`JobHandle` is backed by a `std::shared_ptr<detail::JobHandleState>` -
  exactly ONE heap allocation at `JobHandle` construction, never one per
  job scheduled against it.** A single `JobHandle` is meant to be reused
  across MANY `Schedule()` calls (Phase 2's whole batch-`Dispatch()` design
  shares one `JobHandle` across every batch of a single call) -
  `JobSystem::Schedule()` itself never allocates on the heap in its
  steady-state path (`JobQueue` is a fixed-capacity ring buffer, sized once
  at construction - see `JobQueue.h`'s own comment on why this is a fixed
  size rather than a growable container, mirroring
  `kMaxCpuScopesPerFrame`/`kMaxFrameHistory`'s precedent in
  `src/Profiling/`). A full queue is handled by `Schedule()` falling back
  to running the job immediately, inline, on the calling thread - never by
  blocking, growing the buffer, or dropping the job silently.
- **A worker's pending-count decrement MUST be bracketed by the SAME mutex
  `WaitForJobs()` holds while checking its own predicate
  (`JobSystem::m_completionMutex`) - never just an atomic write on its
  own.** This is a real, confirmed-in-practice classic
  `condition_variable` lost-wakeup race, not a theoretical concern: a
  waiter can check `IsComplete()` (see it as still false, while still
  holding the mutex) and, before it finishes registering itself as a
  waiter on `m_completionCondition`, a worker's decrement-then-
  `notify_all()` sequence can run to completion on another thread and find
  no one registered yet to wake - the waiter then blocks forever waiting
  for a notification that already happened moments earlier. This was
  reproduced intermittently (roughly 1 run in 4) under a stress test
  scheduling 256 jobs against one shared handle, before the fix (holding
  `m_completionMutex` around the `fetch_sub`, in both `JobSystem::
  WorkerLoop()` and `Schedule()`'s own full-queue fallback path) closed it
  - a 100-iteration `--gtest_repeat` stress run showed zero hangs
  afterward. Any FUTURE piece of code that mutates state a
  `condition_variable` wait's predicate depends on must follow this same
  "bracket the mutation with the SAME mutex the waiter holds while
  checking" rule - see `cppreference`'s own
  `condition_variable::notify_all()` documentation ("even though the
  shared variable is atomic, it must be modified while owning the mutex to
  correctly publish the modification to the waiting thread").
- **Never gate a new `src/Jobs/` test file behind `GTE_ENABLE_EDITOR`/
  `GTE_ENABLE_JOB_SYSTEM` in `tests/CMakeLists.txt`.** `JobQueue`/
  `JobSystem` both always compile (see above), so
  `tests/Jobs/JobQueueTests.cpp`/`JobSystemTests.cpp` are added to
  `GTE_TEST_SOURCES` unconditionally, the same "always built" bucket as
  `Profiling/FrameProfilerTests.cpp`/`ScopeTimerTests.cpp` - and both pass
  identically whether `GTE_ENABLE_JOB_SYSTEM` is `ON` or `OFF` (verified:
  the full suite passes in both configurations, and separately under a
  completely different toolchain - MinGW/GCC vs. this project's usual
  MSVC/Ninja build - as an extra cross-check for this module specifically,
  since it's the engine's first genuinely multi-threaded code).
- **A concurrency bug in this module can pass by luck on a single test
  run.** Any new test that exercises real cross-thread interaction (not
  just `JobQueue`'s own single-threaded ring-buffer logic) should be
  stress-repeated (e.g. `--gtest_repeat=50` or more) at least once before
  being trusted, mirroring the discipline
  `JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md` already calls for
  future phases - a single green run is not sufficient evidence for
  genuinely concurrent code, as this phase's own lost-wakeup bug
  demonstrated directly.
- **Phase 2's `Dispatch(fn, itemCount, payload, handle, minItemsPerBatch)`
  (`src/Jobs/JobDispatch.h/.cpp`) turns Phase 1's "one job per `Schedule()`
  call" primitive into an ergonomic parallel-for/batch API, built ENTIRELY
  on top of `Schedule()`/`WaitForJobs()`/`JobHandle` - no second scheduler,
  no second queue, no parallel bookkeeping duplicated.** `Dispatch()` splits
  `[0, itemCount)` into a bounded number of contiguous batches
  (`ComputeBatchRanges()` - never more than `JobSystem::Instance().WorkerCount()`
  batches, and never smaller than the caller's own `minItemsPerBatch` floor
  unless `itemCount` itself is smaller, in which case it collapses to
  exactly one batch) and calls `Schedule()` once per batch, every batch
  sharing the SAME `payload` pointer (read-only) and the SAME `handle` (so
  one `WaitForJobs(handle)` call waits for the whole dispatch, never one
  per batch).
- **BATCHES, not items, are the unit of scheduling - `Dispatch()` never
  schedules one job per array element.** Every `Schedule()` call touches
  the shared, mutex-guarded `JobQueue` plus an atomic increment on the
  handle's counter - for genuinely tiny per-item work, scheduling one job
  PER ITEM would spend more total time on scheduling overhead than on the
  actual work (the classic "over-parallelized until it's slower than
  serial" trap). `Dispatch()`'s own batch count is derived automatically
  from `WorkerCount()`, never something a caller has to compute by hand -
  `minItemsPerBatch` (default 1) is the one knob a caller with real
  per-item-cost knowledge can use to floor the batch size (e.g. "never
  split fewer than 8 vertices' worth of work into their own batch" for a
  future vertex-skinning call site) - `Dispatch()` never second-guesses
  that floor by splitting smaller anyway.
- **A NECESSARY, DELIBERATE, DOCUMENTED exception to Phase 1's own "zero
  heap allocation in the steady-state per-job path" guarantee: each batch
  gets its own small, heap-allocated `DispatchJobContext`
  (`JobDispatch.cpp`, anonymous namespace) - the function pointer + user
  payload pointer + that batch's own `BatchRange` - freed by the batch job
  itself (`RunBatchJobTrampoline()`) right before it returns.** This is
  required because each batch needs its OWN distinct `[begin, end)` range,
  and `Dispatch()` itself does not block (it returns immediately), so that
  range has to live somewhere between `Dispatch()` returning and the batch
  job actually running - a plain `Schedule()` call can hand the caller's
  own long-lived `payload` straight through with zero allocation, but
  `Dispatch()` cannot, since no single long-lived object naturally holds
  N different ranges. The number of these allocations per `Dispatch()`
  call is bounded by `ComputeBatchRanges()`'s own batch-count ceiling (at
  most `WorkerCount()` - a handful, never one per array element) - a
  small, BOUNDED, explicitly-reviewed trade for API ergonomics, not an
  accidental violation of Phase 1's guarantee. If this were ever found to
  matter in practice, the fix is a small fixed-size pool of reusable
  `DispatchJobContext` slots (mirroring `JobQueue`'s own fixed-capacity
  philosophy) - deliberately NOT built preemptively.
- **`ComputeBatchRanges()` (the pure batch-splitting math) is
  Tier-1-tested completely separately from `Dispatch()` itself
  (`tests/Jobs/JobDispatchMathTests.cpp` vs. `JobDispatchTests.cpp`)** -
  the same "test the pure math in isolation before wiring it into anything
  stateful" discipline this codebase already applies elsewhere
  (`DrawStats.h` before `FrameRecorder`, `GpuTiming.h` before
  `GpuTimingService`). The three invariants every test in
  `JobDispatchMathTests.cpp` checks - ranges are contiguous/gap-free, never
  overlap, and their union is exactly `[0, itemCount)` - are the single
  most important property this function guarantees; a future consumer
  (e.g. Phase 6's CPU vertex skinning) would silently corrupt or skip data
  if any of them were ever violated.
- **Phase 3 (Job Dependencies / Continuations - `src/Jobs/JobContinuation.h/.cpp`,
  see `task_manager/job_system/JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md`)
  adds `ScheduleAfter()`/`DispatchAfter()` - the ability to say "run this
  job/batch dispatch only once these OTHER handles have completed" -
  without the main thread ever having to call `WaitForJobs()` in between
  and manually stitch stages together.** Built ENTIRELY on top of Phases
  1-2's existing `JobSystem::Schedule()`/`WaitForJobs()`/`JobHandle`/
  `Dispatch()` - no second scheduler, no second queue, no parallel
  bookkeeping duplicated. If EVERY handle in `dependencies` is already
  complete at call time (the common case - a dependency that finished
  earlier in the same frame), this degrades to an ordinary `Schedule()`
  call with zero continuation bookkeeping at all - explicit dependencies
  only, there is no automatic data-flow dependency inference anywhere in
  this module.
- **A deferred continuation's `handle` becomes "incomplete" the INSTANT
  `ScheduleAfter()`/`DispatchAfter()` returns - not lazily, once the first
  dependency clears - via `JobHandle::AddPendingUnit()` (a manual pending-
  counter increment, paired 1:1 with `JobSystem::ScheduleAlreadyPending()`'s
  own decrement once the deferred work actually finishes running).** This
  is what makes it safe for a caller to call `WaitForJobs(handle)`
  immediately after `ScheduleAfter()` returns and correctly block until the
  real work has run, no matter how long its dependencies take to clear - a
  naive "decrement then later increment again via a normal `Schedule()`
  call" approach would risk a transient false-complete gap a concurrent
  `WaitForJobs()` caller could observe. `ScheduleAlreadyPending()` is
  deliberately a SEPARATE method from `Schedule()` (never increments
  `pending` itself) purely for this reason - production/test code
  scheduling ordinary, non-continuation work must always call `Schedule()`/
  `Dispatch()` directly instead.
- **A dependency handle's watcher list (`detail::JobHandleState::
  watcherFns`/`watcherContexts`, `JobTypes.h`) is a small, FIXED-CAPACITY
  array (`detail::kMaxWatchersPerHandle`, 8 by default) - never a growable
  container.** `JobHandle::AddCompletionWatcher()` registers a callback to
  run once that ONE handle's `pending` reaches zero (or calls it
  immediately, synchronously, if already zero) - guarded by the same
  mutex-bracketing discipline Phase 1's own lost-wakeup-race fix already
  established for `m_completionMutex` (see above), applied here to a
  SEPARATE, per-handle `watcherMutex` instead: the pending-zero check and
  the watcher-list mutation happen under the same lock `FireWatchers()`
  takes to read/clear the list, so a registration can never be "too late"
  to see a completion that raced it. Firing every registered watcher
  (`JobHandleState::FireWatchers()`, called by whichever thread performs
  the decrement of `pending` down to zero - see `JobSystem::WorkerLoop()`/
  `Schedule()`/`ScheduleAlreadyPending()`'s own full-queue-fallback paths)
  is deliberately done AFTER releasing that lock, since a fired watcher
  may itself call back into `JobSystem::Schedule()`/`ScheduleAlreadyPending()`.
- **Once a single handle already has `kMaxWatchersPerHandle` OTHER
  continuations registered against it, any further dependent falls back to
  a dedicated, DETACHED `std::thread` that busy-polls
  (`JobContinuation.cpp`'s `WatchDependencyWithFallback()`/
  `RunPollingFallbackJob()`) - never silently dropped, and, just as
  importantly, NEVER routed through `JobSystem::Schedule()`.** This is a
  real, confirmed-in-practice correctness fix, not a style preference: with
  `GTE_ENABLE_JOB_SYSTEM=OFF`, `Schedule()` runs its job IMMEDIATELY,
  SYNCHRONOUSLY, on whichever thread calls it (see Phase 1's own OFF-mode
  contract) - if the overflow fallback's poll job were scheduled that way
  and the dependency it's polling can only ever be completed by something
  running concurrently on ANOTHER thread (the only way it could still be
  genuinely pending after 8 other watchers are already ahead of it), that
  `Schedule()` call would spin forever on the calling thread instead of
  yielding it back - a genuine deadlock, reproduced directly by this
  phase's own `JobContinuationTests.OverflowingWatcherCapacityStillRunsEveryContinuation`
  test before the fix. A raw, dedicated thread sidesteps this entirely,
  regardless of `GTE_ENABLE_JOB_SYSTEM`.
- **`JobSystem::WaitForJobs()`'s `GTE_ENABLE_JOB_SYSTEM=OFF` branch is a
  real spin-wait (`while (!handle.IsComplete()) { std::this_thread::yield(); }`),
  NOT the historical `(void)handle;` no-op Phases 1-2 could get away with.**
  Before Phase 3, every `Schedule()`/`Dispatch()` call in the OFF
  configuration ran its job(s) synchronously to completion before
  returning, so a handle was always already complete by the time any
  caller could reach `WaitForJobs()` - nothing was ever left "in flight"
  for a caller on a different thread to wait for. `JobHandle::AddPendingUnit()`
  breaks that assumption: a handle can be marked incomplete well BEFORE the
  work that will eventually complete it is actually scheduled, and that
  work may run to completion on a genuinely different thread (e.g. one
  concurrently calling `Schedule()`/`ScheduleAlreadyPending()` for the
  handle's own dependency). Reproduced directly by this phase's own
  `JobContinuationTests.HandleStaysIncompleteUntilPendingDependencyClears`/
  `FanInWaitsForEveryDependencyBeforeRunning` tests hanging/failing under
  `GTE_ENABLE_JOB_SYSTEM=OFF` before this fix - fixed, and re-verified
  clean (including a 15-iteration `--gtest_repeat` stress run) under a
  full, separate MinGW/GCC `GTE_ENABLE_JOB_SYSTEM=OFF` configure+build+test
  run, alongside the default `GTE_ENABLE_JOB_SYSTEM=ON` configuration.
- **A test that needs to hold a dependency handle "genuinely pending" for a
  controlled duration must NEVER call `Schedule()` with a blocking/spin-
  waiting job directly from the test's own (main) thread - only from a
  dedicated `std::thread` it spawns for exactly that purpose** (see
  `tests/Jobs/JobContinuationTests.cpp`'s own `StartHeldDependency()`/
  `WaitUntilPending()` helpers and their header comment). Doing it directly
  from the main thread deadlocks immediately under
  `GTE_ENABLE_JOB_SYSTEM=OFF`, for the exact same reason described above -
  `Schedule()` would block that same thread forever waiting for a release
  flag only that thread could ever set. This is now the established
  pattern for any FUTURE `src/Jobs/` test that needs a genuinely
  long-pending dependency, in either build configuration.

- **Phase 4 (Thread-Safety Audit + Integration Point Whitelist - see
  `task_manager/job_system/JOBSYSTEM_PHASE4_THREAD_SAFETY_AUDIT_INTEGRATION_POINTS_v2.md`
  and `task_manager/job_system/JOB_SYSTEM_PHASE4_COMPLETION_REPORT.md`) is a
  deliberately documentation/verification-heavy phase, not a new API
  surface: it produces a definitive, written answer to "if a job body,
  running on a worker thread, tries to touch THIS engine subsystem, is that
  safe" for every existing shared/global/singleton piece of engine state,
  so Phase 6's real production migration (CPU vertex skinning) has a
  reviewed whitelist to build against instead of each future call site
  re-deriving its own answer by inspection.** The classification table
  below uses the same three buckets the strategy document defines:
  **NEVER** (a job body must never touch this at all, not even read-only,
  without dedicated new synchronization this campaign has not added);
  **READ-SAFE** (safe for a job body to READ, but only for as long as
  nothing - including the main thread itself - concurrently MUTATES the
  same data for the duration of the `Dispatch()`/`WaitForJobs()` bracket
  the job body runs inside); and **JOB-SAFE** (genuinely safe to call
  concurrently, from any number of threads at once, no external
  synchronization needed at all - either because it is pure logic with no
  shared mutable state, or because it was specifically built with its own
  internal synchronization for exactly this purpose, e.g. `JobSystem`
  itself).

  | Subsystem | Classification | Why |
  |---|---|---|
  | `gte::Jobs::JobSystem`/`detail::JobQueue`/`detail::JobHandleState` (`src/Jobs/`) | **JOB-SAFE** | The entire point of Phases 1-3 - `Schedule()`/`Dispatch()`/`ScheduleAfter()`/the queue's mutex+condition_variable/the handle's atomic pending-counter and mutex-guarded watcher list are all specifically built, and stress-tested (see this section's own lost-wakeup-race bullets above), to be called concurrently from many threads at once. A job body scheduling MORE work via `JobSystem::Instance().Schedule()`/`Dispatch()` from inside another job is safe by this same design (not exercised by a real call site yet, but nothing about the implementation assumes single-threaded access). |
  | `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()` (`<SDL3/SDL_timer.h>`, used by `src/Profiling/ScopeTimer.h`) | **JOB-SAFE** | This phase's own required verification item (see the strategy document's Step 3.2) - confirmed by a new, dedicated multi-threaded test, `tests/Jobs/JobSystemSdlClockThreadSafetyTests.cpp`: several threads released at the same instant via a shared start barrier all observe the exact same, non-zero `SDL_GetPerformanceFrequency()`, and each thread's own sequence of `SDL_GetPerformanceCounter()` reads stays strictly non-decreasing under concurrent load from every other thread. Consistent with SDL3's own documented contract - both are stateless queries against a platform-level monotonic counter/its fixed frequency, with no shared engine-owned mutable state involved in servicing the call. This is what lets Phase 5's planned `JobScopeTimer` safely call both functions from an arbitrary worker thread while the main thread's own `ScopeTimer` scopes call the identical functions concurrently. |
  | `Profiling::FrameProfiler` (`src/Profiling/FrameProfiler.h/.cpp`) | **NEVER, except `RecordWorkerJobSample()` specifically - JOB-SAFE** | `RecordCpuScope()`'s linear scan + non-atomic `++m_current.cpuScopeCount`, and `BeginFrame()`/`EndFrame()`'s ring-buffer bookkeeping, remain completely unsynchronized by design - a job body must still never call `GTE_PROFILE_SCOPE`/touch any OTHER `FrameProfiler::Instance()` method directly. As of Phase 5, `RecordWorkerJobSample()` is a genuinely separate, thread-safe write path (an atomic fetch-and-increment reservation into its own dedicated array, `m_captureEnabled` itself now `std::atomic<bool>`) - see this section's own dedicated Phase 5 bullets below for the full design - so `GTE_PROFILE_JOB_SCOPE` (`src/Profiling/JobScopeTimer.h`), which routes through it, is the one sanctioned way for a job body to record its own CPU scope. |
  | `GpuMemoryTracker`, `Renderer`/`Vulkan/*` (`VulkanInstance`/`VulkanDevice`/`VulkanSwapchain`/`VulkanAllocator`/`Buffer`/`RenderTexture`/`Pipeline`/`FramePresenter`/`FrameRecorder`/`GpuResourceFactory`), `GpuTimingService`/`VulkanQueryPool` | **NEVER** | `GpuMemoryTracker`'s own class comment already says "Not thread-safe" outright, and nothing under `Renderer`/`Vulkan/` was ever built with any synchronization in mind - every `VkCommandBuffer`/`VkQueue`/`VmaAllocator` call in this engine assumes single-threaded, main-thread-only access. A job body must never touch a live GPU resource, submit Vulkan work, or read/write `GpuMemoryTracker`'s tables directly - any future GPU-adjacent job (e.g. CPU vertex skinning writing into a `Mesh`'s host-visible buffer, Phase 6's actual target) resolves the destination pointer/buffer on the MAIN thread first and hands job bodies only a plain, disjoint output span to write into, never a `Mesh*`/`Renderer&`/Vulkan handle itself. |
  | `src/Renderer/RenderGraph/*` (`RenderGraph`, `RenderGraphBuilder`, `RenderGraphCompiler`, `RenderGraphResourcePool`, `RenderGraphBarrierPlanner`, `RenderGraphTimestampPool`) | **NEVER** | Every one of these either directly issues Vulkan calls, touches `GpuMemoryTracker`-tracked resources, or mutates shared, unsynchronized compiler/pool state (`RenderGraphResourcePool`'s frame-to-frame texture reuse bookkeeping) - the same GPU-resource-adjacent reasoning as the row above. No job in this campaign's current or planned scope has any reason to touch any of it, and none ever should without a fresh, dedicated audit of its own. |
  | `src/Editor/*` (the ImGui context, `EditorContext`, every `Panels/*Panel`) | **NEVER** | Dear ImGui's own context (`ImGuiContext`) is explicitly documented upstream as unsafe for concurrent access from multiple threads, and every Editor panel in this engine already only ever runs on the main thread inside `IEditorLayer::BuildUI()`/`Render()`. No job body has any legitimate reason to touch ImGui state directly - a future Editor "Jobs" panel (Phase 7) reads job/profiler DATA that a job body already finished writing (via `FrameProfiler`'s own thread-safe write path, Phase 5), never ImGui state from a worker thread. |
  | `Registry`/`EntityManager`/`ComponentStorage<T>` (`src/ECS/`) | **mutation: NEVER; read: READ-SAFE** | `EntityManager::Create()`/`Destroy()` and `ComponentStorage<T>::Add()`/`Remove()` mutate unsynchronized vectors/free-lists/generation counters with zero locking - a job body must never call any mutating `Registry`/ECS method. Reading a component's plain data fields (e.g. a `Transform`'s `position`) from a job body is READ-SAFE, but only under the same rule Phase 6's own design already assumes: the main thread must not mutate that SAME `Registry` for the entire duration of the `Dispatch()`/`WaitForJobs()` bracket a job body reading it runs inside - in practice, the safest and simplest pattern (and the one Phase 6 actually uses) is to never hand a `Registry&`/component reference into a job body at all, extracting whatever plain values are needed into a copy/span on the main thread first. |
  | `AssetDatabase` (`src/Assets/AssetDatabase.h/.cpp`) | **NEVER** (not yet proven safe for reads either) | Backed by a plain `std::vector`/two `std::unordered_map`s with zero internal synchronization - `RefreshFromDirectory()`/`ImportAsset()` are main-thread-only calls today, and nothing in this campaign's current or planned scope needs a job body to read from it. Unlike `Registry` above, this is classified NEVER outright rather than "read-safe with a caveat," since no real call site has ever needed to reason through the caveat for this specific class - a future job that genuinely needs read access must have that specific call site re-audited and documented here first, not merely assume the same reasoning transfers automatically. |
  | `ResourcePool<T, HandleT>` (`src/Renderer/ResourcePool.h`, the `MeshHandle`/`PipelineHandle` pools owned by `RenderSystem`) | **NEVER** | Zero internal locking, and resolving a handle returns a live `Mesh*`/`Pipeline*` - a GPU-adjacent pointer that compounds directly with the `Renderer`/`Vulkan` NEVER row above. A job body must never call `RenderSystem::TryGetMesh()`/`TryGetPipeline()` (or any future equivalent) itself; the resolved pointer/buffer must always be resolved on the main thread and handed to job bodies only as a plain, disjoint output span, mirroring Phase 6's own boundary design exactly. |
  | `src/Game/Instantiation/*` GPU catalogs (`PrimitiveGpuCatalog`, `MaterialTextureGpuCache`, `MeshAssetGpuCatalog`) | **NEVER** | GPU-resource-creating/caching code, unsynchronized, main-thread-only by construction today (called only from `MeshInstantiationSystem`, itself called only from `Game`'s own main-thread methods). A job body must never touch these directly - this is exactly what Phase 6's boundary design (a job body only ever sees plain CPU-side spans, never a `Mesh`/GPU handle) is built to guarantee structurally, not just by convention. |
  | Pure `src/Animation/*` modules (`BoneChainResolver`, `BonePoseMath`, `SkeletonPose`, `IkSolver`, `AppendBoneSolver`, `MotionSampler`, `AnimationPoseEvaluator`, `VertexSkinning`) | **JOB-SAFE** | Every one of these is pure logic operating only on its own parameters - no static/global/singleton mutable state anywhere in this module (see AGENTS.md's own [Skeletal Animation Pose Resolution](skeletal-animation-pose-resolution.md) section) - safe to call concurrently from any number of threads at once, PROVIDED each individual call's own inputs/outputs (e.g. one model's own `skinnedPositions`/`skinnedNormals` output vectors) are never shared/aliased across two concurrent calls. This is exactly the pure-function foundation Phase 6's planned `SkinVertices()` migration depends on. |
  | `src/Math/*` (`Vec2`/`Vec3`/`Vec4`/`Mat4`/`Quat`) | **JOB-SAFE** | Plain value types - every operation is a pure function of its own operands, no shared/static state of any kind. |
  | `MeshData`/`SkeletonData`/`MotionData`/`MaterialData` (`src/Assets/*Data.h`), READ-ONLY | **READ-SAFE** | Plain data structs with no internal synchronization, but populated exactly once (at import/cache-load time, main-thread-only) and never mutated again for the lifetime of the cache entry that owns them (see `SkeletalRigCache`/`AnimationClipCache`'s own "load once, cache, never mutate again" design) - concurrent read-only access from multiple job bodies is safe as long as nothing concurrently mutates the SAME instance, which nothing in this engine's current design ever does after initial load. |
  | `SkeletalRigCache`/`AnimationClipCache`/`ResolvedAnimationBindingCache` (`src/Game/Animation/*`) - the CACHE CONTAINERS themselves (`GetOrLoad()`/`Register()`/`TryGet()`) | **NEVER** for concurrent mutation; a resolved lookup's VALUE is **READ-SAFE** under a REQUIRED ordering rule | The containers are plain `std::unordered_map`s with zero locking - must only ever be called from the main thread, exactly as today. Once a lookup returns a pointer/reference to an already-cached value, reading that value from a job body is safe under the same rule as `MeshData` above, but with one REQUIRED addition specific to these three caches: the main thread must not call `GetOrLoad()`/`Register()` again on the SAME cache while a job holding an earlier lookup's pointer is still running, since an `unordered_map` insertion can invalidate previously-returned references - this is why Phase 6's planned design resolves every cache lookup up front, before any `Dispatch()` call, and never touches the cache again until `WaitForJobs()` returns. This is a REQUIRED correctness rule, not a performance convenience, and must never be relaxed by a future edit that "just wants one more lookup mid-flight." |
  | Cross-entity/cross-instance shared GPU mesh buffers (the documented `README.md` limitation: two entities spawned from the same `*.gta` file share one underlying `Mesh`, including its CPU-side cached bind-pose vertex arrays and output skinning buffers) | **NEVER concurrently - sequential-only, main-thread-orchestrated** | Not a "touch this subsystem" rule like the rows above, but a cross-cutting HAZARD this table must call out explicitly: today this sharing is safe only because `AnimationSystem::Update()` (`src/Game/Animation/AnimationSystem.cpp`) processes every live `SkeletalAnimator` strictly one at a time, on one thread - at any given instant, at most one animator is ever touching that shared memory. The moment more than one animator's own `Dispatch()`/`WaitForJobs()` sequence is allowed to be in flight AT THE SAME TIME, two worker threads could write the same shared buffer concurrently - a genuine data race, not merely today's harmless "last write wins" visual bug. See `JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md`, Step 3.6, for the permanent mitigating rule (each model's own `Dispatch()`+`WaitForJobs()` pair must complete in full before the next model's begins) this campaign commits to - this row exists so that rule is discoverable from this table directly, not only from Phase 6's own document. |
  | `src/Physics/*` (`VerletIntegration`, `ChainConstraints`, `WindField`, `DynamicChainSolver`, `BoneChainPhysicsResolver`, `SphereCollider`, `BoxCollider`, `CapsuleCollider`, `Collider`, `FixedTimestepAccumulator`) | **JOB-SAFE** | (verlet-integration-1 campaign, `PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md`, 3.4.1) - the exact same reason the existing "Pure `src/Animation/*` modules" row is: every function is pure logic over only its own parameters, no static/global/singleton mutable state anywhere in the module, safe to call concurrently from any number of threads PROVIDED each individual call's own inputs/outputs (one chain's own `DynamicChainRuntimeState`) are never shared/aliased across two concurrent calls - true by construction here, since Phase 4's `DetectDynamicChains()` guarantees disjoint chains and this phase's own 3.4 dispatches exactly one job per chain, from `Game/Physics/PhysicsSystem.cpp` (v3/v4 - never `AnimationSystem.cpp`). `BoxCollider`/`CapsuleCollider`/`Collider` (task_manager/verlet-integration-9, PHASE1) are equally pure/stateless and execute through that exact same `StepDynamicChain()` job-body call path (PHASE2 wires `SolveCollision()` into `StepDynamicChain()`'s own step 5) - added here so a future contributor never has to assume job-safety by omission (verlet-integration-9, PHASE6). |
  | Concurrent, DISJOINT-INDEX writes into ONE shared `ResolvedAnimationPose::pose` (`std::vector<BoneLocalOffset>`), from several job bodies at once (verlet-integration-1, `PHASE5_...md`, 3.4) | **JOB-SAFE, conditioned on two invariants** | A NEW pattern, not covered by the existing `Registry`/ECS "mutation: NEVER" row above (that row is about the ECS `Registry`/`ComponentStorage<T>` specifically, not the CONTENTS of one already-fetched component's own `std::vector` field) - JOB-SAFE conditioned on TWO invariants this campaign's own design guarantees and any future edit must preserve: (1) `pose`'s SIZE is fixed and never resized/reallocated for the duration of the `Dispatch()`/`WaitForJobs()` bracket (no `push_back`/`resize` call anywhere inside a job body), and (2) every two concurrently-dispatched chains' own `jointBoneIndices` sets are provably DISJOINT (Phase 4's `DetectDynamicChains()` guarantee) - writing to genuinely disjoint indices of one fixed-size `std::vector` from different threads at once is safe (no reallocation, no false sharing of the SAME element), the same reasoning `Jobs::Dispatch()`'s own per-batch output-span writes already rely on for `skinnedPositions`/`skinnedNormals` in the existing CPU vertex-skinning row. Note this row is scoped to ONE entity's own `pose` at a time - `PhysicsSystem::Update()`'s own outer per-entity loop stays serial in this phase (see that method's own header comment), so no two entities' own `pose` vectors are ever touched concurrently either. NOTE (caveat, not yet closed): a chain's `rootBoneIndex` MAY be a branch bone shared by SEVERAL sibling chains (see `PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG_COMPLETION_REPORT.md`'s own "Next Steps" - `ApplyDynamicChainPhysicsToPose()` writes `pose[rootBoneIndex]`, not just `pose[jointBoneIndices[...]]`) - if two such sibling chains are ever dispatched into DIFFERENT concurrent batches, this specific shared write is NOT disjoint and remains an accepted, documented "last write wins" hazard identical in kind to the pre-existing single-threaded one, not a new regression introduced by this phase. |

  This table is not exhaustive of every symbol in the engine, but every row
  above was chosen because a future job body (starting with Phase 6's real
  CPU vertex skinning migration) would plausibly be tempted to reach for it
  - a future phase that needs to classify something not listed here should
  add a new row rather than assume an unlisted subsystem is safe by
  omission.

- **Phase 5 (Profiler Integration - Worker Timeline - see
  `task_manager/job_system/JOBSYSTEM_PHASE5_PROFILER_INTEGRATION_WORKER_TIMELINE_v2.md`
  and `task_manager/job_system/JOB_SYSTEM_PHASE5_COMPLETION_REPORT.md`)
  extends `src/Profiling/` so a worker thread's own scopes show up as real,
  attributed data, closing the Phase 4 table's own `Profiling::FrameProfiler`
  row ("NEVER (until Phase 5)") for exactly one new, narrow, genuinely
  thread-safe write path - every OTHER `FrameProfiler` method remains
  main-thread-only, unchanged.** `ProfilingTypes.h` gained `WorkerJobSample`
  (`workerIndex`/`name`/`milliseconds`/`startTicks`) and
  `kMaxWorkerJobSamplesPerFrame` (1024 - deliberately far larger than
  `kMaxCpuScopesPerFrame`, since this is a raw per-CALL log, never
  summed/deduplicated by name like `cpuScopes`), plus `FrameSample` gained
  `frameStartTicks` (the raw `SDL_GetPerformanceCounter()` reading
  `BeginFrame()` took to start that frame) and a
  `workerJobs`/`workerJobCount` array - all still plain, fixed-size, POD
  fields, so `FrameSample` itself remains trivially copyable into
  `FrameProfiler`'s existing ring buffer with zero design change there.
- **`FrameProfiler::RecordWorkerJobSample(workerIndex, name, milliseconds,
  startTicks)` is the ONE method on `FrameProfiler` safe to call
  CONCURRENTLY, from any number of Job System worker threads at once** -
  every other method (`BeginFrame()`/`EndFrame()`/`RecordCpuScope()`/
  `SetGpuPassTiming()`/`SetGpuPassDrawStats()`/`SetMemorySnapshot()`)
  remains main-thread-only, exactly as the Phase 4 table already documents.
  Implemented as a single atomic fetch-and-increment reservation
  (`m_currentWorkerJobCount`, a `std::atomic<std::size_t>` kept SEPARATE
  from `FrameSample::workerJobCount` itself, precisely because
  `std::atomic` is neither copyable nor assignable and could therefore
  never live INSIDE `FrameSample` without breaking its "plain, copyable
  POD" contract) - each caller gets its own, never-repeated index, so
  concurrent writes always land on DISJOINT array elements; no lock, no
  allocation, ever. `BeginFrame()` resets this counter to 0; `EndFrame()`
  snapshots it (clamped to `kMaxWorkerJobSamplesPerFrame`, mirroring
  `RecordCpuScope()`'s own overflow-drop behavior) into
  `m_current.workerJobCount` right before `m_current` is copied into
  history.
- **`FrameProfiler::m_captureEnabled` is now `std::atomic<bool>`, not a
  plain `bool` like every other member - this is a real, deliberate
  correctness fix, not a style change.** `RecordWorkerJobSample()` is the
  one place this flag is genuinely read from a worker thread, possibly at
  the EXACT same instant `SetCaptureEnabled()` is called from the main
  thread (e.g. a user toggling the Editor's "Capture" checkbox while jobs
  are in flight) - a plain `bool` read/written across threads with no
  synchronization is undefined behavior, not just "probably fine". Every
  other read of this flag (`BeginFrame()`/`EndFrame()`/`RecordCpuScope()`/
  etc.) remains main-thread-only and unaffected by this change.
  `m_frameInProgress`, by contrast, DELIBERATELY stays a plain `bool` -
  reading it from a worker thread is safe without atomics ONLY because of
  the Job System's own caller obligation that every `Dispatch()`/
  `WaitForJobs()` bracket completes in full before the frame it belongs to
  ends, which establishes a real happens-before edge from
  `BeginFrame()`/`EndFrame()`'s own writes through to a job body's read,
  via the Job System's internal mutex/condition-variable synchronization -
  do not "fix" this one the same way; it would just be redundant.
- **`gte::Jobs::JobSystem::WorkerIndexForCurrentThread()` (returns
  `std::optional<std::size_t>`) is what `Profiling::JobScopeTimer`
  (`src/Profiling/JobScopeTimer.h`) uses to attribute a recorded scope to
  the worker that ran it - a genuinely NEW public method on `JobSystem`,
  backed by a `thread_local std::optional<std::size_t>` set exactly once,
  at the very top of `WorkerLoop()`, for the real worker thread running
  it.** Returns `std::nullopt` for any thread that is NOT one of this
  pool's own real worker threads (the main thread, a Phase 3
  polling-fallback thread, ...) WHEN `GTE_ENABLE_JOB_SYSTEM` is `ON` - this
  is what actually enforces the "never call `GTE_PROFILE_JOB_SCOPE` from
  the main thread" rule below (a violation silently records nothing rather
  than crashing or fabricating a worker index). When
  `GTE_ENABLE_JOB_SYSTEM` is `OFF`, this instead ALWAYS returns `0` (never
  `std::nullopt`) - mirroring `WorkerCount()`'s own "always >= 1, never 0"
  contract, since there is no real worker-thread pool in that
  configuration to distinguish "the main thread" from "a job body running
  inline" in the first place (they are, by construction, the exact same
  thread) - this is a deliberate design choice so an `OFF` build still
  produces meaningful (if trivially single-row) worker-timeline data
  instead of permanently blank data, at the honest cost of this specific
  misuse-detection rule only being genuinely enforced when
  `GTE_ENABLE_JOB_SYSTEM` is `ON`.
- **`GTE_PROFILE_JOB_SCOPE("Name")` (`src/Profiling/JobScopeTimer.h`) is the
  per-job-body counterpart of `GTE_PROFILE_SCOPE` - the ONLY correct way to
  profile code running INSIDE a job body.** Mirrors `ScopeTimer`'s own
  two-layer on/off convention exactly (compiles to a true empty no-op when
  `GTE_ENABLE_PROFILER` is `OFF`; skips the clock read at runtime when
  `FrameProfiler::IsCaptureEnabled()` is `false`), plus the one additional
  runtime check described above (`WorkerIndexForCurrentThread()` must
  return a value). NEVER call `GTE_PROFILE_SCOPE` from inside a job body
  (it is completely unsynchronized - see the Phase 4 table's own
  `FrameProfiler` row), and NEVER call `GTE_PROFILE_JOB_SCOPE` from the
  main thread (see the previous bullet for exactly what happens if you do,
  and why that enforcement is `GTE_ENABLE_JOB_SYSTEM`-dependent).
- **`Profiling::BuildWorkerTimelinePoints()`/`ComputeDistinctWorkerCount()`
  (`src/Profiling/WorkerTimelineData.h/.cpp`) is the pure, always-compiled,
  ImGui-free "one frame's raw `WorkerJobSample` log -> a per-worker
  timeline" reshape - mirrors `FrameGraphData.h`'s own "always-compiled
  reshape" precedent exactly, so a future Phase 7 "Jobs" panel (and any
  future benchmark-mode consumer) reads through this ONE function rather
  than re-deriving the same reshape logic independently.** Each returned
  `WorkerTimelinePoint::startMilliseconds` is computed relative to
  `FrameSample::frameStartTicks` (never a raw absolute tick count a future
  caller would otherwise have to re-derive the frame's own start from) via
  `SDL_GetPerformanceFrequency()` - the same clock/units this whole module
  standardizes on. Never re-sorts `FrameSample::workerJobs` - preserves
  recording order exactly, and only ever reads the first
  `workerJobCount` entries, never anything beyond it (stale/leftover array
  slots past that count are never touched).
- **Phase 6 (First Production Consumer - Animation / Vertex Skinning - see
  `task_manager/job_system/JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md`
  and `task_manager/job_system/JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md`) is
  the campaign's own production cut-over: `AnimationSystem::Update()`
  (`src/Game/Animation/AnimationSystem.cpp`) is now the first, and only,
  real (non-test) call site anywhere in the engine that calls
  `gte::Jobs::Dispatch()`/`WaitForJobs()`.** For each currently-playing
  `SkeletalAnimator`, CPU vertex skinning (previously always a single,
  serial `Animation/VertexSkinning.h::SkinVertices()` call covering the
  WHOLE model) now branches on vertex count: below
  `kMinVerticesToParallelize` (512) it still runs inline, serially, via a
  direct `SkinVertexRange(0, vertexCount, ...)` call (scheduling a
  `Dispatch()` for a genuinely tiny model would cost more in scheduling
  overhead than it saves); at or above that threshold, it is split into
  batches (floored at `kMinVerticesPerBatch`, 256, so `Dispatch()` never
  splits smaller than that) and skinned via a real
  `Jobs::Dispatch(&RunSkinningBatch, ...)` + exactly one
  `JobSystem::Instance().WaitForJobs(skinningHandle)` call before that
  model's parts are re-uploaded to the GPU.
- **`Animation/VertexSkinning.h`'s `SkinVertexRange(beginIndex, endIndex,
  ...)` is the new, pure, always-compiled function both the serial and
  parallel skinning paths above are built on - `SkinVertices()` itself is
  now implemented purely in terms of `SkinVertexRange(0,
  bindPositions.size(), ...)`, so there is exactly ONE copy of the actual
  per-vertex blending logic, never two independently-maintained copies.**
  Unlike `SkinVertices()`, `SkinVertexRange()` NEVER resizes its
  `outPositions`/`outNormals` vectors - the caller must size them to the
  full vertex count BEFORE dispatching any batches, since two concurrent
  batches writing into two different `[begin, end)` slices of the same
  vectors must never race a third, hidden reallocation triggered by one of
  them calling `resize()`. `endIndex` is defensively clamped internally
  against the real vertex/output-vector sizes - never reads/writes out of
  bounds even if a caller ever passed a bad range. This refactor is
  behavior-preserving only: every pre-existing
  `tests/Animation/VertexSkinningTests.cpp` test passes unchanged against
  it, and a new `tests/Animation/VertexSkinningParityTests.cpp` (Tier 1,
  real `JobSystem::Instance()`, no live Renderer/GPU involved - see below)
  proves a large, synthetic model skinned via several CONCURRENT
  `Dispatch()` batches produces results IDENTICAL, vertex-for-vertex, to
  the original serial `SkinVertices()` call.
- **`AnimationSystem::Update()`'s per-batch job-body trampoline
  (`RunSkinningBatch()`, an anonymous-namespace function local to
  `AnimationSystem.cpp`) wraps its call to `SkinVertexRange()` in
  `GTE_PROFILE_JOB_SCOPE("SkinVertices")`** - the one sanctioned way (per
  this section's own Phase 5 bullets above) to profile code running inside
  a job body. The GPU upload step that follows (`Mesh::UpdateVertexData()`
  via `RenderSystem::TryGetMesh()`) is completely unchanged and stays
  main-thread-only, unconditionally - exactly matching the Phase 4
  thread-safety audit table's own `Renderer`/`Mesh` NEVER row; a job body
  never sees a `Mesh*`/GPU handle of any kind, only the plain
  `SkinningBatchContext` (five plain pointers into read-only input data
  plus this call's own output vectors).
- **`AnimationSystem::Update()`'s OUTER loop over every live
  `SkeletalAnimator` MUST remain strictly sequential - one animator at a
  time - and this is now enforced by an explicit, prominent code comment
  directly at that loop's own call site, not merely documented here or in
  the strategy document.** Two entities spawned from the SAME `*.gta` file
  share one underlying GPU `Mesh` (see `README.md`'s own documented
  limitation, cross-referenced by this section's own Phase 4 table row
  above) - today this sharing is safe ONLY because this loop processes one
  animator's entire per-model sequence (skinning dispatch + wait + every
  part's GPU upload) to full completion before the next animator's own
  sequence begins. Restructuring this loop to fire off every animator's
  own `Dispatch()` up front and wait on all of them together - a
  natural-looking next optimization once this phase exists - would let two
  different worker threads write the SAME shared GPU buffer at the SAME
  time: a genuine, unsynchronized data race, strictly worse than today's
  harmless "last write wins" visual bug. This rule may only be lifted once
  every spawned model instance owns its own private GPU mesh buffers - a
  separate, unstarted piece of engine work (see `README.md`/`TODO.md`).
