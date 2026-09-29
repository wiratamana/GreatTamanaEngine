# PHASE2 — COMPLETION REPORT: `RegisterProjectFeature()`/`UnregisterProjectFeature()` and the bounded GPU-state slot pool

## What was added

### 1. The slot constant and free-list (`RenderFeatureCompositor.h`)

- `static constexpr int kMaxConcurrentProjectRenderFeatures = 16;` (private),
  with a doc comment re-confirming `GpuResourceFactory.cpp`'s
  `kMaxComputeDescriptorSets = 256` sizing (line 84 of that file, checked
  fresh during this phase) — 16 slots x 2 views x 2 descriptor sets/slot = 64
  permanent entries, comfortable headroom against the shared 256-set pool.
- `std::vector<int> m_freeProjectFeatureSlots;` (private member), initialized
  in the constructor body to `{0, 1, ..., 15}`.

### 2. New public methods (`RenderFeatureCompositor.h`/`.cpp`)

- `bool RegisterProjectFeature(const GtePluginRenderFeatureDescriptor& descriptor, ProjectRenderFeatureCallback callback)`
  — implemented exactly per the phase file's own Step 3.3 ordering:
  1. Free-list-empty check → refuse, no slot touched.
  2. `FindEntryByName()` duplicate check → refuse, no slot touched.
  3. Pop a slot.
  4. Construct the `Entry` (`projectCallback`, `descriptor`, `projectFeatureSlot`).
  5. Unwired-stage check (`PreOpaque`/`PostOpaque`/`PostTransparent`, mirroring
     `OnPluginsLoaded()`'s own identical refusal block) → refuse, **push the
     slot back before returning** (confirmed: `m_freeProjectFeatureSlots.push_back(claimedSlot);`
     runs before the `return false;`, `RenderFeatureCompositor.cpp` lines
     310–318).
  6. Push into `m_postComposite`/`m_preUi` (routed by `descriptor.stage ==
     RenderFeatureStage::PreUI`, the same rule `OnPluginsLoaded()` uses), then
     `SortAndDetectCollisionsInStage()` (the exact same, reused static
     method).
  7. Re-seed the name pool for **both** `"Game"`/`"Scene"` views, using the
     slot-derived `gpuStateKey = "ProjectFeatureSlot" + std::to_string(claimedSlot)`
     — never `descriptor.name`.
  8. Return `true`.
- `bool UnregisterProjectFeature(const char* name)` — `FindEntryByName()` →
  refuse if not found; refuse (no slot mutation) if `entry->projectCallback`
  is unset (a `moduleV2`/`moduleV3` entry); otherwise push the slot back onto
  the free list **before** erasing the entry (erase invalidates the pointer),
  erase from whichever vector (`m_postComposite`/`m_preUi`) actually holds it
  (re-derived defensively, mirroring `SetFeaturePriority()`'s own precedent —
  never assumed from `descriptor.stage` alone), return `true`.

### 3. `ContributeRenderGraphPasses()`'s `gpuStateKey` rewrite

Every one of the three `m_namePool.PrivateName()/AccumName()/BlendPassName()`
call sites inside the per-entry loop now feeds a locally-computed
`gpuStateKey` instead of the raw `pluginName`:

```cpp
const std::string gpuStateKey = (entry.projectCallback)
    ? ("ProjectFeatureSlot" + std::to_string(entry.projectFeatureSlot))
    : pluginName;
```

`pluginName` itself is completely unchanged and still feeds `FindEntryByName()`
(N/A here), every log line, and both adapters' own construction (the `_v3`
adapter's `pluginName + "_" + viewName + "_"` prefix, the `_v2` adapter's
`privateName` argument, which is itself now derived from `gpuStateKey`).
**Confirmed by direct read**: for every `moduleV2`/`moduleV3` entry,
`entry.projectCallback` is always the default-constructed empty
`std::function` (`operator bool() == false`), so `gpuStateKey` is
byte-for-byte identical to `pluginName` for every entry that could exist
before this phase — a complete no-op for existing plugin behavior, confirmed
by re-reading the branch after the edit (no leftover `pluginName` reaches
any of the three specific calls; all three now read `gpuStateKey`).

