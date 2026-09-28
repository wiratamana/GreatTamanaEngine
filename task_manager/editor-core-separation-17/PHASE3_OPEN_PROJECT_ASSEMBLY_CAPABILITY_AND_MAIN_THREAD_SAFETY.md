# PHASE3 — OpenProjectAssembly() Capability + Main-Thread-Safety Fix
## editor-core-separation-17 (On-Engine Project Workflow, BIG-STEP 3 "Open Project")

Parent: `PHASE0_MASTER_STRATEGY.md` — **read Section 2.2 in full before
starting this phase**; this file is where that fix is actually implemented.
Depends on: PHASE1 (`ProjectValidityTier`/`ClassifyProjectAssemblyFolder()`),
PHASE2 (`ProjectLifecycleLoadCommandBridge`).
Blocks: PHASE4 (the HTTP route and the ImGui window each call one of this
phase's two new public methods).

This is the largest, highest-risk phase in this campaign — take it slowly,
re-read every cited real file before touching it, and do not skip the
compile-check at the end.

---
## Step 1: The Goal

One, single, correct implementation of "open a project" that:
1. Is reachable from the network thread (`POST /project_assembly/open_project`,
   PHASE4) via `OpenProjectAssembly()` — safe to block this caller's own
   thread.
2. Is reachable from the ImGui main thread (`OpenProjectWindow`, PHASE4)
   via `OpenProjectAssemblyOnMainThread()` — must **never** block waiting
   on anything, since there is no other thread to hop to.
3. Both share the exact same Tier-classification-driven business logic
   (Section 2 of the source `.txt` file), and the exact same real
   `.dll`-loading code for Tier 3, so the two call paths can never silently
   drift apart from each other.
4. Also adds a third, simple, any-thread-safe method,
   `ListProjectAssemblies()`, for the new `GET /project_assembly/list_projects`
   route (PHASE4) and internally for `OpenProjectWindow`'s own initial
   row-list population.

## Step 2: The Situation

`IProjectLifecycleCapability` (`src/Core/EditorCapabilities.h`, line 263)
today has exactly one method. `EditorProjectLifecycleCapability`
(`src/Editor/EditorProjectLifecycleCapability.h/.cpp`) today holds **zero**
member state — every call resolves the build/source directories fresh,
inline. This phase gives it its first-ever members: a
`ProjectLifecycleLoadCommandBridge*`, plus enough to reach
`ProjectAssemblyHost`/`Core`/`EditorHost` for the direct (main-thread) load
path — mirroring `EditorHotReloadDebugCapability`'s own **3 separate
setters** precedent EXACTLY (`SetProjectAssemblyHost()`/
`SetHotReloadCommandBridge()`/`SetEngineCommandBridge()`, all called once
from `EditorHost`'s constructor body, all "setter, not a constructor
parameter" because the singletons/capability objects are constructed via
static initialization before `Core`/`EditorHost` exist).

Re-read `PHASE0_MASTER_STRATEGY.md` Section 2.2 now if you have not already
— the rest of this phase assumes you understand exactly why one bridge and
one direct call path both exist, and why merging them back into one is a
regression, not a simplification.

## Step 3: The Plan

### 3.1 — `Core/EditorCapabilities.h` additions

Immediately after the existing `CreateProjectOutcome`
struct/`CreateNewProjectAssembly()` declaration, inside the SAME
`IProjectLifecycleCapability` class body:

```cpp
struct OpenProjectOutcome {
    bool success = false;         // true even for Tier NotBuildable/NotCompiled (LDD-PW4) - false only for NotAProject or an internal error
    std::string errorMessage;     // only meaningful when success == false
    std::string statusMessage;    // human-readable outcome, ALWAYS populated when success == true
    bool loadAttempted = false;
    bool loadSucceeded = false;
};

// Struct used by ListProjectAssemblies() below - deliberately a plain,
// dependency-free value type (no ProjectValidityTier leaked through this
// public interface header would be fine too, since ProjectAssemblyBuildRunner.h
// is already gte_core-tier and safe to include here - but a plain int/
// string pair keeps this specific interface's own JSON-shaping trivial for
// NetworkRoutes.cpp, mirroring this file's existing preference for plain
// scalars in every outcome struct above).
struct ProjectListEntry {
    std::string name;
    std::string tierName; // "NotAProject" / "NotBuildable" / "NotCompiled" / "Compiled" / "AlreadyLoaded"
};

// Callable from ANY thread. For a Tier 3 ("Compiled") folder, this method
// BLOCKS the calling thread until the real load has been performed on the
// main thread (via ProjectLifecycleLoadCommandBridge) - safe for a network
// route handler (a genuinely separate OS thread), NEVER safe to call from
// the engine's own main thread (see OpenProjectAssemblyOnMainThread() right
// below for that caller instead - calling THIS method from the main thread
// deadlocks the whole Editor, see EditorProjectLifecycleCapability.cpp's
// own top-of-file comment for the full reasoning).
virtual OpenProjectOutcome OpenProjectAssembly(const std::string& name) = 0;

// Callable ONLY from the engine's own main thread (e.g. from inside
// OpenProjectWindow::Build(), itself called from EditorHost::Run()'s own
// per-frame ImGui build step). Identical outcome/business logic to
// OpenProjectAssembly() above, but the Tier 3 real-load step is performed
// DIRECTLY, inline, with NO cross-thread bridge/wait at all - there is no
// thread to hop to, since the caller already IS the main thread. Mirrors
// IEditorLayer::ImportExternalAssetIntoProject()'s own "one real function,
// called directly by main-thread ImGui code" precedent.
virtual OpenProjectOutcome OpenProjectAssemblyOnMainThread(const std::string& name) = 0;

// Callable from ANY thread - a plain, read-only filesystem enumeration +
// classification, touches no mutable engine state beyond a
// GetHotReloadEngineStateMutex()-guarded read of the currently-loaded
// assembly list (identical safety contract to ActiveProjectAssemblyState::
// GetActive()). Enumerates every one-level-deep folder directly under the
// resolved Project Assembly source root.
virtual std::vector<ProjectListEntry> ListProjectAssemblies() = 0;
```

Add `#include <vector>` to this header if not already present (confirm by
reading the file's current include list first).

### 3.2 — `EditorProjectLifecycleCapability.h` additions

```cpp
#pragma once

#include "../Core/EditorCapabilities.h"

namespace gte {

class Core;
class EditorHost;
class ProjectLifecycleLoadCommandBridge;

class EditorProjectLifecycleCapability : public IProjectLifecycleCapability {
public:
    CreateProjectOutcome CreateNewProjectAssembly(const std::string& name) override;
    OpenProjectOutcome OpenProjectAssembly(const std::string& name) override;
    OpenProjectOutcome OpenProjectAssemblyOnMainThread(const std::string& name) override;
    std::vector<ProjectListEntry> ListProjectAssemblies() override;

    // Called once, from EditorHost's own constructor body - mirrors
    // EditorHotReloadDebugCapability's own 3-setter precedent exactly
    // (setter, not a constructor parameter, since this object is
    // constructed via static initialization before Core/EditorHost exist).
    void SetLoadCommandBridge(ProjectLifecycleLoadCommandBridge& bridge) noexcept;

    // Gives this capability direct, main-thread-only access to the real
    // Core&/EditorHost* needed by OpenProjectAssemblyOnMainThread()'s own
    // Tier-3 load step (LoadOneProjectAssemblyFromExactPath[IfExists]()
    // needs both). `editorHost` may be nullptr (mirrors every existing
    // "*_Editor.dll load is optional" precedent in this codebase) but
    // `core` must never be.
    void SetEngineReferences(Core& core, EditorHost* editorHost) noexcept;

private:
    // Shared by BOTH OpenProjectAssembly() and OpenProjectAssemblyOnMainThread()
    // - everything EXCEPT the actual Tier-3 load call (name validation,
    // resolving the source root, classifying the tier, marking
    // ActiveProjectAssemblyState active, composing the per-tier
    // statusMessage). Returns the tier it classified, plus a
    // partially-filled OpenProjectOutcome - the caller fills in
    // loadAttempted/loadSucceeded itself after deciding HOW to perform the
    // Tier-3 load (bridge vs. direct).
    struct PreLoadResult {
        OpenProjectOutcome outcome;
        bool needsLoad = false; // true only when tier == Compiled
        std::string resolvedProjectName;
    };
    PreLoadResult ClassifyAndMarkActive(const std::string& name);

    ProjectLifecycleLoadCommandBridge* m_loadCommandBridge = nullptr;
    Core* m_core = nullptr;
    EditorHost* m_editorHost = nullptr;
};

} // namespace gte
```

### 3.3 — `EditorProjectLifecycleCapability.cpp` — the real bodies

Before writing any of the bodies below, add these NEW `#include` lines to
`EditorProjectLifecycleCapability.cpp`'s existing include list (none of
these are in the file today — confirmed by reading its current, real
include list first): `#include "../Core/Core.h"` (needed for the full
`Core` class definition — `EditorProjectLifecycleCapability.h` only
forward-declares `class Core;`, and `m_core->GetProjectAssemblyHost()`
below needs the complete type; this transitively pulls in
`Plugins/ProjectAssemblyHost.h` too, so no separate include for that one is
needed — confirmed by reading `Core.h`'s own include list), `#include
"../Core/Plugins/HotReloadEngineStateMutex.h"` (needed for
`GetHotReloadEngineStateMutex()`, used below exactly like
`ActiveProjectAssemblyState::GetActive()`'s own already-shipped precedent),
and `#include "../Application/ProjectLifecycleLoadCommandBridge.h"` (needed
for the real `ProjectLifecycleLoadCommandBridge::SubmitResult`/
`LoadProjectAssemblyCommandRequest`/`LoadProjectAssemblyCommandResult`
types — this file's own header only forward-declares
`class ProjectLifecycleLoadCommandBridge;`, which is not enough to call
`SubmitAndWait()` or read its result fields).

Top-of-file comment (add this, verbatim in spirit, as load-bearing
documentation for the next reader):

```cpp
// PHASE0_MASTER_STRATEGY.md, Section 2.2 (editor-core-separation-17
// campaign) - OpenProjectAssembly() and OpenProjectAssemblyOnMainThread()
// are NOT interchangeable. OpenProjectAssembly() may BLOCK its own calling
// thread waiting for EditorHost::Run()'s own bridge-drain point to service
// it - calling it from the main thread itself deadlocks the whole Editor,
// since that drain point is reached earlier in the SAME frame's own call
// stack and cannot run again until this call returns. Always call
// OpenProjectAssembly() from a network route handler; always call
// OpenProjectAssemblyOnMainThread() from ImGui/main-thread code.
```

`ClassifyAndMarkActive()` (the shared helper both public methods call
first):

```cpp
EditorProjectLifecycleCapability::PreLoadResult EditorProjectLifecycleCapability::ClassifyAndMarkActive(
    const std::string& name)
{
    PreLoadResult result;
#if !GTE_ENABLE_PROJECT_ASSEMBLIES
    result.outcome.errorMessage = "this build was configured with GTE_ENABLE_PROJECT_ASSEMBLIES=OFF";
    return result;
#else
    std::string validationError;
    if (!IsValidProjectAssemblyIdentifierName(name, validationError)) {
        result.outcome.errorMessage = validationError;
        return result;
    }

    const std::filesystem::path executableDirectory = gte::ExecutableDirectory();
    const std::filesystem::path buildDirectory = ResolveCMakeBuildDirectory(executableDirectory);
    const std::filesystem::path sourceRoot = buildDirectory.empty()
        ? std::filesystem::path{} : ResolveProjectAssemblySourceRootDirectory(buildDirectory);
    if (sourceRoot.empty()) {
        result.outcome.errorMessage = "could not resolve the project source directory - is this a real, configured CMake build?";
        return result;
    }

    const std::filesystem::path candidateFolder = sourceRoot / name;
    const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(executableDirectory);

    std::vector<std::string> loadedDllFileNames;
    if (m_core != nullptr) {
        std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
        loadedDllFileNames = m_core->GetProjectAssemblyHost().GetLoadedAssemblyFileNames();
    }

    const ProjectValidityTier tier = ClassifyProjectAssemblyFolder(candidateFolder, outputDirectory, loadedDllFileNames);
    if (tier == ProjectValidityTier::NotAProject) {
        result.outcome.errorMessage = "not a valid project folder (missing Libraries/CMakeLists.txt)";
        return result;
    }

    ActiveProjectAssemblyState::Instance().SetActive(name, candidateFolder);
    result.resolvedProjectName = name;
    result.outcome.success = true;

    switch (tier) {
    case ProjectValidityTier::NotBuildable:
        result.outcome.statusMessage = "opened '" + name + "' - no buildable source yet; use the Assets panel's "
            "Create menu to add a Render Pass/Compute Shader, or add your own .cpp file";
        break;
    case ProjectValidityTier::NotCompiled:
        result.outcome.statusMessage = "opened '" + name + "' - not yet compiled, use Compile to build it";
        break;
    case ProjectValidityTier::Compiled:
        result.outcome.statusMessage = "opened '" + name + "' - loading...";
        result.needsLoad = true;
        break;
    case ProjectValidityTier::AlreadyLoaded:
        result.outcome.statusMessage = "opened '" + name + "' - already loaded and running "
            "(use the existing Compile & Reload feature to update its code, not Open)";
        break;
    case ProjectValidityTier::NotAProject:
        break; // unreachable - handled above.
    }
    return result;
#endif
}
```

`OpenProjectAssembly()` (network-thread caller — uses the bridge):

```cpp
IProjectLifecycleCapability::OpenProjectOutcome EditorProjectLifecycleCapability::OpenProjectAssembly(
    const std::string& name)
{
    PreLoadResult pre = ClassifyAndMarkActive(name);
    if (!pre.outcome.success || !pre.needsLoad) {
        return pre.outcome;
    }
    if (m_loadCommandBridge == nullptr) {
        pre.outcome.statusMessage += " (warning: load command bridge unavailable - marked active only)";
        return pre.outcome;
    }
    LoadProjectAssemblyCommandRequest request;
    request.projectName = pre.resolvedProjectName;
    const ProjectLifecycleLoadCommandBridge::SubmitResult submit = m_loadCommandBridge->SubmitAndWait(request);
    pre.outcome.loadAttempted = true;
    if (submit.result.has_value()) {
        pre.outcome.loadSucceeded = submit.result->loadSucceeded;
        pre.outcome.statusMessage = pre.outcome.loadSucceeded
            ? ("opened '" + name + "' - loaded successfully")
            : ("opened '" + name + "' - marked active, but the load itself failed; see the engine log");
    } else {
        pre.outcome.statusMessage = "opened '" + name + "' - marked active, but the load request "
            + std::string(submit.alreadyPending ? "was rejected (another load is already in progress)" : "timed out");
    }
    return pre.outcome;
}
```

`OpenProjectAssemblyOnMainThread()` (ImGui caller — direct, no bridge):

```cpp
IProjectLifecycleCapability::OpenProjectOutcome EditorProjectLifecycleCapability::OpenProjectAssemblyOnMainThread(
    const std::string& name)
{
    PreLoadResult pre = ClassifyAndMarkActive(name);
    if (!pre.outcome.success || !pre.needsLoad) {
        return pre.outcome;
    }
    if (m_core == nullptr) {
        pre.outcome.statusMessage += " (warning: engine reference unavailable - marked active only)";
        return pre.outcome;
    }
    const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory());
    pre.outcome.loadAttempted = true;
    pre.outcome.loadSucceeded =
        m_core->GetProjectAssemblyHost().LoadOneProjectAssemblyFromExactPath(
            outputDirectory / (pre.resolvedProjectName + "_Game.dll"), *m_core, m_editorHost)
        && m_core->GetProjectAssemblyHost().LoadOneProjectAssemblyFromExactPathIfExists(
            outputDirectory / (pre.resolvedProjectName + "_Editor.dll"), *m_core, m_editorHost);
    pre.outcome.statusMessage = pre.outcome.loadSucceeded
        ? ("opened '" + name + "' - loaded successfully")
        : ("opened '" + name + "' - marked active, but the load itself failed; see the engine log");
    return pre.outcome;
}
```

**Note the deliberate, near-total duplication** between these two methods'
own Tier-3 bodies — this is intentional, not sloppy: the ONLY difference is
"go through the bridge" vs. "call the loader directly", which is precisely
the one axis that must never be silently unified back into one path (see
Section 2.2). If you find yourself wanting to factor the two `LoadOne...()`
calls into a shared private helper taking a `Core&`/`EditorHost*` pair —
that IS safe and encouraged (it's the exact same 2-line `&&`-chain either
way); just do not factor the BRIDGE-VS-DIRECT decision itself into shared
code.

`ListProjectAssemblies()`:

```cpp
std::vector<IProjectLifecycleCapability::ProjectListEntry> EditorProjectLifecycleCapability::ListProjectAssemblies()
{
    std::vector<ProjectListEntry> entries;
#if GTE_ENABLE_PROJECT_ASSEMBLIES
    const std::filesystem::path executableDirectory = gte::ExecutableDirectory();
    const std::filesystem::path buildDirectory = ResolveCMakeBuildDirectory(executableDirectory);
    if (buildDirectory.empty()) return entries;
    const std::filesystem::path sourceRoot = ResolveProjectAssemblySourceRootDirectory(buildDirectory);
    if (sourceRoot.empty() || !std::filesystem::is_directory(sourceRoot)) return entries;
    const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(executableDirectory);

    std::vector<std::string> loadedDllFileNames;
    if (m_core != nullptr) {
        std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
        loadedDllFileNames = m_core->GetProjectAssemblyHost().GetLoadedAssemblyFileNames();
    }

    std::error_code iterationError;
    for (const auto& entry : std::filesystem::directory_iterator(sourceRoot, iterationError)) {
        if (!entry.is_directory()) continue;
        const ProjectValidityTier tier = ClassifyProjectAssemblyFolder(entry.path(), outputDirectory, loadedDllFileNames);
        entries.push_back({ entry.path().filename().string(), ToTierName(tier) });
    }
#endif
    return entries;
}
```

(`ToTierName()` - a small, local, anonymous-namespace `switch`-based helper
mapping each enum value to its exact string, matching `ProjectListEntry::tierName`'s
own doc comment above.)

### 3.4 — `EditorHost.h`/`EditorHost.cpp` wiring

`EditorHost.h`: add one new member, right next to the other bridges
(`m_hotReloadCommandBridge`/`m_assetImportCommandBridge`):

```cpp
ProjectLifecycleLoadCommandBridge m_projectLifecycleLoadCommandBridge;
```

`EditorHost.cpp` constructor body — add, immediately after the existing
`s_editorHotReloadDebugCapability.SetEngineCommandBridge(m_commandBridge);`
call (same section, same style):

```cpp
// editor-core-separation-17 campaign (On-Engine Project Workflow plan,
// BIG-STEP 3), PHASE3 - hands EditorProjectLifecycleCapability the new
// bridge + a live Core&/EditorHost* so OpenProjectAssembly()/
// OpenProjectAssemblyOnMainThread() can both do real work. Same "setter,
// not a constructor parameter" placement as every capability wiring call
// above.
s_editorProjectLifecycleCapability.SetLoadCommandBridge(m_projectLifecycleLoadCommandBridge);
s_editorProjectLifecycleCapability.SetEngineReferences(m_core, this);
```

`EditorHost::Run()` — add a new 5th drain block, immediately alongside the
existing `m_assetImportCommandBridge` block (same section, `EditorHost.cpp`
around line 727 in the pre-this-phase file):

```cpp
// editor-core-separation-17 campaign (On-Engine Project Workflow plan,
// BIG-STEP 3), PHASE3 - drains at most ONE pending Tier-3 "Open Project"
// load request per frame, submitted ONLY by
// EditorProjectLifecycleCapability::OpenProjectAssembly() (the
// network-thread-facing method) - the ImGui-facing
// OpenProjectAssemblyOnMainThread() never reaches this bridge at all (see
// PHASE0_MASTER_STRATEGY.md, Section 2.2).
if (const std::optional<LoadProjectAssemblyCommandRequest> loadRequest =
        m_projectLifecycleLoadCommandBridge.TryPeekPendingCommandRequest()) {
    GTE_PROFILE_SCOPE("EditorHost::ExecuteProjectLifecycleLoad");
    const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory());
    const std::string& name = loadRequest->projectName;
    LoadProjectAssemblyCommandResult loadResult;
    loadResult.loadSucceeded =
        m_core.GetProjectAssemblyHost().LoadOneProjectAssemblyFromExactPath(
            outputDirectory / (name + "_Game.dll"), m_core, this)
        && m_core.GetProjectAssemblyHost().LoadOneProjectAssemblyFromExactPathIfExists(
            outputDirectory / (name + "_Editor.dll"), m_core, this);
    m_projectLifecycleLoadCommandBridge.FulfillCommand(loadResult);
}
```

(`ProjectAssemblyHost.cpp` already locks `GetHotReloadEngineStateMutex()`
internally around this mutation — confirmed, lines 103/212 — this new call
site needs no extra lock of its own.)

Add `#include "../Application/ProjectLifecycleLoadCommandBridge.h"` to
`EditorHost.h` (mirrors the existing
`#include "../Application/AssetImportCommandBridge.h"` line).

### 3.5 — Definition of done

- [ ] `EditorCapabilities.h`/`EditorProjectLifecycleCapability.h/.cpp`
      compile with the 3 new methods + 2 new setters.
- [ ] `EditorHost.h/.cpp` compile with the new member + 2 new constructor
      wiring calls + the new 5th drain block.
- [ ] `NullEditorLayer`/`ImGuiEditorLayer`'s own `IProjectLifecycleCapability*`
      plumbing needs **no changes** (it already stores/forwards the same
      pointer type — confirm this by re-reading `ImGuiEditorLayer.cpp`
      lines 941/1170 before assuming so).
- [ ] Fast incremental compile check of `GreatTamanaEditor` succeeds.
- [ ] A throwaway, manual smoke test (temporary code or a debugger
      breakpoint is fine, delete before committing): confirm calling
      `OpenProjectAssembly("ProjectAssemblyProbe")` from a background
      thread (simulating the future HTTP route) does NOT hang, and
      confirm calling `OpenProjectAssemblyOnMainThread(...)` from the main
      thread also does not hang and performs a real load when the probe
      project is compiled. PHASE4/5 add the permanent, real callers and
      tests — this phase's own job is only to prove the two methods
      themselves are correct and safe before anything public calls them.
- [ ] `git_add` + `git_commit` + `PHASE3_COMPLETION_REPORT.md`, explicitly
      recording that the manual smoke test above was actually run and its
      result.

### 3.6 — Non-goals for this phase specifically

- Does NOT add any HTTP route yet (PHASE4).
- Does NOT add `OpenProjectWindow` yet (PHASE4).
- Does NOT change `DockLayout.cpp`'s menu yet (PHASE4).
