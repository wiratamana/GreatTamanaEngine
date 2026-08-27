# GreatTamanaEngine — Phase 5 Grand Strategy: GPU Memory Usage Over Time (v2)

Status: PROPOSAL / PLANNING DOCUMENT — no implementation yet.
Scope: `PROFILER_STRATEGY_v2.md`'s own **Phase 5 — "GPU memory usage over
time"**, immediately following the already-shipped Phase 2 (frame-time
graph data) and Phase 3 (draw-call/triangle counts). Phase 4 (GPU
timestamp queries) is explicitly, deliberately skipped for now, per your
own instruction — nothing below depends on it, reaches for it, or blocks
on it in any way.

--------------------------------------------------------------------------
## Changes from v1 (this is a review/2nd-iteration pass, not a rewrite)
--------------------------------------------------------------------------

v1 was re-checked line-by-line against the CURRENT real source tree
(`ProfilingTypes.h`, `FrameProfiler.h/.cpp`, `GpuMemoryTracker.h`,
`Renderer.h/.cpp`, `Application.cpp`, `FrameProfilerTests.cpp`,
`TESTING.md`, `AGENTS.md`, root `CMakeLists.txt`, `tests/CMakeLists.txt`)
one more time for this pass. The overwhelming majority of v1's factual
claims (exact field names/signatures/line numbers/existing test names)
were verified accurate and are carried forward unchanged. Three real
problems were found and are fixed in this version:

1. **v1 contained a factual error about `TESTING.md`.** Step 2.3/3.5 of v1
   claimed `TESTING.md`'s `Profiling/FrameProfilerTests.cpp` bullet was
   stale, still describing a pre-Phase-3 `SetGpuPassSample()` method. This
   is **not true** — the actual current `TESTING.md` already correctly
   reads "...`RecordCpuScope()`/`SetGpuPassTiming()`/`SetGpuPassDrawStats()`/
   `SetMemorySnapshot()`..." (that staleness was fixed during the Phase 3
   session, per `PROFILER_IMPLEMENTATION_STATUS_v4.md`'s own changelog).
   Acting on v1's claim would have sent an implementer hunting for a
   staleness that doesn't exist. Removed from this version's documentation
   checklist; see Step 3.5 below for what actually still needs updating.
2. **v1 missed a real, still-live piece of doc drift.** `AGENTS.md`'s
   "Profiling" section currently contains this sentence, verbatim: *"GPU
   TIMING (Phase 4) and the memory snapshot (Phase 5) remain the only
   producers still unwired, set only synthetically by tests via
   `FrameProfiler::SetGpuPassTiming()`/`SetMemorySnapshot()`."* This
   sentence becomes FALSE the moment this phase lands (memory becomes a
   real, wired producer — only Phase 4/GPU timing remains synthetic-only).
   v1's Step 3.5 talked about adding a new bullet to that section but never
   flagged this specific existing sentence as something that must also be
   edited in the same change, exactly the kind of "a document that no
   longer matches reality" drift this project's own conventions
   (`PROFILER_IMPLEMENTATION_STATUS_v4.md`'s own "living document" framing)
   explicitly warn against tolerating. Added to Step 3.5 below.
3. **The plan's own testability reasoning didn't hold up (the actual
   substantive fix in this version).** v1 places the
   `GpuMemoryTracker::Totals -> Profiling::MemorySnapshot` field-mapping
   conversion in an anonymous-namespace helper INSIDE `Application.cpp`
   (`BuildMemorySnapshot()`), then argues (Step 3.2) that this needs no
   dedicated test of its own because the six new `FrameProfiler` tests
   "would catch a transposed-field bug... by symmetry." **That reasoning is
   wrong.** Every one of those six tests hand-constructs a `MemorySnapshot`
   directly and passes it straight to `FrameProfiler::SetMemorySnapshot()`
   — none of them ever CALL `BuildMemorySnapshot()` at all (it's in an
   anonymous namespace, unreachable from `tests/`, and `Application.cpp` is
   itself untested Tier 2 composition-root code). A real bug in exactly the
   one genuinely new piece of logic this phase introduces — e.g.
   transposing `totals.bufferBytes`/`totals.textureBytes`, the specific
   failure mode v1's own Step 3.2 comment warns about by name — would
   compile clean, pass all six new tests, and silently ship. This directly
   contradicts `AGENTS.md`'s own "Testability & Regression Safety" rule:
   *"Design new logic to be Tier-1-testable whenever the underlying
   problem allows it"* — and it clearly does allow it here: the function
   takes a plain, already-resolved `GpuMemoryTracker::Totals` POD (no live
   `VkDevice`/`VmaAllocator` needed to construct one by hand, exactly like
   `tests/Renderer/VertexTests.cpp`/`DrawStatsTests.cpp` already touch
   Vulkan-adjacent plain types with zero live GPU device) and returns a
   plain `Profiling::MemorySnapshot`. **Fix: give this conversion its own
   tiny, real, Tier-1-tested home** — a new header,
   `src/Application/MemorySnapshotBuilder.h`, plus a new test file,
   `tests/Application/MemorySnapshotBuilderTests.cpp` — mirroring exactly
   how this codebase already treats every other small "trivial but
   risky-to-get-wrong" pure conversion (`src/Renderer/DrawStats.h`,
   `src/Game/Instantiation/MeshVertexPacking.h`,
   `src/Game/Instantiation/MeshMaterialPartitioner.h`, all of which got
   their own file + dedicated test rather than being inlined untested).
   This is a genuine, if small, deviation from v1's "zero new files, zero
   new CMake wiring" framing — see the revised Step 1.3/2.2/3.2/3.4/4/5.2
   below for exactly what that costs (one new header, one new test file,
   two one-line `CMakeLists.txt` additions) and why it's worth it. Nothing
   else about v1's plan needed to change to accommodate this — the
   production call site, the six `FrameProfiler`-level tests, and every
   other piece of v1's reasoning stand as originally written.

Everything below is the full strategy, v1's text carried forward with
these three corrections folded in (not a diff you have to cross-reference
against v1).

--------------------------------------------------------------------------
## Step 1: The Goal (Where are we going?)
--------------------------------------------------------------------------

### 1.1 What "done" looks like, concretely

When this phase is complete, every frame the engine renders will carry a
**real, measured** GPU memory snapshot — total bytes, buffer bytes,
texture bytes, buffer count, and texture count (plus, since the data
model already has room for them at zero extra cost, the existing
`gpuOnlyBytes`/`cpuOnlyBytes`/`sharedBytes` breakdown too) — sitting
inside `FrameProfiler`'s per-frame ring buffer
(`FrameSample::memory`, a `Profiling::MemorySnapshot`) the instant this
phase lands. This is the exact same "reachable by a unit test, a
throwaway diagnostic, or `Profiling::FrameGraphData.h` with zero further
work" bar Phase 2/3 already cleared for CPU frame time and draw/triangle
counts respectively.

No Editor panel is built in this phase (that is Phase 7's job, and is
explicitly out of scope here — see Step 4). No new memory *tracking*
mechanism is introduced anywhere (the engine already has exactly one,
`GpuMemoryTracker`, and this phase's entire job is to read it, once per
frame, into the already-existing `FrameProfiler` ring buffer — never to
duplicate it). Production-code-wise this phase really is one new call
site plus one small, independently-testable field-mapping function — see
Step 2.1/3.2.

### 1.2 Why this phase, why now

Both `PROFILER_IMPLEMENTATION_STATUS_v4.md` (this session's own starting
point) and `PROFILER_STRATEGY_v2.md` itself independently rank Phase 5 as
"cheap" — and, having re-read the actual current source end to end again
for this pass, that verdict is still accurate. Almost every piece Phase 5
needs already exists, already tested, already O(1), already wired into
`Renderer`'s public API:

- `GpuMemoryTracker::GetTotals()` (`src/Renderer/Memory/GpuMemoryTracker.h`)
  — an O(1), incrementally-maintained aggregate, exposed publicly as
  `Renderer::GetMemoryTotals()` (`src/Renderer/Renderer.h`, forwarding
  straight to `m_resources.GetMemoryTotals()` — confirmed in
  `Renderer.cpp`), already safe to call every frame (its own doc comment
  says exactly that — "safe to call every frame if desired (e.g. the
  Editor's 'Memory' panel header)").
- `Profiling::MemorySnapshot` (`src/Profiling/ProfilingTypes.h`) — a
  plain, Vulkan-free struct **already shaped as an exact mirror of
  `GpuMemoryTracker::Totals`**, field-for-field, plus the one thing
  `Totals` itself doesn't carry: a `GpuSampleStatus status` tri-state.
  This struct was **already written, during Phase 0**, specifically so
  "Phase 5's actual per-frame sampling step is the one place that
  converts between the two, once it exists" (its own doc comment, quoted
  verbatim) — Phase 5 is not designing a new data shape, it is filling in
  a blank that was left for it on purpose, three phases ago.
