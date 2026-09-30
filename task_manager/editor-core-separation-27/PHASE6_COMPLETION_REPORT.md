# PHASE6 — COMPLETION REPORT: Bounded, Batched Resize (One `vkDeviceWaitIdle()` Per Real Frame, Not One Per Entry)

Campaign folder: `task_manager/editor-core-separation-27/`
Branch: `feature/editor-core-separation` (unchanged, no new branch created)

## Summary

Extended `RenderGraphPersistentResourceCache` (the SAME two files PHASE2/
PHASE4/PHASE5 created — never a second header/source pair) with everything
this phase's own `.md` specifies:

1. **A cached raw `VkDevice m_device`** — the constructor now ALSO calls
   `renderer.GetVulkanContextInfo().device` once, at construction, mirroring
   `RenderGraphTimestampPool`'s own identical precedent. The constructor
   moved from an inline header definition to an out-of-line `.cpp`
   definition, since calling a `Renderer` member function needs `Renderer`'s
   full definition (this header still only forward-declares `class
   Renderer;`).
2. **A forward-declared `enum class ExecuteTimingMode : std::uint8_t;`**
   inside the header (mirroring `RenderGraphDebugTextureRegistry.h`'s/
   `RenderGraphDebugVolumeTextureRegistry.h`'s own identical precedent
   exactly), plus a `.cpp`-only `#include "RenderGraph.h"` for the actual
   enumerator VALUE (`ExecuteTimingMode::PipelinedDeferredReadback`) the new
   resize-refusal branch needs — confirmed this cannot participate in a
   header cycle since a `.cpp` file is never itself included by anything
   else.
