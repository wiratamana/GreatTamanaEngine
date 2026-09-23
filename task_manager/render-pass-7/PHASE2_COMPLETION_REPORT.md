# PHASE2 — The Generic Core Facility: `RenderPassGroupRegistry` — COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. **Phase doc followed:** `PHASE2_PASS_GROUP_REGISTRY.md`.
**Depends on:** PHASE1 (`RenderPassTag`/`RenderPassTagMask` already relocated into
`RenderGraphTypes.h`) — confirmed already landed by reading `PHASE1_COMPLETION_REPORT.md` and
the live `RenderGraphTypes.h` source before starting.

## Summary

Implemented exactly what the phase document's Step 3 specified: a brand-new, generic Core
facility, `RenderPassGroupRegistry.h/.cpp`, that lets any Layer-2 module register a
human-readable Frame Debugger tree heading for its own `RenderPassTag`, with Core itself never
learning or caring what any tag or heading means. This phase is **pure, additive vocabulary +
mechanism with ZERO real consumers wired up yet** — no existing call site anywhere in the
engine was touched. PHASE3 (Atmosphere self-registration) and PHASE4 (the Frame Debugger
consumer) will be the first real callers.

## Changes made (file by file)

1. **`src/Renderer/RenderGraph/RenderPassGroupRegistry.h`** (new) — public API in namespace
   `gte::rg`, matching the phase document's Step 3.1 signature exactly:
   - `void RegisterPassGroupLabel(RenderPassTag tag, const char* uiHeading) noexcept;`
   - `std::size_t PassGroupLabelCount() noexcept;`
   - `RenderPassTag PassGroupLabelTagAt(std::size_t index) noexcept;`
   - `const char* PassGroupLabelUiHeadingAt(std::size_t index) noexcept;`
   - `std::optional<std::size_t> FindPassGroupIndexForTags(RenderPassTagMask tags) noexcept;`
   - `void ResetPassGroupRegistryForTesting() noexcept;`
   Doc comments carried over verbatim from the phase document (registration-order enumeration
   contract, first-registered-wins tie-break, idempotent-by-tag-bit registration, testing-only
   reset).

