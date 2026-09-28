# editor-core-separation-13 — PHASE3: ProjectAssemblyRegistrationLedger + Entry-Point Wiring

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.

Depends on: PHASE1 (`ComponentTypeRegistry::UnregisterDescriptor()`) and
PHASE2 (`EditorPanelRegistry::UnregisterPluginPanel()`) — BOTH must already
exist and compile; this phase's own `UnregisterEverythingFor()` calls both by
name.
Blocks: PHASE4 (`ProjectAssemblyHost::UnloadProjectAssembly()` calls this
phase's own `UnregisterEverythingFor()`).

---

## Step 1: The Goal

A new, single class, `ProjectAssemblyRegistrationLedger`, that transparently
records every render-pass debugName / editor-panel name / ECS component
typeName a SPECIFIC Project Assembly's `GTE_RegisterProject` call registers —
with **zero change** to any Project Assembly author's own source code — by
having the THREE existing registration entry points append to it themselves,
internally, only while a "currently loading this project" bracket is active.
Then a single teardown method, `UnregisterEverythingFor(projectName, core)`,
walks that project's own recorded names and calls the matching Unregister on
each. Replace `EditorHotReloadDebugCapability::GetLedgerEntry()`'s current
hard-coded-empty placeholder body with a real call into this class. Prove all
of it live via `GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe`
— **but note this phase's own Definition of Done cannot show non-empty data
for `ProjectAssemblyProbe` specifically until PHASE5 gives it a real
GTE_RegisterProject-time bracket to record through** (see Step 3.6 below for
exactly what this phase itself can, and cannot yet, prove live).

## Step 2: The Situation

Confirmed, current, `src/Core/Core.cpp` lines 337-340:

```cpp
void Core::RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope, rg::RenderPassProvider provider)
{
    m_offscreenRenderPipeline.Register(debugName, scope, std::move(provider));
}
```

Confirmed, current, `src/Core/EditorPanelRegistry.cpp`'s
`RegisterPluginPanel()` and `src/ECS/Reflection/ComponentTypeRegistry.cpp`'s
`RegisterDescriptor()` — both already read in PHASE1/PHASE2's own strategy
files; neither currently records anything beyond its own internal vector(s).

Confirmed, current, `src/Core/Plugins/ProjectAssemblyHost.cpp`,
`TryLoadOneAssembly()` (lines 43-100): calls `entry(core)` for a `_Game.dll`,
or `entry(core, *editorHost)` for an `_Editor.dll`, exactly once per `.dll`,
with `fileName` (e.g. `"ProjectAssemblyProbe_Game.dll"`) already known at
that call site, BEFORE the call, in a local variable. **The project's own
plain name** (e.g. `"ProjectAssemblyProbe"`, with neither `"_Game.dll"` nor
`"_Editor.dll"` suffix) must be derived from `fileName` right there, since
this is the ONE place in the whole engine that knows both "which project is
this" and "we are about to call its `GTE_RegisterProject` export" at the same
time — this derivation does not exist anywhere today and is new code this
phase adds.

Confirmed, current, `src/Core/EditorCapabilities.h` (BIG-STEP 1 shipped
shape, lines 173-237) — `IHotReloadDebugCapability::GetLedgerEntry()`'s
`LedgerEntry` struct is ALREADY exactly `{renderPassNames, panelNames,
componentTypeNames}` (three `std::vector<std::string>`) — this phase's own
new `ProjectAssemblyRegistrationLedger::Entry` struct must have the IDENTICAL
field names and order, so `GetLedgerEntry()`'s real body is a trivial
1-to-1 copy, never a re-mapping.

Confirmed, current, `src/Editor/EditorHotReloadDebugCapability.cpp` lines
21-32 — the exact placeholder this phase replaces:

```cpp
IHotReloadDebugCapability::LedgerEntry EditorHotReloadDebugCapability::GetLedgerEntry(
    const std::string& /*projectName*/) const
{
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
    return LedgerEntry{};
}
```

Confirmed, current, `src/Core/Plugins/HotReloadEngineStateMutex.h` — its own
doc comment (lines 29-34) explicitly requires this phase's new ledger-writing
code (the three entry-point wrapper calls) to lock
`GetHotReloadEngineStateMutex()` around every write, exactly like the
placeholder above already locks it around its (currently trivial) read.

## Step 3: The Plan

### 3.1 — New files: `src/Core/Plugins/ProjectAssemblyRegistrationLedger.h` / `.cpp`

```cpp
// src/Core/Plugins/ProjectAssemblyRegistrationLedger.h
//
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE3. Records every render-pass debugName / editor-panel
// name / ECS component typeName a SPECIFIC Project Assembly's own
// GTE_RegisterProject call registered, with ZERO change required to that
// Project Assembly's own authored code - Core::RegisterProjectRenderPassProvider()/
// EditorPanelRegistry::RegisterPluginPanel()/ComponentTypeRegistry::
// RegisterDescriptor() each call this class's own RecordX() immediately
// after doing their own real work, unconditionally, every time they are
// called by ANYTHING (including the engine's own built-in startup
// registrations) - RecordX() itself is a safe, silent no-op whenever no
// BeginRecordingFor() bracket is currently active, which is true for every
// one of the engine's own built-in registrations (they never run inside
// such a bracket).
//
// Locking: every public method takes gte::GetHotReloadEngineStateMutex()
// internally (src/Core/Plugins/HotReloadEngineStateMutex.h) - callers must
// NOT also hold it themselves before calling into this class (would
// deadlock; std::mutex is not recursive here by design, matching every
// other lock user in this codebase).
#pragma once

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace gte {

class Core;

class ProjectAssemblyRegistrationLedger {
public:
    static ProjectAssemblyRegistrationLedger& Instance();

    // Called by ProjectAssemblyHost::TryLoadOneAssembly() immediately BEFORE
    // invoking a specific project's GTE_RegisterProject export (both the
    // initial startup load AND, from a future BIG-STEP 3 campaign onward,
    // every reload) - every RecordRenderPass()/RecordPanel()/
    // RecordComponentType() call, from ANY thread, until EndRecording() is
    // called, is attributed to `projectName`. Nests safely via a plain
    // stack push/pop: EndRecording() always pairs with the MOST RECENT
    // BeginRecordingFor() - relevant because a _Game.dll's own
    // GTE_RegisterProject and its sibling _Editor.dll's own
    // GTE_RegisterProject are two SEPARATE calls for the SAME projectName,
    // both accumulating into the SAME ledger entry.
    void BeginRecordingFor(const std::string& projectName);
    void EndRecording();

    // Called by Core::RegisterProjectRenderPassProvider()/
    // EditorPanelRegistry::RegisterPluginPanel()/ComponentTypeRegistry::
    // RegisterDescriptor() themselves, immediately after each does its own
    // real work - ALWAYS, unconditionally, from every call site, including
    // the engine's own built-in startup registrations. A safe, silent no-op
    // whenever no BeginRecordingFor() bracket is currently active (the
    // engine's own built-in registrations never run inside one).
    void RecordRenderPass(const std::string& debugName);
    void RecordPanel(const std::string& panelName);
    void RecordComponentType(const std::string& typeName);

    struct Entry {
        std::vector<std::string> renderPassNames;
        std::vector<std::string> panelNames;
        std::vector<std::string> componentTypeNames;
    };

    // The teardown step (Hazards 1/2 fix, BIG-STEP 0 Section 2) - walks
    // `projectName`'s own recorded names, in REVERSE registration order,
    // calling core.GetRenderPipelineForProjectAssemblies().Unregister() (see
    // 3.3 below for the exact accessor this needs from Core),
    // EditorPanelRegistry::Instance().UnregisterPluginPanel(), and
    // ComponentTypeRegistry::Instance().UnregisterDescriptor() for each, then
    // erases `projectName`'s own ledger entry entirely (a fresh
    // BeginRecordingFor(projectName) on the next load/reload starts from a
    // genuinely empty slate). Idempotent - calling this for a projectName
    // with no ledger entry (never loaded, or already torn down) is a safe,
    // GTE_LOG_INFO-logged no-op, never a warning or crash - this is an
    // expected, normal path a future BIG-STEP 3 campaign's own retry/failure
    // handling relies on.
    void UnregisterEverythingFor(const std::string& projectName, Core& core);

    // Read-only peek at projectName's current ledger entry, for
    // EditorHotReloadDebugCapability::GetLedgerEntry() (GET
    // /project_assembly/debug/ledger). Returns a VALUE COPY of an empty
    // Entry (never a reference/pointer) for a projectName with no current
    // entry - a debug HTTP caller never needs to special-case "never
    // loaded" versus "loaded, but registered nothing"; both look identical
    // (all three vectors empty). Never mutates anything.
    Entry PeekEntry(const std::string& projectName) const;

private:
    ProjectAssemblyRegistrationLedger() = default;

    mutable std::mutex m_mutex;
    std::vector<std::string> m_activeProjectStack; // supports nested Begin/End (Game then Editor).
    std::vector<std::pair<std::string, Entry>> m_entries; // one per known projectName, insertion order.

    Entry& GetOrCreateEntryLocked(const std::string& projectName); // caller already holds m_mutex.
};

} // namespace gte
```

Implementation notes for `ProjectAssemblyRegistrationLedger.cpp`:

- `BeginRecordingFor(name)`: lock `m_mutex` (this class's OWN mutex — NOT
  `HotReloadEngineStateMutex`; that one is taken by the CALLERS of
  `RecordX()`/`UnregisterEverythingFor()` around the wider engine-state
  mutation, this class's own internal mutex only protects this class's own
  data), push `name` onto `m_activeProjectStack`.
- `EndRecording()`: lock, pop the back of `m_activeProjectStack` (assert
  it's non-empty in a debug build — an `EndRecording()` with no matching
  `BeginRecordingFor()` is a genuine programmer error, mirrors
  `ComponentTypeRegistry::RegisterDescriptor()`'s own `assert()`-on-
  programmer-error precedent).
- `RecordRenderPass(name)`/`RecordPanel(name)`/`RecordComponentType(name)`:
  lock, if `m_activeProjectStack` is empty return immediately (silent
  no-op — this is what makes it safe for every built-in engine
  registration), else append `name` to the CURRENT (i.e.
  `m_activeProjectStack.back()`) entry's matching vector, found/created via
  `GetOrCreateEntryLocked()`.
- `UnregisterEverythingFor(projectName, core)`: lock, find `projectName`'s
  entry (return early + `GTE_LOG_INFO` if absent). Walk
  `componentTypeNames` in REVERSE, calling
  `ComponentTypeRegistry::Instance().UnregisterDescriptor(name)` for each.
  Walk `panelNames` in REVERSE, calling
  `EditorPanelRegistry::Instance().UnregisterPluginPanel(name)`. Walk
  `renderPassNames` in REVERSE, calling `core`'s own render-pipeline
  Unregister accessor (see 3.3). Then erase this project's entry from
  `m_entries` entirely. `#include` `ComponentTypeRegistry.h`/
  `EditorPanelRegistry.h`/`Core.h` in the `.cpp` only (keep the header
  free of these, matching this codebase's own "only the .cpp needs the
  complete type" convention, e.g. `EditorHotReloadDebugCapability.h`'s own
  precedent).
- `PeekEntry(projectName)`: lock, return a copy of the found entry, or a
  default-constructed empty `Entry{}` if not found.

**Do not forget to register these two new files with the build — confirmed,
real, easy-to-miss gap**: the root `CMakeLists.txt`'s `add_library(gte_core
STATIC ...)` source list is HAND-MAINTAINED, never a `file(GLOB ...)`
(confirmed by that same file's own comment on
`src/Core/Plugins/ProjectAssemblyBuildRunner.h`/`.cpp`'s entry: "added here
explicitly because gte_core's own source list is hand-maintained... PHASE5's
own completion report flagged this exact gap for PHASE6's benefit"). Add
`src/Core/Plugins/ProjectAssemblyRegistrationLedger.h` and
`src/Core/Plugins/ProjectAssemblyRegistrationLedger.cpp` to that same list
(a natural spot: immediately after the existing
`src/Core/Plugins/HotReloadEngineStateMutex.h`/`.cpp` entry, since both are
part of the same `editor-core-separation-12`/`-13` Hot-Reload-plan file
group) — skipping this step means every symbol this phase adds fails to
link, even though every `.cpp`/`.h` edit above is otherwise correct.

### 3.2 — Wire the ledger into the THREE existing registration entry points

**`Core::RegisterProjectRenderPassProvider()`** (`src/Core/Core.cpp` line
337-340) becomes:

```cpp
void Core::RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope, rg::RenderPassProvider provider)
{
    m_offscreenRenderPipeline.Register(debugName, scope, std::move(provider));
    // editor-core-separation-13 campaign, PHASE3 - safe no-op outside an
    // active ProjectAssemblyRegistrationLedger::BeginRecordingFor() bracket.
    ProjectAssemblyRegistrationLedger::Instance().RecordRenderPass(debugName);
}
```
`#include "Plugins/ProjectAssemblyRegistrationLedger.h"` at the top of
`Core.cpp` — confirmed, current, real relative-include convention: `Core.cpp`
itself already directly `#include`s four other `Plugins/` headers this exact
same way (`"Plugins/IPluginCapabilityOrchestrator.h"`,
`"Plugins/LegacyRenderFeatureOrchestrator.h"`,
`"Plugins/EditorPanelCapabilityOrchestrator.h"`,
`"Plugins/RenderFeatureCompositor.h"`) — mirror that exact style. (Note:
`"Plugins/ProjectAssemblyHost.h"` itself is actually included one level up,
by `Core.h`, not directly by `Core.cpp` — but the relative-path CONVENTION is
identical either way, confirmed by the four `Core.cpp`-direct includes
above.)

