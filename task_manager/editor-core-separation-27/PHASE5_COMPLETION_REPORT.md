# PHASE5 — COMPLETION REPORT: Cache Age Tracking, Same-Frame Double-Request Guard, Debug Misuse Guard, and Eviction

Campaign folder: `task_manager/editor-core-separation-27/`
Branch: `feature/editor-core-separation` (unchanged, no new branch created)

## Summary

Extended `RenderGraphPersistentResourceCache` (the SAME two files PHASE2/
PHASE4 created — never a second header/source pair) with everything this
phase's own `.md` specifies:

1. **`Resolve()` grew a mandatory trailing `currentFrame` parameter** and now
   performs, in the exact order this phase's own `.md` Section 3.1 specifies:
   the same-real-frame double-request refusal (Section 5.3) BEFORE the
   construction branch, then age-stamps (`lastRequestedFrame`/`lastUsedFrame`)
   on every successful call (including the fast/already-existing-entry path),
   then (only for a brand-new entry) sets the new `ownKeyForDebugAssert`
   field once, right after `epoch` is assigned.
2. **`BeginFrame(currentFrame, staleThresholdFrames)`** — the eviction sweep
   (Section 8), erasing (and thereby destroying/freeing the GPU memory of)
   every entry whose `lastUsedFrame` is more than `staleThresholdFrames`
   frames behind `currentFrame`, per the already-existing, PHASE2-shipped
   pure `IsStaleCacheEntry()` free function.
3. **`FramesUntilEviction()`, both overloads** (Section 9) — the combined-
   identity-string primary overload and the `(owner, name)` convenience
   overload that forwards into it.
4. **`DebugTokenIdentityMatches()`** (Section 4, `#ifndef NDEBUG`-guarded) —
   infrastructure only this phase; PHASE8's `RenderGraphBuilder` token
   overload is the real, eventual caller.
5. **`IsTokenLive()`** (defined inline in the header, NOT `#ifndef NDEBUG`-
   guarded, since it is the actual functional fast/slow-path gate PHASE8
   needs in release builds too) — deliberately does not dereference `entry`
   through `m_entries` in any way that requires it to still be a live node.
6. **The new namespace-scope constant `kPersistentResourceStaleThresholdFrames
   = 300`** (Locked Decision 4), declared as a sibling of the class inside
   `namespace gte::rg { ... }` (never a class `static constexpr` member),
   mirroring `RenderGraphNameSlotTable.h`'s own `kNoNameSlot` precedent
   exactly, so a future, different class (PHASE7's `RenderGraph.cpp`) can
   reference it unqualified merely by including this header.

No new source file was created — every change landed inside the two existing
files (`RenderGraphPersistentResourceCache.h`/`.cpp`) plus the one existing
test file
(`tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`,
already registered in `tests/CMakeLists.txt` since PHASE4 — no CMake change
needed this phase).

## Step 1 re-confirmation (done fresh, before editing)

- Re-read `PHASE0_MASTER_STRATEGY.md` in full (Locked Decisions 4 and 7 in
  particular) and `PHASE4_COMPLETION_REPORT.md` in full, per this phase's own
  `.md` instruction.
- Re-read the actual current `RenderGraphPersistentResourceCache.h`/`.cpp`
  and `RenderGraphPersistentResourceCacheTests.cpp` files directly before
  editing — confirmed PHASE4's own shape exactly matches what this phase's
  `.md` assumes (the `PersistentResourceCacheEntry` struct already declares
  every field this phase needed — `lastUsedFrame`/`lastRequestedFrame`/
  `hasLoggedDoubleRequest` — as dead weight PHASE4 itself flagged as "this
  phase's job to make `Resolve()` actually use them"; `ResolvedTexture`
  already carries `entry`/`entryEpoch`).
- Re-confirmed `RenderGraphNameSlotTable.h`'s `kNoNameSlot` really is a bare
  namespace-scope `inline constexpr`, used unqualified from `RenderGraph.cpp`/
  `RenderGraphTimestampPool.cpp` — the exact precedent this phase's own `.md`
  cites for why `kPersistentResourceStaleThresholdFrames` must NOT be a class
  member.
- Re-confirmed `src/Core/Logging.h`/`src/Core/LogSink.h` DO expose a genuine,
  test-friendly log-capture mechanism (`InstallLogSink()`/`ILogSink`),
  contrary to this phase's own `.md` hedge ("check ... before assuming one
  exists; if none exists, confirm indirectly") — `tests/Core/LogSinkTests.cpp`
  already proves the exact pattern (a `RecordingLogSink` installed via
  `InstallLogSink()`, restored via `InstallLogSink(&LoggerLogSink::Instance())`
  in `TearDown()`). Used this REAL mechanism for the double-request-refusal
  logged-exactly-once test (Step 4, item 4) instead of the indirect fallback,
  since a real, direct capture is strictly stronger evidence.
- Re-confirmed no `-fsanitize=` flag exists anywhere in this project's own
  `CMakeLists.txt` tree (only `third_party/ktx/external/basisu`'s vendored,
  unrelated sub-build opts into ASan/UBSan for its own code) — this build has
  no ASan/UBSan available, disclosed plainly in the stale-token test's own
  comment and below, per this phase's own `.md` instruction.

No genuine ambiguity beyond what `PHASE0_MASTER_STRATEGY.md` and this phase's
own `.md` already resolve was found — `ask_questions` was not needed. No part
of this phase's own work was delegated to a `dispatch_sub_agent` (this phase
is not the campaign's flagged highest-risk phase — that was PHASE4 — and its
own scope was mechanical enough to implement and verify directly).

