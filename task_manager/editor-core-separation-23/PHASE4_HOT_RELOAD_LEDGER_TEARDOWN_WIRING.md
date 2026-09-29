# PHASE4 — Hot-reload teardown wiring: `ProjectAssemblyRegistrationLedger` gains render features

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first).
Campaign folder: `task_manager/editor-core-separation-23/`
Design doc citation: `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_01_CORE_ENGINE_WIRING_2026-09-29.txt`,
Step 5 (re-read it in full before starting — this is a HARD REQUIREMENT, not
optional polish, per that Step's own closing paragraph).
Previous phase report to read first: `PHASE3_COMPLETION_REPORT.md`.

## Step 1: The Goal (Where are we going?)

A `RenderFeatureCompositor::Entry` whose `projectCallback` points into an
unloaded `.dll`'s own code is a dangling-callback hazard identical in kind to
the ECS Registry's own `ComponentStorage<T>` vtable teardown bug this
codebase already found and fixed once (`editor-core-separation-15`). A
hot-reload cycle that recompiles a project WITHOUT correct teardown wiring
crashes or corrupts memory the moment `ContributeRenderGraphPasses()` next
invokes that now-dangling `std::function`. By the end of this phase,
`ProjectAssemblyRegistrationLedger::UnregisterEverythingFor()` tears down a
project's own render features — releasing their claimed GPU-state slots back
to PHASE2's free list as part of the SAME call — correctly ordered BEFORE
that project's render-pass providers, proven via a real Tier-1 test, with
live confirmation deferred to PHASE6.

## Step 2: The Situation (Where are we now?)

Re-confirmed fresh (`src/Core/Plugins/ProjectAssemblyRegistrationLedger.h/.cpp`):

- `Entry` (header lines 68-72) has exactly 3 vectors today:
  `renderPassNames`, `panelNames`, `componentTypeNames`.
- `RecordRenderPass()` (`.cpp` lines 53-60) is the exact shape to mirror for
  the new `RecordRenderFeature()`:
  ```cpp
  void ProjectAssemblyRegistrationLedger::RecordRenderPass(const std::string& debugName)
  {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_activeProjectStack.empty()) {
          return; // Safe no-op - no active BeginRecordingFor() bracket.
      }
      GetOrCreateEntryLocked(m_activeProjectStack.back()).renderPassNames.push_back(debugName);
  }
  ```
- `UnregisterEverythingFor()`'s real, CURRENT teardown order (`.cpp` lines
  97-116) is: `componentTypeNames` (reverse order, also destroys each
  component's own `ComponentStorage<T>` pool) -> `panelNames` (reverse order)
  -> `renderPassNames` (reverse order, `core.UnregisterProjectRenderPassProvider(name)`).
  This phase inserts a NEW loop for `renderFeatureNames`, in reverse order,
  calling `core.UnregisterProjectRenderFeature(name)` — placed immediately
  BEFORE the existing `renderPassNames` loop (i.e. the final order becomes:
  component types, panels, render FEATURES, render-pass PROVIDERS). The
  reasoning (design doc Step 5, item 3): a render feature's own callback may
  reference the project's offscreen render-pass output via the blackboard,
  so the CONSUMER (the render feature) must be torn down before the PRODUCER
  it may depend on (the render-pass provider) — mirroring the existing
  `componentTypeNames` -> `panelNames` cross-category dependency direction.
- `IHotReloadDebugCapability::LedgerEntry` (`src/Core/EditorCapabilities.h`
  lines 197-201) mirrors the ledger's 3 fields exactly today.
  `EditorHotReloadDebugCapability::GetLedgerEntry()`
  (`src/Editor/EditorHotReloadDebugCapability.cpp` lines 29-47) is a trivial
  1-to-1 field copy off `ProjectAssemblyRegistrationLedger::PeekEntry()`.
- `Core::RegisterProjectRenderPassProvider()`'s own real call site for
  `RecordRenderPass()` is somewhere in `Core.cpp` — find it via
  `search_in_dir` before writing PHASE3's mirror-image call site for render
  features (this phase's `Core::RegisterProjectRenderFeature()`, already
  built in PHASE3, needs ONE new line added to its success path — see 3.2
  below).

## Step 3: The Plan (detailed strategy)

### 3.1 — Ledger changes (`ProjectAssemblyRegistrationLedger.h`/`.cpp`)

1. Add `std::vector<std::string> renderFeatureNames;` to `Entry`, matching
   `renderPassNames`'s exact shape, placed adjacently (pick a position and
   note it in the completion report — e.g. immediately after
   `renderPassNames` for readability, though field ORDER inside the struct
   itself is not semantically load-bearing).