- `FrameProfiler::SetMemorySnapshot(const MemorySnapshot&)`
  (`src/Profiling/FrameProfiler.h/.cpp`) — **already fully implemented,
  already correctly no-ops outside a `BeginFrame()`/`EndFrame()` bracket
  or while capture is disabled, already has a partial test proving it
  works** (`FrameProfilerTests.cpp`'s
  `SetGpuPassTimingAndDrawStatsAndMemorySnapshotAreRecorded`). Zero
  changes needed to `FrameProfiler.h/.cpp` themselves.
- `FrameSample::memory` — already a field on the per-frame ring-buffer
  record, already defaults to `GpuSampleStatus::Absent` on every
  `BeginFrame()` (via `m_current = FrameSample{}`), already proven
  correct by an existing test
  (`GpuPassAndMemorySamplesDefaultToAbsent`).

**Every "Must have" requirement you listed is therefore either already
satisfied by existing, unmodified code, or is a small, well-understood,
narrowly-scoped addition.** This document spells out exactly which is
which, and closes two real gaps this pass actually found: a missing
outside-the-frame-bracket test for `SetMemorySnapshot()` (Step 2.3, same
finding as v1), and a missing direct test for the one bit of NEW
production logic this phase writes, the `Totals -> MemorySnapshot` field
mapping (Step 2.2, new in this version — see "Changes from v1" above).

### 1.3 Concrete deliverables

1. **One new call site** inside `Application::Run()`
   (`src/Application/Application.cpp`) that reads `m_renderer.GetMemoryTotals()`
   once per frame, converts it into a `Profiling::MemorySnapshot`, and
   hands it to `Profiling::FrameProfiler::Instance().SetMemorySnapshot()`
   — the ONE and ONLY production caller of that method, mirroring exactly
   how Phase 3's `SetGpuPassDrawStats()` calls are the ONE and ONLY
   production callers of that method in the same file today.
2. **One new, tiny, Tier-1-tested header**,
   `src/Application/MemorySnapshotBuilder.h`, holding exactly one pure
   function, `BuildMemorySnapshot(const GpuMemoryTracker::Totals&)`, that
   does the field mapping above — this is the one deliberate deviation
   from "zero new files" (see "Changes from v1" and Step 3.2 for the full
   reasoning: the six `FrameProfiler`-level tests alone cannot catch a bug
   in this specific function, since none of them call it).
3. **Seven new Tier-1 tests total**: six added to the already-existing
   `tests/Profiling/FrameProfilerTests.cpp` (mapped 1:1 to your six
   numbered test requirements — see Step 3.4), plus one new file,
   `tests/Application/MemorySnapshotBuilderTests.cpp`, directly exercising
   `BuildMemorySnapshot()`'s field mapping (not one of your six numbered
   requirements, but a necessary addition — see Step 2.2).