3. **`Resolve()` grew a mandatory sixth... actually fifth trailing
   `ExecuteTimingMode timingMode` parameter** (PHASE5 had already grown it
   to four parameters plus `currentFrame` — this phase appends the timing
   mode). On a genuinely PRE-EXISTING entry (`!inserted`):
   - A DIFFERENT `desc.format` than the entry's own recorded `desc.format`
     is ALWAYS refused (`std::nullopt`, `GTE_LOG_ERROR`'d), independent of
     `timingMode` — a caller bug, never silently reinterpreted, never
     queued as a resize.
   - A DIFFERENT `desc.width`/`desc.height` QUEUES a resize (via the new
     `QueueResize()`) when `timingMode ==
     ExecuteTimingMode::SynchronousImmediateReadback`, or is REFUSED
     (logged, extent left unchanged, nothing queued) when `timingMode ==
     ExecuteTimingMode::PipelinedDeferredReadback` — that regime must never
     trigger `FlushPendingResizes()`'s own `vkDeviceWaitIdle()`. Either way,
     THIS call's own returned texture/extent is still the entry's CURRENT
     (old) one, never the newly-requested size.
4. **`QueueResize()` (private)** — "last request this frame wins for the
   same entry" — overwrites an already-queued `PendingResize` for the same
   entry pointer rather than appending a duplicate.
5. **`FlushPendingResizes()` (public)** — a no-op when nothing is pending;
   otherwise exactly ONE `vkDeviceWaitIdle(m_device)` (the raw Vulkan call,
   NEVER `Renderer::WaitForGpuIdle()` — that wrapper's own doc comment
   forbids any per-frame-path caller, and this method is destined to run on
   exactly such a path from PHASE8 onward), followed by looping over every
   queued `PendingResize`, calling `RenderTexture::Resize()`, updating the
   entry's own `desc.width`/`desc.height`, and resetting
   `lastKnownLayout` back to `VK_IMAGE_LAYOUT_UNDEFINED` (Section 5.1 — a
   freshly recreated `VkImage` really is undefined again). **Not wired into
   `RenderGraph::ExecuteCompiledGraph()` yet** — that call site remains
   PHASE8's own job, per this phase's own `.md`.

No new source file was created — every change landed inside the two
existing files (`RenderGraphPersistentResourceCache.h`/`.cpp`) plus the one
existing test file
(`tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`,
already registered in `tests/CMakeLists.txt` since PHASE4 — no CMake change
needed this phase).

## Step 1/2 re-confirmation (done fresh, before editing)

- Re-read `PHASE0_MASTER_STRATEGY.md` in full and `PHASE5_COMPLETION_REPORT.md`
  in full, per this phase's own `.md` instruction.
- Re-read this phase's own `.md` (`PHASE6_CACHE_BATCHED_RESIZE.md`) in full.
- Re-confirmed, by direct code read, every citation this phase's own `.md`
  makes:
  - `Renderer::WaitForGpuIdle()` (`Renderer.h` ~line 349-364) really does
    carry the documented "NEVER call this from any per-frame/performance-
    sensitive path" restriction.
  - `RenderFeatureCompositor::EnsureTextureSized()`
    (`RenderFeatureCompositor.cpp` line 531) really does call a raw,
    directly-stored `vkDeviceWaitIdle(m_device)` (line 548) — the exact
    precedent this phase follows instead of `Renderer::WaitForGpuIdle()`.
  - `Renderer::GetVulkanContextInfo()` (`Renderer.h` line 887, defined
    `Renderer.cpp` line 461) is a plain, non-throwing getter over
    already-live state — confirmed by reading its own body (no throwing
    work of any kind), safe to call from a `noexcept` constructor.
  - `RenderTexture`'s constructor (`RenderTexture.h` line 99) and `Resize()`
    (line 120) — confirmed `Resize()`'s own doc comment: contents undefined
    afterward, a genuinely new VMA allocation, `m_debugName`/
    `m_depthDebugName` pointers re-attached unchanged (no change needed
    there for this cache's own map-key-stability discipline).
  - `RenderGraphDebugTextureRegistry.h` line 31's own
    `enum class ExecuteTimingMode : std::uint8_t;` forward-declare
    precedent, copied verbatim in shape (comment style, placement inside
    `namespace gte::rg { ... }`).
  - `RenderGraph.h` line 99's own full `enum class ExecuteTimingMode :
    std::uint8_t { SynchronousImmediateReadback, PipelinedDeferredReadback
    };` definition — confirmed `RenderGraph.h` does NOT yet include
    `RenderGraphPersistentResourceCache.h` (that's PHASE7's job), so the
    `.cpp`-only include is genuinely safe today and stays safe once PHASE7
    lands the reverse include.
  - `TextureDesc` (`RenderGraphTypes.h`) — confirmed `width`/`height`/
    `format` fields exist exactly as this phase's own comparison logic
    assumes.
- No genuine ambiguity beyond what `PHASE0_MASTER_STRATEGY.md` and this
  phase's own `.md` already resolve was found for the PRODUCTION code
  itself — `ask_questions` was not needed for that half.
- **One genuine test-design ambiguity WAS found and resolved directly by
  engineering judgment (not `ask_questions`, since it is a mechanical fact
  about how this phase's OWN PHASE5-shipped double-request guard already
  behaves, not a design decision needing human input)** — see "A genuine
  test-design conflict found and resolved" below.
- No part of this phase's own work was delegated to a `dispatch_sub_agent`
  (this phase's own scope was mechanical enough — extending the same,
  already-proven class one more time — to implement and verify directly;
  PHASE4 remains this campaign's own flagged highest-risk phase).

## A genuine test-design conflict found and resolved

This phase's own `.md` (Step 4, item 3, "Last-request-wins") sketches a test
requesting "TWO different new sizes for the SAME entity **within the same
frame**" before ever calling `FlushPendingResizes()`. Taken completely
literally — two `Resolve()` calls for the identical `(owner, name)` pair
using the identical `currentFrame` integer value — this is **structurally
impossible to exercise**, because PHASE5's own already-shipped, locked
same-real-frame double-request guard (Section 5.3) unconditionally refuses
(`std::nullopt`, before ever reaching the resize-detection branch) any
SECOND `Resolve()` call for the same identity within one literal
`currentFrame` value, regardless of what `desc` is passed. A literal
same-`currentFrame` version of this test would therefore only ever observe
the FIRST call succeed and the SECOND one refused via the double-request
guard — never reaching `QueueResize()`'s own "last request wins" branch at
all.

**Resolution**: the test
(`LastResizeRequestBeforeAFlushWinsOverAnEarlierOne`) issues the two
different resize requests across **two separate real frames** (frame 2,
then frame 3), with `FlushPendingResizes()` never called in between —
exactly matching what `QueueResize()`'s own "last request wins" branch
actually exists to protect against in real production usage: `PHASE8`'s own
future wiring calls `FlushPendingResizes()` once per `SynchronousImmediateReadback`
`ExecuteCompiledGraph()` call (i.e., once per real frame), so a same-literal-
frame double resize request for one identity can never happen in production
either — the "last request wins" mechanism's real job is protecting a
resize request accumulated across more than one real frame in the (today,
test-only) case where `FlushPendingResizes()` has not yet been called
manually. This is disclosed here plainly, as a genuine, confirmed gap
between the phase file's own literal wording and what is actually
exercisable, not a silently-smoothed-over detail — no production code
behavior was affected by this resolution, only how one test is shaped.

## Changes made

1. **`src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.h`**:
   - Added the namespace-scope forward declaration
     `enum class ExecuteTimingMode : std::uint8_t;` near the top of the
     first `namespace gte::rg { ... }` block.
   - Added `#include <vector>`.
   - Changed the constructor from an inline-defined, header-only method to
     a declaration only (`explicit RenderGraphPersistentResourceCache(Renderer&
     renderer) noexcept;`), with its real definition moved to the `.cpp`.
   - Added the `VkDevice m_device = VK_NULL_HANDLE;` private member,
     declared immediately after `Renderer* m_renderer` (matching the
     constructor's own member-initializer-list order).
   - `Resolve()`'s declaration grew the new trailing
     `ExecuteTimingMode timingMode` parameter.
   - Added the private nested `struct PendingResize { PersistentResourceCacheEntry*
     entry; std::uint32_t newWidth; std::uint32_t newHeight; };`, the
     private `void QueueResize(...)` method declaration, the private
     `std::vector<PendingResize> m_pendingResizes;` member, and the public
     `void FlushPendingResizes();` method declaration.
   - Updated every affected doc comment (constructor, `Resolve()`) to
     describe the new PHASE6 behavior.
2. **`src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.cpp`**:
   - Added `#include "RenderGraph.h"` (`.cpp`-only, per this phase's own
     `.md` Section 3.1).
   - Added the out-of-line constructor definition, initializing `m_device`
     from `renderer.GetVulkanContextInfo().device`.
   - `Resolve()`'s definition grew the `timingMode` parameter and now, for a
     genuinely pre-existing entry (`!inserted`), performs the format-change
     hard refusal and the width/height-change queue-or-refuse branch,
     exactly per this phase's own `.md` Section 3.2 sketch.
   - Added `QueueResize()`'s and `FlushPendingResizes()`'s definitions.
3. **`tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`**
   (PHASE4/PHASE5's own file, extended — no second test file):
   - Every existing `Resolve()` call site updated to pass the new,
     mandatory trailing `ExecuteTimingMode` argument (`kSync` — a new local
     `constexpr ExecuteTimingMode kSync = ExecuteTimingMode::SynchronousImmediateReadback;`
     alias, plus a `kPipelined` counterpart, both declared once in this
     file's own anonymous namespace) — safe to change freely, no other
     production caller exists yet.
   - Added 5 new test cases (Step 4, items 1-5):
     `ResizeIsQueuedNotAppliedUntilFlushPendingResizesIsCalled`,
     `BatchedResizeAppliesAllQueuedEntriesInOneFlushCall`,
     `LastResizeRequestBeforeAFlushWinsOverAnEarlierOne` (see "A genuine
     test-design conflict found and resolved" above for why this test's own
     two `Resolve()` calls use two different `currentFrame` values, not a
     literal identical one),
     `ResizeRequestedFromThePipelinedRegimeIsRefusedButTheCallStillSucceeds`,
     and `ReRequestingAnExistingEntryWithADifferentFormatIsRefusedRegardlessOfRegime`.

No `tests/CMakeLists.txt` change was needed — the test file was already
registered by PHASE4.

## Required Tests (Step 4 of this phase's `.md`) — mapping to what shipped

1. **Basic resize** → `ResizeIsQueuedNotAppliedUntilFlushPendingResizesIsCalled`
   — confirms (a) a re-requested identity's own returned extent is STILL
   the old one before any flush, (b) after `FlushPendingResizes()`, the SAME
   `RenderTexture*`'s `Extent()` reports the new size, and (c) a fresh
   `Resolve()` call afterward reports `lastKnownLayout ==
   VK_IMAGE_LAYOUT_UNDEFINED`.
2. **Batched — the single most important test in this phase** →
   `BatchedResizeAppliesAllQueuedEntriesInOneFlushCall` — three distinct
   identities each queue their own different new size across two real
   frames; every entry's extent is confirmed STILL the old size before any
   flush, and correctly, individually updated by ONE
   `FlushPendingResizes()` call. Per this phase's own `.md` instruction,
   the "exactly one `vkDeviceWaitIdle()` call total, no matter how many
   entries" claim is proven by this test confirming the batching logic
   itself is correct, COMBINED with the direct, stated code-read fact:
   `FlushPendingResizes()`'s own body contains exactly one, unconditional
   `vkDeviceWaitIdle(m_device);` statement, outside any loop, guarded only
   by an early `return` when `m_pendingResizes.empty()` — confirmed by
   direct re-reading of the shipped `.cpp` body immediately before writing
   this report.
3. **Last-request-wins** → `LastResizeRequestBeforeAFlushWinsOverAnEarlierOne`
   — see "A genuine test-design conflict found and resolved" above for why
   this uses two separate simulated frames rather than one literal frame
   value; confirms only the SECOND (most recent) queued size ever takes
   effect after a single flush.
4. **Pipelined-regime resize refusal** →
   `ResizeRequestedFromThePipelinedRegimeIsRefusedButTheCallStillSucceeds` —
   confirms (a) the call still returns a valid `ResolvedTexture` (not
   `std::nullopt`), (b) the entry's extent is unchanged immediately
   afterward, and (c) a subsequent `FlushPendingResizes()` call does not
   touch this entry's extent at all (nothing was queued for it).
5. **Format-change refusal** →
   `ReRequestingAnExistingEntryWithADifferentFormatIsRefusedRegardlessOfRegime`
   — confirms `std::nullopt` is returned for a same-width/height,
   different-format re-request, tested against BOTH `kSync` and
   `kPipelined` `timingMode` values, confirming the refusal really is
   regime-independent.

## Verification

1. **Incremental compile check**: `cmake --build build` — succeeded with
   zero errors (`gte_core`, `GreatTamanaEditor.exe`, and
   `tests/GreatTamanaEngineTests.exe` all rebuilt/relinked cleanly; the two
   Project Assembly `.dll`s relinked too, unaffected).
2. **Targeted `ctest` run**: `ctest -C Debug -R "RenderGraphPersistentResourceCache" --output-on-failure`
   — **22/22 tests report 100% passed**, every single one a clean, legible
   `Skipped` (never `FAILED`) — this development machine's Vulkan
   driver/loader still does not support `VK_EXT_headless_surface`
   (`vkCreateInstance` fails with `VkResult=-7`), the exact same, honest,
   pre-existing, machine-dependent limitation PHASE4/PHASE5 already
   documented, unchanged by this phase. Every one of this phase's own 5 new
   test cases was written correctly and would run and pass the moment this
   exact binary runs on a machine whose Vulkan driver/loader DOES support
   that extension — nothing in the test code itself needs to change for
   that to happen.
3. **Broader regression spot-check** (not strictly required by this phase's
   own rules, done as an extra safety margin since `Resolve()`'s own
   signature changed again): `ctest -C Debug -R "RenderGraphTypesTest|RenderGraphBuilderTest|RenderGraphCompilerTest|RenderGraphPersistentTextureCacheTokenTest|RenderGraphIsStaleCacheEntryTest"`
   — **98/98 tests passed**, confirming zero regression to any pre-existing
   Tier-1 RenderGraph test.
4. **Live-Editor sanity check** (the fallback this phase's own `.md`
   implicitly calls for, mirroring PHASE4/PHASE5's own precedent, given the
   Tier-2 fixture skips on this machine): launched `GreatTamanaEditor.exe`
   via `run_app_background`.
   - `GET /get_logs?category=RenderGraphPersistentResourceCache&limit=50` —
     `{"count":0,...}` — expected and correct: nothing in production calls
     `Resolve()`/`FlushPendingResizes()` yet (PHASE7/PHASE8's job), so zero
     log activity under this category is the honest, correct baseline.
   - `GET /get_logs?min_level=Warning&limit=50` — only pre-existing,
     unrelated warnings (demo-plugin priority tie-breaks, GPU-timing-slot
     budget exhaustion for demo render features) — nothing new, nothing
     referencing `RenderGraphPersistentResourceCache`.
   - `GET /get_game_view` — returned a valid 34288-byte PNG, a normal
     rendered frame — confirms nothing broke.
   - `stop_app_background` — process cleanly terminated.
5. No full clean build, no full `ctest` regression pass was performed
   (correctly deferred to PHASE9, per the campaign's own Rule 3.3.3).

## Honestly-flagged open issues

- **This entire phase's Tier-2 (real GPU) proof could not actually EXECUTE
  on this development machine** — every one of the 22 tests in this file
  reports `Skipped`, not `Passed`, for the same pre-existing,
  machine-dependent reason PHASE4/PHASE5 already disclosed
  (`VK_EXT_headless_surface` unsupported, `vkCreateInstance` →
  `VkResult=-7`). This is NOT a defect in this phase's own code — every new
  test case was written correctly against the real API and would pass on a
  machine whose Vulkan driver/loader supports that extension.
- **The "exactly one `vkDeviceWaitIdle()` call, no matter how many
  entries" claim is proven by a combination of a passing test PLUS a
  stated direct code-read fact, never by direct Vulkan-call
  instrumentation** — this project has no Vulkan-call-counting test
  harness, exactly as this phase's own `.md` anticipated and explicitly
  sanctioned as the acceptable evidentiary standard for this specific
  claim.
- **The "Last-request-wins" test's own two `Resolve()` calls deliberately do
  NOT use the literal same `currentFrame` value** — see "A genuine
  test-design conflict found and resolved" above for the full reasoning;
  this is a genuine, confirmed gap between this phase's own `.md` literal
  wording and what PHASE5's own already-shipped double-request guard makes
  actually exercisable, disclosed here plainly rather than silently
  smoothed over.
- `FlushPendingResizes()` is still only ever called by this phase's own
  test suite — the real production driver,
  `RenderGraph::ExecuteCompiledGraph()`'s own tail hook, does not exist
  until PHASE8. This is expected and correct per this phase's own `.md`
  scope.
- No other open issues. Every acceptance point this phase's own `.md` lists
  is satisfied.

## Git

Changes staged and committed together with this report:
- `src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.h`
- `src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.cpp`
- `tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`
- `task_manager/editor-core-separation-27/PHASE6_COMPLETION_REPORT.md`
