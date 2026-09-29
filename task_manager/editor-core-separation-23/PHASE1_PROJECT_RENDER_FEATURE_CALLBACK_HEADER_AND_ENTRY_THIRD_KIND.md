# PHASE1 — New `ProjectRenderFeatureCallback.h`; `RenderFeatureCompositor::Entry` gains its third module-kind

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first, including its Locked
Decisions in Step 3.1 — this phase does not repeat that file's own content).
Campaign folder: `task_manager/editor-core-separation-23/`
Design doc citation: `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_01_CORE_ENGINE_WIRING_2026-09-29.txt`,
Step 2 (re-read it in full before starting).
Previous phase report to read first: none — this is the first implementation
phase of this campaign.

## Step 1: The Goal (Where are we going?)

`RenderFeatureCompositor::Entry` must gain a real, working THIRD module-kind
— a plain `std::function`-based callback, no ABI, no `QueryCapability()` —
alongside its existing `moduleV2`/`moduleV3` pointers, WITHOUT:
  - `Core.h` ever gaining a full `#include` of the real
    `Plugins/RenderFeatureCompositor.h` (it must keep forward-declaring
    `class RenderFeatureCompositor;` only).
  - Any existing `_v2`/`_v3` plugin behavior changing in any observable way.

By the end of this phase: the project compiles cleanly with the new header
and the new `Entry` fields in place, `ContributeRenderGraphPasses()`'s
per-entry loop has a real (if currently unreachable — nothing constructs a
`projectCallback` entry yet, that is PHASE2's job) third branch arm, and a
standalone Tier-1 test proves the new header introduces zero circular
dependency.

## Step 2: The Situation (Where are we now?)

Re-confirmed fresh, 2026-09-29 (re-confirm again yourself before editing —
this is a fast-moving codebase):

- `src/Core/Plugins/RenderFeatureCompositor.h` line 60:
  `class Core; // forward declaration only - this header must not #include "../Core.h"`.
  `Core.h` itself only ever forward-declares `class RenderFeatureCompositor;`
  (confirmed by `search_in_dir` — no stray full include exists today). If you
  find this has changed since this document was written, STOP and
  `ask_questions` before proceeding — PHASE3's own new `Core.h` methods depend
  on this staying true.
- `RenderFeatureCompositor::Entry` (`RenderFeatureCompositor.h` lines 194-218)
  has exactly 4 fields today: `moduleV2`, `moduleV3`, `descriptor`,
  `enabledOverride`.
- `RenderFeatureCompositor.cpp`'s `ContributeRenderGraphPasses()` (lines
  631-671) contains the ONE call site this phase must widen into a real
  three-way branch:
  ```cpp
  if (entry.moduleV3 != nullptr) {
      PluginRenderPassBuilderAdapter_v3 adapter(frame.builder, privateTarget, m_operationRegistry,
          m_blackboardAdapter, resolved->target, resolved->sampler, *this,
          pluginName + "_" + viewName + "_");
      entry.moduleV3->AddRenderGraphPasses(adapter);
  } else {
      PluginRenderPassBuilderAdapter_v2 adapter(frame.builder, privateTarget, *this, privateName);
      entry.moduleV2->AddRenderGraphPasses(adapter);
  }
  ```
  Everything AFTER this branch (the `DispatchBlend()` call reading
  `privateTarget`) is already completely generic over "an entry has some way
  to fill its own private target" — it needs ZERO further change in this
  phase.
- `OnPluginsLoaded()` (lines 282-351) is UNCHANGED by this phase — it only
  ever constructs entries from `gte_plugin_abi` modules; a Project Assembly's
  own entry is constructed by PHASE2's new `RegisterProjectFeature()` method,
  never routed through this bulk-scan method.
- `Core.h` already transitively includes both `RenderGraphBuilder.h` and
  `RenderGraphTypes.h` (via `RenderGraph.h`/`RenderPipeline.h`), so the new
  header this phase creates adds no new real compile-time weight to `Core.h`.

## Step 3: The Plan (detailed strategy)

### 3.1 — New header: `src/Core/Plugins/ProjectRenderFeatureCallback.h`

