# PHASE2 — The Generic Core Facility: `RenderPassGroupRegistry`

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). **Depends on:** `PHASE1` (needs
`RenderPassTag`/`RenderPassTagMask` to already live in `RenderGraphTypes.h`).

---

## Step 1 — The Goal

Add the ONE new, still fully generic Core facility the source strategy document explicitly
calls for (Section 3, Core Campaign 1, bullet 2): a debug-grouping REGISTRY that lets any
Layer-2 module say, from its OWN header/source file, "every pass carrying my tag should be
grouped under this UI heading in the Frame Debugger" — with Core itself never knowing or
caring what that heading's text means, or which feature registered it.

This phase produces **pure, additive vocabulary + mechanism with ZERO real consumers wired
up yet** — exactly this codebase's own established, repeatedly-successful discipline (see
`RenderPipeline.h`'s own PHASE1 in `render-pass-3`, or `RenderGraphBuilder::
CreatePersistentTexture()`'s planned shape in the source strategy document's Campaign 5).
PHASE3 (Atmosphere self-registration) and PHASE4 (the Frame Debugger consumer) are the first
real callers — not this phase.

---

## Step 2 — The Situation

- No such registry exists anywhere in the codebase today — confirmed by searching `src/` for
  `RegisterPassGroupLabel`/`PassGroupLabel`/`FindPassGroup` (zero hits before this phase).
- The closest existing PRECEDENT to copy the *shape* of is `RenderPipeline.cpp`'s own
  `PassIdDebugNameRegistry()` (a debug-build-only function-local-static
  `std::unordered_map<std::uint64_t, const char*>`, with `RegisterPassIdDebugName()`/
  `DebugNameForPassId()` as its public read/write pair) — BUT this new registry must work in
  BOTH debug AND release builds (a release-built Editor's Frame Debugger panel must still
  group correctly; grouping is not a debug-only diagnostic), so it is a genuinely NEW, always-
  compiled facility, not a copy-paste of that `#ifndef NDEBUG` pattern.
- The other close precedent for API shape/style is `RenderPassBlackboard`'s own flat,
  linearly-scanned `std::vector<Slot>` (`RenderPipeline.h`) — deliberately NOT a hashed
  container, because the realistic number of live registered tags is "single digits to low
  tens" (design doc Section 4/11's own stated assumption, which applies here identically).
- `AGENTS.md`'s "Logging" section is the one hard rule this phase must not violate: **never**
  use `std::fprintf`/`std::cerr`/raw C/C++ logging for anything user/AI-agent-visible — use
  `GTE_LOG_WARNING` (`src/Editor/Logger.h`) for the one soft diagnostic this phase needs (a
  tag registered twice under two different labels — see Step 3.2). **Confirmed live, already
  safe — no fallback needed**: `src/Renderer/RenderGraph/RenderGraph.cpp` (a sibling file in
  this exact folder, itself NOT Editor-gated at the file level) already `#include`s
  `"../../Editor/Logger.h"` and calls `GTE_LOG_WARNING(...)` directly (see its own PHASE1,
  `render-pass-6` campaign, item 2.4 — the GPU-timing-slot-budget-overflow warning) and compiles
  fine in both `GTE_ENABLE_EDITOR=ON` and `=OFF` configurations, because the macro itself (not
  the including file) is what branches on `GTE_ENABLE_EDITOR` — `Logger.h` requires zero
  `#ifdef` guarding at the include site. Copy that exact include path/pattern into
  `RenderPassGroupRegistry.cpp` with no further verification needed, and do not add a
  `#ifndef NDEBUG`/`assert()` fallback — it would be unnecessary defensive code for a risk that
  does not actually exist.

---

## Step 3 — The Plan

### 3.1 New files

Create `src/Renderer/RenderGraph/RenderPassGroupRegistry.h` and its matching `.cpp`. Public
API (namespace `gte::rg`, mirroring every sibling file in this folder):