**`EditorPanelRegistry::RegisterPluginPanel()`** (`src/Core/
EditorPanelRegistry.cpp`) — insert the record call right after the existing
`m_pluginPanels.push_back(...)` line (i.e. only on the success path, never
inside the `IsKnownName()` early-return branch):

```cpp
m_allNames.push_back(name);
m_pluginPanels.push_back(PluginPanelEntry{ name, module });
ProjectAssemblyRegistrationLedger::Instance().RecordPanel(name); // editor-core-separation-13, PHASE3.
```
`#include "Plugins/ProjectAssemblyRegistrationLedger.h"` added to
`EditorPanelRegistry.cpp`.

**`ComponentTypeRegistry::RegisterDescriptor()`** (`src/ECS/Reflection/
ComponentTypeRegistry.cpp`) — insert AFTER the existing `assert()` and
`push_back()`/`sort()` (i.e. only once registration has genuinely,
successfully happened):

```cpp
void ComponentTypeRegistry::RegisterDescriptor(ComponentTypeDescriptor descriptor)
{
    assert(Find(descriptor.typeName) == nullptr && "...");
    const std::string typeName = descriptor.typeName; // copy BEFORE std::move below.
    m_descriptors.push_back(std::move(descriptor));
    std::sort(m_descriptors.begin(), m_descriptors.end(), ...);
    ProjectAssemblyRegistrationLedger::Instance().RecordComponentType(typeName); // editor-core-separation-13, PHASE3.
}
```
Note the explicit `typeName` copy BEFORE `std::move(descriptor)` — `descriptor`
is moved-from by the `push_back` call, so `descriptor.typeName` must not be
read afterward. `#include "../../Core/Plugins/ProjectAssemblyRegistrationLedger.h"`
added to `ComponentTypeRegistry.cpp` — CONFIRMED exactly `../../` (2 levels),
not guessed: this same directory (`src/ECS/Reflection/`) already has a real,
existing precedent doing the identical depth of relative include, in the
sibling file `BuiltinComponentReflection.cpp`
(`#include "../../Renderer/Primitives/PrimitiveMeshGenerator.h"`) — `src/ECS/Reflection/`
→ `../` → `src/ECS/` → `../` → `src/`, then into whichever sibling top-level
folder is needed (`Renderer/...` there, `Core/Plugins/...` here).