2. Add `void RecordRenderFeature(const std::string& debugName);` to the
   header's public surface, mirroring `RecordRenderPass()`'s own doc-comment
   placement.
3. Implement it in the `.cpp`, byte-for-byte mirroring `RecordRenderPass()`'s
   body shape (shown in Step 2 above), writing into
   `.renderFeatureNames.push_back(debugName)` instead.
4. In `UnregisterEverythingFor()`, insert the new loop IMMEDIATELY BEFORE the
   existing `renderPassNames` loop:
   ```cpp
   for (auto nameIt = entry.renderFeatureNames.rbegin(); nameIt != entry.renderFeatureNames.rend(); ++nameIt) {
       core.UnregisterProjectRenderFeature(nameIt->c_str());
   }
   for (auto nameIt = entry.renderPassNames.rbegin(); nameIt != entry.renderPassNames.rend(); ++nameIt) {
       core.UnregisterProjectRenderPassProvider(nameIt->c_str());
   }
   ```
   `core.UnregisterProjectRenderFeature()` (PHASE3) already reaches
   `RenderFeatureCompositor::UnregisterProjectFeature()` (PHASE2), which
   already releases the claimed slot back to the free list as part of its
   own normal contract — this teardown loop needs NO separate slot
   bookkeeping of its own; releasing the slot and removing the registration
   are the same call.
5. Update the trailing `GTE_LOG_INFO` summary line (`.cpp`, immediately
   before `m_entries.erase(it)`) to also report
   `entry.renderFeatureNames.size()` alongside the existing three counts.

### 3.2 — `Core::RegisterProjectRenderFeature()`'s success path calls `RecordRenderFeature()`

