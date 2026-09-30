# PHASE2 — New Vocabulary: Owners Header, `PersistentTextureCacheToken`, `IsStaleCacheEntry()`, and the Builder-Side Root List

Campaign folder: `task_manager/editor-core-separation-27/`

## Parent — MUST READ
`PHASE0_MASTER_STRATEGY.md`. Also re-read
`PHASE1_COMPLETION_REPORT.md` for any drift in `RenderTexture`/
`GpuResourceFactory` signatures (should be none relevant to this phase, but
confirm).

## Step 1: The Goal (Where are we going?)

Add every small, standalone piece of new VOCABULARY this campaign needs —
none of which requires a live `VkDevice` or the (not-yet-built)
`RenderGraphPersistentResourceCache` class to exist — so later phases have
stable types/fields to build against:

1. A new, tiny, standalone header,
   `src/Renderer/RenderGraph/RenderGraphPersistentResourceOwners.h`, holding
   every BUILT-IN owner-identifier constant this engine has (empty except
   for one placeholder today — see Step 3).
2. `PersistentTextureCacheToken` — a small, POD, caller-owned, default-
   constructible token struct (source document Section 4).
3. `IsStaleCacheEntry()` — a pure, Tier-1-testable free function (source
   document Section 8) deciding "is this entry idle long enough to evict".
4. `CompiledGraphInput::finalOutputs`'s sibling for persistent textures:
   `CompiledGraphInput::persistentCacheTextures` (a plain
   `std::vector<TextureHandle>`), plus the matching builder-private
   `RenderGraphBuilder::m_persistentCacheTextures` vector, moved into
   `CompiledGraphInput` by `Finish()` — mirroring
   `finalVolumeTextureOutputs`/`finalBufferOutputs`'s exact, already-shipped
   three-line shape.

When this phase is done: everything above compiles, is unit-tested where
testable (Tier-1), and is completely INERT — nothing yet populates
`persistentCacheTextures` (no public method pushes onto it), nothing yet
constructs a `RenderGraphPersistentResourceCache`, and every pre-existing
call site is completely unaffected (an empty `persistentCacheTextures`
vector is indistinguishable from today's behavior everywhere it will later
be read).

## Step 2: The Situation (Where are we now?)

- `RenderGraphBuilder.h`'s `CompiledGraphInput` struct (~line 153-196)
  already carries the EXACT precedent to copy twice over:
  `finalVolumeTextureOutputs` (~line 178, Atmosphere Scattering Phase 6)
  and `finalBufferOutputs` (~line 195, `editor-core-separation-26` PHASE1).
  Both are declared as `std::vector<XHandle>`, populated by a
  `RenderGraphBuilder::KeepXOutput(XHandle)` public method that does one
  `push_back` onto a matching private `m_finalXOutputs` vector (~line
  686/690), moved into `CompiledGraphInput` inside `Finish()`
  (`RenderGraphBuilder.cpp` — confirm the exact `Finish()` body/line before
  editing).
- **Deviation from this precedent, on purpose**: `persistentCacheTextures`
  is populated INTERNALLY by `GetOrCreatePersistentTexture()` itself
  (PHASE8), never by a caller-visible `KeepPersistentTextureOutput()`-style
  public method — the source document's own Section 5.2 is explicit that
  "no new public 'keep this alive' call is needed... every handle either
  `GetOrCreatePersistentTexture()` overload mints is automatically,
  unconditionally a required root". This phase therefore adds the FIELD
  and the PRIVATE vector only — the public push happens in PHASE8, inside
  `GetOrCreatePersistentTexture()`'s own body, once that method exists.
  This phase's own vector therefore stays genuinely empty/unused end-to-end
  until PHASE8 lands — that is expected and correct, not a mistake.