```cpp
#pragma once

#include "RenderGraphTypes.h" // RenderPassTag / RenderPassTagMask (PHASE1 relocated these here)

#include <cstddef>
#include <optional>

namespace gte::rg {

// render-pass-7 campaign (task_manager/render-pass-7), PHASE2 - Core Campaign 1's own new
// generic facility (source doc: CORE_EXPANSION_STRATEGY_v2.md, Section 3, Core Campaign 1).
// Lets ANY Layer-2 module register a human-readable Frame-Debugger-tree heading for its own
// RenderPassTag, from that module's own header/source file - Core itself never learns or
// cares what any tag or heading MEANS. See RenderPassTag's own doc comment
// (RenderGraphTypes.h) for the "no feature-specific tag VALUES live in Core" rule this
// registry exists to serve without breaking.

// Registers (or, if `tag.bit` is already registered, RE-registers under a possibly-different
// `uiHeading`) the heading every surviving pass carrying this tag should be grouped under.
// Idempotent by tag bit - calling this twice for the SAME tag.bit is always safe (e.g. a
// feature class constructed more than once in the same process, or a test re-running this
// call). `uiHeading` MUST be a string-literal/static-storage-duration const char* - never
// owned or copied, mirrors PassRecord::name's own identical rule.
//
// Debug-only correctness net: if `tag.bit` was already registered under a DIFFERENT
// `uiHeading` (compared by pointer OR by content - mirrors RenderPipeline::Unregister()'s own
// "pointer-OR-content match" convention), this logs ONE soft warning - never a crash, never
// an assert - a mislabeled Frame Debugger heading is a cosmetic bug, not a correctness
// hazard, exactly like RenderPassBlackboard::ReportUnusedPublishesIfAny()'s own identical
// "soft, non-fatal" precedent for an analogous developer-facing nuisance.
void RegisterPassGroupLabel(RenderPassTag tag, const char* uiHeading) noexcept;

// Registration-order enumeration - lets a consumer (PHASE4's Frame Debugger) walk every
// registered (tag, label) pair in the EXACT order features registered them, which is what
// produces a deterministic, first-registered-wins Frame Debugger tree bucket ORDER (never
// alphabetical, never tied to real per-frame pass execution order).
std::size_t PassGroupLabelCount() noexcept;
RenderPassTag PassGroupLabelTagAt(std::size_t index) noexcept;   // Precondition: index < PassGroupLabelCount().
const char* PassGroupLabelUiHeadingAt(std::size_t index) noexcept; // Precondition: index < PassGroupLabelCount().

// Returns the registration-order INDEX (suitable for PassGroupLabelUiHeadingAt() above) of
// the FIRST registered entry whose tag bit is set anywhere in `tags`, or std::nullopt if
// `tags` carries no registered bit at all (the common "untagged"/"tagged but nobody
// registered a heading for it" case - e.g. GPU Skinning's own tag today, PHASE3). If a pass
// somehow carries more than one REGISTERED tag bit at once, the FIRST-REGISTERED one wins -
// a documented, deterministic tie-break, not a real scenario in production today.
std::optional<std::size_t> FindPassGroupIndexForTags(RenderPassTagMask tags) noexcept;

// Testing-only (mirrors this codebase's own "...ForTesting()" convention, e.g.
// FrameProfiler::ResetForTesting() / RenderPassBlackboard::SlotCapacityForTesting()) - clears
// every registered entry. REQUIRED so Tier-1 tests never leak state into each other via this
// otherwise-global, process-lifetime registry - call this at the start of every test that
// touches this registry (directly, or indirectly via constructing a self-registering class
// like AtmosphereLutRenderer, PHASE3).
void ResetPassGroupRegistryForTesting() noexcept;

} // namespace gte::rg
```

### 3.2 Implementation shape (`.cpp`)

A single function-local static `std::vector<Entry>` (`struct Entry { RenderPassTag tag;
const char* uiHeading; };`), NOT `#ifndef NDEBUG`-gated (unlike `PassIdDebugNameRegistry()` —
this facility must work identically in release, since it drives real, always-visible Editor
UI). `RegisterPassGroupLabel()` linearly scans for an existing entry with the same
`tag.bit`; if found, compares `uiHeading` (pointer-then-content, mirroring
`RenderPipeline::Unregister()`'s own established match logic) and either no-ops (same
heading) or updates in place plus logs the soft warning described above (different heading);
if not found, appends a new entry. `FindPassGroupIndexForTags()` linearly scans in
registration order and returns the first index whose `tag.bit & tags` is nonzero.

### 3.3 Tier-1 tests — new file `tests/Renderer/RenderGraph/RenderPassGroupRegistryTests.cpp`

Mirror this folder's existing test-file conventions (see any sibling `*Tests.cpp` in
`tests/Renderer/RenderGraph/` for the exact GTest include/namespace-using pattern). Every
test MUST call `ResetPassGroupRegistryForTesting()` first (this is genuinely global,
process-lifetime state — GTest does not guarantee test ordering/isolation for you here).
Required coverage:

- Registering one tag/heading pair makes `PassGroupLabelCount() == 1`,
  `PassGroupLabelTagAt(0).bit` matches, `PassGroupLabelUiHeadingAt(0)` matches.
- `FindPassGroupIndexForTags()` returns the correct index when the queried mask has that
  exact bit set, `std::nullopt` when it has an unrelated/no bit set, and correctly resolves
  a mask carrying SEVERAL bits where only one is registered.
- Registering the SAME `tag.bit` twice with the SAME heading does not grow
  `PassGroupLabelCount()` (idempotent).
- Registering the SAME `tag.bit` twice with a DIFFERENT heading updates
  `PassGroupLabelUiHeadingAt()` to the new value and still does not grow the count (still one
  entry, content updated) — do not assert on the soft warning's exact text/log call itself
  (that would be brittle); just assert the observable state (heading updated, count
  unchanged).
- Two DIFFERENT tags registered in a specific order preserve that exact order via
  `PassGroupLabelTagAt(0)`/`(1)` — this is the load-bearing "first-registered-wins ordering"
  guarantee PHASE4 depends on.
- `ResetPassGroupRegistryForTesting()` itself actually clears everything (`PassGroupLabelCount()
  == 0` immediately after).

### 3.4 Build + verify

Add the new `.cpp`/test file to whatever build-file mechanism this codebase uses to enumerate
sources (check `CMakeLists.txt`/`tests/CMakeLists.txt` for whether sources are globbed or
listed explicitly — mirror whichever convention every sibling file in
`src/Renderer/RenderGraph/`/`tests/Renderer/RenderGraph/` already uses). Incremental build,
then run just this new test file's cases (targeted `ctest -R RenderPassGroupRegistry` or
equivalent `--gtest_filter`).

### 3.5 Report

Write `PHASE2_COMPLETION_REPORT.md` in this same folder; commit via `git_add`/`git_commit`.

### Acceptance bar for this phase

- New facility compiles in both a normal (`GTE_ENABLE_EDITOR=ON`) build and, if quick to
  verify, a `GTE_ENABLE_EDITOR=OFF` build (`build-editor-off`, per this repo's own existing
  second build directory) — confirm it does NOT accidentally pull in an Editor-only header
  transitively; this facility must be usable by a Renderer-layer feature (Atmosphere, GPU
  Skinning) regardless of whether the Editor module is compiled in at all.
- Zero existing call site anywhere touched — this phase is 100% new files.
- All new Tier-1 tests pass; nothing else in the suite regresses.
