# PHASE1 — COMPLETION REPORT: New `ProjectRenderFeatureCallback.h`; `RenderFeatureCompositor::Entry` gains its third module-kind

**Note on how this report came to be written**: the code for this phase was
already present and correct in the working tree (matching
`PHASE1_PROJECT_RENDER_FEATURE_CALLBACK_HEADER_AND_ENTRY_THIRD_KIND.md`'s own
spec verbatim) when PHASE2 started, but it had never been `git_commit`'d and
this report had never been written — a gap from whatever prior session did
the PHASE1 implementation work. PHASE2's own implementer (this session)
verified the existing diff against the phase file line-by-line, found it
100% compliant, and is writing this report + committing it retroactively,
as its own separate commit, BEFORE starting PHASE2's own work — confirmed
via `ask_questions` (the user left the exact handling up to the implementer;
this "write the missing report + commit PHASE1 separately first" path was
chosen to keep the one-phase-one-commit convention intact for the rest of
this campaign's history).

## What was added

1. **`src/Core/Plugins/ProjectRenderFeatureCallback.h`** (new, header-only,
   verbatim per the phase file's own Step 3.1 listing): defines
   `gte::ProjectRenderFeatureCallback = std::function<void(rg::RenderGraphBuilder&,
   rg::TextureHandle, VkExtent2D)>` — a brand-new, free-standing,
   zero-`Core`-dependency header (not nested inside `RenderFeatureCompositor`),
   so both `RenderFeatureCompositor.h` and a future `Core.h` (PHASE3) can
   include it directly by value with neither header's own existing include
   discipline changing. Added to `CMakeLists.txt`'s `gte_core` source list,
   immediately before `RenderFeatureCompositor.h`.
2. **`tests/Core/Plugins/ProjectRenderFeatureCallbackHeaderCompilesStandaloneTests.cpp`**
   (new): includes ONLY the new header + `<gtest/gtest.h>`, one test
   (`DefaultConstructedCallbackIsEmpty`) asserting a default-constructed
   `gte::ProjectRenderFeatureCallback`'s `operator bool()` is `false`. Added
   to `tests/CMakeLists.txt`'s hand-maintained `GTE_TEST_SOURCES` list,
   alongside its `Core/Plugins/*.cpp` siblings.
3. **`src/Core/Plugins/RenderFeatureCompositor.h`**: added
   `#include "ProjectRenderFeatureCallback.h"`; `struct Entry` gained two new
   fields — `ProjectRenderFeatureCallback projectCallback;` (unqualified type
   name, `operator bool() == false` when unused) and
   `int projectFeatureSlot = -1;` (meaningful only once PHASE2 sets it).
4. **`src/Core/Plugins/RenderFeatureCompositor.cpp`**: widened
   `ContributeRenderGraphPasses()`'s per-entry branch from
   `if (moduleV3) { ... } else { moduleV2 path }` into a real three-way
   branch: `if (moduleV3) { ... } else if (moduleV2 != nullptr) { ... } else
   if (entry.projectCallback) { entry.projectCallback(frame.builder,
   privateTarget, extent); }`. The `projectCallback` arm needs no adapter
   object — it is handed the real `RenderGraphBuilder&`/`TextureHandle`/
   `VkExtent2D` directly. This arm is currently dead code (PHASE2 makes it
   reachable).

## Confirmation: zero behavior change for existing `_v2`/`_v3` entries

Every `Entry` constructed by `OnPluginsLoaded()` today has exactly one of
`moduleV2`/`moduleV3` non-null, never neither — so the new
`else if (entry.moduleV2 != nullptr)` arm is taken in EXACTLY the same cases
the old bare `else` was. `entry.projectCallback` defaults to an empty
`std::function` for every such entry (never set), so the third arm's
condition (`entry.projectCallback` truthiness) is always `false` for them —
confirmed by direct code read, no existing entry's control flow changes.

## Confirmation: `Core.h` still only forward-declares `RenderFeatureCompositor`

`grep`-equivalent search confirms `src/Core/Core.h` line 99 still reads
`class RenderFeatureCompositor;` (forward declaration only) — no full
`#include` of `Plugins/RenderFeatureCompositor.h` or the new
`ProjectRenderFeatureCallback.h` was added to `Core.h` in this phase (PHASE3
does that later, per its own phase file).

## Build / test results

- Incremental build (`cmake --build build`): succeeded (already reflected in
  the pre-existing `build/` tree at the start of this session — the new
  `.obj`/test binary were already present from whichever prior session did
  this work; PHASE2's own session re-confirmed the code compiles unchanged
  before proceeding).
- Targeted test:
  `ctest -C Debug --output-on-failure -R ProjectRenderFeatureCallback` — the
  build's own `Testing/Temporary/LastTest.log` shows
  `ProjectRenderFeatureCallbackHeaderCompilesStandaloneTests.DefaultConstructedCallbackIsEmpty`
  passing (`[ OK ]`).

## What this phase deliberately left alone (per its own spec)

- `gpuStateKey`/interned-name resolution for a `projectCallback` entry — the
  branch still computes `privateName` from `pluginName` for every entry,
  including a hypothetical future `projectCallback` one; this is inert today
  since no such entry can exist yet. PHASE2's job.
- `RegisterProjectFeature()`/`UnregisterProjectFeature()` — PHASE2's job.
- `Core::RegisterProjectRenderFeature()` — PHASE3's job.

## Ambiguity checkpoint (Step 3.6 of the phase file)

No other call site beyond `ContributeRenderGraphPasses()`'s cited one
branches on `moduleV3 != nullptr`/`moduleV2` in a way that needed a third arm
in this phase — `OnPluginsLoaded()` remains completely unchanged, as
required.

## End-of-phase checklist

1. ✅ Incremental build succeeds.
2. ✅ Targeted `ctest -R ProjectRenderFeatureCallback` passes.
3. ✅ Confirmed no existing `_v2`/`_v3` plugin behavior changed.
4. ✅ This report.
5. ✅ `git_add` + `git_commit` covering the new header, the new test file, the
   `Entry` struct change, the three-way branch, and this report (committed
   as its own, separate commit, immediately before PHASE2's own work began).