2. **`src/Renderer/RenderGraph/RenderPassGroupRegistry.cpp`** (new) — implementation shape per
   Step 3.2:
   - A single function-local static `std::vector<Entry>` (`struct Entry { RenderPassTag tag;
     const char* uiHeading; }`), **NOT** `#ifndef NDEBUG`-gated (unlike
     `RenderPipeline.cpp`'s `PassIdDebugNameRegistry()`), since this facility must work
     identically in release builds — it drives real, always-visible Editor UI.
   - `RegisterPassGroupLabel()` linearly scans for an existing entry with the same `tag.bit`;
     if found, compares `uiHeading` via a pointer-then-content `HeadingsMatch()` helper
     (mirroring `RenderPipeline::Unregister()`'s own established match logic) and either
     no-ops (same heading) or updates in place and logs one soft `GTE_LOG_WARNING` (different
     heading); if not found, appends a new entry.
   - `FindPassGroupIndexForTags()` linearly scans in registration order and returns the first
     index whose `tag.bit & tags` is nonzero.
   - `#include "../../Editor/Logger.h"` copied verbatim from `RenderGraph.cpp`'s own confirmed-
     safe precedent (the phase document explicitly said no further verification was needed for
     this include path/pattern) — compiles fine in both `GTE_ENABLE_EDITOR=ON` and `=OFF`
     (see Build result below).

3. **`tests/Renderer/RenderGraph/RenderPassGroupRegistryTests.cpp`** (new) — 9 tests, mirroring
   this folder's established GTest include/namespace pattern
   (`RenderGraphDebugVolumeTextureRegistryTests.cpp` was used as the direct style precedent).
   Every test calls `ResetPassGroupRegistryForTesting()` first, per the phase document's Step
   3.3 requirement (this is genuinely global, process-lifetime state). Coverage exactly matches
   the phase document's required list:
   - `RegisteringOneTagMakesCountOneAndRoundTripsFields`
   - `FindPassGroupIndexForTagsReturnsCorrectIndexForExactBit`
   - `FindPassGroupIndexForTagsReturnsNulloptForUnrelatedBit`
   - `FindPassGroupIndexForTagsResolvesMaskWithOnlyOneRegisteredBit`
   - `RegisteringSameTagTwiceWithSameHeadingIsIdempotent`
   - `RegisteringSameTagTwiceWithDifferentHeadingUpdatesInPlace`
   - `TwoDifferentTagsPreserveRegistrationOrder`
   - `FindPassGroupIndexForTagsFirstRegisteredWinsWhenMaskCarriesBoth`
   - `ResetForTestingActuallyClearsEverything`

4. **`CMakeLists.txt`** (root) — added `src/Renderer/RenderGraph/RenderPassGroupRegistry.h` and
   `.cpp` to the explicit source list, immediately after `RenderPipeline.cpp` (this codebase
   lists sources explicitly rather than globbing — confirmed by checking for `file(GLOB...)`
   usage in the main build files before editing).

5. **`tests/CMakeLists.txt`** — added `Renderer/RenderGraph/RenderPassGroupRegistryTests.cpp` to
   the explicit test source list immediately after `RenderPipelineTests.cpp`, plus a matching
   documentation-comment entry in this file's own top-of-file per-test-file description block
   (mirroring every sibling entry's own descriptive style), for consistency with how every other
   test file in this list is documented.

## Deviations from the phase document

None. Every new file/API/test matches the phase document's Step 3 instructions exactly,
including the exact function signatures, doc-comment wording, and required test coverage list.

## Build & test result

- **Configure** (`cmake -S . -B build`): succeeded (pre-existing, unrelated KTX git-describe
  warning only, present before this phase too).
- **Incremental compile** (`cmake --build build --target gte_core`): succeeded, zero new
  warnings/errors — 2 objects built (`RenderPassGroupRegistry.cpp.obj` + relink).
- **Test binary rebuild** (`cmake --build build --target GreatTamanaEngineTests`): succeeded,
  zero new warnings/errors.
- **Targeted test run** (`GreatTamanaEngineTests.exe --gtest_filter="RenderPassGroupRegistryTest.*"`):
  **9/9 tests passed.**
- **Broader safety-net run** (`--gtest_filter="*RenderGraph*:*RenderPass*:*RenderPipeline*"`):
  **279/279 tests passed** — confirms zero regression in any sibling Render Graph test suite
  from this phase's purely-additive edits.
- **`GTE_ENABLE_EDITOR=OFF` build check** (`cmake --build build-editor-off --target gte_core`):
  succeeded, zero warnings/errors — confirms the new facility compiles cleanly with the Editor
  module disabled and does NOT accidentally pull in an Editor-only header transitively (the
  `Logger.h` include is safe in both configurations by design, per `AGENTS.md`'s "Logging"
  section — the macro itself, not the including file, branches on `GTE_ENABLE_EDITOR`).
- Per this campaign's own process rules, the FULL `ctest` suite was deliberately **not** run —
  that is PHASE5's job. No full/clean build was performed either, only incremental checks.

## Acceptance bar check (against the phase document's own criteria)

- ✅ New facility compiles in both `GTE_ENABLE_EDITOR=ON` (`build`) and `=OFF`
  (`build-editor-off`) configurations.
- ✅ Zero existing call site anywhere touched — this phase is 100% new files (plus the two
  build-file source-list additions, which are purely additive too).
- ✅ All new Tier-1 tests pass (9/9); nothing else in the Render Graph test suite regressed
  (279/279).

PHASE2 is complete and ready for PHASE3
(`PHASE3_DEHARDCODE_CATEGORY_AND_MIGRATE_CALL_SITES.md`).