4. **Two one-line `CMakeLists.txt` additions** (the new header into root
   `CMakeLists.txt`'s unconditional source list, the new test file into
   `tests/CMakeLists.txt`'s unconditional `GTE_TEST_SOURCES` list) —
   everything else about this phase's production code lands inside files
   that are already unconditional entries in their respective lists, so no
   further build-system wiring is needed.
5. Documentation updates (`AGENTS.md`'s "Profiling" section — including
   correcting its own now-stale "memory snapshot... remains... unwired"
   sentence, not just adding a new bullet — `PROFILER_IMPLEMENTATION_STATUS_v4.md`
   → `_v5`, optionally `TESTING.md`) recording what changed, in the same
   spirit every prior phase's completion was recorded. See Step 3.5.
6. The full existing test suite (475 tests as of
   `PROFILER_IMPLEMENTATION_STATUS_v4.md` — **re-run `ctest` fresh
   immediately before writing any final count into a commit message or
   status-doc update**, per this codebase's own repeatedly-learned lesson
   about not trusting a copied-forward number — see Step 5.1) still
   passes, plus this phase's own seven new tests, with zero regressions.

### 1.4 What "done" looks like, in a sentence

A developer can spawn a hundred cubes, watch `FrameProfiler::LastCompletedFrame().memory`
climb in real time via a throwaway diagnostic (or, later, Phase 7's own
panel), destroy them, watch it fall back down — a frame where, for
whatever reason, no snapshot was captured reads back as **absent**, never
as a **lying zero** — and the one new piece of field-mapping logic behind
all of that has its own test proving no field was ever silently
transposed.

--------------------------------------------------------------------------
## Step 2: The Situation / The Problem (Where are we now?)
--------------------------------------------------------------------------

### 2.1 What already exists that this phase gets to reuse (the very good news)

Confirmed again, by direct inspection of the current source tree, for
this pass:

**`GpuMemoryTracker::Totals` — the single source of truth this phase reads
from, and must never duplicate** (`src/Renderer/Memory/GpuMemoryTracker.h`,
`struct Totals`, inside the class body):

```cpp
struct Totals {
    VkDeviceSize totalBytes = 0;
    VkDeviceSize bufferBytes = 0;
    VkDeviceSize textureBytes = 0;
    VkDeviceSize gpuOnlyBytes = 0;
    VkDeviceSize cpuOnlyBytes = 0;
    VkDeviceSize sharedBytes = 0;
    std::size_t bufferCount = 0;
    std::size_t textureCount = 0;
};
// O(1) - maintained incrementally by Track()/Untrack(), never
// recomputed by summing every live record.
Totals GetTotals() const noexcept { return m_totals; }
```

Exposed publicly, with the exact same shape, via `Renderer::GetMemoryTotals()`
(`Renderer.h`): `GpuMemoryTracker::Totals GetMemoryTotals() const;` —
documented as "Aggregate live-memory totals across every Buffer/
RenderTexture this Renderer has ever created and not yet destroyed... O(1);
safe to call every frame if desired." Re-confirmed this pass that
`Renderer::GetMemoryTotals()`'s body (`Renderer.cpp`) is a plain one-line
forward to `m_resources.GetMemoryTotals()` — no locking, no per-call
allocation, nothing that would make "call it once, unconditionally, every
single frame" a concern. This is **the existing, proven tracker your
instructions say to use, and not build a second one of** — there is
nothing else in this engine that measures GPU memory, and this phase must
never introduce one. `Renderer` already owns exactly one `GpuMemoryTracker`
(`m_memoryTracker`, a single `std::shared_ptr` shared by both `m_presenter`
and `m_resources` — see `Renderer.h`'s own comment on why there is
deliberately only ever one instance), so `GetMemoryTotals()` already
reflects the whole engine's GPU footprint, not some partial view.

**`Profiling::MemorySnapshot` — already exists, already shaped to receive
exactly this data, already Vulkan-free** (`src/Profiling/ProfilingTypes.h`):

```cpp
// A plain, Vulkan-free copy of GpuMemoryTracker::Totals's shape
// (src/Renderer/Memory/GpuMemoryTracker.h) - deliberately NOT that type
// itself, so this always-compiled, Editor/Renderer-independent module
// (see PROFILER_STRATEGY_v2.md, Phase 0's own "no Vulkan, no ImGui, no
// Editor dependency at all" goal) never needs to include a single Vulkan
// header. Phase 5's actual per-frame sampling step is the one place that
// converts between the two, once it exists.
struct MemorySnapshot {
    GpuSampleStatus status = GpuSampleStatus::Absent;
    std::uint64_t totalBytes = 0;
    std::uint64_t bufferBytes = 0;
    std::uint64_t textureBytes = 0;
    std::uint64_t gpuOnlyBytes = 0;
    std::uint64_t cpuOnlyBytes = 0;
    std::uint64_t sharedBytes = 0;
    std::uint64_t bufferCount = 0;
    std::uint64_t textureCount = 0;
};
```

Read that doc comment again: **this struct was written during Phase 0,
specifically anticipating this exact phase, by name.** Every field your
"Must have" list asks for (`total GPU bytes`, `buffer bytes`, `texture
bytes`, `buffer count`, `texture count`) is already present — plus three
more (`gpuOnlyBytes`/`cpuOnlyBytes`/`sharedBytes`) that cost nothing extra
to also fill in correctly, since they come from the exact same
`GpuMemoryTracker::Totals` value in the exact same call.

**`FrameProfiler::SetMemorySnapshot()` — already fully implemented, already
correct, already partially tested** (`FrameProfiler.h`/`.cpp`):

```cpp
void SetMemorySnapshot(const MemorySnapshot& snapshot) noexcept;
```
```cpp
void FrameProfiler::SetMemorySnapshot(const MemorySnapshot& snapshot) noexcept
{
    if (!m_captureEnabled || !m_frameInProgress) {
        return;
    }
    m_current.memory = snapshot;
}
```

This is the ENTIRE method. It already does exactly what your "Must have"
requirement #4 asks for: it is a no-op (does nothing at all) whenever
called outside a `BeginFrame()`/`EndFrame()` bracket or while capture is
disabled — the exact same guard every sibling setter
(`SetGpuPassTiming`/`SetGpuPassDrawStats`) already uses, already proven
correct for those two. **Zero changes are needed to `FrameProfiler.h`/`.cpp`
for this phase.**

**`FrameSample::memory` defaults to `Absent` on every `BeginFrame()`,
already proven** — `FrameProfiler::BeginFrame()` does `m_current = FrameSample{};`
unconditionally, and `FrameSample::memory{}` default-constructs to
`MemorySnapshot{}`, whose `status` field defaults to `GpuSampleStatus::Absent`
(`ProfilingTypes.h`). An existing test,
`FrameProfilerTests.cpp`'s `GpuPassAndMemorySamplesDefaultToAbsent`,
already asserts exactly this:
`EXPECT_EQ(frame.memory.status, GpuSampleStatus::Absent);` for a frame
where nothing was ever set.

**The exact production call-site pattern to copy already exists, twice,
in the very file this phase needs to touch** — `Application::Run()`
(`Application.cpp`) already calls `Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(...)`
twice (once for `GameView`, once for `SceneView`) plus a third,
conditionally-guarded call for `Present` (guarded on
`presentStats.has_value()`, since `Present()` can genuinely record
nothing this frame — see that code's own comment). **Every one of these
calls is unconditional with respect to `GTE_ENABLE_PROFILER`/
`GTE_ENABLE_EDITOR`** — deliberately, per that code's own comment. Phase
5's own new call must follow this exact same "unconditional" convention —
see Step 3.1.

**`Application.cpp` already `#include`s everything this phase's new code
needs, transitively, with zero new `#include` lines required in
`Application.cpp` itself** — `Application.h` includes `"../Renderer/Renderer.h"`
(which itself includes `"Memory/GpuMemoryTracker.h"`, bringing
`GpuMemoryTracker::Totals` into scope), and `Application.cpp` already
includes `"../Profiling/FrameProfiler.h"` (which itself includes
`"ProfilingTypes.h"`, bringing `MemorySnapshot`/`GpuSampleStatus` into
scope). `Application.cpp` will need exactly one new `#include` for the new
`MemorySnapshotBuilder.h` header itself (see Step 3.2/3.3) — everything
ELSE it needs is already transitively visible.

**`Application.cpp`/`.h` and `Profiling/FrameProfilerTests.cpp` are
already unconditional entries in their respective CMake lists** —
`src/Application/Application.cpp`/`.h` sit unconditionally inside
`gte_core`'s `add_library()` call (root `CMakeLists.txt`), and
`Profiling/FrameProfilerTests.cpp` sits unconditionally inside
`tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list ("Always built (this
module has no `GTE_ENABLE_EDITOR` dependency at all — see AGENTS.md,
'Profiling')"). The two brand-new files this version of the plan adds
(`src/Application/MemorySnapshotBuilder.h`,
`tests/Application/MemorySnapshotBuilderTests.cpp`) need one new line each
in those same two lists — see Step 3.2/3.4.

### 2.2 What is genuinely missing today (the actual, narrow gap)

- **Nothing calls `Renderer::GetMemoryTotals()` once per frame and feeds
  it to `FrameProfiler::SetMemorySnapshot()`.** Confirmed directly: a
  repository-wide look at every call site of `SetMemorySnapshot(` shows it
  is called ONLY from `tests/Profiling/FrameProfilerTests.cpp`, never from
  `Application.cpp` or anywhere else in `src/`. This is the one real,
  substantive production gap this whole phase closes.
- **There is no small, named, reviewable, TESTED place where a
  `GpuMemoryTracker::Totals` value is converted into a
  `Profiling::MemorySnapshot` value.** This is where this version departs
  from v1's conclusion. v1 correctly identified that this conversion is
  "trivial (a field-for-field copy plus one hardcoded `status = Present`)"
  and is "exactly the kind of code that is easy to get subtly wrong by
  transposing two fields" — but then concluded no dedicated test was
  needed, reasoning that the six `FrameProfiler`-level tests would catch
  such a bug "by symmetry." They would not: every one of those six tests
  constructs its own `MemorySnapshot` by hand and passes it straight to
  `FrameProfiler::SetMemorySnapshot()` — none of them ever invoke the
  conversion function itself. A field transposition inside the conversion
  function (e.g. writing `totals.textureBytes` into
  `snapshot.bufferBytes`) would be invisible to all six of those tests,
  would compile cleanly, and would silently ship incorrect data into every
  real frame's memory history — precisely the failure mode this document
  otherwise goes out of its way to guard against (see Step 2.4's own
  "never use 0 bytes to mean no data" discipline; a transposed-but-still-
  Present-and-nonzero value is arguably an even quieter failure, since
  nothing about it LOOKS wrong at a glance). Per `AGENTS.md`'s own
  "Testability & Regression Safety" section — "design new logic to be
  Tier-1-testable whenever the underlying problem allows it," and "every
  change to Tier 1 code must come with a matching test change" — this
  function should be pulled out of `Application.cpp`'s anonymous namespace
  into its own small, directly-testable, Tier-1 header. See Step 3.2 for
  exactly where it goes and Step 3.4 for its own test.
- **`tests/Profiling/FrameProfilerTests.cpp`'s existing memory coverage is
  real but shallow, and has one outright gap, same finding as v1** — see
  2.3 immediately below.

### 2.3 The other real gap: `SetMemorySnapshot()` has no "outside the frame bracket" test, unlike its two siblings

Re-reading `tests/Profiling/FrameProfilerTests.cpp` line by line (re-
confirmed again this pass, not just trusted from the prior read) surfaces
a genuine, concrete asymmetry:

- `SetGpuPassTiming()` has its own dedicated no-op test:
  `SetGpuPassTimingOutsideFrameBracketIsNoOp`.
- `SetGpuPassDrawStats()` has its own dedicated no-op test:
  `SetGpuPassDrawStatsOutsideFrameBracketIsNoOp`.
- `SetMemorySnapshot()` has **no such test at all.** Its only coverage
  today is indirect, inside `SetGpuPassTimingAndDrawStatsAndMemorySnapshotAreRecorded`,
  which only ever calls it correctly, INSIDE a `BeginFrame()`/`EndFrame()`
  bracket, and only ever checks `status` and `totalBytes` — not the other
  six fields.

This is exactly your own explicit requirement #6's second bullet,
verbatim: "Calling `SetMemorySnapshot()` outside `BeginFrame()` /
`EndFrame()` does nothing." The production code already does this
correctly (see 2.1's exact quote of the method body) — but there is, right
now, today, zero automated proof of it for THIS specific setter, unlike
its two siblings. Closed by this phase's own new test suite — see Step
3.4, test 2.

### 2.4 Constraints discovered while reading the code (must be respected)

- **`Renderer::GetMemoryTotals()` returns `GpuMemoryTracker::Totals` BY
  VALUE** — there is no lifetime/reference concern to manage; the returned
  struct is a small, cheap-to-copy POD with no Vulkan handles inside it,
  safe to hold in a local variable for exactly as long as this phase's new
  code needs it.
- **`VkDeviceSize` is always a 64-bit unsigned integer per the Vulkan
  spec** (`typedef uint64_t VkDeviceSize;`), and `std::size_t` on this
  target is also 64-bit — so every field of `GpuMemoryTracker::Totals`
  converts to `std::uint64_t` (the type every field of `MemorySnapshot`
  already uses) via a plain, lossless `static_cast`, never a narrowing
  conversion. Write the casts out explicitly anyway (not an implicit
  conversion) — documents intent and silences a legitimate compiler
  warning on a platform where the two types might one day differ in
  width, matching this codebase's existing convention elsewhere
  (`static_cast<std::size_t>(pass)` in `FrameProfiler.cpp`, etc.).
- **This engine's GPU memory total is available EVERY frame,
  unconditionally — unlike a `GpuPass`'s draw-call/triangle count, which
  is only available for a pass that actually ran this frame.**
  `Renderer::GetMemoryTotals()` has no "didn't run" concept at all — it is
  always a valid, meaningful O(1) read as long as a live `Renderer`
  exists, which it does for the entire lifetime of `Application::Run()`'s
  loop. Phase 5's own production call, unlike Phase 3's three draw-stats
  calls, needs **no `if (target != nullptr)`-style guard, and no
  `std::optional` short-circuit** — it should be called unconditionally,
  once, every single frame, with `status` always set to `Present`. The
  `Absent` branch of `MemorySnapshot::status` is therefore, correctly,
  never produced by real production code once this phase lands — it
  remains meaningful only for (a) a frame recorded before
  `SetMemorySnapshot()` is ever called at all (impossible once this
  phase's call site is unconditional and always reached), and (b) tests
  that deliberately never call it. Worth stating explicitly so a future
  reader doesn't go looking for a production code path that produces
  `Absent` — there isn't meant to be one, by design.
- **`Application::Run()`'s loop has no early `continue`/`break` between
  its top (`BeginFrame()`) and its bottom (`EndFrame()`)** — re-confirmed
  by reading the whole function body again this pass. Every iteration
  reaches the bottom of the loop and calls `EndFrame()` exactly once. A
  single call placed anywhere between `BeginFrame()` and `EndFrame()` is
  guaranteed to run exactly once per frame.
- **GPU resources can legitimately be created or destroyed mid-frame**
  (e.g. the Editor's Inspector loading a newly-selected asset's preview
  texture, or the Project panel's drag-and-drop import creating a new
  `*.gta` texture asset) — so the exact instant within the frame this
  phase's new call is placed can, in principle, observe a slightly
  different total than one line earlier or later. This is not a bug to
  route around; it is the same "instantaneous snapshot, not a
  frame-spanning aggregate" nature `GpuMemoryTracker` has always had. Step
  3.1 picks one single, deliberate, documented point in the frame (the
  very end, right before `EndFrame()`) so the snapshot reflects the most
  complete picture of what that frame actually did.
- **`FrameProfiler::SetMemorySnapshot()` takes the WHOLE struct as one
  argument, not individual scalar parameters** — already decided back in
  Phase 0 and not something this phase gets to revisit. This phase's job
  is only to CONSTRUCT one correctly and pass it — never to add an
  alternate, scalar-parameter overload.
- **The conversion function this phase introduces must NOT live inside
  `src/Profiling/`.** `ProfilingTypes.h`'s own doc comment is explicit
  that this whole module is deliberately Vulkan-free, and
  `GpuMemoryTracker::Totals` is declared inside a header
  (`GpuMemoryTracker.h`) that transitively includes real Vulkan headers
  (via `VulkanAllocator.h`). It also should not live inside
  `GpuMemoryTracker.h`/`.cpp` (that module has no reason to know the
  Profiling module exists, same reasoning). See Step 3.2 for exactly
  where it DOES go, and why that's a small, well-justified NEW file rather
  than either of those two existing locations or an untested inline
  helper (the choice v1 made and this version corrects — see "Changes
  from v1").

--------------------------------------------------------------------------
## Step 3: The Plan (How will we get there?)
--------------------------------------------------------------------------

### Step 3.1 — Decide, precisely, WHERE in the frame the snapshot is taken

Placed as the very last profiling-related action before
`Profiling::FrameProfiler::Instance().EndFrame();` at the bottom of
`Application::Run()`'s loop body — i.e. immediately after the existing
`m_editorLayer->RenderPlatformWindows();` line and immediately before the
existing `Profiling::FrameProfiler::Instance().EndFrame();` line. This is
a deliberate choice, not an arbitrary one:

- It is the LATEST possible point still inside the `BeginFrame()`/
  `EndFrame()` bracket, so it captures every GPU resource creation/
  destruction that happened anywhere else this frame.
- It deliberately mirrors where the `Present`-pass draw-stats reporting
  already happens (right after `Present()` returns, still before
  `EndFrame()`) — this phase's new call sits naturally right alongside
  that existing block.
- It is unconditional — no `if` guard of any kind, matching the reasoning
  in Step 2.4 and matching this same function's own existing,
  already-verified `BeginFrame()`/`EndFrame()`/`SetGpuPassDrawStats()`
  calls, none of which are `#if GTE_ENABLE_PROFILER`/`GTE_ENABLE_EDITOR`
  gated either.

### Step 3.2 — Add `BuildMemorySnapshot()` as its own small, Tier-1-tested header (the corrected approach)

**New file: `src/Application/MemorySnapshotBuilder.h`** (header-only — the
function is small enough that a matching `.cpp` buys nothing; this mirrors
how e.g. `src/ECS/Components/Name.h` and other trivial single-purpose
headers in this codebase stay header-only when there's no meaningful body
to hide):

```cpp
#pragma once

#include "../Profiling/ProfilingTypes.h"
#include "../Renderer/Memory/GpuMemoryTracker.h"

#include <cstdint>

namespace gte {

// Reshapes Renderer::GetMemoryTotals()'s result (GpuMemoryTracker::Totals,
// a Vulkan-tied VkDeviceSize/std::size_t-based type) into a
// Profiling::MemorySnapshot (a plain, Vulkan-free std::uint64_t-based
// type) for FrameProfiler::SetMemorySnapshot() - see PHASE5_GPU_MEMORY_
// HISTORY_STRATEGY_v2.md. Deliberately lives HERE, not inside
// src/Profiling/ (which must stay Vulkan-free - see ProfilingTypes.h's
// own doc comment on MemorySnapshot) and not inside GpuMemoryTracker.h/.cpp
// (which has no reason to know the Profiling module exists) - Application
// is already the one place in this engine that knows about BOTH Renderer's
// data shapes and Profiling's API.
//
// Deliberately its OWN small header (not an anonymous-namespace helper
// inlined into Application.cpp) specifically so it can be called directly
// from a unit test - see tests/Application/MemorySnapshotBuilderTests.cpp.
// The mapping below is arithmetic-free and branch-free, genuinely
// "trivial" - but trivial field-mapping code (eight fields, easy to
// silently transpose two of, e.g. bufferBytes/textureBytes) is exactly
// the class of bug that is easy to introduce and easy to miss in review
// when it's buried anonymously among unrelated per-frame loop logic, and
// a bug here would be invisible to every OTHER test this phase adds
// (those all hand-construct a MemorySnapshot directly and never call this
// function - see PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md, "Changes from
// v1"). Needs nothing but a plain GpuMemoryTracker::Totals value (no live
// VkDevice/VmaAllocator/Renderer) to test directly, same Tier-1 bar this
// engine already applies to e.g. Renderer/DrawStats.h.
//
// Always reports GpuSampleStatus::Present: unlike a GpuPass's draw-call/
// triangle count (which can genuinely be "this pass didn't run this
// frame"), Renderer::GetMemoryTotals() has no such concept - it is always
// a valid, meaningful O(1) read for as long as a live Renderer exists.
inline Profiling::MemorySnapshot BuildMemorySnapshot(const GpuMemoryTracker::Totals& totals) noexcept
{
    Profiling::MemorySnapshot snapshot;
    snapshot.status = Profiling::GpuSampleStatus::Present;
    snapshot.totalBytes = static_cast<std::uint64_t>(totals.totalBytes);
    snapshot.bufferBytes = static_cast<std::uint64_t>(totals.bufferBytes);
    snapshot.textureBytes = static_cast<std::uint64_t>(totals.textureBytes);
    snapshot.gpuOnlyBytes = static_cast<std::uint64_t>(totals.gpuOnlyBytes);
    snapshot.cpuOnlyBytes = static_cast<std::uint64_t>(totals.cpuOnlyBytes);
    snapshot.sharedBytes = static_cast<std::uint64_t>(totals.sharedBytes);
    snapshot.bufferCount = static_cast<std::uint64_t>(totals.bufferCount);
    snapshot.textureCount = static_cast<std::uint64_t>(totals.textureCount);
    return snapshot;
}

} // namespace gte
```

Add exactly one line to root `CMakeLists.txt`'s unconditional `gte_core`
source list, alongside the other `src/Application/*.h` entries:
`src/Application/MemorySnapshotBuilder.h`.

**Why a named, standalone, tested function, not eight inline field
assignments at the call site, and not an anonymous-namespace helper
either (the correction vs. v1)**: the conversion itself is
arithmetic-free and branch-free — genuinely "trivial" — but trivial
field-mapping code is exactly the class of bug that is easy to introduce
silently and easy to overlook in review. Pulling it into one small, named,
single-purpose, INDEPENDENTLY CALLABLE function (as opposed to v1's
anonymous-namespace version, which could only ever be exercised
indirectly through `Application::Run()` itself — untestable Tier 2 code)
makes the entire field mapping visible in one place AND lets a test call
it directly with a hand-built `GpuMemoryTracker::Totals` carrying eight
distinct values, asserting each one lands in the matching
`MemorySnapshot` field. See Step 3.4 for that test.

**Why this still isn't given a full `.h`/`.cpp` pair with heavier
machinery, unlike `DrawStats.h`**: `DrawStats.h`'s `AccumulateDrawStats()`
contains real logic (a division, a running accumulation across a loop,
two documented assumptions about topology/instancing) that benefits from
a `.cpp` translation unit. `BuildMemorySnapshot()` contains no logic
beyond a straight-through field copy plus one hardcoded constant — small
enough to stay `inline` in a single header with no `.cpp` needed, while
still getting the one thing that actually matters here: a real,
independent unit test.

### Step 3.3 — Wire the one call

Inside `Application::Run()`, immediately before the existing
`Profiling::FrameProfiler::Instance().EndFrame();` line, and add
`#include "MemorySnapshotBuilder.h"` to `Application.cpp`'s existing
`#include` block:

```cpp
        // Update/present any panel the user has dragged outside the main OS
        // window (Dear ImGui multi-viewport/"platform windows" - a no-op in
        // a release build, see NullEditorLayer::RenderPlatformWindows()).
        m_editorLayer->RenderPlatformWindows();

        // Phase 5 (GPU memory usage over time) - see PHASE5_GPU_MEMORY_
        // HISTORY_STRATEGY_v2.md: one real GPU memory snapshot per
        // profiler frame, taken as late as possible in the frame (still
        // inside this BeginFrame()/EndFrame() bracket) so it reflects
        // every resource created/destroyed anywhere this frame, including
        // by IEditorLayer::BuildUI()'s own Inspector/Project-panel asset
        // loading above. Unconditional - not #if GTE_ENABLE_PROFILER/
        // GTE_ENABLE_EDITOR-gated, matching this same function's own
        // BeginFrame()/EndFrame()/SetGpuPassDrawStats() calls, none of
        // which are gated either (only GTE_PROFILE_SCOPE(...)'s own macro
        // body is compile-time-gated - see AGENTS.md, "Profiling").
        // Renderer::GetMemoryTotals() is O(1) and always meaningful (no
        // "didn't run this frame" concept, unlike a GpuPass's draw-call
        // count), so this is always GpuSampleStatus::Present.
        Profiling::FrameProfiler::Instance().SetMemorySnapshot(BuildMemorySnapshot(m_renderer.GetMemoryTotals()));

        Profiling::FrameProfiler::Instance().EndFrame();
```

That is the entire production change (this call + the new header from
Step 3.2). No other file needs a single line touched for the engine to
actually start recording real, per-frame GPU memory history.

### Step 3.4 — Add seven new Tier-1 tests

**One new test file: `tests/Application/MemorySnapshotBuilderTests.cpp`**
(new — this is the direct fix for the gap in Step 2.2/"Changes from v1").
Add exactly one line to `tests/CMakeLists.txt`'s unconditional
`GTE_TEST_SOURCES` list: `Application/MemorySnapshotBuilderTests.cpp`.

```cpp
#include "Application/MemorySnapshotBuilder.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// Every field is given a DISTINCT value so a transposed-field bug (e.g.
// bufferBytes/textureBytes swapped) is guaranteed to be caught - this is
// the ONE test in this phase that actually calls BuildMemorySnapshot()
// itself, rather than hand-constructing a MemorySnapshot directly (see
// tests/Profiling/FrameProfilerTests.cpp's own new tests, which
// deliberately do NOT exercise this function - see
// PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md, "Changes from v1").
TEST(MemorySnapshotBuilderTest, MapsEveryFieldToItsMatchingSnapshotField)
{
    GpuMemoryTracker::Totals totals;
    totals.totalBytes = 1000;
    totals.bufferBytes = 200;
    totals.textureBytes = 800;
    totals.gpuOnlyBytes = 600;
    totals.cpuOnlyBytes = 150;
    totals.sharedBytes = 250;
    totals.bufferCount = 3;
    totals.textureCount = 5;

    const Profiling::MemorySnapshot snapshot = BuildMemorySnapshot(totals);

    EXPECT_EQ(snapshot.status, Profiling::GpuSampleStatus::Present);
    EXPECT_EQ(snapshot.totalBytes, 1000u);
    EXPECT_EQ(snapshot.bufferBytes, 200u);
    EXPECT_EQ(snapshot.textureBytes, 800u);
    EXPECT_EQ(snapshot.gpuOnlyBytes, 600u);
    EXPECT_EQ(snapshot.cpuOnlyBytes, 150u);
    EXPECT_EQ(snapshot.sharedBytes, 250u);
    EXPECT_EQ(snapshot.bufferCount, 3u);
    EXPECT_EQ(snapshot.textureCount, 5u);
}

// A Totals value that is genuinely all-zero (e.g. a Renderer with no live
// Buffer/RenderTexture yet) must still map to status == Present, never
// Absent - the function's whole point is that it ALWAYS reports a real
// measurement; "no data" is a FrameProfiler-level concept (never calling
// SetMemorySnapshot() at all), never something this function decides.
TEST(MemorySnapshotBuilderTest, AllZeroTotalsStillReportsPresent)
{
    const GpuMemoryTracker::Totals totals; // Default-constructed, all zero.
    const Profiling::MemorySnapshot snapshot = BuildMemorySnapshot(totals);

    EXPECT_EQ(snapshot.status, Profiling::GpuSampleStatus::Present);
    EXPECT_EQ(snapshot.totalBytes, 0u);
    EXPECT_EQ(snapshot.bufferCount, 0u);
    EXPECT_EQ(snapshot.textureCount, 0u);
}

} // namespace
} // namespace gte
```

**Six tests added to the already-existing
`tests/Profiling/FrameProfilerTests.cpp`** (no new fixture — the existing
`FrameProfilerTest` fixture already resets `FrameProfiler::Instance()` via
`ResetForTesting()`/`SetCaptureEnabled(true)` before AND after every test),
placed directly after the existing
`SetGpuPassDrawStatsOutsideFrameBracketIsNoOp` test. Each one maps
directly, 1:1, onto one of your six numbered "Must have" test
requirements:

**1. "A snapshot is saved correctly in a completed frame" →
`SetMemorySnapshotRecordsEveryFieldExactly`**

```cpp
TEST_F(FrameProfilerTest, SetMemorySnapshotRecordsEveryFieldExactly)
{
    // Unlike SetGpuPassTimingAndDrawStatsAndMemorySnapshotAreRecorded
    // above (which only checks status/totalBytes as part of a broader,
    // multi-setter test), this is a dedicated, exhaustive check of every
    // single MemorySnapshot field at the FrameProfiler storage level. Note
    // this deliberately does NOT go through BuildMemorySnapshot() - that
    // function has its own separate test,
    // tests/Application/MemorySnapshotBuilderTests.cpp - this test is
    // purely about FrameProfiler's own storage/retrieval correctness.
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();

    MemorySnapshot memory;
    memory.status = GpuSampleStatus::Present;
    memory.totalBytes = 1000;
    memory.bufferBytes = 200;
    memory.textureBytes = 800;
    memory.gpuOnlyBytes = 600;
    memory.cpuOnlyBytes = 150;
    memory.sharedBytes = 250;
    memory.bufferCount = 3;
    memory.textureCount = 5;
    profiler.SetMemorySnapshot(memory);
    profiler.EndFrame();

    const MemorySnapshot& recorded = profiler.HistoryAt(0).memory;
    EXPECT_EQ(recorded.status, GpuSampleStatus::Present);
    EXPECT_EQ(recorded.totalBytes, 1000u);
    EXPECT_EQ(recorded.bufferBytes, 200u);
    EXPECT_EQ(recorded.textureBytes, 800u);
    EXPECT_EQ(recorded.gpuOnlyBytes, 600u);
    EXPECT_EQ(recorded.cpuOnlyBytes, 150u);
    EXPECT_EQ(recorded.sharedBytes, 250u);
    EXPECT_EQ(recorded.bufferCount, 3u);
    EXPECT_EQ(recorded.textureCount, 5u);
}
```

**2. "Calling `SetMemorySnapshot()` outside `BeginFrame()`/`EndFrame()`
does nothing" → `SetMemorySnapshotOutsideFrameBracketIsNoOp`** (closes the
gap identified in Step 2.3, mirroring its two existing GPU-pass siblings
exactly):

```cpp
TEST_F(FrameProfilerTest, SetMemorySnapshotOutsideFrameBracketIsNoOp)
{
    FrameProfiler& profiler = FrameProfiler::Instance();

    MemorySnapshot stray;
    stray.status = GpuSampleStatus::Present;
    stray.totalBytes = 999;
    profiler.SetMemorySnapshot(stray); // No BeginFrame() yet this test.

    profiler.BeginFrame();
    profiler.EndFrame();

    const MemorySnapshot& recorded = profiler.HistoryAt(0).memory;
    EXPECT_EQ(recorded.status, GpuSampleStatus::Absent);
    EXPECT_EQ(recorded.totalBytes, 0u);
}
```

**3. "Memory data stays correct across multiple frames" →
`MemorySnapshotStaysCorrectAcrossMultipleFrames`**

```cpp
TEST_F(FrameProfilerTest, MemorySnapshotStaysCorrectAcrossMultipleFrames)
{
    FrameProfiler& profiler = FrameProfiler::Instance();

    MemorySnapshot first;
    first.status = GpuSampleStatus::Present;
    first.totalBytes = 100;
    first.bufferCount = 1;
    profiler.BeginFrame();
    profiler.SetMemorySnapshot(first);
    profiler.EndFrame();

    MemorySnapshot second;
    second.status = GpuSampleStatus::Present;
    second.totalBytes = 5000;
    second.bufferCount = 9;
    profiler.BeginFrame();
    profiler.SetMemorySnapshot(second);
    profiler.EndFrame();

    ASSERT_EQ(profiler.HistoryCount(), 2u);
    EXPECT_EQ(profiler.HistoryAt(0).memory.totalBytes, 100u);
    EXPECT_EQ(profiler.HistoryAt(0).memory.bufferCount, 1u);
    EXPECT_EQ(profiler.HistoryAt(1).memory.totalBytes, 5000u);
    EXPECT_EQ(profiler.HistoryAt(1).memory.bufferCount, 9u);
}
```

**4. "Memory data survives ring-buffer wraparound correctly" →
`MemorySnapshotSurvivesRingBufferWraparound`** (mirrors
`FrameProfilerTests.cpp`'s own existing `RingBufferWrapsAndKeepsMostRecentFrames`
pattern, asserting exact boundary values rather than merely "still
there"):

```cpp
TEST_F(FrameProfilerTest, MemorySnapshotSurvivesRingBufferWraparound)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const std::uint64_t totalFrames = static_cast<std::uint64_t>(kMaxFrameHistory) + 5;

    for (std::uint64_t i = 0; i < totalFrames; ++i) {
        profiler.BeginFrame();
        MemorySnapshot snapshot;
        snapshot.status = GpuSampleStatus::Present;
        snapshot.totalBytes = i * 10;
        profiler.SetMemorySnapshot(snapshot);
        profiler.EndFrame();
    }

    ASSERT_EQ(profiler.HistoryCount(), kMaxFrameHistory);
    const std::uint64_t oldestRetainedFrameIndex = totalFrames - kMaxFrameHistory;
    EXPECT_EQ(profiler.HistoryAt(0).memory.totalBytes, oldestRetainedFrameIndex * 10);
    EXPECT_EQ(profiler.HistoryAt(0).memory.status, GpuSampleStatus::Present);
    EXPECT_EQ(profiler.HistoryAt(kMaxFrameHistory - 1).memory.totalBytes, (totalFrames - 1) * 10);
    EXPECT_EQ(profiler.HistoryAt(kMaxFrameHistory - 1).memory.status, GpuSampleStatus::Present);
}
```

**5. "Absent memory data is different from real 0 bytes" →
`AbsentMemorySnapshotIsDistinctFromRealZeroBytes`** — the single most
important test in this whole phase, directly proving the "never use 0
bytes to mean 'no data'" rule your instructions call out by name:

```cpp
TEST_F(FrameProfilerTest, AbsentMemorySnapshotIsDistinctFromRealZeroBytes)
{
    FrameProfiler& profiler = FrameProfiler::Instance();

    // Frame 0: SetMemorySnapshot() is never called at all - this is what
    // "no snapshot captured" looks like.
    profiler.BeginFrame();
    profiler.EndFrame();

    // Frame 1: SetMemorySnapshot() IS called, reporting a GENUINELY EMPTY
    // GPU memory total (status = Present, every byte/count field
    // legitimately 0). This is a real, valid, meaningful measurement, not
    // a missing one - see MemorySnapshotBuilderTests.cpp's own
    // AllZeroTotalsStillReportsPresent for the production-code-level
    // equivalent of this same case.
    MemorySnapshot genuinelyEmpty;
    genuinelyEmpty.status = GpuSampleStatus::Present;
    profiler.BeginFrame();
    profiler.SetMemorySnapshot(genuinelyEmpty);
    profiler.EndFrame();

    ASSERT_EQ(profiler.HistoryCount(), 2u);

    const MemorySnapshot& absentFrame = profiler.HistoryAt(0).memory;
    const MemorySnapshot& presentZeroFrame = profiler.HistoryAt(1).memory;

    // Both frames report totalBytes == 0 numerically...
    EXPECT_EQ(absentFrame.totalBytes, 0u);
    EXPECT_EQ(presentZeroFrame.totalBytes, 0u);

    // ...but their STATUS is what actually tells them apart - this is the
    // entire point of the tri-state, and the entire point of this test.
    EXPECT_EQ(absentFrame.status, GpuSampleStatus::Absent);
    EXPECT_EQ(presentZeroFrame.status, GpuSampleStatus::Present);
    EXPECT_NE(absentFrame.status, presentZeroFrame.status);
}
```

**6. "Setting memory data does not change CPU scopes, GPU timing, or
draw statistics" → `SetMemorySnapshotDoesNotAffectCpuScopesGpuTimingOrDrawStats`**
(a direct isolation/regression test, in the same spirit as the existing
`SetGpuPassTimingNeverTouchesCountStatusOrViceVersa`):

```cpp
TEST_F(FrameProfilerTest, SetMemorySnapshotDoesNotAffectCpuScopesGpuTimingOrDrawStats)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();

    profiler.RecordCpuScope("SomeSystem::Update", 4.0);
    profiler.SetGpuPassTiming(GpuPass::GameView, GpuSampleStatus::Present, 2.5);
    profiler.SetGpuPassDrawStats(GpuPass::GameView, GpuSampleStatus::Present, 7, 150);

    MemorySnapshot memory;
    memory.status = GpuSampleStatus::Present;
    memory.totalBytes = 4096;
    profiler.SetMemorySnapshot(memory);

    profiler.EndFrame();

    const FrameSample& frame = profiler.HistoryAt(0);

    EXPECT_EQ(frame.memory.status, GpuSampleStatus::Present);
    EXPECT_EQ(frame.memory.totalBytes, 4096u);

    ASSERT_EQ(frame.cpuScopeCount, 1u);
    EXPECT_EQ(std::string(frame.cpuScopes[0].name), "SomeSystem::Update");
    EXPECT_DOUBLE_EQ(frame.cpuScopes[0].totalMilliseconds, 4.0);

    const GpuPassSample& gameView = frame.gpuPasses[static_cast<std::size_t>(GpuPass::GameView)];
    EXPECT_EQ(gameView.timingStatus, GpuSampleStatus::Present);
    EXPECT_DOUBLE_EQ(gameView.milliseconds, 2.5);
    EXPECT_EQ(gameView.countStatus, GpuSampleStatus::Present);
    EXPECT_EQ(gameView.drawCallCount, 7u);
    EXPECT_EQ(gameView.triangleCount, 150u);
}
```

No new `#include` is needed in `FrameProfilerTests.cpp` for any of the
above — `MemorySnapshot`/`GpuSampleStatus`/`GpuPass`/`GpuPassSample`/
`FrameSample` are all already visible via the file's existing
`#include "Profiling/FrameProfiler.h"`.

### Step 3.5 — Documentation updates

- **`AGENTS.md`'s "Profiling" section** needs TWO edits, not one (v1 only
  called for the first):
  1. A new bullet, placed alongside the existing bullet describing
     `DrawStats.h`/the `timingStatus`/`countStatus` split, stating that
     `Application::Run()`'s own call to `BuildMemorySnapshot()`
     (`src/Application/MemorySnapshotBuilder.h` — a small, Tier-1-tested,
     standalone header, deliberately NOT an anonymous-namespace helper
     inlined into `Application.cpp`, and why — see Step 3.2's own
     reasoning) is the ONE production place that ever calls
     `FrameProfiler::SetMemorySnapshot()` with a real value, reading it
     from `Renderer::GetMemoryTotals()` (the existing, single
     `GpuMemoryTracker` — never a second tracker), unconditionally, once
     per frame, as late as possible before `EndFrame()`.
  2. **A correction to the existing sentence** (currently reading: "GPU
     TIMING (Phase 4) and the memory snapshot (Phase 5) remain the only
     producers still unwired, set only synthetically by tests via
     `FrameProfiler::SetGpuPassTiming()`/`SetMemorySnapshot()`.") — this
     must be edited to say GPU timing (Phase 4) ALONE remains unwired,
     since the memory snapshot becomes real, wired, production data the
     moment this phase lands. v1 missed this specific sentence; leaving
     it as-is would make `AGENTS.md` actively wrong about this phase's own
     result the instant it ships — see "Changes from v1" at the top of
     this document.
- **`PROFILER_IMPLEMENTATION_STATUS_v4.md` → `PROFILER_IMPLEMENTATION_STATUS_v5.md`**:
  following this codebase's own established "bump the version, delete the
  superseded file" convention, move Phase 5 from "What was NOT
  implemented" into "What was implemented this session," with the same
  level of concrete detail (exact file/function names, exact new test
  count — seven, not six, once `MemorySnapshotBuilderTests.cpp` is
  counted) the existing Phase 2/3 entries already have, and record a
  freshly re-counted `ctest` total (see Step 5.1 — do not copy "475"
  forward without re-running it).
- **`TESTING.md`** needs two changes, not the one v1 claimed:
  1. Its EXISTING `Profiling/FrameProfilerTests.cpp` bullet is actually
     already accurate (re-verified directly against the real file this
     pass — it already reads `SetGpuPassTiming()`/`SetGpuPassDrawStats()`/
     `SetMemorySnapshot()`, not a stale `SetGpuPassSample()`; v1's claim
     to the contrary was a factual error in v1 itself, not a real gap in
     the codebase). That bullet should still gain a clause noting the new,
     thorough `SetMemorySnapshot()` coverage this phase adds (all-fields-
     exact round-trip, outside-bracket no-op, multi-frame/wraparound
     correctness, absent-vs-real-zero, and cross-category isolation), but
     do NOT "fix" a staleness that isn't there.
  2. A genuinely NEW bullet is needed for the new
     `Application/MemorySnapshotBuilderTests.cpp` file (there was no
     `tests/Application/` bullet at all before this phase, since
     `Application.cpp` itself has never had a Tier-1-testable piece pulled
     out of it until now) — describing `BuildMemorySnapshot()`'s exact
     field-mapping coverage, and noting it needs nothing but a plain
     `GpuMemoryTracker::Totals` value (no live `VkDevice`/`Renderer`).
- **This document itself** should gain a short "Result" addendum once
  implemented, mirroring every prior phase strategy document's own
  closing convention (see `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`'s own
  "Result (filled in after implementation)" section for the exact shape
  to copy).

### Step 3.6 — Build and verify

- `cmake --build build --config Debug` — must succeed cleanly, including
  the two newly-added source files (`MemorySnapshotBuilder.h` in
  `gte_core`, `MemorySnapshotBuilderTests.cpp` in
  `GreatTamanaEngineTests`).
- `ctest --test-dir build -C Debug --output-on-failure` — must report the
  full suite passing: the pre-existing baseline (last independently
  recounted at **475** as of `PROFILER_IMPLEMENTATION_STATUS_v4.md` —
  **re-confirm this exact number with a fresh run immediately before
  implementation and again immediately before writing it into any
  commit/status-doc update**), plus this phase's **7** new tests (6 in
  `FrameProfilerTests.cpp` + 2 in the new
  `MemorySnapshotBuilderTests.cpp` — note this file has two tests, not
  one, so "7 new tests" really means 6 + 2 = 8 new test CASES across 2
  new/modified files; double-check the exact final count against what was
  actually written before quoting it anywhere), with zero regressions.
- As an optional, additional sanity check (not a substitute for the
  automated tests above, and removed before this phase is considered
  done): a short-lived, throwaway diagnostic reading
  `Profiling::FrameProfiler::Instance().LastCompletedFrame().memory` for a
  few real frames — spawn a batch of primitive entities via "Hierarchy" →
  "Create 3D Object" and confirm `totalBytes`/`bufferCount`/`textureCount`
  visibly climb.

--------------------------------------------------------------------------
## Step 4: What We Will NOT Do (Focus)
--------------------------------------------------------------------------

- **No second GPU memory tracker, of any kind, anywhere.** This phase
  reads exclusively from the one, already-existing, already-proven
  `GpuMemoryTracker` via `Renderer::GetMemoryTotals()`.
- **No Editor "Profiler" panel, and no ImGui code of any kind, in this
  phase.** Displaying this history graphically is Phase 7's job.
- **No `Profiling::FrameGraphData.h` extension** (e.g. a hypothetical
  `ComputeMemoryBytesRange()` sibling). Real, useful, cheap follow-on work
  once a consumer actually needs it — not requested by your "Must have"
  list, and adding it now would be scope creep ahead of an actual stated
  need.
- **No change to `FrameProfiler.h`/`.cpp`, `ProfilingTypes.h`, or
  `GpuMemoryTracker.h`/`.cpp` themselves.** Every type/method this phase
  needs already exists, is already correct, and is already partially
  tested.
- **Only two small, well-justified new files, and only two matching
  one-line `CMakeLists.txt` additions** — this is a deliberate, narrow
  correction to v1's "zero new files, zero new CMake wiring" framing, not
  an invitation to add more. `src/Application/MemorySnapshotBuilder.h`
  (production) and `tests/Application/MemorySnapshotBuilderTests.cpp`
  (its test) exist purely because the alternative — an untested,
  anonymous-namespace helper inlined into `Application.cpp` — leaves the
  one genuinely new piece of logic in this whole phase with zero direct
  test coverage (see "Changes from v1"/Step 2.2). Nothing else about this
  phase gets a new file: the one new production call site stays inline in
  `Application::Run()`, and the six `FrameProfiler`-level tests stay
  inside the already-existing `FrameProfilerTests.cpp`.
- **No GPU timestamp queries, and no dependency on Phase 4 whatsoever.**
- **No attempt to make the per-frame snapshot instant "smarter"** (e.g.
  averaging across several points within the frame, or capturing it twice
  and diffing). One O(1) read, once, as late as reasonably possible in
  the frame, is exactly what an "instantaneous snapshot" primitive like
  `GpuMemoryTracker` is for.
- **No change to any existing call site** (`RenderSystem`, `Game.cpp`,
  `AnimationSystem`, the Editor's own "Memory" panel/`MemoryPanelData.h`).

--------------------------------------------------------------------------
## Step 5: Their Role (What does this mean for you?)
--------------------------------------------------------------------------

### 5.1 How to start

1. Read this document fully, then re-read `PROFILER_STRATEGY_v2.md`'s own
   Phase 5 section one more time, side by side with this document's Step
   3.
2. Re-read `AGENTS.md`'s "Profiling" AND "Testability & Regression Safety"
   sections in full one more time immediately before writing code — this
   version's own central correction (Step 2.2/3.2) exists specifically
   because that second section's rule ("design new logic to be
   Tier-1-testable whenever the underlying problem allows it") was not
   fully honored in v1.
3. Run `ctest --test-dir build -C Debug --output-on-failure` once, BEFORE
   writing any code, to record the actual current passing-test count on
   your own machine/checkout — treat "475" as historical context only,
   never as a number to trust without a fresh check.
4. Before writing `BuildMemorySnapshot()`, actually open
   `src/Renderer/Memory/GpuMemoryTracker.h` one more time and re-confirm
   its `Totals` struct's exact field order/names against this document's
   Step 2.1 quote.
5. Implement in the order: frame-placement decision → the new
   `MemorySnapshotBuilder.h` header + its own direct test → the one
   production call site → the six `FrameProfiler`-level tests →
   documentation → build/verify.

### 5.2 Non-negotiable checklist for this phase (copy into the PR/commit description)

- [ ] `Renderer::GetMemoryTotals()` (the existing `GpuMemoryTracker`) is
      the ONLY source this phase's snapshot ever reads from — no second
      tracker, no duplicated bookkeeping, anywhere.
- [ ] `FrameProfiler::SetMemorySnapshot()` is called exactly once per
      completed frame, unconditionally, from exactly one production call
      site (`Application::Run()`), placed as late as possible in the
      frame while still inside the `BeginFrame()`/`EndFrame()` bracket.
- [ ] The recorded `MemorySnapshot` carries, at minimum, `totalBytes`,
      `bufferBytes`, `textureBytes`, `bufferCount`, `textureCount` — plus
      `gpuOnlyBytes`/`cpuOnlyBytes`/`sharedBytes` for free.
- [ ] `status` is set to `GpuSampleStatus::Present` by the ONE production
      call site every time it's reached (every frame) — never
      hardcoded/defaulted to a bare `0`/`Absent` anywhere a real value was
      actually available.
- [ ] A frame where `SetMemorySnapshot()` is never called still reads back
      `status == GpuSampleStatus::Absent` with all-default-zero fields —
      verified by the EXISTING `GpuPassAndMemorySamplesDefaultToAbsent`
      test remaining green, unchanged.
- [ ] **`BuildMemorySnapshot()` lives in its own new header
      (`src/Application/MemorySnapshotBuilder.h`) and has its own new,
      direct, standalone test file
      (`tests/Application/MemorySnapshotBuilderTests.cpp`) that actually
      CALLS it — not just a test that hand-builds a `MemorySnapshot` and
      never invokes the conversion function.** This is this version's own
      central correction versus v1 — verify it wasn't quietly reverted
      back to an untested anonymous-namespace helper during
      implementation.
- [ ] All six `FrameProfiler`-level tests from Step 3.4 exist, are named
      as specified (or equivalently clearly), and are green — in
      particular, `AbsentMemorySnapshotIsDistinctFromRealZeroBytes` is the
      one that most directly proves the "never use 0 bytes to mean no
      data" rule.
- [ ] `SetMemorySnapshotOutsideFrameBracketIsNoOp` exists, closing the
      exact pre-existing gap identified in Step 2.3 — verify by confirming
      this test did NOT exist before this phase's change.
- [ ] `SetMemorySnapshotDoesNotAffectCpuScopesGpuTimingOrDrawStats`
      populates CPU scope, GPU timing, AND GPU draw-stats data BEFORE
      calling `SetMemorySnapshot()`, and asserts every one of them is
      unchanged afterward.
- [ ] Zero changes to `src/Profiling/FrameProfiler.h`/`.cpp`,
      `src/Profiling/ProfilingTypes.h`, or
      `src/Renderer/Memory/GpuMemoryTracker.h`/`.cpp`.
- [ ] Exactly two new files total (`src/Application/MemorySnapshotBuilder.h`,
      `tests/Application/MemorySnapshotBuilderTests.cpp`) and exactly two
      new `CMakeLists.txt` lines (one per new file, into the two
      already-existing unconditional source lists) — no new CMake
      `option()`, no new conditional build path.
- [ ] Full clean build + full `ctest` run, both green, before calling this
      phase done — the exact passing-test count used in any commit
      message/status-doc update is taken from a FRESH run performed at
      that time, never copied from this planning document.
- [ ] `AGENTS.md`'s "Profiling" section (BOTH the new bullet AND the
      correction to the existing "memory snapshot... remains... unwired"
      sentence), `PROFILER_IMPLEMENTATION_STATUS_v4.md` (→ `_v5`), and
      `TESTING.md` (the new `Application/MemorySnapshotBuilderTests.cpp`
      bullet, plus a small addition to the existing, already-accurate
      `Profiling/FrameProfilerTests.cpp` bullet — NOT a "fix" of a
      staleness that doesn't actually exist there) are all updated in the
      same change.

### 5.3 What happens after this phase lands

Unchanged from v1's own assessment: **Phase 7 (the Editor "Profiler"
panel)** becomes the natural next crossing, now with Phase 2/3's data AND
this phase's own GPU memory history all ready in `FrameProfiler`'s ring
buffer. Phase 6 (benchmark mode) remains a pure consumer of whatever
Phases 0–5/7 have already built. Phase 4 (GPU timestamp queries) remains
the one deliberately-deferred, substantial piece.

### 5.4 A closing thought

The cheapest, safest crossing is the one where the bridge was already
built three sessions ago and all that's left is to actually walk across
it — but "cheap" is not an excuse to skip testing the one plank you
yourself just nailed down. Phase 0 deliberately reserved
`MemorySnapshot`'s exact shape and `FrameProfiler::SetMemorySnapshot()`'s
exact contract for this moment, specifically so this phase would be this
small. Respect that — implement the one call site, give its one new
helper function a real, direct test of its own (not just tests of the
storage layer around it), add the six `FrameProfiler`-level tests, verify
the whole suite, document it honestly (including correcting what's
already there, not just adding to it), and stop there. Phase 7's own
strategy document can be written once this one is real.

--------------------------------------------------------------------------
## Result (filled in after implementation)
--------------------------------------------------------------------------

*(Leave this section for whoever actually implements Phase 5 to fill in,
mirroring `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`'s own "Result" section —
record what was actually built, any deviation from this plan and why, the
final freshly-recounted test total, and confirmation of zero
regressions.)*

Implemented exactly as planned in this document, with no deviations:

- `src/Application/MemorySnapshotBuilder.h` (new, header-only) holds
  `BuildMemorySnapshot()`, added to root `CMakeLists.txt`'s unconditional
  `gte_core` source list.
- `Application::Run()` (`src/Application/Application.cpp`) gained one new
  `#include` and one new, unconditional call to
  `Profiling::FrameProfiler::Instance().SetMemorySnapshot(BuildMemorySnapshot(m_renderer.GetMemoryTotals()))`,
  placed right after `m_editorLayer->RenderPlatformWindows();` and right
  before the existing `EndFrame()` call, exactly as Step 3.1/3.3 specified.
- `tests/Application/MemorySnapshotBuilderTests.cpp` (new, 2 tests) and six
  new tests added to `tests/Profiling/FrameProfilerTests.cpp`, both exactly
  as specified in Step 3.4 (test bodies used verbatim). Both new/changed
  files added to `tests/CMakeLists.txt`'s unconditional `GTE_TEST_SOURCES`
  list.
- Zero changes to `src/Profiling/FrameProfiler.h/.cpp`,
  `src/Profiling/ProfilingTypes.h`, or
  `src/Renderer/Memory/GpuMemoryTracker.h/.cpp` — confirmed by inspection
  before and after implementation.
- `AGENTS.md`'s "Profiling" section: corrected the pre-existing "GPU TIMING
  (Phase 4) and the memory snapshot (Phase 5) remain the only producers
  still unwired" sentence (Phase 4 alone remains unwired now), and added a
  new bullet for `src/Application/MemorySnapshotBuilder.h` right after the
  existing `FrameGraphData.h/.cpp` bullet.
- `TESTING.md`: added a new bullet for
  `Application/MemorySnapshotBuilderTests.cpp`, and extended (not
  "fixed" — it was already accurate) the existing
  `Profiling/FrameProfilerTests.cpp` bullet with a clause describing the
  new `SetMemorySnapshot()` coverage.
- `PROFILER_IMPLEMENTATION_STATUS_v4.md` was replaced by
  `PROFILER_IMPLEMENTATION_STATUS_v5.md` (v4 deleted), following this
  codebase's own established "bump the version, delete the superseded
  file" convention (confirmed `v3` was likewise deleted when `v4` was
  created).
- **Build**: `cmake -S . -B build` then `cmake --build build --config Debug`
  succeeded cleanly with no new warnings.
- **Tests**: a fresh `ctest --test-dir build -C Debug --output-on-failure`
  run reports **482/483 passing, 1 skipped** (the same pre-existing,
  unrelated, machine-gated `PmxLoaderRealModelSmokeTest.
  LoadsAnMmdModelIfPresentOnThisMachine`, skipped for the same reason as
  every prior session). This "483 total" was freshly counted after
  implementation (475 from v4's own recorded baseline, +2
  `MemorySnapshotBuilderTests.cpp` + 6 new `FrameProfilerTests.cpp` cases =
  483) — zero regressions confirmed across the entire pre-existing suite.