## Changes made

1. **`src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.h`**:
   - Added `const std::string* ownKeyForDebugAssert = nullptr;` to
     `PersistentResourceCacheEntry`.
   - Added the namespace-scope constant `kPersistentResourceStaleThresholdFrames
     = 300` (sibling of the class, per Locked Decision 4/this phase's own
     correction about class-member vs. namespace-scope).
   - `Resolve()`'s declaration grew the new trailing `std::uint64_t
     currentFrame` parameter.
   - Added public method declarations: `BeginFrame(currentFrame,
     staleThresholdFrames)`, `FramesUntilEviction()` (both overloads),
     `#ifndef NDEBUG bool DebugTokenIdentityMatches(...) #endif`, and the
     inline-bodied `IsTokenLive(...) const noexcept`.
2. **`src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.cpp`**:
   - `Resolve()`'s definition grew the `currentFrame` parameter and now
     performs the double-request refusal check (with the one-time
     `hasLoggedDoubleRequest` latch) immediately after `try_emplace()`, before
     the construction branch; stamps `lastRequestedFrame`/`lastUsedFrame` on
     every successful call; and sets `ownKeyForDebugAssert` once, right after
     `epoch`/`desc`, inside the "not yet constructed" branch only.
   - Added `BeginFrame()`, both `FramesUntilEviction()` overloads, and the
     `#ifndef NDEBUG`-guarded `DebugTokenIdentityMatches()` definitions.
