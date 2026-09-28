# editor-core-separation-12 — PHASE2: Real Capability Implementation +
Engine Command Wiring

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.
Prior phase report: read PHASE1's own completion report (once it exists)
for any deviation found while building `IHotReloadDebugCapability`/
`ProjectAssemblyHotReloadDebugStatus`/`HotReloadEngineStateMutex` before
proceeding.

Depends on: PHASE1 (needs every type it declares).
Blocks: PHASE3 (needs `EditorHotReloadDebugCapability` and the extended
`ExecuteEngineCommand()` signature to exist).

---

## Step 1: The Goal

Give `IHotReloadDebugCapability` a real, working, `gte_editor`-tier
implementation (`EditorHotReloadDebugCapability`), and extend the existing
`EngineCommandBridge`/`ExecuteEngineCommand()` dispatch pipeline to actually
handle `EngineCommandKind::GetSceneSnapshot`. By the end of this phase,
every method on the new capability class does exactly what PHASE0/PHASE1
specified: three are genuinely real (`GetHotReloadStatus`,
`GetRegisteredComponentTypeNames`, `BuildSceneSnapshotJson`), two are honest,
clearly-marked placeholders (`GetLedgerEntry`, `GetLoadedAssemblyFileNames`),
and two trigger real engine behavior (`TriggerCompileOnly` real,
`TriggerHotReload` a permanent-for-this-campaign placeholder).

## Step 2: The Situation

`src/Editor/EditorLogQueryCapability.h/.cpp` (read in full during PHASE0's
investigation) is the exact structural template for a capability whose real
body needs no live `Game&`/`Renderer&` per call — a stateless class with
pure-delegation methods. `src/Editor/EditorSceneIOCapability.h/.cpp` is the
template for a method that DOES need a live `Game&`/`Renderer&` handed to it
per call (`SaveScene(Game&, path, ...)`) — `BuildSceneSnapshotJson(Game&)`
follows this second shape exactly.

`src/Core/Plugins/ProjectAssemblyBuildRunner.h`'s
`TriggerProjectAssemblyCompile(const std::string&, const std::string&)`
currently returns `void` and has zero existing callers anywhere (confirmed
via `search_in_dir`) — safe to change its return type.

`src/Application/EngineCommandDispatch.cpp`'s `ExecuteEngineCommand()` has
exactly ONE call site today, `src/Editor/EditorHost.cpp` line 408:
`ExecuteEngineCommand(m_game, m_renderer, m_sceneIOCapability, *request)`
— PHASE3 updates that one call site; this phase only changes the function's
own signature/body and its header declaration.

## Step 3: The Plan

### 3.1 — `src/Core/Plugins/ProjectAssemblyBuildRunner.h` — signature change

Change:
```cpp
void TriggerProjectAssemblyCompile(const std::string& projectName, const std::string& buildDirectory);
```
to:
```cpp
// editor-core-separation-12 campaign, PHASE2 (Project Assembly Hot Reload
// plan, BIG-STEP 1) - return type changed from void to bool so a caller
// (EditorHotReloadDebugCapability::TriggerCompileOnly()) can honestly
// report "started" vs. "rejected, a build for this project is already in
// flight" - confirmed zero existing callers anywhere in this codebase
// before this change, so this is a zero-risk signature change. Returns
// true the moment the background build thread is actually registered
// (before any compiler output exists yet) - false only when
// TryMarkInFlight() rejects it.
bool TriggerProjectAssemblyCompile(const std::string& projectName, const std::string& buildDirectory);
```

### 3.2 — `src/Core/Plugins/ProjectAssemblyBuildRunner.cpp` — matching body change

```cpp
bool TriggerProjectAssemblyCompile(const std::string& projectName, const std::string& buildDirectory)
{
    if (!TryMarkInFlight(projectName)) {
        GTE_LOG_WARNING("ProjectAssemblyBuild",
            "A build for Project Assembly '" + projectName + "' is already in progress - ignoring this new request.");
        return false;
    }

    auto completionFlag = std::make_shared<std::atomic<bool>>(false);
    std::thread buildThread(&RunBuildThreadBody, projectName, buildDirectory, completionFlag);
    gte::Jobs::JobSystem::Instance().RegisterBackgroundThread(std::move(buildThread), completionFlag);
    return true;
}
```
(Only the `void` → `bool` change, the added `return false;`/`return true;` —
everything else in this function is unchanged.)

### 3.3 — New file: `src/Editor/EditorHotReloadDebugCapability.h`