- `RenderFeatureNamePool.h` (`src/Core/Plugins/`) already establishes BOTH
  precedents Section 6.2 of the source document asks this campaign to
  apply to `RenderGraphPersistentResourceOwners.h`: (a) a plugin's own
  `descriptor.name` is already unique by construction (population (a) —
  needs NO new owners-header entry, ever — a `_v2`/`_v3` plugin or Project
  Assembly feature passes its OWN name directly as `owner`), and (b) a
  small, bounded, centrally-declared, single-file set of keys
  (`"ProjectFeatureSlot0".."15"`) for the case where a raw, unbounded,
  caller-typed string would otherwise be the only option (population (b)
  — built-in engine features). `RenderGraphPersistentResourceOwners.h`
  is the SAME demotion applied to THIS feature's own `owner` parameter for
  population (b) specifically.
- No real built-in consumer (TAA/SSR/etc.) exists in this engine yet — so
  this header starts with ZERO real production constants. It must still
  exist as a real file (not deferred to "whenever the first consumer is
  written"), because THIS CAMPAIGN's own new automated Tier-2 test suite
  (PHASE4 onward, `RenderGraphPersistentResourceCacheTests.cpp`) is itself
  a real, permanent, built-in "population (b)" caller — it needs ONE real
  constant to use as its own `owner` when exercising the built-in-owner
  population's own code path (as opposed to its OTHER tests, which
  deliberately use raw, ad hoc, plugin-style literal owner strings like
  `"OwnerA"`/`"OwnerB"` to mimic Section 6.2 population (a) — a `_v2`/`_v3`
  plugin's own `descriptor.name` is exactly this kind of raw runtime
  string, so using literals there is MORE representative, not a shortcut).
  Add exactly one constant, named for what it is (a test-fixture owner,
  not a placeholder): `kPersistentOwnerCacheValidation`. Nothing else. No
  new, permanent, production `RenderPassCategory::Debug` validation pass is
  built for this campaign (unlike `editor-core-separation-26`'s
  `BlitValidation`) — Locked Decision 3 already makes the automated
  headless test suite this campaign's PRIMARY, permanent proof mechanism,
  so a second, redundant, always-running production pass would add
  maintenance cost for no additional coverage; PHASE9's own live-Editor
  check is explicitly a cheap, secondary sanity pass, not a new permanent
  fixture.
- `RenderGraphNameSlotTable.h`'s existing `kNoNameSlot`/general "small
  header, deliberately no logic beyond named constants" shape
  (`RenderGraphTypes.h` for similarly-shaped sentinels like `kInvalidIndex`)
  is the style precedent for this new header — plain
  `inline constexpr const char*` constants, a short file-header comment,
  nothing else.
- `RenderGraphTypes.h`'s general house style ("pure, Tier-1-testable free
  function extracted beside the type it decides about" —
  `FindMismatchedColorAttachmentExtent()`, `IsValidBlitRegion()`) is the
  precedent `IsStaleCacheEntry()` follows. It does NOT need to live in
  `RenderGraphTypes.h` itself (it has nothing to do with that file's own
  vocabulary) — it lives directly inside the new
  `RenderGraphPersistentResourceCache.h` (declared) — but since PHASE4 has
  not created that file yet at this point in the campaign, **this phase
  creates `RenderGraphPersistentResourceCache.h` itself, containing ONLY
  `IsStaleCacheEntry()` and `PersistentTextureCacheToken` for now** — PHASE4
  ADDS the real `RenderGraphPersistentResourceCache` class into this SAME,
  already-existing file (never a second header).

## Step 3: The Plan

### 3.1 — `RenderGraphPersistentResourceOwners.h` (new file)

