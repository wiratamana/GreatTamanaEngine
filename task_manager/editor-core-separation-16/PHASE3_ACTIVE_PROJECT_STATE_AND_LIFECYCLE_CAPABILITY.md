# PHASE3 — ActiveProjectAssemblyState, IProjectLifecycleCapability & EditorProjectLifecycleCapability

Parent: `PHASE0_MASTER_STRATEGY.md` (read first — LDD-CP1, LDD-CP2, Section
2.1's `ResolveProjectRootDirectory()` vs `Projects/` distinction is
load-bearing here — do NOT confuse the two).

Depends on: PHASE1 (`ResolveProjectAssemblySourceRootDirectory()`,
`RunPlainCMakeReconfigureAndWait()`) and PHASE2
(`IsValidProjectAssemblyIdentifierName()`) — both must already compile and
pass their own tests before this phase's own `CreateNewProjectAssembly()`
can call them.
Blocks: PHASE4 (the HTTP route and the ImGui window both call
`IProjectLifecycleCapability::CreateNewProjectAssembly()`, the one method
this phase adds).

End state of this phase: a brand-new project's real, on-disk, immediately-
compileable source folder can be created by calling ONE C++ method —
nothing about HTTP or ImGui exists yet (that is PHASE4).

---

## STEP 1 — `ActiveProjectAssemblyState` (the shared singleton, LDD-CP1: built FULLY)

New file, `src/Editor/ActiveProjectAssemblyState.h` (`gte_editor`-tier —
confirmed safe: it needs `gte::ExecutableDirectory()`/
`ResolveProjectAssemblyOutputDirectory()`, both already `gte_editor`/
`gte_core`-tier respectively and already used together throughout
`src/Editor/`):

```cpp
#pragma once

// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE3 (PHASE0_MASTER_STRATEGY.md, LDD-CP1). The ONE,
// single, shared concept of "which Project Assembly is currently active" -
// every later campaign in this 5-file series (Open Project, Create Script
// Asset, Compile menu) reads/extends THIS class, never inventing a second,
// competing one. Mirrors ProjectAssemblyHotReloadDebugStatus::Instance()'s
// exact shape (a plain namespace-scope singleton, no dependency injection
// needed, gte_editor-only).
#include <filesystem>
#include <mutex>
#include <string>

namespace gte {

class ProjectAssemblyHost;

// Snapshot returned by GetActive() - a plain value type, safe to read from
// any thread that already holds no other lock (see GetActive()'s own doc
// comment for its own internal locking).
struct ActiveProjectAssemblyInfo {
    bool hasActiveProject = false;
    std::string name;
    std::filesystem::path sourceDirectory; // Projects/<Name>/
    std::filesystem::path assetsDirectory; // Projects/<Name>/Assets/
    bool isCompiled = false; // <Name>_Game.dll exists in the output dir right now.
    bool isLoaded = false;   // its own .dll filename is in ProjectAssemblyHost::GetLoadedAssemblyFileNames().
};

class ActiveProjectAssemblyState {
public:
    static ActiveProjectAssemblyState& Instance();

    // Called by CreateNewProjectAssembly() (this phase) on success, and by
    // a later "Open Project" campaign on a successful open - the ONE place
    // this state is ever set.
    void SetActive(const std::string& name, const std::filesystem::path& sourceDirectory);

    // Re-derives isCompiled/isLoaded FRESH on every single call (a cheap
    // std::filesystem::exists() check plus a linear scan of
    // ProjectAssemblyHost::GetLoadedAssemblyFileNames(), both already
    // cheap, already-existing operations) - never cached, so a Compile
    // that finishes in the background, or an Open that succeeds, is
    // reflected correctly the very next time ANY panel/menu reads this,
    // with no manual "refresh" button. If SetProjectAssemblyHost() has
    // never been called (should only happen in an isolated unit test),
    // isLoaded is always false rather than crashing.
    ActiveProjectAssemblyInfo GetActive() const;

    void Clear();

    // Called exactly once, from EditorHost's own constructor body (mirrors
    // EditorHotReloadDebugCapability::SetProjectAssemblyHost()'s own
    // "setter, not a constructor parameter" precedent exactly - this
    // singleton is constructed via ordinary static initialization before
    // any Core/ProjectAssemblyHost object exists).
    void SetProjectAssemblyHost(ProjectAssemblyHost& projectAssemblyHost) noexcept;

private:
    ActiveProjectAssemblyState() = default;

    mutable std::mutex m_mutex;
    bool m_hasActiveProject = false;
    std::string m_name;
    std::filesystem::path m_sourceDirectory;
    ProjectAssemblyHost* m_projectAssemblyHost = nullptr;
};

} // namespace gte
```

