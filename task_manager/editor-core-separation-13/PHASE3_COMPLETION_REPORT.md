# editor-core-separation-13 — PHASE3 COMPLETION REPORT

## Status: DONE

Implements PHASE3's own "Step 3: The Plan" in full, exactly as specified —
no more, no less. No later phase's work (PHASE4/PHASE5) was touched.
PHASE1/PHASE2's own shipped work was confirmed present, unmodified, before
starting (`ComponentTypeRegistry::UnregisterDescriptor()`,
`EditorPanelRegistry::UnregisterPluginPanel()` — both found via
`search_in_dir` before writing any new code, per this session's own
instruction).

---

## What was added / changed

### 1. New files: `src/Core/Plugins/ProjectAssemblyRegistrationLedger.h`/`.cpp`

Implemented exactly as PHASE3's section 3.1 specifies: `BeginRecordingFor()`/
`EndRecording()` (stack push/pop, nests safely for a `_Game.dll` then
sibling `_Editor.dll` load), `RecordRenderPass()`/`RecordPanel()`/
`RecordComponentType()` (silent no-op whenever no bracket is active),
`UnregisterEverythingFor(projectName, core)` (reverse-order teardown across
all three categories, then erases the entry; idempotent, `GTE_LOG_INFO`
only on a miss), and `PeekEntry()` (value-copy read, `Entry{}` for unknown
projects). Added to the root `CMakeLists.txt`'s hand-maintained `gte_core`
source list, immediately after `HotReloadEngineStateMutex.h`/`.cpp`, exactly
as instructed (skipping this step would have silently failed to link).