**Layering question, already resolved by this strategy (confirmed, do not
re-litigate)**: `src/ECS/Reflection/ComponentTypeRegistry.cpp` including a
header from `src/Core/Plugins/` is SAFE — confirmed, `CMakeLists.txt` line
288 (`add_library(gte_core STATIC ...)`): every engine source file (`Core/`,
`ECS/`, `Renderer/`, etc.) compiles into exactly ONE static library target,
`gte_core`. There is no separate `ECS` CMake target and therefore no
circular-library-dependency hazard — a `.cpp` anywhere under `src/` may
`#include` a header from anywhere else under `src/` without any build-target
implication at all (only genuine `#include` cycles between two HEADERS would
be a problem, and this pairing has none: `ComponentTypeRegistry.h` never
includes anything from `Core/Plugins/`, only the `.cpp` does). Proceed with
the `#include` as specified above with no further check needed.

### 3.3 — `Core`'s own accessor for render-pass unregistration

`ProjectAssemblyRegistrationLedger::UnregisterEverythingFor()` needs to call
`m_offscreenRenderPipeline.Unregister(debugName)` for each recorded render
pass name, but `m_offscreenRenderPipeline` is `Core`'s own PRIVATE member.
Add one new, small, public method to `Core` (`src/Core/Core.h`/`.cpp`),
immediately after `RegisterProjectRenderPassProvider()`:

```cpp
// editor-core-separation-13 campaign, PHASE3 - the teardown counterpart of
// RegisterProjectRenderPassProvider() (immediately above), called ONLY by
// ProjectAssemblyRegistrationLedger::UnregisterEverythingFor() - never by
// any Project Assembly's own authored code directly.
void UnregisterProjectRenderPassProvider(const char* debugName);
```
```cpp
void Core::UnregisterProjectRenderPassProvider(const char* debugName)
{
    m_offscreenRenderPipeline.Unregister(debugName);
}
```

### 3.4 — Bracket `ProjectAssemblyHost::TryLoadOneAssembly()`'s two `entry(...)` calls

`src/Core/Plugins/ProjectAssemblyHost.cpp`, `TryLoadOneAssembly()`. Add a
small, private, static free function (anonymous namespace) at the top of the
`.cpp`:

```cpp
namespace {
// Strips a known Project Assembly .dll suffix to recover the plain project
// name - the ONE place this derivation is needed (TryLoadOneAssembly()
// already has `fileName` computed locally, right before this).
std::string DeriveProjectNameFromDllFileName(const std::string& fileName)
{
    static constexpr const char* kGameSuffix = "_Game.dll";
    static constexpr const char* kEditorSuffix = "_Editor.dll";
    if (fileName.size() > std::strlen(kGameSuffix) && fileName.ends_with(kGameSuffix)) {
        return fileName.substr(0, fileName.size() - std::strlen(kGameSuffix));
    }
    if (fileName.size() > std::strlen(kEditorSuffix) && fileName.ends_with(kEditorSuffix)) {
        return fileName.substr(0, fileName.size() - std::strlen(kEditorSuffix));
    }
    return fileName; // unreachable in practice - TryLoadOneAssembly() already filtered by these two suffixes.
}
}
```

