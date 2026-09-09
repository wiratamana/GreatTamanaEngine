# PHASE1 — Debug Texture Registry: Core Data Model

> **Second-iteration audit note (this revision):** this file was re-checked
> against the live source tree (`RenderGraphNameSlotTable.h`,
> `RenderGraphBarrierPlanner.h`'s real `ResourceState` shape,
> `RenderGraph.h`'s real `ExecuteTimingMode` enum, `RenderTarget.h`) and
> against the actual test-fixture conventions already used elsewhere in this
> codebase. Every structural claim (the `ResourceState`/`RenderTarget`
> shapes, the `RenderGraphNameSlotTable.h`/`RenderGraphSnapshot.h`
> precedents, the forward-declared-enum approach) checked out correct — no
> design change was needed. Three concrete, narrow corrections were made:
> 1. The forward-declared `ExecuteTimingMode` question (previously left as
>    an open "confirm with a real compile" hedge) has now actually been
>    verified with a standalone reproduction compile — see the updated note
>    at the end of Step 3.1: it compiles cleanly, no fallback header split
>    is needed.
> 2. Step 3.4's test guidance previously suggested fabricating a fake
>    `VkImage` via `reinterpret_cast<VkImage>(1)` — that does NOT match this
>    codebase's own established convention for fabricating Vulkan handles in
>    a Tier-1 test (`reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(...))`,
>    confirmed live in `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`'s
>    `MakeFakeSwapchainTarget()`/`MakeFakeGameViewTarget()` and
>    `RenderGraphBarrierPlannerTests.cpp`) — corrected to match, and a
>    reusable fake-snapshot helper is now suggested to cut down repetition
>    across the several assertions this phase's test file needs.
> 3. Step 3.3 never actually said to register the new test file in
>    `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list (only the main
>    `CMakeLists.txt`'s source list was covered) — a real gap that would have
>    left the new test file compiled nowhere and silently never run,
>    violating `AGENTS.md`'s own "every change to Tier 1 code must come with
>    a matching test change" rule in spirit even though a test file would
>    technically exist on disk. Now called out explicitly.

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: nothing (this is the first,
purely-data phase — mirrors `network-impl-2/PHASE1_THIRDPARTY_STB_IMAGE_WRITE_AND_PURE_ENCODING_UTILITIES.md`'s
own "pure utilities first, wired into anything live later" shape).

## Step 1: The Goal (Where are we going?)

Build a small, self-contained, Tier-1-testable (no live `VkDevice`/
`VmaAllocator` needed at all) class that remembers, keyed by a texture's own
human-readable name, everything a later phase needs to read its pixels back:
which Vulkan image/format/extent it currently is, its last-known
`ResourceState` (layout/stage/access — for both its color image and, if it
has one, its companion depth image), which `ExecuteTimingMode` regime last
touched it, and how long ago (in "engine frames", per
`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 4) it was last updated.
This class knows NOTHING about `RenderGraph`/`Renderer`/Vulkan device calls —
it is handed already-resolved, plain data and just remembers/serves it back.

## Step 2: The Situation (Where are we now?)

- `src/Renderer/RenderGraph/RenderGraphNameSlotTable.h` is the closest
  existing precedent in this codebase for "a small, persistent, name-keyed
  table, deliberately extracted into its own tiny header specifically so it
  is Tier-1-testable with no live device at all" (see its own header
  comment) — this phase's class follows the same spirit, but stores a real
  VALUE per name (a whole snapshot struct), not just an assigned integer
  slot, and never has a fixed budget/never runs out (`std::vector`, grown
  on demand — this engine declares a small, single-digit number of distinct
  texture names per frame today, and even a future atmosphere-scattering
  campaign adding a handful of LUT names is nowhere near a scale that needs
  a hash map — see `AGENTS.md`'s "no hashing on the hot path, plain vector
  scan" convention, e.g. `RenderGraph::m_lastKnownStats` already does
  exactly this for pass names — confirmed live: `UpdateDrawStatsFor()`/
  `UpdateTimingFor()`/`LastKnownStatsFor()` in `RenderGraph.cpp` all do a
  plain linear scan, pointer-compare-then-`strcmp()`-fallback, over a
  `std::vector<NamedStats>`).
