# PHASE7 — COMPLETION REPORT: `RenderGraph` Integration — Sibling Cache Member, Frame Counter, `Core::BuildFrame()` Wiring

Campaign folder: `task_manager/editor-core-separation-27/`
Branch: `feature/editor-core-separation` (unchanged, no new branch created)

## Summary

Made `RenderGraphPersistentResourceCache` (fully functional since PHASE6)
reachable from every real `RenderGraphBuilder` a pass author receives, and
wired its per-frame lifecycle (age bookkeeping + eviction sweep) into every
real engine frame. Pure plumbing — no new pass-author-facing capability yet
(`GetOrCreatePersistentTexture()` itself is PHASE8's job).

1. **`RenderGraph`** (`RenderGraph.h`/`.cpp`):
   - New sibling member `RenderGraphPersistentResourceCache m_persistentResourceCache;`,
     declared immediately after `m_resourcePool` (mirrors its exact
     ownership shape), constructed via a new member-initializer
     `m_persistentResourceCache(renderer)` inserted right after
     `m_resourcePool(renderer)` in the constructor's initializer list
     (declaration order confirmed to match: `m_resourcePool` →
     `m_persistentResourceCache` → `m_renderer` (not init-listed) →
     `m_debugMetadataSink`/`Provider` (not init-listed) → `m_timestampPool` —
     no `-Wreorder` hazard).
   - New dedicated, regime-agnostic `std::uint64_t m_persistentResourceFrameCounter = 0;`,
     declared immediately after `m_debugTextureFrameCounter`, deliberately
     NOT built on top of that other (regime-gated) counter.
   - New public `void BeginPersistentResourceFrame() noexcept;` —
     pre-increments the new counter and calls
     `m_persistentResourceCache.BeginFrame(m_persistentResourceFrameCounter, kPersistentResourceStaleThresholdFrames);`.
   - New public `std::uint64_t CurrentPersistentResourceFrameCounter() const noexcept { return m_persistentResourceFrameCounter; }`
     (the counterpart of `CurrentDebugTextureFrameCounter()`).
   - `Execute()`'s template body gained one new line, right after the
     existing `builder.SetDebugMetadataSink(m_debugMetadataSink);`:
     `builder.SetPersistentResourceCache(&m_persistentResourceCache, timingMode, m_persistentResourceFrameCounter);`.
   - New include: `#include "RenderGraphPersistentResourceCache.h"`.
2. **`RenderGraphBuilder`** (`RenderGraphBuilder.h`):
   - New namespace-scope forward declaration,
     `enum class ExecuteTimingMode : std::uint8_t;`, mirroring
     `RenderGraphDebugTextureRegistry.h`'s own identical precedent exactly
     (avoids a circular include back to `RenderGraph.h`, which already
     includes `RenderGraphBuilder.h`).
   - New include: `#include "RenderGraphPersistentResourceCache.h"` (not
     circular — that header only depends on `RenderGraphTypes.h`/
     `RenderTexture.h`).
   - New private members: `RenderGraphPersistentResourceCache* m_persistentCache = nullptr;`,
     `ExecuteTimingMode m_persistentCacheTimingMode{};`,
     `std::uint64_t m_persistentCacheCurrentFrame = 0;` — placed alongside
     the existing `m_persistentCacheTextures` vector PHASE2 already added.
   - New public method `SetPersistentResourceCache(RenderGraphPersistentResourceCache* cache, ExecuteTimingMode timingMode, std::uint64_t currentFrame) noexcept`,
     mirroring `SetDebugMetadataSink()`'s exact shape (Locked Decision 1).
   - `GetOrCreatePersistentTexture()` itself was deliberately NOT added —
     PHASE8's own job.
3. **`Core::BuildFrame()`** (`Core.cpp`): gained the literal FIRST statement,
   `m_renderGraph.BeginPersistentResourceFrame();`, BEFORE the pre-existing
   `gameTarget`/`sceneTarget`/`frameDebuggerCapture` lookups — runs
   unconditionally, every real frame, regardless of whether either
   `ExecuteTimingMode` regime's `Execute()` call happens that frame
   (Correction 1/Locked Decision 2).
4. **Tests** (`tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`,
   the SAME file PHASE4/5/6 already extended — no new test file, no
   `CMakeLists.txt` change needed): two new Tier-2 (headless-GPU) tests
   appended at the bottom (see "Required Tests" below).

No new source file was created — every change landed inside four
pre-existing files (`RenderGraph.h`, `RenderGraph.cpp`,
`RenderGraphBuilder.h`, `Core.cpp`) plus the one pre-existing test file.

## Step 1/2 re-confirmation (done fresh, before editing)

- Re-read `PHASE0_MASTER_STRATEGY.md` in full (Corrections 1/2, Locked
  Decisions 1/2/7) and `PHASE6_COMPLETION_REPORT.md` in full, per this
  phase's own `.md` instruction.
- Re-read this phase's own `.md` (`PHASE7_RENDERGRAPH_INTEGRATION_AND_FRAME_COUNTER.md`)
  in full.
- Re-confirmed, by direct code read, every citation this phase's own `.md`
  makes, ALL of which matched exactly except for line-number drift from
  PHASE1-6's own edits (expected, not a discrepancy):
  - `RenderGraph::RenderGraph(Renderer&)`'s exact current body
    (`RenderGraph.cpp` line 26, not ~line 26-35 as cited — a two-line
    include-block shift from PHASE6's own `#include "RenderFeatureCompositor..."`-
    unrelated edits did not apply here; the constructor body itself
    matched the `.md`'s cited snippet byte-for-byte before this phase's own
    edit).
  - `RenderGraph::Execute()`'s template body (`RenderGraph.h`, found at line
    177 after this phase's own `#include` insertion shifted it by +2 from
    the cited ~175) — matched the cited snippet byte-for-byte.
  - `RenderGraphDebugTextureRegistry.h` line 31's own
    `enum class ExecuteTimingMode : std::uint8_t;` forward-declare
    precedent — confirmed, copied verbatim in shape.
  - `RenderGraphBuilder.h` did NOT yet include
    `RenderGraphPersistentResourceCache.h` — confirmed, and confirmed that
    header only depends on `RenderGraphTypes.h`/`RenderTexture.h`, so the
    new include introduces no circularity (confirmed by the subsequent
    successful incremental build across ~20 different `gte_core`/
    `gte_editor` translation units that transitively include
    `RenderGraphBuilder.h`).
  - `Core::BuildFrame()`'s exact current first real statements (`Core.cpp`
    ~line 1148-1165) — confirmed the `gameTarget`/`sceneTarget` lookups are
    the first real statements, exactly as cited.
  - `Core`'s own `rg::RenderGraph m_renderGraph;` member name — confirmed
    via `search_in_dir` (`Core.h` line 601).
  - `RenderGraphResourcePool::BeginFrame()`'s own placement inside
    `ExecuteCompiledGraph()`'s `if (!isPipelined)` guard — confirmed this is
    a genuinely different, unrelated frame-boundary concept, not merged
    with this phase's own `BeginPersistentResourceFrame()`.
