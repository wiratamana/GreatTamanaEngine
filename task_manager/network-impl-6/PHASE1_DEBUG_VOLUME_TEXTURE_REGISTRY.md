# PHASE1_DEBUG_VOLUME_TEXTURE_REGISTRY

Parent: `PHASE0_MASTER_STRATEGY.md` — **READ THAT FILE FIRST.**

## Step 1 — The Goal

Create a brand-new, pure, Tier-1-testable data-model class —
`gte::rg::RenderGraphDebugVolumeTextureRegistry` — that is the exact
volume-texture counterpart of the existing
`gte::rg::RenderGraphDebugTextureRegistry`
(`src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h/.cpp`). By the
end of this phase, the class exists, compiles, and has full test coverage —
but nothing calls it yet (that is Phase 2's job). This phase is
deliberately scoped to ONLY the data model, mirroring how
`RenderGraphDebugTextureRegistry` itself was originally introduced in
isolation before `RenderGraph` was wired to call it (see
`task_manager/network-impl-4/PHASE1_DEBUG_TEXTURE_REGISTRY_CORE_DATA_MODEL.md`
for that exact precedent — read it for style/tone reference, this phase
should read almost identically, just for volumes).

## Step 2 — The Situation / The Problem

Read `src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h` and
`.cpp` in full before writing anything — this phase's whole job is to
produce a structurally near-identical sibling file. The existing class:

- Stores a flat `std::vector<DebugTextureSnapshot>`, scanned linearly
  (deliberately no hash map — see that file's own header comment for why,
  at this engine's scale).
- `DebugTextureSnapshot` carries: `name` (owned `std::string`, NOT a raw
  `const char*` — see that struct's own doc comment for exactly why a copy
  is required, not merely allowed), `regime` (`ExecuteTimingMode`,
  forward-declared to avoid a circular include with `RenderGraph.h`),
  `target` (a `RenderTarget` — color image/view/extent/format, plus
  optional depth), `hasDepth`, `colorState`/`depthState`
  (`rg::ResourceState`), and `lastUpdatedFrameCounter` (a
  `std::uint64_t`, compared against `RenderGraph`'s own monotonically
  increasing frame counter to compute "how many frames old is this").
- Four methods: `Upsert()` (insert-or-overwrite by name),
  `ApplyColorStateOverride()` (correct just the tracked layout without
  bumping freshness — needed for a graph-external manual barrier), `FindByName()`
  (`std::optional<DebugTextureSnapshot>`), `ListAll()` (every entry, in
  first-seen order).
- Zero locking — it is only ever touched from the main thread (documented
  explicitly in its own class comment).

A volume texture is described differently at the physical-resolution
level: `RenderGraph::PhysicalVolumeTexture`
(`src/Renderer/RenderGraph/RenderGraph.h`, around line 374) is `{ bool
resolved; bool isImported; VolumeTarget target; ResourceState state; }` —
note there is only ONE `ResourceState` (no depth split at all — a volume
texture has no depth-companion concept, see `VolumeTarget.h`'s own doc
comment), and `target` is a `VolumeTarget`
(`src/Renderer/VolumeTarget.h`: `{ VkImage image; VkImageView imageView;
VkExtent3D extent; VkFormat format; }`) rather than a `RenderTarget`.

## Step 3 — The Plan

### 3.1 — New file: `src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.h`

Namespace `gte::rg`. Structure:

```cpp
#pragma once

#include "RenderGraphBarrierPlanner.h"   // ResourceState
#include "../VolumeTarget.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte::rg {

enum class ExecuteTimingMode : std::uint8_t; // forward-declared, mirrors RenderGraphDebugTextureRegistry.h's own identical forward-declare (both enumerators are re-declared nowhere else).

// Volume-texture counterpart of DebugTextureSnapshot (RenderGraphDebugTextureRegistry.h).
// No depth concept at all - a VolumeTexture has none (see VolumeTarget.h).
struct DebugVolumeTextureSnapshot {
    std::string name;
    ExecuteTimingMode regime{};

    VolumeTarget target; // image/imageView/extent (VkExtent3D: width/height/depth)/format.

    ResourceState state; // layout/stage/access this registry LAST believes the volume image is actually in.

    std::uint64_t lastUpdatedFrameCounter = 0;
};

class RenderGraphDebugVolumeTextureRegistry {
public:
    RenderGraphDebugVolumeTextureRegistry() = default;

    void Upsert(const DebugVolumeTextureSnapshot& snapshot);
    void ApplyStateOverride(const std::string& name, const ResourceState& newState);
    std::optional<DebugVolumeTextureSnapshot> FindByName(const std::string& name) const;
    std::vector<DebugVolumeTextureSnapshot> ListAll() const;

private:
    std::vector<DebugVolumeTextureSnapshot> m_entries;
};

} // namespace gte::rg
```

Copy every relevant doc comment from `RenderGraphDebugTextureRegistry.h`
verbatim where it still applies (the "why a `std::string` copy, not a raw
pointer" reasoning; the "zero locking, main-thread-only" reasoning; the
"first-seen, stable order" reasoning for `ListAll()`) — do not silently
drop this documentation just because it's "the same as the other file",
since a future reader of THIS file specifically should not have to cross-
reference the 2D file to understand it.

`ApplyStateOverride()` is named without the `Color` prefix
(`RenderGraphDebugTextureRegistry::ApplyColorStateOverride()` has one only
because it needs to disambiguate from a hypothetical depth override that
doesn't exist for volumes) — keep it, but do not build any call site for it
in this phase (see Phase 2's own scope: whether a graph-external volume
barrier correction is even needed today is a Phase 2 question, not a Phase
1 one — Phase 1 only needs the METHOD to exist so Phase 2 has the option).

### 3.2 — New file: `src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.cpp`

A direct structural mirror of `RenderGraphDebugTextureRegistry.cpp`'s four
method bodies, adjusted for the renamed method and the absence of a depth
half. No new logic to invent here — this is a pure transcription exercise
with the type names swapped.

### 3.3 — New test file: `tests/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistryTests.cpp`

Mirror whatever `tests/Renderer/RenderGraph/RenderGraphDebugTextureRegistryTests.cpp`
already covers (read it first) — hand-fabricate `DebugVolumeTextureSnapshot`
values with plain, made-up `VkImage`/`VkImageView` integer-cast handles
(no real `VkDevice` needed at all, exactly like the 2D registry's own
tests), and cover at minimum:

- `Upsert()` on a brand-new name inserts it; `FindByName()` then finds it
  with every field intact.
- `Upsert()` on an EXISTING name overwrites it in place (verify
  `ListAll().size()` does not grow, and the returned entry reflects the
  NEW values, not the old ones).
- `FindByName()` on a name never seen returns `std::nullopt`.
- `ApplyStateOverride()` on an existing name updates ONLY `state`, leaving
  every other field (crucially `lastUpdatedFrameCounter`) untouched.
- `ApplyStateOverride()` on a name that doesn't exist yet is a safe no-op
  (does not insert a new, incomplete entry).
- `ListAll()` returns entries in first-seen order across several `Upsert()`
  calls, including after one of those names was subsequently re-`Upsert()`ed
  (its position must NOT move to the end).

Add the new test file to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list,
right next to the existing `RenderGraphDebugTextureRegistryTests.cpp` entry
— unconditionally built, same bucket (this class has no
`GTE_ENABLE_EDITOR`/`GTE_ENABLE_NETWORK` dependency at all).

### 3.4 — CMakeLists.txt wiring

Add `src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.cpp`
(and its header, if this project's `CMakeLists.txt` lists headers
explicitly — check the existing pattern for
`RenderGraphDebugTextureRegistry.cpp`/`.h` and mirror it exactly) to
`gte_core`'s source list, in the same `RenderGraph`-grouped section the 2D
registry's own files live in. This file has zero `GTE_ENABLE_EDITOR`/
`GTE_ENABLE_NETWORK` conditional wrapping — it must compile into every
single build configuration, matching Locked Design Decision 4 in
`PHASE0_MASTER_STRATEGY.md`.

### Verification

- Fast compile check: build `gte_core` and `GreatTamanaEngineTests` only
  (no full `GreatTamanaEngine` app build needed for this phase).
- Run the new test file specifically (e.g.
  `GreatTamanaEngineTests.exe --gtest_filter=RenderGraphDebugVolumeTextureRegistryTests.*`)
  and confirm every new test passes, plus a full `ctest` pass to confirm
  zero regressions elsewhere (this is cheap enough at Phase 1's small
  scope to be worth doing here despite Workflow Rule 1's general "no full
  regression until Phase 6" — use judgment: if the full suite is fast
  enough to run in reasonable time, running it after every phase is
  strictly safer and still satisfies "no full BUILD of the game app" for
  phases before 6; if it is slow, a `ctest -R RenderGraph` targeted filter
  is an acceptable, faster proxy for phases 1-5, with the one true full
  pass reserved for Phase 6).
- Write `PHASE1_COMPLETION_REPORT.md` in this folder, commit.