- `src/Renderer/RenderGraph/RenderGraphBarrierPlanner.h` already declares
  `struct ResourceState { VkImageLayout layout; VkPipelineStageFlags2
  stageMask; VkAccessFlags2 accessMask; };` — this phase's snapshot struct
  reuses this EXACT type for both its color and depth state fields, rather
  than inventing a parallel one. (Confirmed live: the real struct also has
  default member initializers plus a defaulted `operator==` — irrelevant to
  this phase's own usage, but harmless either way.)
- `src/Renderer/RenderTarget.h`'s `RenderTarget` struct is the exact,
  existing "plain, non-owning bag of an image/view/extent/format (+ an
  optional depth counterpart)" shape this phase's snapshot needs to carry —
  reused directly rather than re-declaring the same six-ish fields by hand.
  (Confirmed live: `image`/`imageView`/`extent`/`format` plus
  `depthImage`/`depthImageView`/`depthFormat`/`depthHasStencil` — exactly
  the fields this phase's own doc comments assume.)
- `src/Renderer/RenderGraph/RenderGraph.h`'s `ExecuteTimingMode` enum
  (`SynchronousImmediateReadback`/`PipelinedDeferredReadback`) is the exact,
  existing vocabulary for "which regime" — reused directly. (Confirmed
  live: `enum class ExecuteTimingMode : std::uint8_t { ... }`, declared
  inside `namespace gte::rg { ... }` — matching the forward declaration
  this phase's header uses; see Step 3.1's own closing note below for why
  that forward declaration is now confirmed safe.)
- This file must live under `src/Renderer/RenderGraph/` (not `src/Encoding/`
  or `src/Application/`) since it is conceptually part of the render
  graph's own vocabulary (mirrors where `RenderGraphSnapshot.h` — a
  similarly "pure reshape of render-graph-internal data for an external
  consumer" module — already lives, confirmed present in that same folder),
  namespaced `gte::rg` like everything else in that folder.
- Downstream confirmation: Phase 2's population loop
  (`RenderGraph::ExecuteCompiledGraph()`) reads from the engine's real,
  private `PhysicalTexture` struct — confirmed live in `RenderGraph.h` as
  `{ bool resolved; bool isImported; bool hasDepth; RenderTarget target;
  VkSampler sampler; ResourceState colorState; ResourceState depthState; }`
  — every field this phase's `DebugTextureSnapshot` needs
  (`target`/`hasDepth`/`colorState`/`depthState`) is present there with
  matching names/types, so Phase 2's `Upsert()` call site can copy them
  straight across with no reshaping. Phases 4/5 (`FrameCaptureBridge`'s
  named-texture capture path and the `/get_texture`/`/list_textures` routes)
  were also re-checked against this phase's exact field list and need
  nothing this phase doesn't already provide.

## Step 3: The Plan

### 3.1 — `src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h`

```cpp
#pragma once

// network-impl-4 campaign, Phase 1
// (task_manager/network-impl-4/PHASE1_DEBUG_TEXTURE_REGISTRY_CORE_DATA_MODEL.md) -
// the pure, Vulkan-header-only (no live VkDevice/VmaAllocator anywhere in
// this file) name -> physical-texture-snapshot table behind GET
// /get_texture and GET /list_textures. Deliberately its own tiny,
// Tier-1-testable module (mirrors RenderGraphNameSlotTable.h's own "small,
// persistent, name-keyed table, extracted into its own header specifically
// so it's directly testable with hand-fabricated inputs" precedent) - see
// RenderGraph.h for how this is actually kept up to date every frame
// (Phase 2) and Application.cpp for the one, narrow, per-call-site
// "correction" hook a graph-external manual barrier needs (Phase 3).
//
// Every texture RenderGraphBuilder::CreateTexture()/ImportTexture() ever
// declares, in EITHER ExecuteTimingMode regime, is automatically eligible
// to appear here - see PHASE0_MASTER_STRATEGY.md's Locked Design Decision 6.
// This class itself has no opinion about HOW it gets populated - it is a
// dumb, passive store; RenderGraph (Phase 2) is what actually calls
// Upsert() every ExecuteCompiledGraph() call.

#include "RenderGraphBarrierPlanner.h"
#include "../RenderTarget.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte::rg {

enum class ExecuteTimingMode : std::uint8_t; // see RenderGraph.h - forward-declared here to avoid a circular include; both enumerators are re-declared nowhere else. Confirmed safe by a standalone compile check - see this section's own closing note below.

// One texture's full current knowledge - color required, depth optional.
// Deliberately a plain, copyable value (no pointers/handles owned) - safe
// to hand back out of the registry by value with no lifetime concerns
// (mirrors RenderGraphSnapshot.h's own "every string/value copied, never
// referenced" rule, applied here to Vulkan handles instead of strings -
// note a VkImage/VkImageView handle copied out of this registry is only
// meaningful for as long as the real underlying resource is still alive,
// exactly like any other raw Vulkan handle this engine already threads
// around by value, e.g. RenderTarget itself).
struct DebugTextureSnapshot {
    std::string name;
    ExecuteTimingMode regime{};

    RenderTarget target; // target.image/imageView/extent/format is the COLOR half; target.depthImage/depthImageView/depthFormat is the optional depth half (see RenderTarget.h).
    bool hasDepth = false;

    ResourceState colorState; // layout/stage/access this registry LAST believes the color image is actually in.
    ResourceState depthState; // meaningful only when hasDepth is true.

    // The value of the registry's own monotonically increasing frame
    // counter (see RenderGraph::Upsert() call site, Phase 2) at the moment
    // this entry was last written by Upsert() - NOT touched by
    // ApplyStateOverride() (see below), which corrects state only, never
    // "freshness" (a graph-external manual barrier is not a new capture of
    // the texture's CONTENTS, just a bookkeeping fix for its LAYOUT - see
    // PHASE0_MASTER_STRATEGY.md's own Step 2 analysis).
    std::uint64_t lastUpdatedFrameCounter = 0;
};

// The registry itself - a flat vector, scanned linearly (see this file's
// own header comment for why a hash map is unwarranted at this engine's
// scale). Every method is a plain, synchronous, single-threaded call -
// this class has NO locking of its own; it is only ever touched from the
// main thread (RenderGraph::ExecuteCompiledGraph()/Application::Run()),
// exactly like every other RenderGraph-adjacent class in this engine (see
// AGENTS.md, "Networking" - a route handler NEVER touches this directly,
// only through FrameCaptureBridge, Phase 4).
class RenderGraphDebugTextureRegistry {
public:
    RenderGraphDebugTextureRegistry() = default;

    // Inserts a brand-new entry for `name`, or overwrites an existing one
    // in place (by value - every field, including lastUpdatedFrameCounter,
    // is fully replaced). `name` is copied into the stored entry (unlike
    // RenderGraphNameSlotTable's own by-pointer/string-literal convention -
    // see this method's own doc comment below for why a copy is required
    // here, not merely allowed).
    //
    // IMPORTANT naming-lifetime note: RenderGraphBuilder::CreateTexture()/
    // ImportTexture()'s own `name` parameter is a `const char*` that must be
    // a string literal / static-storage-duration pointer (see
    // RenderGraphBuilder.h's own class comment) - but THIS registry must
    // remain valid and correct across MANY frames, and a future caller of
    // Upsert() (Phase 2) only ever has that SAME static-storage `const
    // char*` available anyway, so storing a `std::string` copy here (rather
    // than keeping the raw pointer, which would ALSO have been safe given
    // the static-storage-duration rule) is a deliberate, extra-safe choice:
    // it keeps this class's own correctness independent of that upstream
    // convention being upheld perfectly forever, and is what makes
    // ListNames()/FindByName() usable with an arbitrary caller-supplied
    // std::string (e.g. straight out of an HTTP query parameter, Phase 4/5)
    // via plain std::string comparison - never a dangling/lifetime concern
    // either way.
    void Upsert(const DebugTextureSnapshot& snapshot);

    // Overwrites JUST the color ResourceState of the entry named `name` (a
    // safe no-op if no such entry exists yet) - see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision 7. Deliberately
    // does NOT touch `lastUpdatedFrameCounter` - a state correction is not
    // a fresh capture of contents.
    void ApplyColorStateOverride(const std::string& name, const ResourceState& newColorState);

    // Returns a copy of the entry named `name`, or std::nullopt if this
    // registry has never seen that name at all this session. `name`
    // comparison is a plain std::string ==  (see Upsert()'s own doc
    // comment for why this is safe/correct against an HTTP-supplied
    // string).
    std::optional<DebugTextureSnapshot> FindByName(const std::string& name) const;

    // Every currently-known name, in FIRST-SEEN order (stable, so
    // GET /list_textures - Phase 5 - returns a predictable, non-shuffling
    // order across repeated calls within one session) - the primitive
    // behind that endpoint. Returns copies (DebugTextureSnapshot, not just
    // names) so a caller building /list_textures's response body needs no
    // second lookup per name.
    std::vector<DebugTextureSnapshot> ListAll() const;

private:
    std::vector<DebugTextureSnapshot> m_entries;
};

} // namespace gte::rg
```

**Confirmed: the forward-declared `ExecuteTimingMode` compiles cleanly — no
fallback header split is needed.** A forward-declared scoped enum with an
explicit underlying type (`enum class ExecuteTimingMode : std::uint8_t;`) is
a COMPLETE type in standard C++ from the point of that declaration onward
(the underlying type alone is enough to know its size/alignment/how to
value-initialize it — only its enumerator NAMES remain unknown until the
real definition is seen) — it can legally be used as a struct member
(`DebugTextureSnapshot::regime`), default-member-initialized via `{}`, and
`static_cast<ExecuteTimingMode>(someIntegerValue)`'d, all without seeing
`RenderGraph.h`'s real definition at all. This was verified directly, as
part of this second-iteration review, with a standalone two-namespace-block
reproduction (forward-declare in one block, full three-line `enum class
ExecuteTimingMode : std::uint8_t { ... };` definition in a second block
further down the same translation unit, exactly mirroring how
`RenderGraphDebugTextureRegistry.h`'s forward declaration and
`RenderGraph.h`'s real definition will coexist once Phase 2 makes
`RenderGraph.h` `#include` this file) compiling with zero errors/warnings.
The one remaining thing worth double-checking at actual implementation time
is simply that `RenderGraph.h`'s real declaration hasn't drifted from
`enum class ExecuteTimingMode : std::uint8_t` (e.g. gained an `enum class
ExecuteTimingMode` with NO explicit underlying type, which would no longer
match this forward declaration) — re-grep it live before wiring Phase 2, but
no separate `RenderGraphExecuteTimingMode.h` extraction is needed as a
matter of course.

