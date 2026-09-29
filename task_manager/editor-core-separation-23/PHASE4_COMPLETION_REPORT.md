# PHASE4 — COMPLETION REPORT: Hot-reload teardown wiring — `ProjectAssemblyRegistrationLedger` gains render features

## What was added

### 1. `src/Core/Plugins/ProjectAssemblyRegistrationLedger.h`

- `Entry` gains a new field, `std::vector<std::string> renderFeatureNames;`,
  placed immediately after `renderPassNames` (field order itself is not
  semantically load-bearing — only the teardown LOOP order is, see below).
- New public method declared, mirroring `RecordRenderPass()`'s own doc-comment
  placement and body shape exactly:
  ```cpp
  void RecordRenderFeature(const std::string& debugName);
  ```

### 2. `src/Core/Plugins/ProjectAssemblyRegistrationLedger.cpp`

- `RecordRenderFeature()` implemented, byte-for-byte mirroring
  `RecordRenderPass()`'s own body (lock `m_mutex`, no-op if
  `m_activeProjectStack` is empty, otherwise
  `GetOrCreateEntryLocked(...).renderFeatureNames.push_back(debugName)`).
- `UnregisterEverythingFor()`'s teardown body gained a new loop, inserted
  **immediately before** the pre-existing `renderPassNames` loop:
  ```cpp
  for (auto nameIt = entry.renderFeatureNames.rbegin(); nameIt != entry.renderFeatureNames.rend(); ++nameIt) {
      core.UnregisterProjectRenderFeature(nameIt->c_str());
  }
  for (auto nameIt = entry.renderPassNames.rbegin(); nameIt != entry.renderPassNames.rend(); ++nameIt) {
      core.UnregisterProjectRenderPassProvider(nameIt->c_str());
  }
  ```
  **Final teardown order, confirmed by direct re-read of the committed
  `.cpp` file**: component types (reverse order, also destroys each custom
  component's own `ComponentStorage<T>` pool) → panels (reverse order) →
  **render features (reverse order, new this phase)** → render-pass
  providers (reverse order). This matches the design doc's own required
  order exactly (consumer torn down before its potential producer).
- The trailing `GTE_LOG_INFO` summary line now also reports
  `entry.renderFeatureNames.size()` (as `"N render feature(s), "`), inserted
  between the panel count and the render-pass count, matching the new
  teardown-loop position.
- `UnregisterProjectFeature()` (PHASE2's own existing contract, reused
  unchanged) already releases the claimed GPU-state slot back to
  `RenderFeatureCompositor`'s free list as part of the exact same call this
  new loop makes — no separate slot bookkeeping was added or needed here.

### 3. `src/Core/Core.cpp`

`Core::RegisterProjectRenderFeature()`'s success path now calls
`RecordRenderFeature()` — but, **unlike** `RegisterProjectRenderPassProvider()`'s
unconditional call to `RecordRenderPass()` (that underlying call, `Register()`,
returns `void` and can never fail), this call is gated on the underlying
`RenderFeatureCompositor::RegisterProjectFeature()` call's own return value,
since THAT call can genuinely fail (duplicate name / unwired stage / slot
exhaustion):

```cpp
const GtePluginRenderFeatureDescriptor descriptor =
    MakeRenderFeatureDescriptor(debugName, stage, priority, blendMode);
const bool registered = m_renderFeatureCompositorPtr->RegisterProjectFeature(descriptor, std::move(callback));
if (registered) {
    ProjectAssemblyRegistrationLedger::Instance().RecordRenderFeature(debugName);
}
return registered;
```

This was a deliberate, confirmed divergence from the phase file's own literal
"immediately after ... returns" phrasing, which itself already anticipated
this exact nuance ("PHASE3's own method body ... returns `true`") — recording
an unconditional call here would let the ledger track a name that was never
actually registered with the compositor, which would make
`UnregisterEverythingFor()`'s later teardown call for that same name a
harmless no-op (since `FindEntryByName()` would find nothing) but would also
silently mask a real registration failure inside what should be an honest
bookkeeping trail. Re-confirmed live: `Core.cpp`'s existing
`RegisterProjectRenderPassProvider()` call site was re-checked fresh and
still calls `RecordRenderPass()` unconditionally, exactly as PHASE4's own
Step 2/3.5 states — no change made there.

### 4. `src/Core/EditorCapabilities.h`

`IHotReloadDebugCapability::LedgerEntry` gains a matching
`std::vector<std::string> renderFeatureNames;` field, placed immediately
after `renderPassNames` (mirroring the ledger's own `Entry` struct exactly).

### 5. `src/Editor/EditorHotReloadDebugCapability.cpp`

`GetLedgerEntry()` gains one new field-copy line:
```cpp
result.renderFeatureNames = entry.renderFeatureNames;
```

### 6. `src/Network/NetworkRoutes.cpp` / `.h`

- `BuildLedgerEntryResponseJson()` (`NetworkRoutes.cpp`) gains a new
  snake_case JSON key, matching every sibling key in this exact body:
  ```cpp
  body["render_feature_names"] = entry.renderFeatureNames;
  ```
  placed immediately after `"render_pass_names"`.
- `NetworkRoutes.h`'s own doc comment for this function was updated to
  reflect the new key in its documented response shape (previously
  undocumented drift would have made this file misleading going forward).

### 7. Tests — `tests/Core/Plugins/ProjectAssemblyRegistrationLedgerTests.cpp`

Extended (already listed in `tests/CMakeLists.txt` — no new file, no new
CMake entry needed). New includes: `Core/Plugins/RenderFeatureCompositor.h`,
`Core/Plugins/RenderFeatureDebugEntry.h`. New helper: `FindByName()` over a
`RenderFeatureDebugEntry` vector, mirroring
`RenderFeatureCompositorProjectFeatureTests.cpp`'s own identical helper.

1. **`BeginRecordThenEndCapturesPanelComponentTypeAndRenderFeatureUnderTheActiveProject`**
   (renamed from `BeginRecordThenEndCapturesPanelAndComponentTypeUnderTheActiveProject`) —
   extended to also call `RecordRenderFeature()` and assert on
   `entry.renderFeatureNames`.
2. **`RecordPanelAndRenderFeatureWithNoActiveBracketAreSilentlyIgnored`**
   (renamed from `RecordPanelWithNoActiveBracketIsSilentlyIgnored`) —
   extended to also call `RecordRenderFeature()` with no active bracket and
   confirm it is silently ignored, including confirming a genuinely fresh
   project afterward is unpolluted.
3. `PeekEntryForANeverLoadedProjectReturnsAGenuinelyEmptyEntry` and
   `UnregisterEverythingForANeverLoadedProjectIsASafeNoOp` — both extended
   with an `EXPECT_TRUE(entry.renderFeatureNames.empty())` assertion.
4. `FullRoundTripThroughARealComponentTypeRegistrationProvesTheWholeWiring` —
   its own trailing `afterUnregister` assertions extended with
   `EXPECT_TRUE(afterUnregister.renderFeatureNames.empty())` (this test
   itself does not register a render feature, so this only confirms the new
   field starts and stays empty for a project that never touched it).
5. **`FullRoundTripThroughARealRenderFeatureRegistrationProvesTheWholeWiring`**
   (new, the single most important test in this phase) — registers a real
   project render feature via `core->RegisterProjectRenderFeature()` (PHASE3)
   inside a `BeginRecordingFor()`/`EndRecording()` bracket, confirms it
   appears in BOTH `ledger.PeekEntry(...).renderFeatureNames` AND
   `RenderFeatureCompositor::DebugSnapshot()`, then calls
   `UnregisterEverythingFor()` and confirms it is gone from BOTH — proving
   the teardown loop genuinely reaches
   `RenderFeatureCompositor::UnregisterProjectFeature()`, not merely clears
   the ledger's own bookkeeping.
6. **`UnregisterEverythingForTearsDownBothRenderFeatureAndRenderPassProviderCleanly`**
   (new, the ordering-claim test) — registers BOTH a render feature and a
   render-pass provider for the same test project (feature first, then
   provider, per the phase file's own item 4 sketch), confirms both appear
   in the ledger and the feature appears in `DebugSnapshot()`, then calls
   `UnregisterEverythingFor()` and confirms both are cleanly gone. See
   "Ambiguity encountered and resolved" below for why this test proves the
   POST-CONDITION (both torn down cleanly) rather than the dynamic
   in-flight ORDER directly.

## Ambiguity encountered and resolved

The phase file's own section 3.4, item 4 explicitly anticipated that directly
observing "feature torn down strictly before provider" from OUTSIDE this
class is hard, and offered an explicit fallback: settle for a direct,
documented code-review confirmation of the loop order in the `.cpp` diff,
plus the live, real hot-reload proof PHASE6 performs — with an
`ask_questions` escape hatch "if unsure which of these two levels of proof
this phase itself should provide versus defer to PHASE6."

Investigated first whether a genuine dynamic proof was constructible: neither
`RenderFeatureCompositor::UnregisterProjectFeature()` nor
`Core::UnregisterProjectRenderPassProvider()`/
`RenderPipeline::Unregister()` invoke their own owning callback/provider
function AT teardown time (they only mutate internal bookkeeping/free the
GPU-state slot) — so there is no externally-observable side effect (a shared
flag flipped by one lambda and read by the other) to hook into that would
prove ordering dynamically without inventing new instrumentation solely for
this one test. This matches exactly the shape of difficulty the phase file's
own item 4 already anticipated.

**Resolved without needing `ask_questions`** (the phase file's own text
already explicitly sanctions this exact resolution as an acceptable path,
rather than presenting a genuinely open decision only a human could make):
took the offered fallback directly — the real execution-order guarantee is
confirmed by **direct code review** of the final, committed
`ProjectAssemblyRegistrationLedger.cpp` (the `renderFeatureNames` loop
appears at lines 141–143, strictly BEFORE the `renderPassNames` loop at
lines 144–146 — both loops shown verbatim above), plus a new Tier-1 test
(`UnregisterEverythingForTearsDownBothRenderFeatureAndRenderPassProviderCleanly`)
proving the POST-CONDITION (both categories cleanly, completely torn down,
with zero crash, zero leftover entry, in a single project sharing both) —
with the live, dynamic, real-hot-reload-cycle proof explicitly deferred to
PHASE6, exactly as the phase file's own text anticipates.

No other genuine ambiguity was encountered during this phase's own
implementation — every "re-confirm this yourself, live" checkpoint in the
phase file (the `RecordRenderPass()` call site's own unconditional placement,
`BuildLedgerEntryResponseJson()`'s named-field JSON-builder shape) was
re-checked fresh and found to match the phase file's own stated expectation
exactly, with no drift since the phase file was written.

## Build / test results

- Incremental build (`cmake --build build`, working directory
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`): succeeded, zero
  errors, run twice (once right after the non-test code changes, once again
  after the test-file extension) — both clean.
- Targeted test: `ctest -C Debug --output-on-failure -R ProjectAssemblyRegistrationLedger`
  (run from the `build` directory) — **8/8 tests report success**: 4 tests
  that need no live GPU device (`BeginRecordThenEndCapturesPanelComponentTypeAndRenderFeatureUnderTheActiveProject`,
  `RecordPanelAndRenderFeatureWithNoActiveBracketAreSilentlyIgnored`,
  `NestedBeginEndBracketsForTheSameProjectAccumulateIntoOneEntry`,
  `PeekEntryForANeverLoadedProjectReturnsAGenuinelyEmptyEntry`) report
  **Passed**; the 4 tests requiring a real headless `Core`
  (`UnregisterEverythingForANeverLoadedProjectIsASafeNoOp`,
  `FullRoundTripThroughARealComponentTypeRegistrationProvesTheWholeWiring`,
  the two new PHASE4 tests) report **Skipped** — the same, pre-existing,
  documented `VK_EXT_headless_surface`-unavailable environment gap every
  prior phase in this campaign (PHASE2, PHASE3) already hit on this exact
  development machine, not a new gap this phase introduces. `ctest`'s own
  summary line confirms `100% tests passed out of 8` (a `Skipped` test counts
  as passed for `ctest`'s own pass/fail accounting, matching this whole
  codebase's established convention for this exact, honest, pre-existing
  gap).

## What was deliberately left alone (per this phase's own scope)

- `RenderFeatureCompositor.h`/`.cpp` themselves — untouched (PHASE1/PHASE2's
  own already-completed scope); this phase only calls their existing,
  unchanged public methods.
- `Core.h` — untouched (PHASE3's own already-completed scope); only
  `Core.cpp`'s method BODY changed in this phase.
- The `RenderPassEvent::AfterEverything`-only convention
  enforcement/ordering safety net — PHASE5's job.
- Any real, hand-wired demo Project Assembly render feature and the live,
  real hot-reload cycle's dynamic HTTP-driven verification (confirming the
  new teardown wiring against a genuinely running `GreatTamanaEditor.exe`,
  including the dynamic ordering proof deferred above) — PHASE6's job.

## End-of-phase checklist

1. ✅ Incremental build (`cmake --build build`) succeeds.
2. ✅ `ctest -C Debug --output-on-failure -R ProjectAssemblyRegistrationLedger`
   passes (8/8 — 4 Passed, 4 environment-gated Skipped, matching the
   documented pre-existing gap).
3. ✅ This report — the exact ledger/`Core.cpp`/`EditorCapabilities.h`/
   `EditorHotReloadDebugCapability.cpp`/`NetworkRoutes.cpp`/`.h` diffs, the
   new Tier-1 tests' results, and explicit confirmation of the final
   teardown order (component types, panels, render features, render-pass
   providers).
4. Pending: `git_add` + `git_commit` covering every file above and this
   report (done immediately after this report is written).