`src/Editor/ActiveProjectAssemblyState.cpp`:

```cpp
#include "ActiveProjectAssemblyState.h"

#include "ProjectRootPath.h" // gte::ExecutableDirectory()
#include "../Core/Plugins/ProjectAssemblyBuildRunner.h" // ResolveProjectAssemblyOutputDirectory()
#include "../Core/Plugins/ProjectAssemblyHost.h" // GetLoadedAssemblyFileNames()

#include <algorithm>

namespace gte {

ActiveProjectAssemblyState& ActiveProjectAssemblyState::Instance()
{
    static ActiveProjectAssemblyState instance;
    return instance;
}

void ActiveProjectAssemblyState::SetActive(const std::string& name, const std::filesystem::path& sourceDirectory)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hasActiveProject = true;
    m_name = name;
    m_sourceDirectory = sourceDirectory;
}

void ActiveProjectAssemblyState::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hasActiveProject = false;
    m_name.clear();
    m_sourceDirectory.clear();
}

void ActiveProjectAssemblyState::SetProjectAssemblyHost(ProjectAssemblyHost& projectAssemblyHost) noexcept
{
    m_projectAssemblyHost = &projectAssemblyHost;
}

ActiveProjectAssemblyInfo ActiveProjectAssemblyState::GetActive() const
{
    ActiveProjectAssemblyInfo info;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_hasActiveProject) {
            return info; // hasActiveProject stays false; everything else default.
        }
        info.hasActiveProject = true;
        info.name = m_name;
        info.sourceDirectory = m_sourceDirectory;
    }
    info.assetsDirectory = info.sourceDirectory / "Assets";

    const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory());
    info.isCompiled = std::filesystem::exists(outputDirectory / (info.name + "_Game.dll"));

    if (m_projectAssemblyHost != nullptr) {
        const std::vector<std::string> loaded = m_projectAssemblyHost->GetLoadedAssemblyFileNames();
        info.isLoaded = std::find(loaded.begin(), loaded.end(), info.name + "_Game.dll") != loaded.end();
    }
    return info;
}

} // namespace gte
```

Register both new files in root `CMakeLists.txt`'s `gte_editor` source
list, immediately after `src/Editor/EditorHotReloadDebugCapability.h/.cpp`
(confirmed lines 1063-1064 today).

## STEP 2 — `IProjectLifecycleCapability` (new interface, `EditorCapabilities.h`)

Append this new class to `src/Core/EditorCapabilities.h`, immediately
before the file's closing `} // namespace gte` (confirmed current line
259 — `IHotReloadDebugCapability` ends at line 257 today; re-verify the
exact line before editing, this file changes often):

```cpp
// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2) - answers "can this build create a Project Assembly's
// on-disk source folder at all" - mirrors IHotReloadDebugCapability's own
// "gte_core-tier NetworkServer.cpp holds only a nullable pointer" contract.
// nullptr means every route backed by this interface answers 503.
class IProjectLifecycleCapability {
public:
    virtual ~IProjectLifecycleCapability() = default;

    struct CreateProjectOutcome {
        bool success = false;
        std::string errorMessage; // only meaningful when success == false
        std::string createdSourceDirectory; // absolute path, only meaningful when success == true
    };

    // Validates `name` (IsValidProjectAssemblyIdentifierName()), rejects a
    // name that already exists as a folder (or any file) under the
    // resolved Project Assembly source root, then writes the 3-file
    // scaffold and marks the result as the new ActiveProjectAssemblyState.
    // Pure filesystem + a synchronous `cmake` reconfigure child-process
    // call - safe to call from ANY thread (never touches live
    // Core/Registry/GPU state), so this method needs NO cross-thread
    // bridge, unlike a future hot-reload-shaped capability.
    virtual CreateProjectOutcome CreateNewProjectAssembly(const std::string& name) = 0;
};
```

## STEP 3 — `EditorProjectLifecycleCapability` (the real implementation)

New files, `src/Editor/EditorProjectLifecycleCapability.h/.cpp`
(`gte_editor`-tier, mirrors `EditorHotReloadDebugCapability`'s own file
pair exactly):