**A note for a future test file (see Step 3.4 below) that only includes
THIS header, not `RenderGraph.h`:** because `ExecuteTimingMode`'s
enumerator NAMES are only visible once `RenderGraph.h`'s real definition has
been seen, a Tier-1 test for this file alone (which deliberately does NOT
want to pull in the much larger `RenderGraph.h`) cannot write
`ExecuteTimingMode::SynchronousImmediateReadback` directly. It CAN,
however, write `static_cast<ExecuteTimingMode>(0)`/`static_cast<ExecuteTimingMode>(1)`
freely (a `static_cast` to an enum type only requires that type to be
COMPLETE, which — per the note above — a fixed-underlying-type forward
declaration already satisfies) — this is the one recommended way to
exercise the `regime` field in this phase's own test file without widening
its include list.

### 3.2 — `src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.cpp`

```cpp
#include "RenderGraphDebugTextureRegistry.h"

namespace gte::rg {

void RenderGraphDebugTextureRegistry::Upsert(const DebugTextureSnapshot& snapshot)
{
    for (DebugTextureSnapshot& entry : m_entries) {
        if (entry.name == snapshot.name) {
            entry = snapshot;
            return;
        }
    }
    m_entries.push_back(snapshot);
}

void RenderGraphDebugTextureRegistry::ApplyColorStateOverride(
    const std::string& name, const ResourceState& newColorState)
{
    for (DebugTextureSnapshot& entry : m_entries) {
        if (entry.name == name) {
            entry.colorState = newColorState;
            return;
        }
    }
    // No entry yet - a safe no-op, per this method's own doc comment. This
    // can legitimately happen if Application.cpp's correction call ever
    // races ahead of the FIRST Upsert() for a brand-new name (should not
    // happen in practice given call ordering - see Phase 3 - but is not a
    // bug worth asserting on if it ever does; the next real frame's
    // Upsert() will simply establish the entry with a correct state from
    // scratch anyway).
}

std::optional<DebugTextureSnapshot> RenderGraphDebugTextureRegistry::FindByName(const std::string& name) const
{
    for (const DebugTextureSnapshot& entry : m_entries) {
        if (entry.name == name) {
            return entry;
        }
    }
    return std::nullopt;
}

std::vector<DebugTextureSnapshot> RenderGraphDebugTextureRegistry::ListAll() const
{
    return m_entries;
}

} // namespace gte::rg
```