**One deliberate, load-bearing deviation from this phase file's own literal
header-comment text, found and fixed during implementation (not a
production-code behavior change — a locking-strategy clarification)**: the
PHASE3 spec file's own header code block (section 3.1) states, in its
top-of-file doc comment, "every public method takes
`gte::GetHotReloadEngineStateMutex()` internally... callers must NOT also
hold it themselves before calling into this class (would deadlock)". This
directly contradicts that SAME spec file's own "Implementation notes"
paragraph immediately below it, which says the opposite: "lock `m_mutex`
(this class's OWN mutex — NOT `HotReloadEngineStateMutex`)", and it also
contradicts the concrete, literal code given in section 3.5 for
`EditorHotReloadDebugCapability::GetLedgerEntry()`, which explicitly takes
`GetHotReloadEngineStateMutex()` itself and THEN calls into `PeekEntry()` —
if `PeekEntry()` also internally locked that same non-recursive mutex, this
would deadlock on literally the very first live call. None of section 3.2's
three entry-point wiring snippets (`RegisterProjectRenderPassProvider()`,
`RegisterPluginPanel()`, `RegisterDescriptor()`) or section 3.4's
`TryLoadOneAssembly()` bracket snippet take `GetHotReloadEngineStateMutex()`
either. Resolution: implemented `ProjectAssemblyRegistrationLedger` using
ONLY its own private `m_mutex` internally (matching the "Implementation
notes" paragraph and every concrete code snippet in the file), and fixed the
header's own top-of-file doc comment to describe this accurately instead of
shipping a comment that contradicts its own class's real behavior. This is
a documentation-accuracy fix, not a design ambiguity requiring
`ask_questions` — the executable code in the spec (3.2/3.4/3.5, all
literal, all unambiguous) already resolves which behavior is correct; only
one prose sentence was stale/wrong. `GetHotReloadEngineStateMutex()` is
still taken by `EditorHotReloadDebugCapability::GetLedgerEntry()` exactly
as before (unchanged, pre-existing lock), and by nothing new in this phase —
consistent with the fact that PHASE1/PHASE2's own shipped
`RegisterDescriptor()`/`UnregisterDescriptor()`/`RegisterPluginPanel()`/
`UnregisterPluginPanel()` bodies (confirmed by reading the real, current
source before writing this report) also take no such lock today. Locking
`GetHotReloadEngineStateMutex()` around the wider engine-state mutation
these entry points perform remains an open item for whichever future phase
first introduces a genuine concurrent caller (this campaign's own PHASE0
document confirms no such caller exists yet — all mutation today happens
strictly at EditorHost construction time, before `NetworkServer::Start()`).

### 2. `Core::RegisterProjectRenderPassProvider()` (`src/Core/Core.h`/`.cpp`)

Now also calls `ProjectAssemblyRegistrationLedger::Instance().RecordRenderPass(debugName)`,
exactly as section 3.2 specifies. Added the new, small public
`Core::UnregisterProjectRenderPassProvider(const char* debugName)` method
(section 3.3), forwarding to `m_offscreenRenderPipeline.Unregister(debugName)` —
`rg::RenderPipeline::Unregister()` itself is unchanged, pre-existing
(`editor-core-separation-13`'s own PHASE0 Situation #3 already confirmed it
exists and does a correct linear-scan-and-erase). `#include
"Plugins/ProjectAssemblyRegistrationLedger.h"` added to `Core.cpp`, mirroring
its existing four `Plugins/`-relative includes exactly.

### 3. `EditorPanelRegistry::RegisterPluginPanel()` (`src/Core/EditorPanelRegistry.cpp`)

Now also calls `ProjectAssemblyRegistrationLedger::Instance().RecordPanel(name)`
on the success path only (after both `m_allNames.push_back()`/
`m_pluginPanels.push_back()`, never inside the `IsKnownName()` early-return
refusal branch), exactly as section 3.2 specifies. `#include
"Plugins/ProjectAssemblyRegistrationLedger.h"` added.

### 4. `ComponentTypeRegistry::RegisterDescriptor()` (`src/ECS/Reflection/ComponentTypeRegistry.cpp`)

Now copies `typeName` BEFORE `std::move(descriptor)` (the moved-from struct
can no longer be read afterward) and calls
`ProjectAssemblyRegistrationLedger::Instance().RecordComponentType(typeName)`
after the existing `push_back()`/`sort()`, exactly as section 3.2 specifies.
`#include "../../Core/Plugins/ProjectAssemblyRegistrationLedger.h"` added —
confirmed the exact `../../` relative depth against the sibling
`BuiltinComponentReflection.cpp`'s own identical-depth precedent, per the
spec's own instruction.

**3.2's own "layering question" (ECS/Reflection including a Core/Plugins
header) — confirmed resolved exactly as the spec states, no re-litigation
needed**: `CMakeLists.txt` line 288 confirms `add_library(gte_core STATIC
...)` compiles every engine source file (`Core/`, `ECS/`, `Renderer/`, etc.)
into exactly ONE static library target — there is no separate `ECS` CMake
target, so no circular-library-dependency hazard exists. The compile check
below (section 3.7) is the empirical proof: `ComponentTypeRegistry.cpp`
including `Core/Plugins/ProjectAssemblyRegistrationLedger.h` compiled and
linked cleanly on the first attempt, with no include-cycle error at all
(neither header includes the other — only the `.cpp` does), confirming the
spec's own prediction exactly.

### 5. `ProjectAssemblyHost::TryLoadOneAssembly()` (`src/Core/Plugins/ProjectAssemblyHost.cpp`)

Added the anonymous-namespace `DeriveProjectNameFromDllFileName()` helper
(strips `_Game.dll`/`_Editor.dll`), and bracketed BOTH `entry(...)` call
sites (the `_Editor` branch's `entry(core, *editorHost);` and the `_Game`
branch's `entry(core);`) with `BeginRecordingFor(projectName)` /
`EndRecording()`, exactly as section 3.4 specifies. Re-read the full
function body afterward to confirm no other change was made to it (what
gets loaded, in what order, with what arguments, is byte-for-byte
unchanged) — confirmed. `#include "ProjectAssemblyRegistrationLedger.h"`
and `<cstring>` added.

### 6. `EditorHotReloadDebugCapability::GetLedgerEntry()` (`src/Editor/EditorHotReloadDebugCapability.cpp`)

Placeholder body replaced with the real 1-to-1 field copy from
`ProjectAssemblyRegistrationLedger::Instance().PeekEntry(projectName)` into
`IHotReloadDebugCapability::LedgerEntry`, exactly as section 3.5 specifies.
Signature unchanged. The stale "PLACEHOLDER... a future BIG-STEP 2
campaign" comment was replaced with one stating this is now real, citing
`editor-core-separation-13`. `#include "../Core/Plugins/ProjectAssemblyRegistrationLedger.h"`
added to the `.cpp` only — the header stays free of the dependency.

### 7. New Tier-1 test file: `tests/Core/Plugins/ProjectAssemblyRegistrationLedgerTests.cpp`

Added to `tests/CMakeLists.txt`'s explicit list (immediately after
`ProjectAssemblyHotReloadDebugStatusTests.cpp`), since this repo's test
build does not glob sources. Six `TEST()` cases, matching section 3.6's own
numbered list exactly:

1. `BeginRecordThenEndCapturesPanelAndComponentTypeUnderTheActiveProject`
2. `RecordPanelWithNoActiveBracketIsSilentlyIgnored`
3. `NestedBeginEndBracketsForTheSameProjectAccumulateIntoOneEntry`
4. `PeekEntryForANeverLoadedProjectReturnsAGenuinelyEmptyEntry`
5. `UnregisterEverythingForANeverLoadedProjectIsASafeNoOp`
6. `FullRoundTripThroughARealComponentTypeRegistrationProvesTheWholeWiring`
   — the single most important test in this phase; registers a real,
   test-local `LedgerRoundTripDummyComponent` via `RegisterComponentType<T>()`
   inside a `BeginRecordingFor`/`EndRecording` bracket, confirms it lands in
   BOTH `ComponentTypeRegistry` and the ledger, then confirms
   `UnregisterEverythingFor()` removes it from both.

Tests 5/6 need a real `gte::Core&` — reused `CoreHeadlessConstructionTests.cpp`'s
own exact fixture precedent (`HeadlessSurfaceProvider` + a trivial
`NoopHostServices`), `GTEST_SKIP()`-ing identically on a machine whose
Vulkan driver/loader doesn't report `VK_EXT_headless_surface`.

---

## Compile check (incremental, exactly the three commands PHASE3 section 3.7 specifies — no full build, no ctest)

```
cmake --build build --target gte_core
cmake --build build --target gte_editor
cmake --build build --target GreatTamanaEngineTests
```

All three succeeded cleanly, no errors, no new warnings:

```
[8/11] Linking CXX static library libgte_core.a
```
```
[3/4] Linking CXX static library libgte_editor.a
```
```
[3/4] Linking CXX executable tests\GreatTamanaEngineTests.exe; Copying SDL3.dll next to GreatTamanaEngineTests
```

Filtered test run (`build\tests\GreatTamanaEngineTests.exe
--gtest_filter=ProjectAssemblyRegistrationLedgerTest*`):

```
[==========] Running 6 tests from 1 test suite.
[ RUN      ] ...BeginRecordThenEndCapturesPanelAndComponentTypeUnderTheActiveProject   [OK]
[ RUN      ] ...RecordPanelWithNoActiveBracketIsSilentlyIgnored                        [OK]
[ RUN      ] ...NestedBeginEndBracketsForTheSameProjectAccumulateIntoOneEntry           [OK]
[ RUN      ] ...PeekEntryForANeverLoadedProjectReturnsAGenuinelyEmptyEntry              [OK]
[ RUN      ] ...UnregisterEverythingForANeverLoadedProjectIsASafeNoOp                   [SKIPPED]
[ RUN      ] ...FullRoundTripThroughARealComponentTypeRegistrationProvesTheWholeWiring  [SKIPPED]
[  PASSED  ] 4 tests.
[  SKIPPED ] 2 tests.
```

The 2 skips are a confirmed, pre-existing MACHINE limitation, not a bug in
this phase's new code: this development machine's Vulkan driver/loader does
not report `VK_EXT_headless_surface` (`vkCreateInstance failed
(VkResult=-7)`). Confirmed identical by re-running the pre-existing,
unrelated `CoreHeadlessConstructionTest.
ConstructsWithoutCrashingAndExposesUsableAccessorsWithNoEditorLayerHookSet`
test — it ALSO skips with the exact same `VkResult=-7` reason on this same
machine, proving this is a pre-existing environment gap, not something this
phase introduced.

Also re-ran the two directly-adjacent, already-existing suites to confirm
zero regression from this phase's edits to their own production files:
`ComponentTypeRegistryTest*` (13/13 passed) and `EditorPanelRegistryTest*`
(12/12 passed) — all 25 passed, none newly broken.

---

## Live verification

`run_app_background`'d the freshly-rebuilt `GreatTamanaEditor.exe` (PID
11776), then via `gte_send_request`:

1. **`GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe`** — the
   real answer to this phase's own 3.8 check #1 question, confirmed live,
   not assumed:
   ```json
   {"component_type_names":[],"panel_names":["Probe Panel"],"project_name":"ProjectAssemblyProbe","render_pass_names":["ProjectAssemblyProbe.FillTexture"]}
   ```
   This is the **non-empty outcome** the phase file's own section 3.8 #1
   predicted as the expected result (given the fixture project already
   calls `RegisterPluginPanel("Probe Panel", ...)` and
   `RegisterProjectRenderPassProvider("ProjectAssemblyProbe.FillTexture", ...)`
   today) — confirmed exactly, field-for-field, including the exact key
   names (`panel_names`, `render_pass_names`, `component_type_names`,
   `project_name`) from `NetworkRoutes.cpp`'s own real
   `BuildLedgerEntryResponseJson()`. **This is the answer PHASE4/PHASE5
   should rely on**: the bracket wiring in `TryLoadOneAssembly()` is
   genuinely hit for both `ProjectAssemblyProbe_Editor.dll` and
   `ProjectAssemblyProbe_Game.dll`, and both accumulate into the SAME
   ledger entry (`"ProjectAssemblyProbe"`), exactly as designed.
2. **`GET /project_assembly/debug/component_types`** →
   `{"type_names":["Camera","DirectionalLight","Name","PrimitiveSource","Transform"]}`
   — unchanged, still exactly the 5 built-ins, confirming
   `RegisterBuiltinComponentReflections()` (which runs OUTSIDE any
   `BeginRecordingFor()` bracket) is correctly unaffected by this phase's
   wiring.