```cpp
// src/Editor/EditorProjectLifecycleCapability.h
#pragma once

#include "../Core/EditorCapabilities.h"

namespace gte {

// The real, gte_editor-owned implementation of IProjectLifecycleCapability
// (Core/EditorCapabilities.h). Constructed once, as a namespace-scope
// static inside EditorHost.cpp (mirrors s_editorHotReloadDebugCapability's
// exact precedent).
class EditorProjectLifecycleCapability : public IProjectLifecycleCapability {
public:
    CreateProjectOutcome CreateNewProjectAssembly(const std::string& name) override;
};

} // namespace gte
```

```cpp
// src/Editor/EditorProjectLifecycleCapability.cpp
#include "EditorProjectLifecycleCapability.h"

#include "ActiveProjectAssemblyState.h"
#include "ProjectRootPath.h" // gte::ExecutableDirectory()
#include "../Core/Plugins/ProjectAssemblyBuildRunner.h" // ResolveCMakeBuildDirectory/ResolveProjectAssemblySourceRootDirectory/RunPlainCMakeReconfigureAndWait
#include "../Core/Plugins/ProjectAssemblyNameValidation.h"
#include "../Core/LogSink.h" // GTE_LOG_ERROR/GTE_LOG_INFO

#include <fstream>
#include <system_error>

namespace gte {

namespace {

// The exact, minimal-viable-scaffold template content
// (PHASE0_MASTER_STRATEGY.md's own source document, Step 4) - a real,
// immediately-compileable stub. `name` is substituted only into the
// generated comment header and the RegisterProject symbol's own file
// name reference - never into any code path that affects compilation.
std::string BuildGameStubCppContent(const std::string& name)
{
    return
        "// " + name + "Game.cpp - generated by the Editor's \"Create New Project\" action.\n"
        "// This is your project's Game half's entry point. Add your own render\n"
        "// passes/ECS components/systems here, or use the \"Project\" panel's\n"
        "// right-click \"Create\" menu (once this project is open/active) to scaffold\n"
        "// a starting Render Pass / Compute Shader / Vertex+Fragment Shader pair.\n"
        "#include \"../Libraries/ProjectAssemblyExports.h\"\n"
        "#include \"../../../src/Core/Core.h\"\n"
        "\n"
        "namespace {\n"
        "\n"
        "void RegisterProject(gte::Core& /*core*/)\n"
        "{\n"
        "    // Intentionally empty - a freshly-created project registers nothing\n"
        "    // yet. See this file's own header comment above for how to add your\n"
        "    // first real feature.\n"
        "}\n"
        "\n"
        "} // namespace\n"
        "\n"
        "GTE_DEFINE_PROJECT_EXPORTS_GAME(RegisterProject)\n";
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }
    stream << content;
    return stream.good();
}

} // namespace

IProjectLifecycleCapability::CreateProjectOutcome EditorProjectLifecycleCapability::CreateNewProjectAssembly(
    const std::string& name)
{
    CreateProjectOutcome outcome;

    std::string validationError;
    if (!IsValidProjectAssemblyIdentifierName(name, validationError)) {
        outcome.errorMessage = validationError;
        return outcome;
    }

    const std::filesystem::path executableDirectory = gte::ExecutableDirectory();
    const std::filesystem::path buildDirectory = ResolveCMakeBuildDirectory(executableDirectory);
    if (buildDirectory.empty()) {
        outcome.errorMessage = "could not resolve the CMake build directory - is this a real, configured build?";
        return outcome;
    }
    const std::filesystem::path sourceRoot = ResolveProjectAssemblySourceRootDirectory(buildDirectory);
    if (sourceRoot.empty()) {
        outcome.errorMessage = "could not resolve the project source directory - is this a real, configured CMake build?";
        return outcome;
    }

    const std::filesystem::path projectDirectory = sourceRoot / name;
    if (std::filesystem::exists(projectDirectory)) {
        outcome.errorMessage = "a project or file named '" + name + "' already exists";
        return outcome;
    }

    const std::filesystem::path assetsDirectory = projectDirectory / "Assets";
    const std::filesystem::path librariesDirectory = projectDirectory / "Libraries";
    std::error_code errorCode;

    bool scaffoldOk = true;
    scaffoldOk = scaffoldOk && std::filesystem::create_directories(librariesDirectory, errorCode);
    scaffoldOk = scaffoldOk
        && WriteTextFile(librariesDirectory / "CMakeLists.txt", "gte_add_project(" + name + ")\n");
    // Byte-for-byte copy of the engine's own canonical template - never
    // hand-retyped (PHASE0_MASTER_STRATEGY.md Step 4's own reasoning: a
    // silent drift between the template and a hand-typed copy is exactly
    // the kind of bug a future engine change to that template would
    // otherwise reintroduce invisibly).
    const std::filesystem::path templateSource = buildDirectory.parent_path().empty()
        ? std::filesystem::path() // unreachable in practice - buildDirectory always has a parent.
        : sourceRoot.parent_path() / "cmake" / "templates" / "ProjectAssemblyExports.h";
    scaffoldOk = scaffoldOk
        && std::filesystem::copy_file(templateSource, librariesDirectory / "ProjectAssemblyExports.h",
               std::filesystem::copy_options::overwrite_existing, errorCode);
    scaffoldOk = scaffoldOk && std::filesystem::create_directories(assetsDirectory, errorCode);
    scaffoldOk = scaffoldOk
        && WriteTextFile(assetsDirectory / (name + "Game.cpp"), BuildGameStubCppContent(name));

    if (!scaffoldOk) {
        std::error_code removeError;
        std::filesystem::remove_all(projectDirectory, removeError);
        outcome.errorMessage = "failed to write the new project's scaffold files (" + errorCode.message() + ")";
        GTE_LOG_ERROR("ProjectLifecycle", "CreateNewProjectAssembly('" + name + "'): " + outcome.errorMessage);
        return outcome;
    }

    // LDD-CP2 (PHASE0_MASTER_STRATEGY.md) - ALWAYS reconfigure,
    // unconditionally, regardless of whether CMake's own CONFIGURE_DEPENDS
    // glob would already have picked this project up on its own. See that
    // decision's own reasoning for why this campaign deliberately does not
    // try to detect/branch on which CMake behavior is actually true here.
    const std::filesystem::path repoRoot = sourceRoot.parent_path();
    if (!RunPlainCMakeReconfigureAndWait(repoRoot, buildDirectory)) {
        outcome.errorMessage =
            "project files were written successfully, but the automatic CMake reconfigure step failed - "
            "see the engine log for the real cmake output; the project's files were left on disk so you "
            "can retry compiling manually once the underlying problem is fixed";
        GTE_LOG_ERROR("ProjectLifecycle", "CreateNewProjectAssembly('" + name + "'): " + outcome.errorMessage);
        return outcome;
    }

    ActiveProjectAssemblyState::Instance().SetActive(name, projectDirectory);

    outcome.success = true;
    outcome.createdSourceDirectory = projectDirectory.string();
    GTE_LOG_INFO("ProjectLifecycle", "CreateNewProjectAssembly('" + name + "'): created at " + outcome.createdSourceDirectory);
    return outcome;
}

} // namespace gte
```