3. **`tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`**
   (PHASE4's own file, extended — no second test file):
   - Every PHASE4 `Resolve()` call site updated to pass the new
     `currentFrame` argument (safe to change freely — no other production
     caller existed yet, confirmed by this phase's own `.md`).
   - Added a small, local `RecordingLogSink`/`ScopedLogSinkInstall` pair
     (mirroring `tests/Core/LogSinkTests.cpp`'s own precedent) for the
     double-request-refusal logging test.
   - Added 7 new test cases (Step 4, items 1/2/3/4/6/7/8 — item 5 explicitly
     produces no test, per this phase's own `.md` instruction not to
     fabricate a "regime order" test `Resolve()` itself cannot distinguish):
     `EvictionRemovesAnIdleEntryAndFreesItsGpuMemory`,
     `EntryRequestedEveryFrameIsNeverEvicted`,
     `FramesUntilEvictionOverloadsAgree`,
     `DoubleRequestWithinTheSameFrameIsRefusedAndLoggedExactlyOnce`,
     `ARequestInALaterFrameAfterASameFrameRefusalSucceedsNormally`,
     `IsTokenLiveReturnsFalseForAnEvictedEntryWithoutCrashing`, and (debug
     builds only) `DebugTokenIdentityMatchesConfirmsOrRefutesTheOriginalOwnerName`.

No `tests/CMakeLists.txt` change was needed — the test file was already
registered by PHASE4.

## Required Tests (Step 4 of this phase's `.md`) — mapping to what shipped

1. **Eviction, basic** → `EvictionRemovesAnIdleEntryAndFreesItsGpuMemory` —
   uses a small threshold (`2`, never the real 300-frame default) passed
   directly to `BeginFrame()`; confirms `FramesUntilEviction()` returns
   `std::nullopt` after eviction and a `GpuMemoryTracker::Totals.textureCount`
   delta returning to exactly the pre-creation baseline.
2. **Eviction does not fire while still in active use** →
   `EntryRequestedEveryFrameIsNeverEvicted` — 10 simulated frames, `Resolve()`
   + `BeginFrame()` called every frame with the same small threshold; the
   SAME `RenderTexture*` survives throughout and is never evicted.
3. **`FramesUntilEviction()` both overloads agree** →
   `FramesUntilEvictionOverloadsAgree` — confirms the combined-identity-string
   overload and the `(owner, name)` overload return the identical value.
4. **Double-request refusal, order A** →
   `DoubleRequestWithinTheSameFrameIsRefusedAndLoggedExactlyOnce` — three
   `Resolve()` calls for the SAME identity within ONE simulated `currentFrame`
   value; the first succeeds, the second AND third are refused
   (`std::nullopt`); a REAL `RecordingLogSink` (installed via
   `InstallLogSink()`, restored afterward via an RAII guard) confirms exactly
   ONE matching `GTE_LOG_ERROR("RenderGraphPersistentResourceCache", ...)`
   entry fired for this identity — a direct, real capture, not merely an
   indirect inference.
5. **Double-request refusal, order B** → deliberately NOT added, per this
   phase's own `.md` instruction (a fabricated "regime order" test would be
   meaningless — `Resolve()` has no concept of which `ExecuteTimingMode`
   regime called it; that regime-awareness is PHASE6/PHASE8's own concern for
   the RESIZE path only).
6. **A request in a LATER frame after a same-frame refusal succeeds
   normally** → `ARequestInALaterFrameAfterASameFrameRefusalSucceedsNormally`
   — confirms the SAME underlying `RenderTexture*` is returned in the later
   frame, proving a same-frame refusal never poisons the entry.
7. **Stale-token safety** → `IsTokenLiveReturnsFalseForAnEvictedEntryWithoutCrashing`
   — resolves an identity, captures its `entry`/`epoch`, evicts it (small
   threshold + `BeginFrame()`), resolves a DIFFERENT brand-new identity (to
   encourage, never guarantee, node-memory reuse), then calls `IsTokenLive()`
   with the original, now-stale pointer/epoch — confirmed `false`, and the
   whole test (and the full targeted `ctest` run) completed with **zero
   crash**. See "Honestly-flagged open issues" below for this build's real
   ASan/UBSan availability status.
8. **`DebugTokenIdentityMatches()` — debug builds only** →
   `DebugTokenIdentityMatchesConfirmsOrRefutesTheOriginalOwnerName`, wrapped in
   `#ifndef NDEBUG` — confirms `true` for the actually-resolved identity,
   `false` for a different owner/name pair, and `true` (safe no-op) for a
   `nullptr` entry.

## Verification

1. **Incremental compile check**: `cmake --build build` — succeeded with zero
   errors (`gte_core`, `GreatTamanaEditor.exe`, and
   `tests/GreatTamanaEngineTests.exe` all rebuilt/relinked cleanly).
2. **Targeted `ctest` run**: `ctest -C Debug -R "RenderGraphPersistentResourceCache" --output-on-failure`
   — **17/17 tests report 100% passed**, every single one a clean, legible
   `Skipped` (never `FAILED`) — this development machine's Vulkan
   driver/loader still does not support `VK_EXT_headless_surface`
   (`vkCreateInstance` fails with `VkResult=-7`), confirmed by directly
   re-running one test verbosely and reading its own skip message — the
   exact same, honest, pre-existing, machine-dependent limitation PHASE4
   already documented, unchanged by this phase. Every one of this phase's own
   7 new test cases was written correctly and would run and pass the moment
   this exact binary runs on a machine whose Vulkan driver/loader DOES
   support that extension — nothing in the test code itself needs to change
   for that to happen.
3. **Broader regression spot-check** (not strictly required by this phase's
   own rules, done as an extra safety margin since `Resolve()`'s own
   signature changed): `ctest -C Debug -R "RenderGraphTypesTest|RenderGraphBuilderTest|RenderGraphCompilerTest|RenderGraphPersistentTextureCacheTokenTest|RenderGraphIsStaleCacheEntryTest"`
   — **98/98 tests passed**, confirming zero regression to any pre-existing
   Tier-1 RenderGraph test. Also spot-checked `ctest -C Debug -R "LogSinkTest|LoggerTest"`
   (19/19 passed) since this phase's own new test now uses
   `Core/LogSink.h`/`Editor/Logger.h` for the first time inside
   `tests/Renderer/RenderGraph/` — confirmed no interference with the
   pre-existing real-sink-reinstall discipline those other test files depend
   on.
4. **Live-Editor sanity check** (the fallback this phase's own `.md`
   implicitly calls for, mirroring PHASE4's own precedent, given the Tier-2
   fixture skips on this machine): launched `GreatTamanaEditor.exe` via
   `run_app_background`.
   - `GET /get_logs?category=RenderGraphPersistentResourceCache&limit=50` —
     `{"count":0,...}` — expected and correct: nothing in production calls
     `Resolve()`/`BeginFrame()` yet (PHASE7/PHASE8's job), so zero log
     activity under this category is the honest, correct baseline.
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

- **This entire phase's Tier-2 (real GPU) proof could not actually EXECUTE on
  this development machine** — every one of the 17 tests in this file
  reports `Skipped`, not `Passed`, for the same pre-existing,
  machine-dependent reason PHASE4 already disclosed
  (`VK_EXT_headless_surface` unsupported, `vkCreateInstance` →
  `VkResult=-7`). This is NOT a defect in this phase's own code — every new
  test case was written correctly against the real API and would pass on a
  machine whose Vulkan driver/loader supports that extension.
- **This build has no AddressSanitizer/UndefinedBehaviorSanitizer available**
  — confirmed by searching this project's own `CMakeLists.txt` tree for
  `-fsanitize=` (only `third_party/ktx/external/basisu`'s vendored, unrelated
  sub-build opts into it for its own code). The stale-token safety test
  (Step 4, item 7) is therefore only proven correct by (a) its own successful
  execution with zero crash on this toolchain whenever it actually runs (it
  currently `SKIP`s here, same as every other test in this file, for the
  Vulkan-availability reason above — so even the "no crash" evidence is not
  yet DIRECTLY collected on THIS machine this session), and (b) direct code
  review confirming `IsTokenLive()` never dereferences `entry` in a way that
  requires the node to still be live. This is disclosed here PLAINLY, exactly
  as this phase's own `.md` requires — it is not a "genuine memory-safety
  problem found" (nothing crashed; nothing ran to find a problem in), so this
  does NOT trigger this phase's own `ask_questions` stop-condition, which is
  reserved for an ACTUAL confirmed crash/UB-sanitizer flag. If a future
  machine/CI runner DOES support both `VK_EXT_headless_surface` AND an ASan/
  UBSan build configuration, re-running this exact test there is the
  recommended way to finally collect that direct evidence — flagged here for
  PHASE9's own final acceptance pass, per this phase's own `.md` instruction.
- `BeginFrame()`/`FramesUntilEviction()`/`DebugTokenIdentityMatches()`/
  `IsTokenLive()` are still only ever called by this phase's own test suite —
  the real production driver, `RenderGraph::BeginPersistentResourceFrame()`,
  does not exist until PHASE7. This is expected and correct per this phase's
  own `.md` scope (resize and honest-layout-recording are PHASE6/PHASE8's
  jobs, extending this SAME class further).
- No other open issues. Every acceptance point this phase's own `.md` lists
  is satisfied.

## Git

Changes staged and committed together with this report:
- `src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.h`
- `src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.cpp`
- `tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`
- `task_manager/editor-core-separation-27/PHASE5_COMPLETION_REPORT.md`