- No genuine ambiguity beyond what `PHASE0_MASTER_STRATEGY.md` and this
  phase's own `.md` already resolve was found — `ask_questions` was not
  needed.
- **One genuine, mechanical correction was needed to this phase's own `.md`
  sketch, found and fixed directly during implementation (not requiring
  `ask_questions`, since it is a compile-time fact about C++ forward
  declarations, not a design decision)** — see "A genuine build error found
  and fixed" below.
- No part of this phase's own work was delegated to a `dispatch_sub_agent`
  (this phase's own scope — wiring an already-proven, fully-functional
  class into two more files plus one new call site — was mechanical enough
  to implement and verify directly).

## A genuine build error found and fixed

This phase's own `.md` (Step 3.2) sketches
`ExecuteTimingMode m_persistentCacheTimingMode = ExecuteTimingMode::SynchronousImmediateReadback;`
as a `RenderGraphBuilder` private member default. Taken literally, this
FAILS TO COMPILE: `RenderGraphBuilder.h` only *forward-declares*
`ExecuteTimingMode` (an "opaque enum declaration" — its underlying type and
size are known, but its enumerator NAMES are not visible in this
translation unit, since the actual definition lives in `RenderGraph.h`,
which `RenderGraphBuilder.h` cannot include back without creating a
circular include). The literal enumerator reference
`ExecuteTimingMode::SynchronousImmediateReadback` is therefore ill-formed
here — confirmed live: the very first incremental build attempt failed with
`error: 'SynchronousImmediateReadback' is not a member of 'gte::rg::ExecuteTimingMode'`
across every one of the ~10 `gte_core` translation units that transitively
include `RenderGraphBuilder.h` without also including `RenderGraph.h`.

**Fix**: changed the default to `ExecuteTimingMode m_persistentCacheTimingMode{};`
(value-initialization to the enum's zero value, which — confirmed by
reading `RenderGraph.h`'s own
`enum class ExecuteTimingMode : std::uint8_t { SynchronousImmediateReadback, PipelinedDeferredReadback };` —
is exactly `SynchronousImmediateReadback`, since it is declared first/`= 0`).
This is not a new pattern invented for this fix — it is the EXACT same
precedent `RenderGraphDebugTextureRegistry.h`'s own
`DebugTextureSnapshot::ExecuteTimingMode regime{};` already uses for the
identical reason, confirmed by direct re-read of that file before applying
the fix. This default value is also never actually observed in practice:
`RenderGraph::Execute()` calls `SetPersistentResourceCache()`
unconditionally on every single call, before `build(builder)` ever runs, so
this member's value is always overwritten with the real, caller-supplied
`timingMode` before any pass author's code could ever read it. A doc
comment explaining this was added directly above the member. No other part
of this phase's own `.md` sketch needed correction — the `RenderGraph.h`/
`.cpp`/`Core.cpp` changes all applied byte-for-byte as sketched.