In `Core.cpp`, immediately after
`m_renderFeatureCompositorPtr->RegisterProjectFeature(descriptor, std::move(callback))`
returns `true` (PHASE3's own method body), add:
```cpp
ProjectAssemblyRegistrationLedger::Instance().RecordRenderFeature(debugName);
```
placed exactly the same way `RegisterProjectRenderPassProvider()`'s own
existing body calls `RecordRenderPass()` on ITS success path — find that
exact call site first (`search_in_dir` for `RecordRenderPass(` inside
`Core.cpp`) and mirror its placement/style precisely (e.g. whether it is
called unconditionally after the underlying registration succeeds, or
guarded some other way). Confirm `Core.cpp` already `#include`s
`ProjectAssemblyRegistrationLedger.h` (it must, for the existing
`RecordRenderPass()` call to already compile) — no new include needed.

### 3.3 — `EditorCapabilities.h` / `EditorHotReloadDebugCapability`

1. Add `std::vector<std::string> renderFeatureNames;` to
   `IHotReloadDebugCapability::LedgerEntry` (`src/Core/EditorCapabilities.h`),
   matching the ledger's own new field.
2. In `EditorHotReloadDebugCapability::GetLedgerEntry()`
   (`src/Editor/EditorHotReloadDebugCapability.cpp`), add
   `result.renderFeatureNames = entry.renderFeatureNames;` alongside the
   existing 3 field copies.
3. Confirm `GET /project_assembly/debug/ledger`'s own JSON-building code
   ALSO needs a 4th array added to its output JSON. The actual function,
   `BuildLedgerEntryResponseJson()`, lives in `src/Network/NetworkRoutes.cpp`
   (declared in the matching `NetworkRoutes.h`) — NOT in `NetworkServer.cpp`
   (that file only wires the `GET /project_assembly/debug/ledger` route
   handler that CALLS this function; re-confirm both file locations yourself
   via `search_in_dir` before editing, this is a fast-moving codebase). The
   real, current key-naming convention for this exact JSON body is
   **snake_case**, confirmed by direct read of `BuildLedgerEntryResponseJson()`'s
   own body: `"render_pass_names"`, `"panel_names"`, `"component_type_names"`
   — so the new 4th key must be `"render_feature_names"` (snake_case),
   never `"renderFeatureNames"` (camelCase would be INCONSISTENT with every
   sibling key in this exact JSON body, not merely a style nit). This is a
   real, necessary change — the design doc's own Step 5 item 4 explicitly
   requires `GET /project_assembly/debug/ledger` report render features "with
   the same fidelity" as the other 3 categories; skipping this JSON-builder
   update would leave the new field silently invisible over HTTP even though
   the C++ struct itself carries it.

### 3.4 — Tier-1 tests

Extend `tests/Core/Plugins/ProjectAssemblyRegistrationLedgerTests.cpp` (the
existing file, PHASE0's Step 2 citation) with new test cases mirroring its
existing `RecordRenderPass`/`RecordPanel`/`RecordComponentType` coverage
exactly, for `RecordRenderFeature()`:
  1. `BeginRecordingFor`/`RecordRenderFeature`/`EndRecording` round-trips
     through `PeekEntry()` correctly, alongside the existing per-category
     tests (mirror `BeginRecordThenEndCapturesPanelAndComponentTypeUnderTheActiveProject`'s
     own shape, extended to also assert on `renderFeatureNames`).
  2. `RecordRenderFeature()` with no active `BeginRecordingFor()` bracket is
     silently ignored (mirrors `RecordPanelWithNoActiveBracketIsSilentlyIgnored`).
  3. **The single most important NEW test in this phase**: a real,
     end-to-end round trip proving teardown ORDER, mirroring
     `FullRoundTripThroughARealComponentTypeRegistrationProvesTheWholeWiring`'s
     own real-`Core` fixture — register a render feature via
     `core->RegisterProjectRenderFeature(...)` (PHASE3) inside a
     `BeginRecordingFor()`/`EndRecording()` bracket for a uniquely-named test
     project, confirm it appears in `PeekEntry()`'s `renderFeatureNames` AND
     in `RenderFeatureCompositor::DebugSnapshot()`, then call
     `UnregisterEverythingFor()` and confirm BOTH the ledger entry's
     `renderFeatureNames` is empty AND `DebugSnapshot()` no longer lists it
     (proving the teardown loop genuinely reached
     `RenderFeatureCompositor::UnregisterProjectFeature()`, not merely
     cleared the ledger's own bookkeeping).
  4. A test proving the ORDERING claim itself: register BOTH a render
     feature AND a render-pass provider for the SAME test project (in that
     order), then call `UnregisterEverythingFor()` — since directly observing
     "feature torn down strictly before provider" from the outside is hard
     without an instrumentation hook, the practical proof is: the render
     feature's own callback, if it captured a reference/pointer to something
     the render-pass provider produces (e.g. a shared `bool` flag flipped by
     each teardown, captured by both lambdas passed to
     `RegisterProjectRenderFeature`/`RegisterProjectRenderPassProvider`),
     never observes an already-torn-down provider. If constructing this
     precisely is impractical in this fixture, it is acceptable to instead
     assert on the LOGGED ORDER via `GET`-equivalent log inspection is not
     available in a Tier-1 test — in that case, settle for a direct,
     documented code-review confirmation (re-read the final `.cpp` diff and
     confirm the render-feature loop literally appears before the
     render-pass-provider loop in source order) PLUS the live, real
     hot-reload proof PHASE6 performs — `ask_questions` if unsure which of
     these two levels of proof this phase itself should provide versus defer
     to PHASE6.

### 3.5 — Ambiguity checkpoints

  - Re-confirmed while writing this phase file: `Core.cpp`'s real
    `RecordRenderPass()` call site (inside `RegisterProjectRenderPassProvider()`)
    is called unconditionally, immediately after
    `m_offscreenRenderPipeline.Register(...)` returns, with no extra guard
    condition — so `RecordRenderFeature()`'s own call site (3.2 above) should
    mirror that unconditional placement exactly. If a fresh re-read at
    implementation time finds this has changed, `ask_questions` before
    deciding how to adapt.
  - If `NetworkRoutes.cpp`'s `BuildLedgerEntryResponseJson()` has a shape that
    makes adding a 4th array non-trivial (e.g. a hand-written positional JSON
    builder rather than a named-field one), `ask_questions` before
    restructuring it beyond what is strictly needed to add the new field.

### 3.6 — End of phase

1. Incremental build (`cmake --build build`) succeeds.
2. `ctest -C Debug --output-on-failure -R ProjectAssemblyRegistrationLedger`
   passes (targeted filter — the extended existing test file).
3. Write `PHASE4_COMPLETION_REPORT.md`: the exact ledger/`Core.cpp`/
   `EditorCapabilities.h`/`EditorHotReloadDebugCapability.cpp`/
   `NetworkRoutes.cpp` diffs, the new Tier-1 tests' results, and explicit
   confirmation of the final teardown order (component types, panels, render
   features, render-pass providers).
4. `git_add` + `git_commit` covering every file above and the report.