```cpp
#pragma once

#include "../Core/EditorCapabilities.h"

namespace gte {

// editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1), PHASE2 - the real, gte_editor-owned implementation of
// IHotReloadDebugCapability (Core/EditorCapabilities.h). Constructed once,
// as a namespace-scope static inside EditorHost.cpp (mirrors
// s_editorLogQueryCapability's exact precedent - see that file's own
// PHASE3 comment for why a namespace-scope static, not a function-local
// one, is required here: its address is needed inside EditorHost's own
// member-initializer list, which runs before the constructor body).
//
// This HEADER deliberately carries ZERO dependency on ComponentTypeRegistry.h/
// ProjectRootPath.h/SceneBuilder.h/SceneJsonFormat.h/
// ProjectAssemblyBuildRunner.h/HotReloadEngineStateMutex.h themselves (only
// this class's own .cpp does) - mirrors EditorSceneIOCapability.h's own
// identical "the real body needing the complete type lives in a
// gte_editor-only .cpp" precedent exactly.
class EditorHotReloadDebugCapability : public IHotReloadDebugCapability {
public:
    Status GetHotReloadStatus() const override;
    LedgerEntry GetLedgerEntry(const std::string& projectName) const override;
    std::vector<std::string> GetLoadedAssemblyFileNames() const override;
    std::vector<std::string> GetRegisteredComponentTypeNames() const override;
    std::string BuildSceneSnapshotJson(Game& game) override;
    bool TriggerCompileOnly(const std::string& projectName) override;
    bool TriggerHotReload(const std::string& projectName) override;
};

} // namespace gte
```

### 3.4 — New file: `src/Editor/EditorHotReloadDebugCapability.cpp`

```cpp
#include "EditorHotReloadDebugCapability.h"

#include "ProjectRootPath.h"
#include "../Assets/AssetDatabase.h"
#include "../Core/Plugins/HotReloadEngineStateMutex.h"
#include "../Core/Plugins/ProjectAssemblyBuildRunner.h"
#include "../Core/Plugins/ProjectAssemblyHotReloadDebugStatus.h"
#include "../ECS/Reflection/ComponentTypeRegistry.h"
#include "../Game/Game.h"
#include "../Scene/SceneBuilder.h"
#include "../Scene/SceneJsonFormat.h"

#include <mutex>

namespace gte {

IHotReloadDebugCapability::Status EditorHotReloadDebugCapability::GetHotReloadStatus() const
{
    return ProjectAssemblyHotReloadDebugStatus::Instance().GetSnapshot();
}

IHotReloadDebugCapability::LedgerEntry EditorHotReloadDebugCapability::GetLedgerEntry(
    const std::string& /*projectName*/) const
{
    // editor-core-separation-12 campaign, PHASE2 - PLACEHOLDER. The real
    // ProjectAssemblyRegistrationLedger class does not exist yet - a
    // future BIG-STEP 2 campaign builds it and replaces ONLY this method's
    // body with a real PeekEntry(projectName) lookup. Locked (even though
    // there is nothing to protect yet) so a future campaign's own mutating
    // code has an already-established convention to follow.
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
    return LedgerEntry{};
}

std::vector<std::string> EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames() const
{
    // editor-core-separation-12 campaign, PHASE2 - PLACEHOLDER. ProjectAssemblyHost
    // has no accessor for its own loaded-assembly list yet - a future
    // BIG-STEP 2 campaign adds one and replaces ONLY this method's body.
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
    return {};
}

std::vector<std::string> EditorHotReloadDebugCapability::GetRegisteredComponentTypeNames() const
{
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
    const std::vector<ComponentTypeDescriptor>& all = ComponentTypeRegistry::Instance().AllSortedByTypeName();
    std::vector<std::string> names;
    names.reserve(all.size());
    for (const ComponentTypeDescriptor& descriptor : all) {
        names.push_back(descriptor.typeName);
    }
    return names;
}

std::string EditorHotReloadDebugCapability::BuildSceneSnapshotJson(Game& game)
{
    // Mirrors Editor/SceneIO.cpp's SaveScene()'s own body EXACTLY (minus
    // writing to a file) - see that function for the precedent this
    // copies. MUST only ever be called from the main thread (see this
    // class's own header comment, and Application/EngineCommandDispatch.cpp's
    // GetSceneSnapshot case, which is this method's ONLY caller).
    const std::filesystem::path projectRoot = ResolveProjectRootDirectory();
    AssetDatabase assetDatabase;
    assetDatabase.RefreshFromDirectory(projectRoot);
    const SceneDocument document = BuildSceneDocumentFromRegistry(game.GetRegistry(), assetDatabase);
    return SerializeSceneDocument(document);
}

bool EditorHotReloadDebugCapability::TriggerCompileOnly(const std::string& projectName)
{
    const std::filesystem::path buildDirectory = ResolveCMakeBuildDirectory(gte::ExecutableDirectory());
    if (buildDirectory.empty()) {
        return false; // ResolveCMakeBuildDirectory() already logged the reason.
    }
    return TriggerProjectAssemblyCompile(projectName, buildDirectory.string());
}

bool EditorHotReloadDebugCapability::TriggerHotReload(const std::string& /*projectName*/)
{
    // editor-core-separation-12 campaign, PHASE2 - PERMANENT placeholder
    // for this whole campaign's lifetime. A future BIG-STEP 3 campaign
    // replaces ONLY this method's body with the real
    // PerformProjectAssemblyHotReload() call - this method's signature
    // never changes for that.
    return false;
}

} // namespace gte
```