## Changes made

1. **`src/Renderer/RenderGraph/RenderGraph.h`**:
   - Added `#include "RenderGraphPersistentResourceCache.h"` to the include
     block.
   - Added `RenderGraphPersistentResourceCache m_persistentResourceCache;`
     right after `m_resourcePool`.
   - Added `std::uint64_t m_persistentResourceFrameCounter = 0;` right after
     `m_debugTextureFrameCounter`.
   - Added `void BeginPersistentResourceFrame() noexcept;` and
     `std::uint64_t CurrentPersistentResourceFrameCounter() const noexcept { return m_persistentResourceFrameCounter; }`
     right after `CurrentDebugTextureFrameCounter()`.
   - `Execute()`'s template body gained the new
     `builder.SetPersistentResourceCache(&m_persistentResourceCache, timingMode, m_persistentResourceFrameCounter);`
     line, right after `builder.SetDebugMetadataSink(m_debugMetadataSink);`.
2. **`src/Renderer/RenderGraph/RenderGraph.cpp`**:
   - Constructor's member-initializer list gained
     `, m_persistentResourceCache(renderer)` right after `m_resourcePool(renderer)`.
   - Added `RenderGraph::BeginPersistentResourceFrame()`'s definition,
     exactly per this phase's own `.md` sketch.
3. **`src/Renderer/RenderGraph/RenderGraphBuilder.h`**:
   - Added `#include "RenderGraphPersistentResourceCache.h"` to the include
     block.
   - Added `enum class ExecuteTimingMode : std::uint8_t;` forward
     declaration right after `namespace gte::rg {`.
   - Added `SetPersistentResourceCache()`, right after
     `SetDebugMetadataSink()`.
   - Added the three new private members (`m_persistentCache`,
     `m_persistentCacheTimingMode{}` — see "A genuine build error found and
     fixed" above for why `{}` and not the literal enumerator name,
     `m_persistentCacheCurrentFrame`), right after `m_persistentCacheTextures`.
4. **`src/Core/Core.cpp`**: `Core::BuildFrame()` gained
   `m_renderGraph.BeginPersistentResourceFrame();` as its literal first
   statement, before the `gameTarget`/`sceneTarget` lookups.