Create this file, verbatim (this is a small, self-contained, leaf-level
header — copy this exactly, then re-verify every include actually resolves
against this repository's real file layout before moving on):

```cpp
#pragma once

// The one new callback type a Project Assembly's own on-screen render
// feature registration (Core::RegisterProjectRenderFeature(),
// RenderFeatureCompositor::RegisterProjectFeature()) is built around.
// Deliberately NOT a nested member of RenderFeatureCompositor - Core.h only
// ever forward-declares that class (never #includes the real
// Plugins/RenderFeatureCompositor.h - see that header's own "this header
// must not #include ../Core.h" comment for the matching half of this same
// discipline) and therefore has no way to name a type nested inside an
// incomplete class. Living here, in its own free-standing, zero-Core-
// dependency header, lets BOTH RenderFeatureCompositor.h and Core.h include
// this ONE small file directly, by value, with neither header's own
// existing include discipline having to change.
//
// This signature deliberately exposes ONLY a RenderGraphBuilder& and a
// TextureHandle - never a raw VkImage/VkCommandBuffer/RenderTexture& - so a
// Project Assembly's own callback body has no way to record a GPU command
// except through builder.AddRenderPass()/AddPass()/AddComputePass()'s own
// setup/execute pair, the Render Graph's own, one, official entry point.
// Synchronization/barriers/scheduling for whatever this callback declares
// are handled entirely by the SAME RenderGraphCompiler/RenderGraphBarrierPlanner
// every other pass in this engine already goes through - nothing new to
// build, nothing to bypass. See task_manager/editor-core-separation-23/
// PHASE7_FULL_REGRESSION_DOCS_AND_ENTRY_GATE_CLOSEOUT.md's own
// RenderPassEvent::AfterEverything requirement for the ONE convention every
// pass this callback declares must follow.
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <functional>

namespace gte {

using ProjectRenderFeatureCallback = std::function<void(
    rg::RenderGraphBuilder& builder, rg::TextureHandle privateTarget, VkExtent2D extent)>;

} // namespace gte
```

### 3.2 — Standalone-compile Tier-1 proof

Add `tests/Core/Plugins/ProjectRenderFeatureCallbackHeaderCompilesStandaloneTests.cpp`
— a TRIVIAL file that `#include`s ONLY `Core/Plugins/ProjectRenderFeatureCallback.h`
and nothing else, plus `<gtest/gtest.h>`, with exactly one test that
constructs a default `gte::ProjectRenderFeatureCallback` and asserts
`static_cast<bool>(callback) == false` (an empty `std::function` — this is
the "operator bool() == false when unused" contract PHASE2 depends on). The
POINT of this test is that the file compiles as its OWN translation unit with
no other engine header pulled in first — confirms zero circular dependency
back into `Core.h`/`RenderFeatureCompositor.h`. Add it to
`tests/CMakeLists.txt` alongside its sibling `tests/Core/Plugins/*.cpp` files
(mirror the existing glob/list pattern already there — read that file's
relevant section before editing it).

### 3.3 — `RenderFeatureCompositor::Entry` gains two new fields

In `src/Core/Plugins/RenderFeatureCompositor.h`:
  1. Add `#include "ProjectRenderFeatureCallback.h"` near the top of this
     file's own include list, alongside its existing sibling includes
     (`IPluginCapabilityOrchestrator.h`, `PluginRenderOperationRegistry.h`,
     etc.).
  2. Add two new fields to `struct Entry` (private, nested inside
     `RenderFeatureCompositor`), alongside the existing 4:
     ```cpp
     ProjectRenderFeatureCallback projectCallback;  // operator bool() == false when unused.
     int projectFeatureSlot = -1;                   // meaningful ONLY when projectCallback is set.
     ```
     `ProjectRenderFeatureCallback` is named UNQUALIFIED (never
     `RenderFeatureCompositor::ProjectRenderFeatureCallback`) — this class
     already sits inside `namespace gte`, and the type itself is never a
     nested member of this class (see 3.1's own doc comment for why). Do not
     add the slot-pool constant or free-list member in this phase — that is
     PHASE2's own scope; this phase only adds the STRUCTURAL fields every
     later phase needs to already exist.

### 3.4 — Three-way branch in `ContributeRenderGraphPasses()`

Widen the per-entry branch (`RenderFeatureCompositor.cpp`, cited in Step 2
above) into a real three-way branch:

```cpp
if (entry.moduleV3 != nullptr) {
    PluginRenderPassBuilderAdapter_v3 adapter(frame.builder, privateTarget, m_operationRegistry,
        m_blackboardAdapter, resolved->target, resolved->sampler, *this,
        pluginName + "_" + viewName + "_");
    entry.moduleV3->AddRenderGraphPasses(adapter);
} else if (entry.moduleV2 != nullptr) {
    PluginRenderPassBuilderAdapter_v2 adapter(frame.builder, privateTarget, *this, privateName);
    entry.moduleV2->AddRenderGraphPasses(adapter);
} else if (entry.projectCallback) {
    entry.projectCallback(frame.builder, privateTarget, extent);
}
```

Notes:
  - The `entry.moduleV2 != nullptr` arm's own condition CHANGES from an
    unconditional `else` to an explicit `else if` — this is a deliberate,
    defensive widening (mirrors `RenderGraphTypes.h`'s own "every branch site
    audited, no silent `default:`" convention cited by the design doc) so a
    future 4th kind never silently falls into the wrong arm. Confirm this
    does not change behavior for any EXISTING entry: every entry constructed
    by `OnPluginsLoaded()` today always has exactly one of `moduleV2`/
    `moduleV3` non-null (never neither), so the `else if (entry.moduleV2 !=
    nullptr)` arm is taken in EXACTLY the same cases the old bare `else` was.
  - The `projectCallback` arm needs NO adapter object at all (unlike the
    other two arms) — it already receives the real `RenderGraphBuilder&`/
    `TextureHandle`/`VkExtent2D` directly.
  - `extent` is already an in-scope local in this method (computed earlier,
    `const VkExtent2D extent = resolved->extent;`) — reuse it, do not
    recompute.
  - **This arm is currently DEAD CODE** — nothing in this phase constructs an
    `Entry` with `projectCallback` set. That is expected and correct; PHASE2
    makes it reachable. Do not attempt to test this branch's own runtime
    behavior in THIS phase — Tier-1 test coverage for it belongs in PHASE2,
    once `RegisterProjectFeature()` exists to actually populate it.

### 3.5 — What this phase deliberately does NOT touch

  - `gpuStateKey`/interned-name resolution for a `projectCallback` entry
    (`privateName`/`accumName`/`blendPassName` computation) — PHASE2's scope.
    This phase's own three-way branch above still computes `privateName`
    from `pluginName` (`entry.descriptor.name`) exactly like today, for EVERY
    entry, including a hypothetical future `projectCallback` one — this is
    intentionally still WRONG per the design doc's Step 3 reasoning (using
    the raw name would starve the descriptor pool over a long rename
    session), but is harmless and inert here since no `projectCallback` entry
    can exist yet. PHASE2 replaces this exact `privateName`/`accumName`/
    `blendPassName` computation with the slot-keyed `gpuStateKey` — do not
    pre-empt that change in this phase; keep this phase's own diff minimal
    and mechanical.
  - `RegisterProjectFeature()`/`UnregisterProjectFeature()` themselves —
    PHASE2.
  - `Core::RegisterProjectRenderFeature()` — PHASE3.

### 3.6 — Ambiguity checkpoint

If, while widening the branch, you find `OnPluginsLoaded()` or any OTHER call
site (beyond the one cited in Step 2) also branches on
`moduleV3 != nullptr`/`moduleV2`, `ask_questions` before deciding whether it
also needs a third arm in THIS phase or is legitimately PHASE2/PHASE5's own
scope instead — do not silently widen scope.

### 3.7 — End of phase

1. Incremental build (`cmake --build build`) succeeds.
2. `ctest -C Debug --output-on-failure -R ProjectRenderFeatureCallback` (the
   new standalone-header test) passes.
3. Confirm, via a quick re-read of the diff, that NO existing `_v2`/`_v3`
   plugin's live behavior changed (the three-way branch reduces to the exact
   same two cases as before for every entry that can exist today).
4. Write `PHASE1_COMPLETION_REPORT.md` in this same folder: what was added,
   the exact diff locations, the Tier-1 test result, and confirmation that
   `Core.h` still only forward-declares `RenderFeatureCompositor`.
5. `git_add` + `git_commit` covering the new header, the new test file, the
   `Entry` struct change, the three-way branch, and the report.