Confirmed: `gte::ExecutableDirectory()` is declared in
`src/Editor/ProjectRootPath.h` (line 30), the SAME header this file already
`#include`s above for `ResolveProjectRootDirectory()` — no new include is
needed.

### 3.5 — `src/Application/EngineCommandBridge.h` is untouched by this phase

(Already extended in PHASE1 — nothing further needed here.)

### 3.6 — `src/Application/EngineCommandDispatch.h` — new parameter

Change:
```cpp
EngineCommandResult ExecuteEngineCommand(
    Game& game, Renderer& renderer, ISceneIOCapability* sceneIOCapability, const EngineCommandRequest& request);
```
to:
```cpp
// editor-core-separation-12 campaign, PHASE2 - `hotReloadDebugCapability`
// is a NEW, FOURTH parameter, appended AFTER `sceneIOCapability` (never
// inserted before it) so this remains a backward-compatible-in-spirit
// change - its own single call site (EditorHost.cpp) is updated in PHASE3.
// nullptr means "this build/host never registered a hot-reload debug
// capability" - EngineCommandKind::GetSceneSnapshot answers with
// GetSceneSnapshotOutcome::editorAvailable == false in that case, mirroring
// SaveScene/LoadScene's own identical nullptr-degrades-gracefully
// convention exactly.
EngineCommandResult ExecuteEngineCommand(Game& game, Renderer& renderer, ISceneIOCapability* sceneIOCapability,
    IHotReloadDebugCapability* hotReloadDebugCapability, const EngineCommandRequest& request);
```

### 3.7 — `src/Application/EngineCommandDispatch.cpp` — new switch case

Update the function's own definition to match the new signature, and add,
as the LAST case in the `switch` (after the existing `LoadScene` case):

```cpp
    case EngineCommandKind::GetSceneSnapshot: {
        if (hotReloadDebugCapability != nullptr) {
            result.getSceneSnapshot.sceneJson = hotReloadDebugCapability->BuildSceneSnapshotJson(game);
            result.getSceneSnapshot.success = true;
        } else {
            result.getSceneSnapshot.editorAvailable = false;
            result.getSceneSnapshot.errorMessage =
                "scene snapshot requires the Editor module (GTE_ENABLE_EDITOR is OFF in this build)";
        }
        break;
    }
```

### 3.8 — CMakeLists.txt (do this now, not in PHASE3)

This phase's own new file pair must actually be compiled into `gte_editor`
before this phase's own Definition of Done (below) can be satisfied —
mirroring PHASE1's own identical "add it now, in the phase that introduces
the file, never deferred to a later phase" rule
(`PHASE0_MASTER_STRATEGY.md`, Section 3.3). In the `gte_editor` source
list, immediately after the existing `src/Editor/EditorLogQueryCapability.cpp`
line (~1022), add:

```
src/Editor/EditorHotReloadDebugCapability.h
src/Editor/EditorHotReloadDebugCapability.cpp
```

PHASE3 does NOT touch `CMakeLists.txt` at all — every new file this whole
campaign creates is already registered by the time PHASE3 starts (PHASE1's
own two pairs via its Section 3.8, this phase's one pair right above);
PHASE3 only ever modifies pre-existing files.

## Definition of Done — this phase only

- [ ] `EditorHotReloadDebugCapability.h/.cpp` added to `CMakeLists.txt`'s
      `gte_editor` block (immediately after `EditorLogQueryCapability.cpp`,
      ~line 1022) and compile as part of `gte_editor`.
- [ ] `ProjectAssemblyBuildRunner.h/.cpp`'s signature change compiles with
      zero other call sites needing changes (confirmed zero pre-existing
      callers).
- [ ] `EngineCommandDispatch.h/.cpp`'s new parameter/case compile — its ONE
      call site (`EditorHost.cpp` line 408) will now fail to compile with a
      missing-argument error; **this is expected and intentionally left
      broken until PHASE3 fixes that one call site** — note this clearly in
      this phase's own completion report so PHASE3's implementer isn't
      surprised by a red build at the start of their own work. Do a
      compile-only check of `EngineCommandDispatch.cpp` in isolation
      (`g++` with the right include paths, or a targeted `cmake --build`
      of just that one translation unit) rather than a full link, to
      confirm THIS phase's own new code is correct without needing PHASE3
      done first.
