# Job system
> The worker thread pool and the parallel-for API in `src/Jobs/`. For anyone who writes code that runs on worker threads.
> Verified against code: 2026-10-30

## TL;DR
- `gte::Jobs::JobSystem` is a singleton thread pool. Pool size is `std::thread::hardware_concurrency()` (minimum 1).
- Use `Jobs::Dispatch` for loops. It splits the items into batches and runs one job per batch.
- A job body is a plain function pointer plus a `void*` payload. No `std::function`.
- A job body must not touch Vulkan, ImGui, ECS writes, `AssetDatabase` or `ResourcePool`. See the [safety table](#thread-safety-table).
- Profile inside a job with `GTE_PROFILE_JOB_SCOPE` only.

## API
| Call | Header | What it does |
|---|---|---|
| `JobSystem::Instance()` | `JobSystem.h` | Singleton. The workers start in its constructor, on the first call. |
| `Schedule(fn, payload, handle)` | `JobSystem.h` | Run one job. |
| `WaitForJobs(handle)` | `JobSystem.h` | Block until every job on `handle` has finished. |
| `WorkerCount()` | `JobSystem.h` | Pool size. Always 1 or more. |
| `WorkerIndexForCurrentThread()` | `JobSystem.h` | Index of the current worker. `std::nullopt` on any other thread. |
| `Dispatch(fn, itemCount, payload, handle, minItemsPerBatch = 1)` | `JobDispatch.h` | Parallel-for. `fn(begin, end, payload)` per batch. |
| `ComputeBatchRanges(items, workers, minPerBatch)` | `JobDispatch.h` | Pure splitter. Used by `Dispatch`. |
| `ScheduleAfter` / `DispatchAfter` | `JobContinuation.h` | Same as above, but start after other handles finish. |

- `JobFunction` is `void (*)(void*)`. `BatchJobFunction` is `void (*)(uint32_t begin, uint32_t end, void*)`.
- A `JobHandle` can be reused for many jobs. It is complete when every job on it has finished.

## Rules
1. The payload outlives the jobs. Keep it alive until `WaitForJobs` returns, also on the exception path.
2. NEVER throw out of a job body. Workers do not catch. Turn errors into a result value.
3. `Dispatch` schedules batches, not items. Batch count is at most `WorkerCount()`.
4. Each batch has at least `minItemsPerBatch` items. Fewer items than that give exactly one batch.
5. Size output vectors BEFORE `Dispatch`. Batches write disjoint ranges. No `resize` or `push_back` in a batch.
6. Keep small work inline. Check a threshold first (see [Where it is used](#where-it-is-used)).
7. Wrap a main thread wait in `GTE_PROFILE_BLOCKED("Wait.Workers")`.
8. In a job body use `GTE_PROFILE_JOB_SCOPE("Name")`. NEVER `GTE_PROFILE_SCOPE` there. NEVER the job scope on the main thread.
9. Change a completion counter or a watcher list only under the mutex that the waiter checks. This stops lost wake-ups.
10. Pass explicit dependencies. There is no automatic data-flow detection.

## Build switches
| Switch | Default | Effect |
|---|---|---|
| `GTE_ENABLE_JOB_SYSTEM` | ON | OFF: `Schedule` runs the job inline on the caller. `WorkerCount()` is 1. `WorkerIndexForCurrentThread()` is 0. |
| `GTE_ENABLE_PROFILER` | ON | OFF: `GTE_PROFILE_JOB_SCOPE` compiles to nothing. |

- `JobSystem` and `JobQueue` always compile. Only the behaviour changes. Never gate Jobs tests behind a switch.
- With the job system OFF, the "job scope on the main thread" mistake is not detected.

## Thread safety table
| Area | Rule | Why |
|---|---|---|
| `JobSystem`, `JobQueue`, `JobHandle` | JOB-SAFE | Built for concurrent use. A job may schedule more jobs. |
| Profiling clock (`GetProfilingPerformanceCounter`) | JOB-SAFE | Stateless read of a monotonic clock. |
| `FrameProfiler` | NEVER, except `RecordWorkerScope` | Rest is main thread only. `GTE_PROFILE_JOB_SCOPE` uses the safe path. |
| `Renderer`, `Vulkan/*`, `GpuMemoryTracker` | NEVER | Main thread only. No locks inside. |
| `RenderGraph/*` | NEVER | Issues GPU calls. Mutates unsynchronized state. |
| ImGui, `src/Editor/*` | NEVER | ImGui is single threaded. |
| `Registry`, `EntityManager`, `ComponentStorage<T>` | Write: NEVER. Read: READ-SAFE | Read only while the main thread does not write. Best: copy plain values first. |
| `AssetDatabase` | NEVER | No locks. Not proven safe for reads. |
| `ResourcePool<T, Handle>`, GPU catalogs in `Game/Instantiation` | NEVER | They hand out live GPU pointers. Resolve on the main thread. |
| `src/Math`, pure `src/Animation` solvers, `src/Physics` | JOB-SAFE | Pure functions. Inputs and outputs must not be shared between two calls. |
| `MeshData`, `SkeletonData`, `MotionData`, `MaterialData` | READ-SAFE | Written once at load. Never changed after. |
| Rig, clip and binding caches in `Game/Animation` | NEVER to change. Values READ-SAFE | A new insert can move old entries. Resolve all lookups before `Dispatch`. |
| `ResolvedAnimationPose::pose` | JOB-SAFE for disjoint indices | Size must not change. Chains must not share joint indices. |

Meaning of the words:

| Word | Meaning |
|---|---|
| NEVER | A job must not touch it, even to read. |
| READ-SAFE | A job may read it if nobody writes during the `Dispatch` to `WaitForJobs` window. |
| JOB-SAFE | Any number of threads may call it at once. |

## Where it is used
| Caller | Work | Threshold |
|---|---|---|
| `AnimationSystem` (CPU skinning mode only) | Vertex skinning and vertex packing | 512 vertices or more, batches of 256 or more |
| `PhysicsSystem` | One item per Verlet chain | 24 joints or more per entity |
| `Core::BuildClusterBounds` | Cluster bounds chunks | 2 chunks or more |
| `MeshClusterBuildRunner` | One `Schedule` job for a cluster build | Runs inline if `WorkerCount()` is below 2 |

- `ScheduleAfter` and `DispatchAfter` have no production caller. Only tests use them.
- GPU skinning mode does not use the pool. It uploads bone matrices only.

## Continuations
- If every dependency is already complete, `ScheduleAfter` is a plain `Schedule`.
- The output handle is incomplete from the moment the call returns. You may call `WaitForJobs` right away.
- A handle keeps up to 8 watchers (`kMaxWatchersPerHandle`). More than 8 use a polling thread.
- The polling thread is registered with `JobSystem` and joined at shutdown. It never goes through `Schedule`.
- With the job system OFF, `WaitForJobs` spins with `yield`.

## How to add a parallel loop
1. Write a pure function over a range: `Foo(begin, end, inputs..., outputs...)`.
2. Write a trampoline `void RunFooBatch(uint32_t b, uint32_t e, void* p)`. Add `GTE_PROFILE_JOB_SCOPE`.
3. On the main thread, resolve all pointers. Size the output vectors.
4. If the item count is below your threshold, call `Foo(0, n, ...)` directly.
5. Else call `Jobs::Dispatch(&RunFooBatch, n, &context, handle, minItemsPerBatch)`.
6. Call `JobSystem::Instance().WaitForJobs(handle)` before you read the output.
7. Test that serial and parallel results are identical. Repeat the test (`--gtest_repeat=50`).

## Checks
- Tests: `tests/Jobs/`. Pure splitting math is tested apart from `Dispatch`.
- Parity test: `tests/Animation/VertexSkinningParityTests.cpp`.
- Run: `build\tests\GreatTamanaEngineTests.exe --gtest_filter=Job*Tests.*:VertexSkinningParityTests.* --gtest_repeat=50`.

## Gotchas
- The queue has 4096 slots. When it is full, `Schedule` runs the job inline on the caller.
- `Dispatch` allocates one small context per batch. This is bounded by `WorkerCount()`.
- Models from one `*.gta` share one GPU `Mesh`. Process one model at a time: dispatch, wait, upload, next.
- Do not start every model's `Dispatch` first and wait later. Two workers would write the same buffer.
- A test that holds a dependency pending must start it from its own `std::thread`. On the main thread it deadlocks with the job system OFF.

## Related
- [Profiling](../debugging/profiling.md) for job scopes and the worker timeline
- [ECS](../architecture/ecs.md) for the `Registry` threading rules
- [Animation and physics](../architecture/animation-physics.md)
- [Contributing](contributing.md)