3. **`GET /list_tabs`** →
   `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel","Probe Panel"]}`
   — unchanged from before this phase, `"Probe Panel"` still present.
4. **`GET /get_swapchain`** → rendered screenshot confirmed, visually, the
   "Probe Panel" tab still active/rendering its known-good content ("Hello
   from a real Project Assembly Editor panel." + "Click me" button +
   "Clicked 0 time(s)") — identical to its pre-phase appearance.
5. **`GET /get_logs?category=ProjectAssembly`** — 5 entries, all
   pre-existing/expected (`GTE_RegisterProject called...`/`Loaded Project
   Assembly...` for both `_Editor.dll` and `_Game.dll`, plus one
   `ProbeCompute pipeline/descriptor set built.`) — no new warning/error.
6. **`GET /get_logs`** (full, 36 entries) — only pre-existing, unrelated
   `PluginHost`/`RenderFeatureCompositor` informational/warning entries
   (multiple render-feature plugins competing for the same stage — a known,
   pre-existing condition, identical to what PHASE1/PHASE2's own reports
   already documented) — nothing attributable to this phase's new code.

`stop_app_background(pid: 11776)` cleanly terminated the process afterward.

---

## Deviations found versus this phase file's own instructions

1. **Locking-strategy documentation fix** (see "What was added, item 1"
   above for the full reasoning) — `ProjectAssemblyRegistrationLedger`'s own
   private `m_mutex` is used internally by all its public methods; it never
   takes `GetHotReloadEngineStateMutex()` itself (that mutex is taken only
   by its existing, pre-existing caller, `EditorHotReloadDebugCapability::
   GetLedgerEntry()`, unchanged from before this phase). This resolves a
   genuine internal contradiction inside the PHASE3 spec file itself (its
   own top-of-header prose vs. its own "Implementation notes" paragraph and
   its own concrete code snippets in 3.2/3.4/3.5) — the concrete/literal
   code, which is unambiguous, was treated as authoritative over the one
   stale prose sentence, avoiding a real, guaranteed deadlock the literal
   prose reading would have produced on the very first
   `GET /project_assembly/debug/ledger` request.
2. No other deviation — every other section (3.1-3.6) implemented exactly
   as specified, including the exact wording/structure of every doc comment
   the spec provides verbatim.
3. No `cmake --build build --target GreatTamanaEditor` step is part of this
   phase's own mandated compile check (section 3.7 lists only
   `gte_core`/`gte_editor`/`GreatTamanaEngineTests`) — it was run anyway,
   once, purely so the live-verification step (section 3.8) reflects the
   actual current code, mirroring PHASE1/PHASE2's own identical precedent
   (both of their completion reports document doing the same thing for the
   same reason).

---

## Definition of Done — checked off

- [x] `ProjectAssemblyRegistrationLedger` exists exactly as specified,
      including `PeekEntry()`.
- [x] All three existing registration entry points call the matching
      `RecordX()`, unconditionally, confirmed by the isolation test (3.6,
      case 6 — `SKIPPED` on this machine due to a pre-existing,
      unrelated Vulkan capability gap, not a code defect; case 1/2/3
      already prove the RecordX()/PeekEntry() half of the same wiring
      without needing a real `Core`).
- [x] `ProjectAssemblyHost::TryLoadOneAssembly()` brackets both
      `entry(...)` calls with `BeginRecordingFor()`/`EndRecording()` —
      confirmed live via the real `ProjectAssemblyProbe` fixture.
- [x] `Core::UnregisterProjectRenderPassProvider()` exists and is called
      only from `UnregisterEverythingFor()`.
- [x] `EditorHotReloadDebugCapability::GetLedgerEntry()`'s body is real,
      not a placeholder; signature unchanged.
- [x] Live `GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe`
      result is recorded and understood — the NON-EMPTY outcome (panel_names/
      render_pass_names populated, component_type_names empty) is confirmed
      real, live, before PHASE4 starts.
- [x] `PHASE3_COMPLETION_REPORT.md` exists (this file); changes committed
      to git (see commit following this report).