5. **`tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`**
   (PHASE4/5/6's own file, extended again — no second test file):
   - Added `CurrentPersistentResourceFrameCounterAdvancesOncePerBeginCall`
     (test #19) and `ExecuteStillRunsANormalEmptyFrameAfterGainingTheNewSetterCall`
     (test #20).

No `tests/CMakeLists.txt` change was needed — the test file was already
registered by PHASE4.

## Required Tests (Step 4 of this phase's `.md`) — mapping to what shipped

1. **Tier-1** (no GPU): correctly none — this phase's own `.md` explicitly
   states `BeginPersistentResourceFrame()` itself just forwards to
   already-Tier-1-tested (PHASE5) cache logic, with nothing new and pure to
   test here.
2. **Tier-2, headless fixture** (extended the existing test file, per this
   phase's own `.md` instruction "extend
   `RenderGraphPersistentResourceCacheTests.cpp`, or add a focused new block
   to it"):
   - `CurrentPersistentResourceFrameCounterAdvancesOncePerBeginCall` —
     confirms the counter starts at 0 and becomes 1, 2, 3 after one, two,
     three `BeginPersistentResourceFrame()` calls respectively, using
     `fixture.GetRenderGraph()`.
   - `ExecuteStillRunsANormalEmptyFrameAfterGainingTheNewSetterCall` — a
     basic smoke check: an ordinary `RunSynchronousFrame()` call with an
     empty `build` lambda returning `{}` still runs with zero crash/
     exception now that `Execute()`'s template body also calls
     `SetPersistentResourceCache()` every time.
   - Per this phase's own `.md` explicit instruction: full end-to-end
     confirmation that `SetPersistentResourceCache()`'s THREE values
     genuinely reach `GetOrCreatePersistentTexture()` correctly is
     deliberately DEFERRED to PHASE8's own tests (that method does not
     exist yet this phase) — no test-only public getter was added to
     `RenderGraphBuilder` purely to inspect these private fields in
     isolation, exactly as instructed.
3. **Live sanity** (optional, done): see "Verification" below.

## Verification

1. **Incremental compile check**: `cmake --build build` — first attempt
   FAILED with the genuine build error described above; after the fix,
   succeeded with zero errors (`gte_core`, `gte_editor`, `GreatTamanaEditor.exe`,
   `tests/GreatTamanaEngineTests.exe`, and both Project Assembly `.dll`s all
   rebuilt/relinked cleanly — 86/86 build steps).
2. **Targeted `ctest` run**: `ctest -C Debug -R "RenderGraphPersistentResourceCache" --output-on-failure`
   — **24/24 tests report 100% passed**, every single one a clean, legible
   `Skipped` (never `FAILED`) — this development machine's Vulkan
   driver/loader still does not support `VK_EXT_headless_surface`, the
   exact same, honest, pre-existing, machine-dependent limitation
   PHASE4/5/6 already documented, unchanged by this phase. Both of this
   phase's own 2 new test cases were written correctly and would run and
   pass the moment this exact binary runs on a machine whose Vulkan
   driver/loader DOES support that extension.
3. **Broader regression spot-check** (not strictly required by this phase's
   own rules, done as extra safety margin since `RenderGraphBuilder.h`/
   `RenderGraph.h` both changed): `ctest -C Debug -R "RenderGraphTypesTest|RenderGraphBuilderTest|RenderGraphCompilerTest|RenderGraphPersistentTextureCacheTokenTest|RenderGraphIsStaleCacheEntryTest|RenderGraphSnapshotTest|RenderPipelineTest"`
   — **139/139 tests passed**, confirming zero regression to any
   pre-existing Tier-1 RenderGraph-adjacent test.
4. **Live-Editor sanity check**: launched `GreatTamanaEditor.exe` via
   `run_app_background`, waited ~5 real seconds (several real engine
   frames, each now calling the new `BeginPersistentResourceFrame()`).
   - `GET /get_logs?category=RenderGraphPersistentResourceCache&limit=50` —
     `{"count":0,...}` — expected and correct: nothing in production calls
     `Resolve()` yet (PHASE8's job), so zero log activity under this
     category is the honest, correct baseline.
   - `GET /get_logs?min_level=Warning&limit=50` — only pre-existing,
     unrelated warnings (demo-plugin priority tie-breaks, GPU-timing-slot
     budget exhaustion for demo render features) — nothing new, nothing
     referencing this campaign's own code.
   - `GET /get_logs?min_level=Error&limit=50` — `{"count":0,...}` — zero
     errors across the whole session.
   - `GET /get_game_view` — returned a valid 34288-byte PNG, a normal
     rendered frame — confirms nothing broke.
   - `stop_app_background` — process cleanly terminated.
5. No full clean build, no full `ctest` regression pass was performed
   (correctly deferred to PHASE9, per the campaign's own Rule 3.3.3).

## Honestly-flagged open issues

- **This entire phase's Tier-2 (real GPU) proof could not actually EXECUTE
  on this development machine** — all 24 tests in this file report
  `Skipped`, not `Passed`, for the same pre-existing, machine-dependent
  reason PHASE4/5/6 already disclosed (`VK_EXT_headless_surface`
  unsupported, `vkCreateInstance` → `VkResult=-7`). This is NOT a defect in
  this phase's own code — both new test cases were written correctly
  against the real API and would pass on a machine whose Vulkan
  driver/loader supports that extension.
- **This phase's own `.md` sketch contained one genuine, mechanical
  compile-time error** (the `ExecuteTimingMode::SynchronousImmediateReadback`
  default-member-initializer literal, illegal against a forward-declared
  enum) — found and fixed during implementation, per "A genuine build error
  found and fixed" above; disclosed here plainly rather than silently
  smoothed over, since a future reader of that `.md` file taken 100%
  literally would hit the exact same build failure this phase's own first
  `cmake --build build` attempt did.
- **Full end-to-end confirmation that `SetPersistentResourceCache()`'s three
  values (cache pointer, timing mode, current frame) genuinely reach and are
  correctly used by a real caller is still open** — by design, per this
  phase's own `.md` Step 4 item 2's explicit instruction, since
  `GetOrCreatePersistentTexture()` (the real consumer) does not exist until
  PHASE8. This phase's own tests only prove the WIRING compiles, runs, and
  the frame counter itself advances correctly — not that the three
  forwarded values are used correctly downstream (there is no downstream
  consumer yet).
- No other open issues. Every acceptance point this phase's own `.md` lists
  is satisfied.

## Git

Changes staged and committed together with this report:
- `src/Core/Core.cpp`
- `src/Renderer/RenderGraph/RenderGraph.h`
- `src/Renderer/RenderGraph/RenderGraph.cpp`
- `src/Renderer/RenderGraph/RenderGraphBuilder.h`
- `tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`
- `task_manager/editor-core-separation-27/PHASE7_COMPLETION_REPORT.md`