```cpp
#pragma once

// editor-core-separation-27 campaign (BIG STEP 3 of 4 -
// BIG_STEP_3_PERSISTENT_RESOURCE_CACHE_HONEST_LAYOUT_HISTORY_REV2_2026-09-30.txt,
// Section 6.2) - every BUILT-IN engine feature's RenderGraphPersistentResourceCache
// `owner` identifier, as a single, flat, always-reviewed list. A built-in
// feature's call to RenderGraphBuilder::GetOrCreatePersistentTexture() MUST
// pass one of these constants, never a locally hand-typed literal -
// enforced by code-review convention only (a `const char*` parameter
// cannot itself forbid an arbitrary literal - see TR9). Adding a new
// built-in owner is a ONE-LINE addition to this ONE file - a collision
// between two built-in features would require the SAME line to be added
// twice to the SAME file, a mistake any reviewer glancing at this small,
// append-only header is positioned to catch immediately.
//
// A `_v2`/`_v3` plugin or Project Assembly render feature NEVER adds an
// entry here - its own `owner` argument is always its own already-unique
// `descriptor.name` (mechanically enforced elsewhere - see
// RenderFeatureCompositor.h/PluginHost) - see Section 6.2 population (a).

namespace gte::rg {

// This campaign's own automated Tier-2 test suite
// (RenderGraphPersistentResourceCacheTests.cpp, PHASE4 onward) - the ONE
// real, built-in "population (b)" consumer this campaign itself ships.
inline constexpr const char* kPersistentOwnerCacheValidation = "PersistentResourceCacheValidation";

} // namespace gte::rg
```

### 3.2 — `RenderGraphPersistentResourceCache.h` (new file, PARTIAL for now)

```cpp
#pragma once

#include <cstdint>

namespace gte::rg {

// Opaque outside RenderGraphPersistentResourceCache.cpp - see PHASE4.
struct PersistentResourceCacheEntry;

// Source document Section 4 - a small, POD, caller-owned token. Default-
// constructed as "never resolved yet" (entry == nullptr). Safe to keep,
// copy, and reuse across any number of frames.
struct PersistentTextureCacheToken {
    PersistentResourceCacheEntry* entry = nullptr;
    std::uint64_t entryEpoch = 0;
};

// Source document Section 8 - pure, Tier-1-testable. True iff `lastUsedFrame`
// is more than `staleThresholdFrames` frames behind `currentFrame`.
bool IsStaleCacheEntry(
    std::uint64_t lastUsedFrame, std::uint64_t currentFrame, std::uint64_t staleThresholdFrames) noexcept;

} // namespace gte::rg
```

`RenderGraphPersistentResourceCache.cpp` (new file, PARTIAL for now) holds
only `IsStaleCacheEntry()`'s definition:

```cpp
#include "RenderGraphPersistentResourceCache.h"

namespace gte::rg {

bool IsStaleCacheEntry(
    std::uint64_t lastUsedFrame, std::uint64_t currentFrame, std::uint64_t staleThresholdFrames) noexcept
{
    return (currentFrame - lastUsedFrame) > staleThresholdFrames;
}

} // namespace gte::rg
```

Both new files must be added to the root `CMakeLists.txt`'s SOURCE list
(not the test list yet — this is production code) — locate the existing
`RenderGraphResourcePool.cpp`/`.h` entries and add these two right beside
them, in the same `gte_core` target section.

### 3.3 — `CompiledGraphInput::persistentCacheTextures` (`RenderGraphBuilder.h`)

Add, immediately after `finalBufferOutputs` (~line 195):

```cpp
    // editor-core-separation-27 campaign, PHASE2/PHASE8
    // (BIG_STEP_3_PERSISTENT_RESOURCE_CACHE_HONEST_LAYOUT_HISTORY_REV2_2026-09-30.txt,
    // Section 5.1/5.2) - every handle either GetOrCreatePersistentTexture()
    // overload mints (PHASE8) is pushed here too, alongside its normal
    // TextureSlot entry - this is what RenderGraphCompiler::Compile()'s
    // root-marking scan (PHASE3) treats as an ALWAYS-required root, with
    // zero action needed from the pass author, and what
    // RenderGraph::ExecuteCompiledGraph()'s own honest-layout-recording
    // loop (PHASE8) walks at the end of every call. UNLIKE
    // finalVolumeTextureOutputs/finalBufferOutputs above, nothing outside
    // RenderGraphBuilder ever pushes onto this directly - there is no
    // public "KeepPersistentTextureOutput()" method; population is
    // entirely internal to GetOrCreatePersistentTexture() itself (PHASE8).
    std::vector<TextureHandle> persistentCacheTextures;
```