`src/Core/Plugins/RenderFeatureNamePool.h`'s top-of-file doc comment was
updated to state the real, current invariant (a bounded, fixed-size KEY
SPACE — not "nothing here is ever reloaded") per the phase file's own
Step 3.4 requirement.

### 4. Debug/HTTP observability: `isProjectFeature`

- `RenderFeatureDebugEntry.h` gained `bool isProjectFeature = false;`,
  mirroring `isV3`'s own exact precedent and doc-comment style.
- `RenderFeatureCompositor::DebugSnapshot()`'s `appendStage` lambda now also
  sets `debugEntry.isProjectFeature = static_cast<bool>(entry.projectCallback);`
  immediately after the existing `isV3` line.
- `RenderGraphMetadata.cpp`'s `to_json(nlohmann::json&, const
  RenderFeatureDebugEntry&)` gained a sibling `{ "is_project_feature",
  entry.isProjectFeature }` field, same snake_case convention, same JSON
  object as `"is_v3"`.
- `RenderGraphPanel.cpp`'s `BuildPluginRenderFeaturesSection()` gained a
  sibling `if (entry.isProjectFeature) { ImGui::SameLine();
  ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.5f, 1.0f), "[Project]"); }` block,
  immediately after the existing `"[v3]"` block, so a Project Assembly
  feature is visually distinguishable from a plugin feature in the "Render
  Graph" panel at a glance — mirroring `"[v3]"`'s own exact shape with a
  distinct color (light green vs. light blue) so the two tags are never
  confused with each other.

### 5. Tier-1 tests: `tests/Core/Plugins/RenderFeatureCompositorProjectFeatureTests.cpp`

New file, added to `tests/CMakeLists.txt`'s hand-maintained `GTE_TEST_SOURCES`
list (confirmed — this list is NOT a glob). Reuses the exact
`HeadlessSurfaceProvider` + `NoopHostServices` + real `Core` fixture pattern
from `ProjectAssemblyRegistrationLedgerTests.cpp`, via a shared
`TryMakeHeadlessCore()` helper + a `GTE_SKIP_IF_NO_HEADLESS_CORE(core)` macro
(kept local to this file) to avoid repeating the same six-line
try/catch/`GTEST_SKIP()` boilerplate in every one of the 8 tests. Covers,
against a real `Core::GetRenderFeatureCompositor()`:

1. `RegisterProjectFeatureSucceedsForAFreshUniqueName` — item 1.
2. `RegisteringADuplicateNameFailsWithoutConsumingAnExtraSlot` — item 2.
3. `UnregisterFreesItsSlotForReuseByADifferentName` — item 3.
4. `TheSeventeenthRegistrationFailsAndAllPriorSixteenRemainRegistered` — item 4.
5. `UnregisterOnAnUnknownNameFailsHarmlessly` — item 5.
6. `AnUnwiredStageIsRefusedWithoutLeakingItsSlot` — item 7 (numbered 7 in the
   phase file; there is no test literally titled "item 6" — see below).
7. `DebugSnapshotEntryIsMarkedAsProjectFeatureNotV3` — item 9.
8. `AFailedUnregisterAttemptNeverLeaksOrConsumesASlot` — a supplementary test
   for the free-list-never-leaks invariant on the always-reachable
   unknown-name refusal path.

**Item 6 (`UnregisterProjectFeature()` on an existing `moduleV2`/`moduleV3`
entry)**: per the phase file's own explicitly-permitted fallback ("if
constructing one is impractical here, it is acceptable to instead unit-test
this refusal path... via a careful manual trace"), this scenario genuinely
needs a real loaded plugin `.dll`, which this headless-`Core` fixture cannot
provide (no `PluginHost::LoadPlugins()` call is exercised anywhere in this
test file). **Confirmed instead by direct code trace**:
`UnregisterProjectFeature()`'s body (`RenderFeatureCompositor.cpp`) checks
`if (!entry->projectCallback) { ...; return false; }` **before**
`m_freeProjectFeatureSlots.push_back(entry->projectFeatureSlot);` ever runs
— a `moduleV2`/`moduleV3` entry's `projectFeatureSlot` (always `-1`, its
default) can therefore never reach the free list through this path, by
construction, regardless of runtime plugin state. Full live coverage of this
exact scenario (a real loaded plugin's feature alongside a real Project
Assembly feature) is deferred to PHASE6, per the phase file's own explicit
allowance.

**Item 8 (a registered project feature's callback is ACTUALLY INVOKED once a
real frame's `ContributeRenderGraphPasses()` runs)**: deferred to PHASE6's
own live verification, per the phase file's own explicit allowance. Checked
`tests/Renderer/RenderGraph/RenderPipelineTests.cpp` (the only existing test
file constructing a real `rg::RenderPassFrameContext`) — every one of its
fixtures builds a bare `RenderPassFrameContext{...}` directly and calls
`RenderPipeline`/`RenderGraphBuilder` methods without ever going through
`Core::FindPluginRenderFeatureTarget()`, which is what
`RenderFeatureCompositor::ContributeRenderGraphPasses()` needs to resolve a
real Game/Scene view's own composited target and extent before it can do
anything at all. No lightweight harness producing that resolved target
exists anywhere in `tests/` today — building one would mean standing up a
real `Core::BuildFrame()`-driven frame cycle, well beyond this phase's own
Tier-1 scope. This matches the phase file's own anticipated outcome
("otherwise this specific assertion may be deferred to PHASE6's own LIVE
verification instead") exactly, so no `ask_questions` call was made for this
specific, already-anticipated case.

## Build / test results

- Incremental build (`cmake --build build`): succeeded, zero errors, after
  every one of the edits above (confirmed twice — once right after the
  `RenderFeatureCompositor.h`/`.cpp`/`RenderFeatureNamePool.h`/
  `RenderFeatureDebugEntry.h`/`RenderGraphMetadata.cpp`/`RenderGraphPanel.cpp`
  edits, once more after adding the new test file + `tests/CMakeLists.txt`
  entry).
- Targeted test: `ctest -C Debug --output-on-failure -R
  RenderFeatureCompositorProjectFeature` — all 8 new tests report
  **Skipped**, NOT Passed, on this development machine. This is the
  EXPECTED, environment-gated outcome (this machine's Vulkan driver/loader
  does not report `VK_EXT_headless_surface` as available at
  `vkCreateInstance()` time) — confirmed by cross-checking against
  `ctest -R ProjectAssemblyRegistrationLedgerTest`, whose own two
  Core-requiring tests (`UnregisterEverythingForANeverLoadedProjectIsASafeNoOp`,
  `FullRoundTripThroughARealComponentTypeRegistrationProvesTheWholeWiring`)
  skip identically on this same machine, for the identical documented
  reason (`HeadlessSurfaceProvider.h`'s own top-of-file comment). This is
  the same "N legitimate environment-gated skips" pattern every prior
  campaign's own `AGENTS.md` entry documents (currently 8 process-wide) —
  not a new gap this phase introduces, and not something this phase's own
  code can fix (it is a property of this specific development machine's
  installed Vulkan driver, unrelated to any code this phase touched).

## Confirmation: zero behavior change for existing `_v2`/`_v3` plugin entries

- `gpuStateKey` computation is a complete no-op for every entry that could
  exist before this phase (see section 3 above) — confirmed by direct code
  read of both the condition (`entry.projectCallback` truthiness) and every
  call site that now reads `gpuStateKey` instead of `pluginName`.
- `RegisterProjectFeature()`/`UnregisterProjectFeature()` are new,
  additive, incrementally-callable methods — nothing in `OnPluginsLoaded()`
  changed at all in this phase (confirmed via `git diff` — that method is
  untouched).
- `Core.h`/`Core.cpp` were not touched by this phase at all (confirmed via
  `git status` — only `RenderFeatureCompositor.h`/`.cpp`,
  `RenderFeatureDebugEntry.h`, `RenderFeatureNamePool.h`,
  `RenderGraphPanel.cpp`, `RenderGraphMetadata.cpp`, and the two test-related
  files changed). `src/Core/Core.h` line 99 still reads `class
  RenderFeatureCompositor;` (forward declaration only) — PHASE3's own scope.

## Self-double-check (PHASE0's Locked Decision #9)

Per PHASE0_MASTER_STRATEGY.md's Step 3.1 Locked Decision #9 (this phase
explicitly named as a strong candidate), a `delegate_task(position: "next")`
self-double-check was issued, instructed to re-verify (by direct code
reading, not assumption): the `gpuStateKey` no-op guarantee for existing
`_v2`/`_v3` entries, that the slot free-list never leaks on any refusal
path, the exact Step 3.3 ordering, that `Core.h` was untouched, a fresh
incremental build, the targeted `ctest` outcome (with an explicit note that
a Skip — not a Pass — is the CORRECT expected outcome on this machine,
cross-checked against the pre-existing `ProjectAssemblyRegistrationLedgerTest`
precedent), and the `isProjectFeature` end-to-end wiring — with instructions
to use `ask_questions` for any genuine ambiguity found and to report back
inline rather than creating its own report file. That sub-task runs as a
subsequent step in this same task sequence rather than returning
synchronously inside this message — its own findings, if any, will surface
independently of this report. Every item that sub-task was asked to check
was ALSO independently verified directly, first-hand, by this same
implementing session before writing this report (see the "Confirmation"
sections above), so this report's own conclusions do not depend solely on
that delegated check's outcome.

## What was deliberately left alone (per this phase's own scope)

- `Core::RegisterProjectRenderFeature()`/the 63-byte `debugName` length-guard
  — PHASE3's job.
- `ProjectAssemblyRegistrationLedger`/`IHotReloadDebugCapability` wiring —
  PHASE4's job.
- The `RenderPassEvent::AfterEverything`-only convention enforcement/ordering
  safety net — PHASE5's job.
- Any real, hand-wired demo Project Assembly render feature and live,
  HTTP-driven verification (on-screen visibility, hot-reload survival,
  bounded-slot-reuse over `kMaxConcurrentProjectRenderFeatures + 4` rename
  cycles) — PHASE6's job, including the two explicitly-deferred test items
  above (6's live-plugin angle, 8's real-frame-invocation angle).

## Ambiguity encountered and resolved

PHASE1's own code (header, `Entry` fields, three-way branch, standalone
test) was found already correctly implemented in the working tree at the
very start of this session, but had never been `git_commit`'d and
`PHASE1_COMPLETION_REPORT.md` had never been written. Resolved via
`ask_questions` (user left the exact handling up to the implementer's best
judgment): the missing report was written retroactively and PHASE1's
pre-existing diff was committed as its own, separate commit
(`709b229`) BEFORE this phase's own work began, keeping the
one-phase-one-commit convention intact. No other genuine ambiguity was
encountered during this phase's own implementation.

## End-of-phase checklist

1. ✅ Incremental build (`cmake --build build`) succeeds.
2. ✅ `ctest -C Debug --output-on-failure -R RenderFeatureCompositorProjectFeature`
   run — all 8 tests report the expected, environment-gated Skip (not a
   failure; consistent with this machine's pre-existing, documented
   `VK_EXT_headless_surface` gap).
3. ✅ This report — the exact slot-pool design as implemented, the
   `gpuStateKey` diff, the `isProjectFeature` diff across all three
   consumer files, every Tier-1 test's outcome, and explicit confirmation
   that `moduleV2`/`moduleV3` behavior is unchanged.
4. Pending: `git_add` + `git_commit` covering the header/`.cpp` changes, the
   new test file, and this report (done immediately after this report is
   written).