### 3.3 — `CMakeLists.txt`

- Add both new files (`RenderGraphDebugTextureRegistry.h`/`.cpp`) to the SAME
  source list `RenderGraph.cpp`/`RenderGraphSnapshot.cpp`/etc. already belong
  to, in the repository ROOT `CMakeLists.txt` (grep for
  `RenderGraphSnapshot.cpp` there to find the exact list — confirmed live,
  it sits immediately after `RenderGraph.h`/`RenderGraph.cpp`'s own two
  lines) — no new CMake target, no new toggle (Locked Design Decision 8).
- **Also required (a real, easy-to-miss second step): register the new test
  file from Step 3.4 below in `tests/CMakeLists.txt`'s own `GTE_TEST_SOURCES`
  list** — grep for `Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`
  there to find the exact list (confirmed live: it is a plain, flat list of
  relative test-source paths, with `RenderGraphNameSlotTableTests.cpp`/
  `RenderGraphSnapshotTests.cpp` as the two closest neighbors to add this
  new entry alongside). Writing the test file under `tests/Renderer/
  RenderGraph/RenderGraphDebugTextureRegistryTests.cpp` WITHOUT also adding
  it here means it is never compiled into `GreatTamanaEngineTests` and never
  actually runs — `ctest` would stay green not because the new code is
  correct, but because its own tests silently never executed at all. Confirm
  this by checking the test binary's own `--gtest_list_tests` output
  includes `RenderGraphDebugTextureRegistryTests.*` before considering this
  phase done.