Register both new files in root `CMakeLists.txt`'s `gte_editor` source
list, immediately after `ActiveProjectAssemblyState.h/.cpp` (STEP 1
above).

**Re-verify concretely before trusting the snippet above unchanged**:
- The exact current name of this engine's logging macros
  (`GTE_LOG_ERROR`/`GTE_LOG_INFO`) and their real header (`../Core/LogSink.h`
  is this file's own best guess based on `EditorHotReloadDebugCapability.cpp`'s
  own `#include` list — confirm by reading that file's own includes
  directly before trusting this path).
- `templateSource`'s exact relative path — the snippet computes it as
  `sourceRoot.parent_path() / "cmake" / "templates" / ...`, i.e. repo root
  + `cmake/templates/...`; confirm this really is a real, existing path on
  disk (it should be, since `cmake/templates/ProjectAssemblyExports.h` is
  confirmed to exist at the repo root today) before shipping this.
- Never use raw C/C++ `printf`/`std::cout`/`fprintf` anywhere in this file
  — this whole engine's own convention (and this task's own governing
  instruction) requires the engine's internal `GTE_LOG_*` macros only.

## STEP 4 — Wiring: `EditorHost.cpp`

1. Add a namespace-scope static, mirroring `s_editorHotReloadDebugCapability`'s
   own exact declaration site (`EditorHost.cpp`, near the top of the file,
   alongside the other capability statics):
   ```cpp
   EditorProjectLifecycleCapability s_editorProjectLifecycleCapability;
   ```