Then, immediately before EACH of the two `entry(...)` call sites (the
`_Editor` branch's `entry(core, *editorHost);` and the `_Game` branch's
`entry(core);`), add:

```cpp
const std::string projectName = DeriveProjectNameFromDllFileName(fileName);
ProjectAssemblyRegistrationLedger::Instance().BeginRecordingFor(projectName);
entry(core, *editorHost); // (or entry(core); for the _Game branch)
ProjectAssemblyRegistrationLedger::Instance().EndRecording();
```

**This wiring is additive and safe for the EXISTING startup-scan path**: it
does not change what gets loaded, in what order, or with what arguments —
it only adds a begin/end bracket around a call that already happens exactly
as before. This is also EXACTLY the shape a future BIG-STEP 3 campaign's own
reload path will reuse this same `TryLoadOneAssembly()` function for
unchanged — confirm this remains true by re-reading `TryLoadOneAssembly()`'s
full body after this edit, checking no other change was made to it.

`#include "ProjectAssemblyRegistrationLedger.h"` and `<cstring>` (for
`std::strlen`) added to `ProjectAssemblyHost.cpp`.

### 3.5 — Replace `EditorHotReloadDebugCapability::GetLedgerEntry()`'s placeholder body

`src/Editor/EditorHotReloadDebugCapability.cpp`:

```cpp
IHotReloadDebugCapability::LedgerEntry EditorHotReloadDebugCapability::GetLedgerEntry(
    const std::string& projectName) const
{
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
    const ProjectAssemblyRegistrationLedger::Entry entry =
        ProjectAssemblyRegistrationLedger::Instance().PeekEntry(projectName);
    LedgerEntry result;
    result.renderPassNames = entry.renderPassNames;
    result.panelNames = entry.panelNames;
    result.componentTypeNames = entry.componentTypeNames;
    return result;
}
```
Add `#include "../Core/Plugins/ProjectAssemblyRegistrationLedger.h"` to this
`.cpp` (the header itself stays free of this dependency, per its own existing
doc comment). **Do not change this method's signature.** Update the stale
`"PLACEHOLDER"`/"a future BIG-STEP 2 campaign" comment above it to state this
is now real, citing `editor-core-separation-13`.

### 3.6 — New Tier-1 test coverage

New file, `tests/Core/Plugins/ProjectAssemblyRegistrationLedgerTests.cpp` —
add its path to `tests/CMakeLists.txt`'s explicit list. Test in complete
isolation (do NOT depend on a real Project Assembly `.dll` or `Core`
instance for most cases — only the render-pass-unregister case needs a real
or minimal fake `Core`; check whether a lightweight `Core` can be
constructed headlessly the way `CoreHeadlessConstructionTest` (test name;
file `tests/Core/CoreHeadlessConstructionTests.cpp`) already does — confirmed
to exist, correctly attributed to the `editor-core-separation-1` campaign,
PHASE18 (`PHASE18_HEADLESS_TEST_FIXTURE_AND_CORE_STANDALONE_PROBE.md`), NOT
`editor-core-separation-12` — reuse that exact precedent rather than
inventing a new one; note it `GTEST_SKIP()`s if this machine's Vulkan
driver/loader doesn't support `VK_EXT_headless_surface`, which this new test
must handle identically):

1. `BeginRecordingFor("Alpha")`, `RecordPanel("PanelA")`,
   `RecordComponentType("CompA")`, `EndRecording()` — confirm
   `PeekEntry("Alpha")` returns exactly `{panelNames: ["PanelA"],
   componentTypeNames: ["CompA"], renderPassNames: []}`.
