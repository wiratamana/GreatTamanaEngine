# PHASE2 — COMPLETION REPORT: New Vocabulary — Owners Header, `PersistentTextureCacheToken`, `IsStaleCacheEntry()`, and the Builder-Side Root List

Campaign folder: `task_manager/editor-core-separation-27/`
Branch: `feature/editor-core-separation` (unchanged, no new branch created)

## Summary

Added every small, standalone piece of new vocabulary this campaign needs,
with zero live-`VkDevice` involvement and zero behavior change to any
pre-existing call site:

1. **`src/Renderer/RenderGraph/RenderGraphPersistentResourceOwners.h`** (new
   file) — a tiny, append-only header holding every BUILT-IN owner-identifier
   constant. Today it holds exactly one constant,
   `kPersistentOwnerCacheValidation`, reserved for this campaign's own future
   Tier-2 test suite (PHASE4 onward).
2. **`src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.h`/`.cpp`**
   (new files, PARTIAL for now) — contain only:
   - `PersistentTextureCacheToken` (a small POD, default-constructed with
     `entry == nullptr`, `entryEpoch == 0`).
   - `IsStaleCacheEntry(lastUsedFrame, currentFrame, staleThresholdFrames)` — a
     pure, `noexcept`, Tier-1-testable free function (`true` iff the idle gap
     is STRICTLY greater than the threshold).
   - A forward-declared, still-opaque `PersistentResourceCacheEntry` struct —
     PHASE4 will add the real `RenderGraphPersistentResourceCache` class into
     this SAME file (never a second header), per this phase's own `.md`.
3. **`CompiledGraphInput::persistentCacheTextures`** (`RenderGraphBuilder.h`) —
   a new `std::vector<TextureHandle>` field, mirroring
   `finalVolumeTextureOutputs`/`finalBufferOutputs`'s exact shape, plus the
   matching private `RenderGraphBuilder::m_persistentCacheTextures` vector,
   moved into `CompiledGraphInput` by `Finish()`. **Deliberately, by design,
   nothing populates this vector yet** — there is no public
   "KeepPersistentTextureOutput()"-style method; population is entirely
   internal to `GetOrCreatePersistentTexture()` itself, which does not exist
   until PHASE8.

Everything above compiles, is unit-tested where testable (Tier-1), and is
completely INERT — an empty `persistentCacheTextures` vector is
indistinguishable from today's behavior everywhere it will later be read
(PHASE3's compiler root-marking fix, PHASE8's honest-layout-recording loop).

## Step 3.1 re-confirmation (done fresh, before editing)

Re-read every file this phase touches, directly, immediately before editing:

- `RenderGraphBuilder.h`'s `CompiledGraphInput` struct — confirmed
  `finalBufferOutputs` at line 195 (matching PHASE0's `~line 195` estimate
  exactly), and the private `m_finalBufferOutputs` at line 690 (matching
  PHASE0's `~line 690` estimate exactly).
- `RenderGraphBuilder.cpp`'s `Finish()` — confirmed the exact existing
  `input.finalVolumeTextureOutputs = std::move(...)` /
  `input.finalBufferOutputs = std::move(...)` lines (236-246) before adding
  the new matching line.
- `CMakeLists.txt` (root) — confirmed `RenderGraphResourcePool.h`/`.cpp` at
  lines 787-788 (`gte_core`'s own production source list) as the insertion
  point for the two new production files.
- `tests/CMakeLists.txt` — confirmed `RenderGraphTypesTests.cpp` (line 2308)
  and `RenderGraphBuilderTests.cpp` are both ALREADY registered — no new test
  `.cpp` file was needed this phase, so no `tests/CMakeLists.txt` edit was
  required at all (Step 4 of this phase's own `.md` explicitly says so).
- `search_in_dir "PersistentResourceCache"` across `src/` — re-confirmed zero
  pre-existing hits before creating the new files (no naming collision).

No ambiguity was found beyond what PHASE0/PHASE1/this phase's own `.md`
already resolve — `ask_questions` was not needed for this phase.

## Changes made

1. **`CMakeLists.txt`** — added
   `src/Renderer/RenderGraph/RenderGraphPersistentResourceOwners.h`,
   `RenderGraphPersistentResourceCache.h`, and `RenderGraphPersistentResourceCache.cpp`
   to the `gte_core` production source list, immediately after
   `RenderGraphResourcePool.cpp`.
2. **`src/Renderer/RenderGraph/RenderGraphPersistentResourceOwners.h`** (new) —
   as specified in this phase's own `.md`, verbatim.
3. **`src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.h`** (new) —
   `PersistentTextureCacheToken`, `IsStaleCacheEntry()`'s declaration, and the
   opaque `PersistentResourceCacheEntry` forward declaration, as specified.
4. **`src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.cpp`** (new) —
   `IsStaleCacheEntry()`'s definition, as specified.
5. **`src/Renderer/RenderGraph/RenderGraphBuilder.h`** —
   - Added `CompiledGraphInput::persistentCacheTextures` immediately after
     `finalBufferOutputs`.
   - Added the private `RenderGraphBuilder::m_persistentCacheTextures` vector
     immediately after `m_finalBufferOutputs`.
   - **No new `#include` was added**, per this phase's own `.md` Section 3.4 —
     `TextureHandle` is already in scope, and the new cache class itself is
     not yet referenced from this file (that's PHASE7/PHASE8's job).
6. **`src/Renderer/RenderGraph/RenderGraphBuilder.cpp`** — `Finish()` gained
   one new line, `input.persistentCacheTextures = std::move(m_persistentCacheTextures);`,
   immediately after the existing `finalBufferOutputs` move.
7. **`tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`** — added a new
   `#include "Renderer/RenderGraph/RenderGraphPersistentResourceCache.h"` plus
   a new test block (mirroring `FindMismatchedColorAttachmentExtent`/
   `IsValidBlitRegion`'s own precedent):
   - `RenderGraphIsStaleCacheEntryTest`: zero-idle (never stale, any
     threshold including 0), exactly-at-threshold (not yet stale — "more
     than", strictly greater), one-frame-beyond-threshold (stale), and a
     large realistic gap (stale) — 5 cases total, exactly matching this
     phase's own `.md` Step 4 list.
   - `RenderGraphPersistentTextureCacheTokenTest`: default-constructed token
     has `entry == nullptr` and `entryEpoch == 0`.
8. **`tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`** — added
   `RenderGraphBuilderTest.FreshBuilderFinishProducesEmptyPersistentCacheTextures`,
   confirming a fresh builder's `Finish()` produces an empty
   `persistentCacheTextures` vector.

No other file was touched. No new test `.cpp` file was created (both tests
landed in already-registered, already-existing test files, per this phase's
own `.md`).

## Note on an editing mistake caught and corrected mid-phase

While using `edit_line` to append the new `IsStaleCacheEntry`/
`PersistentTextureCacheToken` test block to the END of
`RenderGraphTypesTests.cpp`, replacing a single blank line with a
multi-line block that itself ended with the file's own closing
`} // namespace` / `} // namespace gte::rg` lines produced a duplicated
closing-namespace tail (the tool's auto-dedup only checks IMMEDIATE
boundaries, and my replacement's own trailing lines were not adjacent to
the pre-existing ones at the moment of the splice). Caught immediately via
a follow-up `read_line`, and fixed with one more `edit_line` removing the
stale duplicate 4-line tail. A final `read_line` confirms the file now ends
cleanly with exactly one `} // namespace` / `} // namespace gte::rg` pair —
disclosed here for transparency, matching PHASE1's own precedent for this
kind of self-caught mistake.

## Verification

1. **Incremental compile check**: `cmake --build build` — succeeded with
   zero errors (88/88 steps), including `libgte_core.a`, `libgte_editor.a`,
   `GreatTamanaEditor.exe`, `GreatTamanaEngineTests.exe`, and both Project
   Assembly `.dll` pairs.
2. **Targeted `ctest` run**:
   `ctest -C Debug -R "RenderGraphTypesTest|RenderGraphIsStaleCacheEntryTest|RenderGraphPersistentTextureCacheTokenTest|RenderGraphBuilderTest"`
   — **58/58 tests passed**, 0 failed, including:
   - All 5 new `RenderGraphIsStaleCacheEntryTest.*` cases.
   - The new `RenderGraphPersistentTextureCacheTokenTest.DefaultConstructedTokenIsNeverResolvedYet`.
   - The new `RenderGraphBuilderTest.FreshBuilderFinishProducesEmptyPersistentCacheTextures`.
   - Every pre-existing `RenderGraphBuilderTest.*` case (52 of them),
     confirming zero regression to any pre-existing builder behavior.
3. No full clean build, no full `ctest` regression pass was performed
   (correctly deferred to PHASE9, per the campaign's own Rule 3.3.3).

## Honestly-flagged open items

- `persistentCacheTextures`/`m_persistentCacheTextures` remain genuinely
  unused/unpopulated end-to-end after this phase — this is expected and
  correct per this phase's own `.md`, not a gap: population is PHASE8's job
  (`GetOrCreatePersistentTexture()`), and the consuming compiler check is
  PHASE3's job.
- `RenderGraphPersistentResourceCache.h/.cpp` are intentionally PARTIAL —
  they contain no real `RenderGraphPersistentResourceCache` class yet (only
  the token/stale-check/opaque-entry vocabulary). PHASE4 extends this SAME
  file with the real class.
- No other open issues. Every acceptance point this phase's own `.md` file
  lists is satisfied.

## Git

Changes staged and committed together with this report:
- `CMakeLists.txt`
- `src/Renderer/RenderGraph/RenderGraphPersistentResourceOwners.h` (new)
- `src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.h` (new)
- `src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.cpp` (new)
- `src/Renderer/RenderGraph/RenderGraphBuilder.h`
- `src/Renderer/RenderGraph/RenderGraphBuilder.cpp`
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`
- `task_manager/editor-core-separation-27/PHASE2_COMPLETION_REPORT.md`