### 3.4 — Tests (`tests/Renderer/RenderGraph/RenderGraphDebugTextureRegistryTests.cpp`)

Fully Tier-1 — construct a `DebugTextureSnapshot` by hand (fake Vulkan
handle values), mirroring how `RenderGraphBuilderTests.cpp`/
`RenderGraphBarrierPlannerTests.cpp` already fabricate them — **use the
SAME established idiom those two files actually use,
`reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0xABCD))` (confirmed
live in both, e.g. `RenderGraphBuilderTests.cpp`'s own
`MakeFakeSwapchainTarget()`/`MakeFakeGameViewTarget()` helpers), NOT a bare
`reinterpret_cast<VkImage>(1)` — casting a small integer LITERAL straight to
a pointer-sized handle type without first widening it through
`static_cast<std::uintptr_t>` is exactly the pattern this codebase avoids
elsewhere (see e.g. `third_party/ktx/tests/loadtests/common/SwipeDetector.h`'s
own `#pragma warning(disable : 4312)`, needed for precisely this reason) —
prefer the wider, established idiom rather than reintroducing that
narrower one here.** A small local `MakeFakeSnapshot(name, colorImageTag)`
helper (returning a fully-populated `DebugTextureSnapshot` with distinct
fake color/depth handles derived from `colorImageTag`) is worth writing
once at the top of this file, mirroring `MakeFakeSwapchainTarget()`'s own
role, since several of the assertions below need a fresh, fully-populated
snapshot. `regime` should be set via `static_cast<ExecuteTimingMode>(...)`
per Step 3.1's own closing note (this test file has no reason to include
the much larger `RenderGraph.h` just to name an enumerator).

Assert:

- `FindByName()` on an empty registry returns `std::nullopt`.
- `ListAll()` on an empty registry returns an empty vector (not just
  "eventually empty after some removal" — a brand-new registry with zero
  `Upsert()` calls ever made, the exact state at engine startup before the
  first frame renders).
- `Upsert()` then `FindByName()` round-trips every field correctly,
  including `lastUpdatedFrameCounter`.
- A second `Upsert()` with the SAME name overwrites the first entry in
  place (registry size stays 1, not 2) — assert via `ListAll().size()`.
- Two DIFFERENT names both appear in `ListAll()`, in first-seen order.
- `ApplyColorStateOverride()` on an EXISTING name updates ONLY
  `colorState` — every other field (including `lastUpdatedFrameCounter`)
  is unchanged; assert this explicitly (a regression here would silently
  make Locked Design Decision 4's freshness metric wrong).
- `ApplyColorStateOverride()` on an UNKNOWN name is a safe no-op — no
  crash, no new entry created, `ListAll().size()` unchanged.

### 3.5 — What this phase deliberately does NOT do

- Does not touch `RenderGraph.h`/`.cpp` at all yet — Phase 2's job.
- Does not touch `Renderer`/`Application` — Phases 3/4.
- Does not add any HTTP-facing code — Phase 5.