2. `RecordPanel("ShouldBeIgnored")` called with NO active
   `BeginRecordingFor()` bracket — confirm it is silently ignored (does not
   appear under ANY project's `PeekEntry()`, does not crash).
3. Nested brackets (mirrors a `_Game.dll` then `_Editor.dll` load for the
   SAME project): `BeginRecordingFor("Beta")`,
   `RecordComponentType("CompB1")`, `EndRecording()`,
   `BeginRecordingFor("Beta")`, `RecordPanel("PanelB1")`, `EndRecording()` —
   confirm `PeekEntry("Beta")` accumulates BOTH (`componentTypeNames:
   ["CompB1"]`, `panelNames: ["PanelB1"]"`) in the SAME entry, not two
   separate entries.
4. `PeekEntry("NeverLoaded")` returns a genuinely empty `Entry{}` (all three
   vectors empty), not a crash, not a special sentinel.
5. `UnregisterEverythingFor("NeverLoaded", core)` is a safe no-op (no crash,
   logs at INFO level only).
6. A full round trip: register a real throwaway component type via
   `RegisterComponentType<T>()` (a small, test-local POD struct type) INSIDE
   a `BeginRecordingFor`/`EndRecording` bracket, confirm
   `ComponentTypeRegistry::Instance().Find(typeName)` is non-null AND
   `PeekEntry(project).componentTypeNames` contains it, call
   `UnregisterEverythingFor(project, core)`, confirm
   `ComponentTypeRegistry::Instance().Find(typeName)` is now `nullptr` AND
   `PeekEntry(project)` is empty again. This is the single most important
   test in this whole phase — it proves the ENTIRE wiring end-to-end in
   isolation, without needing a real `.dll` at all.

### 3.7 — Compile check (incremental)

```
cmake --build build --target gte_core
cmake --build build --target gte_editor
cmake --build build --target GreatTamanaEngineTests
```
Fix any error before proceeding (the layering question from 3.2 is already
resolved — a genuine compile/link error here means a typo'd include path,
not an architectural problem).

### 3.8 — Live verification

`run_app_background` the built `GreatTamanaEditor.exe`, then:

1. `GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe` → **expect
   `panelNames`/`renderPassNames` to ALREADY be non-empty at this point in
   the campaign, with `componentTypeNames` still empty** — the fixture
   project's own `RegisterProbeGame()` (`Projects/ProjectAssemblyProbe/
   Assets/HelloGame.cpp`) already calls `RegisterPluginPanel("Probe Panel",
   ...)` and `RegisterProjectRenderPassProvider("ProjectAssemblyProbe.
   FillTexture", ...)` today, and this phase's own wiring makes BOTH of
   those flow through the ledger automatically the moment
   `ProjectAssemblyHost::TryLoadOneAssembly()` brackets its `entry(...)`
   call — no code change to the fixture project itself is needed for this.
   `componentTypeNames` stays empty because the fixture registers zero
   custom component types today (that is PHASE5's own job). Concretely,
   expect `{"panel_names":["Probe Panel"],"render_pass_names":
   ["ProjectAssemblyProbe.FillTexture"],"component_type_names":[]}`
   (field order/exact JSON key names per `EditorHotReloadDebugCapability::
   GetLedgerEntry()`'s own real response shape — confirm the exact keys from
   the live response, do not assume). **Confirm this live, do not assume —
   this is exactly the kind of "verify concretely" moment this whole
   campaign's own culture demands.** If it comes back fully empty instead,
   that is a real bug in this phase's own wiring (most likely: the bracket
   in 3.4 isn't actually being hit, or `ProjectAssemblyHost::
   LoadProjectAssemblies()` runs before `EditorPanelRegistry`/
   `ComponentTypeRegistry` even exist as constructed singletons —
   investigate via `GET /get_logs` before guessing).
2. `GET /project_assembly/debug/component_types` → unchanged from before
   this phase (still the 5 built-ins only — confirms the engine's OWN
   built-in `RegisterBuiltinComponentReflections()` call, which runs
   OUTSIDE any `BeginRecordingFor()` bracket, is correctly unaffected by
   this phase's wiring).
3. `GET /list_tabs`, `GET /get_swapchain` → unchanged visually from before
   this phase (Probe Panel still there, still renders).

`stop_app_background` once confirmed.

### 3.9 — Completion report

Write `PHASE3_COMPLETION_REPORT.md`: the real answer to 3.8 check #1 (this
matters a lot for PHASE4/PHASE5's own expectations), how the 3.2 layering
question was resolved (with citation), test results, compile results, live
verification results. Commit.

## Definition of Done

- [ ] `ProjectAssemblyRegistrationLedger` exists exactly as specified,
      including `PeekEntry()`.
- [ ] All three existing registration entry points call the matching
      `RecordX()`, unconditionally, confirmed by the isolation test (3.6,
      case 6) working end-to-end.
- [ ] `ProjectAssemblyHost::TryLoadOneAssembly()` brackets both `entry(...)`
      calls with `BeginRecordingFor()`/`EndRecording()`.
- [ ] `Core::UnregisterProjectRenderPassProvider()` exists and is called
      only from `UnregisterEverythingFor()`.
- [ ] `EditorHotReloadDebugCapability::GetLedgerEntry()`'s body is real, not
      a placeholder; signature unchanged.
- [ ] Live `GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe`
      result is recorded and understood (whichever of the two outcomes in
      3.8 #1 is actually true) before PHASE4 starts.
- [ ] `PHASE3_COMPLETION_REPORT.md` exists; changes committed to git.