2. Inside `EditorHost`'s constructor body, immediately after the existing
   `s_editorHotReloadDebugCapability.SetProjectAssemblyHost(...)` call
   (confirmed current line 224):
   ```cpp
   // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
   // BIG-STEP 2), PHASE3 - gives ActiveProjectAssemblyState a live
   // ProjectAssemblyHost& so GetActive()'s own isLoaded field is real, not
   // always-false. Same "setter, not a constructor parameter" placement as
   // every capability wiring call immediately above.
   ActiveProjectAssemblyState::Instance().SetProjectAssemblyHost(m_core.GetProjectAssemblyHost());
   ```
3. Add `#include "EditorProjectLifecycleCapability.h"` and
   `#include "ActiveProjectAssemblyState.h"` to `EditorHost.cpp`'s own
   include list, alongside the other capability headers (confirmed lines
   3-5 today).

`s_editorProjectLifecycleCapability`'s own address is threaded into
`NetworkServer`'s constructor call in PHASE4, not this phase — this phase
only needs it to exist and be wireable; PHASE4 adds the 9th constructor
argument.

## STEP 5 — Verification (incremental build + a real, LIVE call — no full ctest)

1. Incremental build succeeds, zero new warnings.
2. Write a small, throwaway Tier-1 test,
   `tests/Core/Plugins/ProjectAssemblyNameValidationTests.cpp` style is
   NOT appropriate here (this needs real disk I/O + a real `cmake` child
   process) — instead, prove this phase's own correctness via ONE real,
   live, manual call, exactly like PHASE5's later full verification will,
   but scoped small:
   - Launch `GreatTamanaEditor.exe` via `run_app_background`.
   - There is no HTTP route yet (PHASE4's job) — instead, temporarily add
     a single debug log line (or a scratch, throwaway direct call from
     inside `EditorHost`'s own constructor, deleted again before this
     phase's own commit) that calls
     `s_editorProjectLifecycleCapability.CreateNewProjectAssembly("EcsPhase3SmokeTest")`
     once, at startup, and logs the outcome via `GTE_LOG_INFO`/`GTE_LOG_ERROR`.
   - Fetch the log via `gte_send_request` against whatever this engine's
     real "get logs" endpoint path is (confirm the exact path from
     `NetworkRoutes.h`'s own doc comments, e.g. `GET /get_logs`) and
     confirm the outcome logged is a real, successful creation, with
     `Projects/EcsPhase3SmokeTest/` genuinely present on disk afterward,
     containing the exact 3-file scaffold.
   - Confirm the automatic reconfigure genuinely ran (the log shows real
     `cmake` output, not merely "skipped").
   - `stop_app_background`, then delete both the throwaway debug call AND
     `Projects/EcsPhase3SmokeTest/` itself before committing this phase's
     real code (never leave a scratch project folder or a scratch direct
     call committed).
3. This phase's own commit therefore contains ONLY the production code
   above — the scratch verification call is a THROWAWAY, local-only step,
   never committed.

## Definition of Done — this phase only

- [ ] `ActiveProjectAssemblyState` exists, compiles, `GetActive()` returns
      a correctly-shaped, all-false/empty result before any project is
      ever created.
- [ ] `IProjectLifecycleCapability`/`EditorProjectLifecycleCapability`
      exist, compile, and `CreateNewProjectAssembly()`'s real body matches
      STEP 3 above (validate -> resolve -> exists-check -> scaffold ->
      reconfigure -> mark active).
- [ ] `EditorHost.cpp`'s two new wiring lines (STEP 4) exist and compile.
- [ ] The scratch, manual, live verification (STEP 5) was actually
      performed and its outcome is written down, verbatim, in this
      phase's own `PHASE3_COMPLETION_REPORT.md` — including whether the
      automatic reconfigure step (LDD-CP2) was genuinely necessary on this
      machine or genuinely a no-op (informational only — this campaign's
      own code path does not change either way, but it is useful,
      permanent, honest information for a future reader).
- [ ] No scratch/throwaway code or test project folder remains in the
      final commit.

## What this phase does NOT do

- Does NOT add any HTTP route or ImGui window — `CreateNewProjectAssembly()`
  is only reachable via direct C++ call from this phase's own scratch
  verification step; PHASE4 is what makes it reachable from outside the
  process.
- Does NOT scaffold an `Assets/Editor/` folder.
- Does NOT auto-compile the freshly-created project (only reconfigures
  CMake so a LATER, separate compile step succeeds — reconfigure and
  compile are two different things; this phase never invokes
  `TriggerProjectAssemblyCompile()`/`cmake --build --target
  <Name>_Game` at all).
