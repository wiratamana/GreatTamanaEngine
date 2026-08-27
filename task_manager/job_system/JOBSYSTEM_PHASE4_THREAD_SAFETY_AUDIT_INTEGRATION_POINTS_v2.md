# Job System — Phase 4: Thread-Safety Audit + Integration Point Whitelist — v2

Parent document: `JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md` (read first).
Previous phase: `JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md` (v1,
unchanged, must be done).
Next phase: `JOBSYSTEM_PHASE5_PROFILER_INTEGRATION_WORKER_TIMELINE_v2.md`.

**Definition of Done for this phase (gates Phase 5 and Phase 6):**
unchanged from v1 — every existing shared/global/singleton piece of engine
state classified NEVER/READ-SAFE/JOB-SAFE in a real, reviewable table
(destined for `AGENTS.md`), with one addition: the table must also cover
the specific new entries this v2 identifies (§3.1 below), and the specific
new verification item in §3.2 (SDL clock thread-safety) must be performed
and its result recorded before this phase is considered closed.

---

## Revision Notes (2nd Iteration / v2)

Checked v1's classification table against the real, current codebase
(`RenderGraph.h`/`.cpp` and friends, `MeshInstantiationSystem.h`/`.cpp`,
`ImGuiEditorLayer.cpp`, and the `Profiling`/`ScopeTimer.h` module's actual
use of `SDL_GetPerformanceCounter()`). Two kinds of findings:

1. **Table completeness gaps** — subsystems that exist, are exercised
   every frame, and were not given their own explicit row in v1's table.
   None of these change any conclusion (they all end up NEVER, same as the
   table's existing catch-all reasoning would already imply) — but Phase
   4's whole reason for existing is to make the implicit explicit, so
   leaving them out defeats its own purpose. Added as new rows in §3.1.
2. **A real, previously-unstated verification requirement**: Phase 5's
   `JobScopeTimer` (see `JOBSYSTEM_PHASE5_PROFILER_INTEGRATION_WORKER_TIMELINE_v2.md`)
   calls `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()` from
   inside a job body running on a worker thread. v1's own `ScopeTimer.h`
   (main-thread-only, today) already calls these same two functions, so
   there was an implicit assumption that doing so from a worker thread is
   equally safe — but nothing in this campaign ever actually said so, or
   confirmed it against SDL3's own documented contract. Added as a new,
   concrete verification requirement in §3.2, with the expected answer
   (yes, these are safe — see the reasoning there) recorded up front so a
   future implementer isn't left guessing, while still requiring the
   quick, cheap confirmation step before Phase 5 is allowed to rely on it
   in production.

Also folded in the master strategy's own Revision Note 3 (the cross-model
shared-GPU-buffer race) as its own explicit table entry and rule, cross-
referenced to Phase 6 v2's §3.6, so anyone reading Phase 4 in isolation
(without having read the master doc's revision notes first) still gets the
warning at the point where they'd naturally look for it — inside the
classification table itself.

---

## Step 1: The Goal (Where are we going?)

Unchanged from v1: a definitive, engine-wide, written answer to "if a job
function body, running on a worker thread, tries to touch this, is that
safe" — now with a more complete table and one additional concrete
verification requirement.

---

## Step 2: The Situation / The Problem (Where are we now?)

Unchanged from v1 in its core argument. One additional fact worth stating
here because it changes how thoroughly future rows must be audited: this
engine's Render Graph (`src/Renderer/RenderGraph/`) and its own GPU
timestamp-query infrastructure (`GpuTimingService`,
`RenderGraphTimestampPool`) are now BOTH real, fully wired, production
code paths (confirmed directly against `Application.cpp`'s real
`Run()`) — not hypothetical future work as an earlier, less-informed
reading of this campaign might assume. Every one of them touches Vulkan
handles and/or `GpuMemoryTracker`, so every one of them is squarely inside
this phase's existing NEVER bucket for GPU-adjacent code — but they are
numerous and easy to individually overlook when listing "everything a
future job body might accidentally reach into," which is exactly why
§3.1 below gives the Render Graph its own explicit row rather than relying
on a reader to infer it from the `GpuMemoryTracker`/`Renderer` rows alone.

---

## Step 3: The Plan (How will we get there?)

### 3.1 — The Thread-Safety Classification Table (v2, expanded)

Every row from v1's table (`FrameProfiler`, `GpuMemoryTracker`, `Registry`
mutation/read, `AssetDatabase`, `ResourcePool<T, HandleT>`, the pure
`Animation/` files, `Math/`, `MeshData`/`SkeletonData`/`MotionData`
read-only access, and the three animation caches) is UNCHANGED and still
correct — re-verify each against the real source before trusting it, per
v1's own §3.2 audit discipline, but no conclusion changes. The following
rows are NEW in v2:

| Subsystem | Classification | Why |
|---|---|---|
| `src/Renderer/RenderGraph/*` (`RenderGraph`, `RenderGraphBuilder`, `RenderGraphCompiler`, `RenderGraphResourcePool`, `RenderGraphBarrierPlanner`, `RenderGraphTimestampPool`) | **NEVER** | Every one of these either directly issues Vulkan calls, touches `GpuMemoryTracker`-tracked resources, or mutates shared, unsynchronized compiler/pool state (`RenderGraphResourcePool`'s frame-to-frame texture reuse bookkeeping) — squarely the same GPU-resource-adjacent reasoning as `GpuMemoryTracker`/`Renderer` itself. No job in this campaign's current scope has any reason to touch any of it, and none ever should without a fresh, dedicated audit of its own. |
| `src/Editor/*` (ImGui context/widget state, `EditorContext`, every `Panels/*Panel`) | **NEVER** | Dear ImGui's own context (`ImGuiContext`) is explicitly documented upstream as not safe for concurrent access from multiple threads, and every Editor panel in this engine already only ever runs on the main thread inside `IEditorLayer::BuildUI()`/`Render()`. No job body has any legitimate reason to touch ImGui state directly — the Phase 7 "Jobs" panel reads job/profiler DATA (via `FrameProfiler`'s own thread-safe `RecordWorkerJobSample()` path, Phase 5), never ImGui state from a worker thread. |
| `src/Game/Instantiation/*` (`PrimitiveGpuCatalog`, `MaterialTextureGpuCache`, `MeshAssetGpuCatalog`) | **NEVER** | GPU-resource-creating/caching code, unsynchronized, main-thread-only by construction today (called only from `MeshInstantiationSystem`, itself called only from `Game`'s own main-thread methods). Never touch these from a job body — this is what Phase 6's boundary design (job bodies only ever see plain CPU-side spans, never a `Mesh`/GPU handle) is specifically built to guarantee by construction, not just by convention. |
| Cross-entity/cross-instance shared GPU mesh buffers (the documented `README.md` limitation: two entities spawned from the same `*.gta` share one underlying `Mesh`) | **NEVER, concurrently** — sequential access only, main-thread-orchestrated | Not a "touch this subsystem" rule like the others above, but a cross-cutting HAZARD this table must call out explicitly: today this sharing is safe only because `AnimationSystem::Update()` processes every animator strictly one at a time, on one thread. The instant more than one animator's own `Dispatch()`/`WaitForJobs()` sequence is allowed to be in flight simultaneously, two worker threads could write the same shared output buffer concurrently — a genuine data race, not merely the pre-existing "last write wins" visual bug. See `JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md`, §3.6, for the permanent mitigating rule (strictly sequential per-model dispatch) this campaign commits to. |

### 3.2 — Verifying "JOB-SAFE", not just asserting it (v2 addition)

All of v1's §3.2 verification steps (source-level audit for
`GTE_PROFILE_SCOPE`/static state/heap allocation/NEVER-classified calls,
optional build-time hardening, Phase 8's ThreadSanitizer backstop) are
unchanged and still required.

**New verification item, required before Phase 5 may rely on it:**
confirm that `SDL_GetPerformanceCounter()` and
`SDL_GetPerformanceFrequency()` are safe to call concurrently from
multiple threads without external synchronization. This matters because
Phase 5's `JobScopeTimer` (the per-job-body equivalent of `ScopeTimer`)
calls both of these functions from inside a job body running on an
arbitrary worker thread, while the MAIN thread may simultaneously be
inside its own `ScopeTimer` construction/destruction calling the exact
same two functions for a main-thread scope. `AGENTS.md`'s own "Profiling"
section already commits this whole module to SDL's performance counter as
its ONE clock — but that commitment was made, and has only ever been
exercised, in a single-threaded engine. The expected, and to-be-confirmed,
answer: yes, these are safe — both are pure, stateless queries against a
platform-level monotonic counter/its fixed frequency (SDL3's own
documentation describes `SDL_GetPerformanceCounter()` as returning the
current value of a high-resolution counter with no shared mutable engine
state involved in servicing the call, and `SDL_GetPerformanceFrequency()`
as returning a fixed, queried-once-effectively-constant value for the
life of the process) — but "expected" is not the same bar this campaign
holds everything else to. Confirm this explicitly (a small, dedicated
multi-threaded smoke check calling both functions from several threads
concurrently and comparing results for sanity, or, at minimum, a clear
citation of SDL's own contract recorded in this phase's own notes) and
record the result in `AGENTS.md`'s new "Job System" section, not just
assumed silently. If this were ever found to be unsafe (it will not be,
per SDL's own design, but the campaign's own discipline requires checking
rather than assuming), Phase 5 would need its own dedicated, job-thread-
safe monotonic clock source instead of reusing SDL's directly — a fallback
worth naming here even though it is not expected to be needed.

### 3.3 — The one remaining subtlety: false sharing (unchanged from v1)

No changes to this subsection.

---

## Step 4: What We Will NOT Do (Focus)

Unchanged from v1, in full.

---

## Step 5: Their Role (What does this mean for you?)

Unchanged from v1's five bullets, plus one addition: perform the SDL clock
concurrency check from §3.2 above and write its result into `AGENTS.md`
alongside the rest of this phase's table — do not let Phase 5 quietly
inherit an unverified assumption just because it "probably works", even
when that assumption is, in fact, almost certainly correct.