Add, in `RenderGraphBuilder`'s private section, immediately after
`m_finalBufferOutputs` (~line 690):

```cpp
    // editor-core-separation-27 campaign, PHASE2/PHASE8 - see
    // CompiledGraphInput::persistentCacheTextures above.
    std::vector<TextureHandle> m_persistentCacheTextures;
```

Update `RenderGraphBuilder::Finish()` (`RenderGraphBuilder.cpp`) to move
this new vector into `CompiledGraphInput` exactly like
`m_finalVolumeTextureOutputs`/`m_finalBufferOutputs` already are — locate
the exact existing lines first via `read_line`, then add one matching
`input.persistentCacheTextures = std::move(m_persistentCacheTextures);`
line.

### 3.4 — Include wiring

`RenderGraphBuilder.h` needs a new `#include "RenderGraphPersistentResourceCache.h"`
only once PHASE7/PHASE8 actually reference `RenderGraphPersistentResourceCache*`
as a member — **do NOT add this include in THIS phase** (it would be
unused, and this phase's own scope is deliberately just the plain-data
`persistentCacheTextures` vector, which needs no new include at all,
`TextureHandle` already being in scope). Confirm this phase's diff needs
zero new `#include` anywhere in `RenderGraphBuilder.h`/`.cpp`.

## Step 4: Required Tests

All Tier-1 (pure, no GPU):

1. `IsStaleCacheEntry()` — add a new, small test block to
   `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` (the natural
   home for small pure decision functions in this module — confirm this
   file already exists and is already registered in `CMakeLists.txt`, no
   new test file needed here):
   - `lastUsedFrame == currentFrame` (0 frames idle) → `false` regardless
     of threshold (unless threshold is 0, an edge case worth its own
     assertion).
   - `currentFrame - lastUsedFrame == staleThresholdFrames` (exactly at
     the boundary) → `false` (source document: "more than", strictly
     greater).
   - `currentFrame - lastUsedFrame == staleThresholdFrames + 1` → `true`.
   - A large, realistic gap (e.g. `currentFrame - lastUsedFrame == 10000`,
     `staleThresholdFrames == 300`) → `true`.
2. `CompiledGraphInput::persistentCacheTextures` / builder-side plumbing —
   add ONE small test to `RenderGraphBuilderTests.cpp` confirming: a fresh
   `RenderGraphBuilder`'s `Finish()` produces a `CompiledGraphInput` whose
   `persistentCacheTextures` is empty (nothing populates it yet, this
   phase adds no public API to do so) — this is a cheap, direct proof that
   the new field/move-wiring compiles and behaves as an inert no-op,
   exactly as intended for this phase.
3. `PersistentTextureCacheToken` — confirm (via a `static_assert` or a
   trivial test) that `PersistentTextureCacheToken{}` default-constructs
   with `entry == nullptr` and `entryEpoch == 0` — this is the exact
   "never resolved yet" state every later phase's fast-path logic depends
   on.

No new test `.cpp` file is needed for this phase — every test above lands
in an already-registered, already-existing test file.

## Step 5: Rules recap

See `PHASE0_MASTER_STRATEGY.md`, Section 3.3, in full. In particular:
this phase adds TWO new production source files
(`RenderGraphPersistentResourceOwners.h`,
`RenderGraphPersistentResourceCache.h/.cpp`) — Rule 10 permits exactly
these (named explicitly in PHASE0's own Rule 10 list); do not add any
OTHER new file. Incremental build + targeted `ctest -R
RenderGraphTypesTest` / `-R RenderGraphBuilderTest` only. End with
`PHASE2_COMPLETION_REPORT.md` + git commit.
