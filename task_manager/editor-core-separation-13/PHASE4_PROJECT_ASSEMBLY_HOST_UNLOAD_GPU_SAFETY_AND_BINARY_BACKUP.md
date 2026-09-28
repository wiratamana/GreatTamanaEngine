# editor-core-separation-13 — PHASE4: ProjectAssemblyHost::UnloadProjectAssembly() + Binary Backup/Restore

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.
Prior phase report: read `PHASE3_COMPLETION_REPORT.md` before starting —
specifically its answer to "is `ProjectAssemblyProbe`'s ledger entry already
non-empty before PHASE5 adds a custom component" (this changes what PHASE4's
own live isolation test in 4.7 should expect to see).

Depends on: PHASE3 (`ProjectAssemblyRegistrationLedger` must exist and
compile — `UnloadProjectAssembly()` calls its `UnregisterEverythingFor()`).
Blocks: PHASE5 (the full live isolation test calls
`UnloadProjectAssembly()` directly).

---

## Step 1: The Goal

Add `ProjectAssemblyHost::UnloadProjectAssembly(projectName, core, renderer)`
— the ONE function a future BIG-STEP 3 orchestrator will call — that
reverses `TryLoadOneAssembly()`'s effects for both `_Game.dll` and (if
present) `_Editor.dll` of one named project, in the exact, non-negotiable
order: (1) `renderer.WaitForGpuIdle()`, (2)
`ProjectAssemblyRegistrationLedger::Instance().UnregisterEverythingFor()`,
(3) `FreeLibrary()` on both handles (Editor first, then Game). Add
`GetLoadedAssemblyFileNames()` as a read-only accessor. Add
`BackupProjectAssemblyBinaries()`/`RestoreProjectAssemblyBinariesFromBackup()`
to `ProjectAssemblyBuildRunner` (LDD-HR3's rollback requirement — this is
about files on disk, unrelated to live engine state, hence a different file).
Wire `GetLoadedAssemblyFileNames()`'s real body into
`EditorHotReloadDebugCapability`, replacing its placeholder.

## Step 2: The Situation

Confirmed, current, `src/Core/Plugins/ProjectAssemblyHost.h`:

```cpp
class ProjectAssemblyHost {
public:
    ProjectAssemblyHost() = default;
    ~ProjectAssemblyHost();

    ProjectAssemblyHost(const ProjectAssemblyHost&) = delete;
    ProjectAssemblyHost& operator=(const ProjectAssemblyHost&) = delete;
    ProjectAssemblyHost(ProjectAssemblyHost&&) = delete;
    ProjectAssemblyHost& operator=(ProjectAssemblyHost&&) = delete;

    void LoadProjectAssemblies(const std::filesystem::path& outputDirectory, Core& core, EditorHost* editorHost);
    std::size_t LoadedAssemblyCount() const noexcept { return m_loadedAssemblies.size(); }
private:
    struct LoadedAssembly {
        void* moduleHandle = nullptr; // HMODULE, stored as void*.
        std::string dllFileName;
    };
    void TryLoadOneAssembly(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);
    std::vector<LoadedAssembly> m_loadedAssemblies;
};
```

`m_loadedAssemblies` entries carry ONLY `moduleHandle` + `dllFileName` (e.g.
`"ProjectAssemblyProbe_Game.dll"`) — no `projectName` field exists yet, and
none is strictly needed as a NEW stored field (it can always be derived from
`dllFileName` via the SAME `DeriveProjectNameFromDllFileName()` helper PHASE3
already added inside this same `.cpp`'s anonymous namespace — reuse it,
do not duplicate it).

Confirmed, current, `Renderer::WaitForGpuIdle()` doc comment
(`src/Renderer/Renderer.h` lines 337-344) names exactly ONE sanctioned caller
family today ("the rare, explicit, human/LLM-triggered `GET /get_texture`
request path"). This phase adds a second sanctioned caller and the comment
must say so.

Confirmed, current, `src/Core/Plugins/ProjectAssemblyBuildRunner.h` — no
backup/restore function exists; `ResolveCMakeBuildDirectory()` already
exists and resolves the CMake BUILD directory, but the Project Assembly
OUTPUT directory (where the real `.dll`s actually live, `<exe dir>/
project_assemblies/`) is a DIFFERENT path, currently only known as a literal
string at ONE call site: `src/Editor/EditorHost.cpp` line 278,
`gte::ExecutableDirectory() / "project_assemblies"` (confirmed — `search_in_dir`
for `"project_assemblies"` across `src/` finds this as the only real
directory-construction call site; the rest of the matches are doc comments/
`#if` guards). This phase needs that same joining computable from
`ProjectAssemblyBuildRunner.cpp` too (for the backup/restore functions) —
extract a single, shared, tiny free function,
`std::filesystem::path ResolveProjectAssemblyOutputDirectory(const std::filesystem::path& executableDirectory)`,
into `ProjectAssemblyBuildRunner.h/.cpp` (a natural home — it's the file that
already owns `ResolveCMakeBuildDirectory()`) and have `EditorHost.cpp` call
it too, rather than two independent copies of the same literal string
silently able to drift apart. **Confirmed, real, load-bearing layering
constraint (see 3.4 below for the full citation)**: this new function must
take `executableDirectory` as an explicit parameter and must NEVER call
`gte::ExecutableDirectory()` itself — that function is declared in
`src/Editor/ProjectRootPath.h`, which compiles into `gte_editor`, while
`ProjectAssemblyBuildRunner.h/.cpp` compiles into `gte_core`, and
`gte_editor` depends on `gte_core`, never the reverse. This mirrors
`ResolveCMakeBuildDirectory()`'s own existing, already-correct
`startDirectory`-as-a-parameter shape in this exact same file — simply follow
that precedent, don't reinvent one.

Confirmed, current, `src/Editor/EditorHotReloadDebugCapability.cpp` lines
34-41 — the exact placeholder this phase replaces:

```cpp
std::vector<std::string> EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames() const
{
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
    return {};
}
```

This method needs a live `ProjectAssemblyHost&` to call
`GetLoadedAssemblyFileNames()` on — check how `EditorHotReloadDebugCapability`
currently gets access to other live engine objects it needs (e.g. how does
`BuildSceneSnapshotJson(Game&)` receive its `Game&` — as a parameter, not a
stored member, confirmed current signature). **`GetLoadedAssemblyFileNames()`
in `IHotReloadDebugCapability` takes NO parameters** (confirmed,
`EditorCapabilities.h` line 208) — so `EditorHotReloadDebugCapability` itself
must hold a pointer to the real, live `ProjectAssemblyHost` it needs.

**A required constructor parameter is IMPOSSIBLE here — confirmed by actually
reading `EditorHost.cpp`, not assumed.** `s_editorHotReloadDebugCapability`
is a NAMESPACE-SCOPE static (`EditorHost.cpp` line 119, inside an anonymous
namespace), constructed via ordinary C++ static initialization — which runs
BEFORE `main()`, and therefore strictly BEFORE any `EditorHost`/`Core`/
`ProjectAssemblyHost` object is EVER constructed anywhere in the process.
This file's own doc comment (lines 92-109, on the near-identical
`s_editorLogQueryCapability` immediately above it) explains exactly WHY this
must stay a namespace-scope static: its ADDRESS is needed inside
`EditorHost`'s own member-INITIALIZER LIST, to construct `m_networkServer`
(`NetworkServer`'s constructor takes `&s_editorHotReloadDebugCapability`
directly) — and a member-initializer list runs before the constructor BODY.
A constructor parameter of type `ProjectAssemblyHost&`/`Core&` would require
a REAL, already-constructed object to exist at the moment this namespace-scope
static's own initializer runs — which is before `EditorHost::EditorHost()` is
ever entered at all, so no such object can possibly exist yet. This is not a
style preference; it would simply not compile correctly (or would compile
only by binding a reference to something that does not exist yet, which is
undefined behavior, not a real fix).

The correct, safe, least-invasive fix — genuinely simpler than a constructor
parameter, not just an alternative — is a SETTER, called once, from
`EditorHost`'s constructor BODY (which DOES run after `m_core` already fully
exists), mirroring an EXISTING precedent already in this exact codebase:
`Core::SetEditorLayerHook(IEditorLayer* editorLayer)` (`Core.h`/`.cpp`) is
called exactly once, from `EditorHost`'s own constructor body
(`EditorHost.cpp`, `m_core.SetEditorLayerHook(m_editorLayer.get());`), to
hand `Core` a live pointer to something (`m_editorLayer`) that is constructed
AFTER `m_core` itself — the exact same structural shape this phase's own
problem has: the dependency (a live `ProjectAssemblyHost`) does not exist yet
at the point the OTHER object (`s_editorHotReloadDebugCapability`) is itself
constructed. `EditorHotReloadDebugCapability` gains the identical shape: a
new `SetProjectAssemblyHost(ProjectAssemblyHost&)` setter, called once from
`EditorHost`'s constructor body, AFTER `m_core` exists but BEFORE
`m_networkServer.Start(8080)` ever accepts a real HTTP request (see 3.5 below
for the exact call site). `Core::m_projectAssemblyHost` is confirmed PRIVATE
today (`Core.h`, inside the private member section, immediately after
`m_pluginHost`) with NO existing public accessor (confirmed, `search_in_dir`
for `GetProjectAssemblyHost` across `src/Core/` finds zero matches) — so this
phase also adds a small `Core::GetProjectAssemblyHost()` accessor, mirroring
`Core::GetRenderer()`'s own existing precedent
(`Renderer& GetRenderer() noexcept { return m_renderer; }`) exactly.

## Step 3: The Plan

### 3.1 — Update `Renderer::WaitForGpuIdle()`'s doc comment

`src/Renderer/Renderer.h` line 337-344 — append (do not remove any existing
sentence) that a second sanctioned caller now exists: the Project Assembly
Hot Reload feature's `ProjectAssemblyHost::UnloadProjectAssembly()`
(`editor-core-separation-13` campaign, BIG-STEP 2), which must call it
BEFORE any Project-Assembly-owned GPU resource can be released via
`FreeLibrary()`'s own static-destructor path. State explicitly this is still
NOT safe to call from any per-frame path — only from this one, rare,
synchronous teardown call site.

### 3.2 — `ProjectAssemblyHost.h` additions

```cpp
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE4. Reverses TryLoadOneAssembly()'s own effects for BOTH
// the _Game.dll and (if it exists) the _Editor.dll of `projectName`, in
// this EXACT order, none of which may be skipped or reordered:
//   1. renderer.WaitForGpuIdle() - MUST run before step 2/3, since
//      FreeLibrary() may run this .dll's own static destructors
//      (DllMain's DLL_PROCESS_DETACH), which may release GPU resources this
//      project's own render pass owns while a frame could still be in
//      flight (BIG-STEP 0, Hazard 4).
//   2. ProjectAssemblyRegistrationLedger::Instance().UnregisterEverythingFor(
//      projectName, core) - removes every render-pass/panel/component-type
//      registration this project ever made (Hazards 1/2 fix). MUST run
//      before step 3 - a dangling pointer left registered past FreeLibrary()
//      is a guaranteed crash the very next frame/graph-declare.
//   3. FreeLibrary() on BOTH module handles (Editor first, then Game -
//      reverse of TryLoadOneAssembly()'s own Game-then-Editor load order),
//      removing both entries from m_loadedAssemblies.
// Safe to call for a projectName that was never loaded, or is already
// unloaded - a no-op, logged at INFO level, never a crash or a warning.
void UnloadProjectAssembly(const std::string& projectName, Core& core, Renderer& renderer);

// editor-core-separation-13 campaign, PHASE4 - read-only list of every
// currently-open .dll's own file name (e.g. "ProjectAssemblyProbe_Game.dll"),
// for EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames() (GET
// /project_assembly/debug/loaded_assemblies). Deliberately returns plain
// strings, never the raw HMODULE.
std::vector<std::string> GetLoadedAssemblyFileNames() const;
```

Add `#include "../Renderer/Renderer.h"`? **No** — forward-declare
`class Renderer;` near the existing `class Core;`/`class EditorHost;`
forward declarations at the top of this header instead (this header
currently has zero dependency on a concrete `Renderer` type, and
`UnloadProjectAssembly()`'s declaration only needs a reference, not a
complete type) — mirrors this file's own existing forward-declaration
style exactly.

### 3.3 — `ProjectAssemblyHost.cpp` implementation

```cpp
void ProjectAssemblyHost::UnloadProjectAssembly(const std::string& projectName, Core& core, Renderer& renderer)
{
    // Find every m_loadedAssemblies entry whose derived project name matches,
    // BEFORE touching anything - this method must be all-or-nothing-safe to
    // call for a projectName with zero matching entries.
    std::vector<std::size_t> matchingIndices;
    for (std::size_t i = 0; i < m_loadedAssemblies.size(); ++i) {
        if (DeriveProjectNameFromDllFileName(m_loadedAssemblies[i].dllFileName) == projectName) {
            matchingIndices.push_back(i);
        }
    }
    if (matchingIndices.empty()) {
        GTE_LOG_INFO("ProjectAssembly", "UnloadProjectAssembly('" + projectName + "') - nothing currently loaded for this project, no-op.");
        return;
    }

    // Step 1 - Hazard 4 fix. MUST happen before any FreeLibrary() below.
    renderer.WaitForGpuIdle();

    // Step 2 - Hazards 1/2 fix. MUST happen before any FreeLibrary() below.
    ProjectAssemblyRegistrationLedger::Instance().UnregisterEverythingFor(projectName, core);

    // Step 3 - FreeLibrary(), Editor entry(s) first, then Game (reverse load
    // order) - sort matchingIndices so "_Editor.dll" entries are freed first.
    std::sort(matchingIndices.begin(), matchingIndices.end(), [this](std::size_t a, std::size_t b) {
        const bool aIsEditor = m_loadedAssemblies[a].dllFileName.ends_with("_Editor.dll");
        const bool bIsEditor = m_loadedAssemblies[b].dllFileName.ends_with("_Editor.dll");
        return aIsEditor && !bIsEditor; // Editor entries sort first.
    });
    for (const std::size_t index : matchingIndices) {
        GTE_LOG_INFO("ProjectAssembly", "Unloading '" + m_loadedAssemblies[index].dllFileName + "'.");
        FreeLibrary(static_cast<HMODULE>(m_loadedAssemblies[index].moduleHandle));
    }
    // Erase in DESCENDING index order so earlier indices remain valid while erasing.
    std::sort(matchingIndices.begin(), matchingIndices.end(), std::greater<std::size_t>());
    for (const std::size_t index : matchingIndices) {
        m_loadedAssemblies.erase(m_loadedAssemblies.begin() + static_cast<std::ptrdiff_t>(index));
    }
    GTE_LOG_INFO("ProjectAssembly", "UnloadProjectAssembly('" + projectName + "') complete.");
}

std::vector<std::string> ProjectAssemblyHost::GetLoadedAssemblyFileNames() const
{
    std::vector<std::string> names;
    names.reserve(m_loadedAssemblies.size());
    for (const LoadedAssembly& loaded : m_loadedAssemblies) {
        names.push_back(loaded.dllFileName);
    }
    return names;
}
```

`#include "../Renderer/Renderer.h"` (needed here, in the `.cpp`, for
`Renderer::WaitForGpuIdle()`'s complete declaration) and
`#include "ProjectAssemblyRegistrationLedger.h"` added. `<algorithm>`
(`std::sort`) and `<functional>` (`std::greater`) added if not already
present.

**A genuinely important correctness detail to verify, not assume**: does
`renderer.WaitForGpuIdle()` need to run ONCE per `UnloadProjectAssembly()`
call (current sketch above), or does calling it while there is NOTHING
GPU-related to wait for (e.g. unloading a project whose render pass was
already culled/never declared this frame) risk any issue? Confirmed, current,
`Renderer::WaitForGpuIdle()`'s body (`Renderer.cpp` line 218) is a plain,
unconditional `vkDeviceWaitIdle()` — always safe to call, regardless of
whether anything is actually "in flight" for this specific project; it waits
for the ENTIRE GPU queue, not a per-resource wait. No special-casing needed.

### 3.4 — `ProjectAssemblyBuildRunner.h`/`.cpp` additions

**Layering constraint, confirmed, not assumed (a genuine hazard an earlier
draft of this phase got wrong)**: `ProjectAssemblyBuildRunner.h/.cpp`
compiles into `gte_core` (`CMakeLists.txt`'s `add_library(gte_core STATIC ...)`
list). `gte::ExecutableDirectory()` is declared in `src/Editor/ProjectRootPath.h`
and defined in `ProjectRootPath.cpp`, BOTH of which compile into `gte_editor`
(`CMakeLists.txt`'s `add_library(gte_editor STATIC ...)` list). `gte_editor`
depends on `gte_core`, ONE WAY, NEVER the reverse (this repo's own Rule 2,
PHASE0_MASTER_STRATEGY.md) — confirmed, `ProjectAssemblyBuildRunner.cpp`'s
real, current `#include` list has ZERO dependency on anything under
`src/Editor/` today, and `ResolveCMakeBuildDirectory()` (the existing
function immediately below the new ones this phase adds) already establishes
the correct precedent: it takes `startDirectory` as an EXPLICIT PARAMETER,
never calling `gte::ExecutableDirectory()` itself — its own caller
(`EditorHotReloadDebugCapability::TriggerCompileOnly()`, a `gte_editor`-tier
file that already has `gte::ExecutableDirectory()` available) passes it in.
Every function this phase adds below follows this SAME precedent exactly —
none of them ever call `gte::ExecutableDirectory()` internally, closing off
the real risk of silently breaking `tools/ci/gte_core_player_link_probe`'s
own "`gte_core.a` links standalone, zero `gte_editor` symbols" proof
(editor-core-separation-2 campaign, PHASE4) the moment this phase's code is
actually written.

```cpp
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE4. Appends "project_assemblies" onto a CALLER-SUPPLIED
// executable directory - the ONE place this joining happens, replacing the
// identical inline expression at src/Editor/EditorHost.cpp's own
// LoadProjectAssemblies() call site (updated by this phase to call this
// function instead, to avoid a second, independently-drifting copy of the
// same path literal). Deliberately takes `executableDirectory` as an
// EXPLICIT, REQUIRED parameter, mirroring ResolveCMakeBuildDirectory()'s
// own "take the starting directory as a parameter, never resolve it
// internally" precedent immediately below - see this phase's own layering
// note above for exactly why (gte::ExecutableDirectory() is gte_editor-tier;
// this file is gte_core-tier).
std::filesystem::path ResolveProjectAssemblyOutputDirectory(const std::filesystem::path& executableDirectory);

// Copies the CURRENT, presumed-good <projectName>_Game.dll (and, if
// present, _Editor.dll) from `outputDirectory` to a dedicated backup slot,
// <outputDirectory>/.hotreload_backup/<name>_Game.dll.bak (/_Editor.dll.bak)
// - a plain file copy, safe to perform WHILE the original is still
// LoadLibraryW()'d (an already-mapped .dll permits shared-read access; only
// a WRITE-mode open, e.g. the linker overwriting it, is blocked).
// `outputDirectory` is an EXPLICIT, REQUIRED parameter (never defaulted to
// an internally-resolved value - see this phase's own layering note above)
// - the real, production caller passes
// ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory()); a test
// passes a throwaway temp directory instead, with zero special-casing
// needed on either side. MUST be called BEFORE
// ProjectAssemblyHost::UnloadProjectAssembly() for the SAME projectName,
// every hot-reload cycle - the backup is unconditionally OVERWRITTEN each
// time. Returns false (GTE_LOG_ERROR, never throws) if the copy fails for
// any reason - the caller MUST treat false as "abort before ever calling
// UnloadProjectAssembly()".
bool BackupProjectAssemblyBinaries(const std::string& projectName, const std::filesystem::path& outputDirectory);

// The rollback half - copies the .hotreload_backup/<name>_*.dll.bak files
// BACK, overwriting whatever a failed compile may have left at the real
// <outputDirectory>/<name>_*.dll paths. Safe ONLY after
// UnloadProjectAssembly() has already run for this project (nothing has the
// target path open). Same explicit, required `outputDirectory` parameter as
// BackupProjectAssemblyBinaries() above, for the identical reason. Returns
// false (GTE_LOG_ERROR) if the backup itself is missing/unreadable.
bool RestoreProjectAssemblyBinariesFromBackup(const std::string& projectName, const std::filesystem::path& outputDirectory);
```

Implementation notes:
- `ResolveProjectAssemblyOutputDirectory(executableDirectory)` is a pure,
  one-line function: `return executableDirectory / "project_assemblies";` -
  it NEVER calls `gte::ExecutableDirectory()` itself (see this phase's own
  layering note above for exactly why). Update `EditorHost.cpp` line 278's
  call site (a `gte_editor`-tier file, which already `#include`s
  `ProjectRootPath.h` and already calls `gte::ExecutableDirectory()` inline)
  from
  `m_core.LoadProjectAssemblies(gte::ExecutableDirectory() / "project_assemblies", this);`
  to
  `m_core.LoadProjectAssemblies(ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory()), this);`
  — note this stays a call on `m_core` (a member function); no bare
  `gte::LoadProjectAssemblies()` free function exists anywhere in this
  codebase.
- `BackupProjectAssemblyBinaries()`/`RestoreProjectAssemblyBinariesFromBackup()`
  use `std::filesystem::copy_file(source, destination,
  std::filesystem::copy_options::overwrite_existing, errorCode)` (the
  non-throwing, `std::error_code&`-taking overload — never the
  throwing one, matching this codebase's own consistent
  "GTE_LOG_ERROR + return false/empty on failure, never an uncaught
  exception" convention seen throughout `ProjectAssemblyBuildRunner.cpp`
  already). For each of `_Game.dll` and `_Editor.dll`: **the `_Editor.dll`
  is OPTIONAL** (a project may have no Editor sources — confirmed,
  `RunBuildThreadBody()`'s own existing "no Editor target is normal, not a
  failure" handling) — `BackupProjectAssemblyBinaries()` must only attempt
  to back up `_Editor.dll` if it actually exists at the source path
  (`std::filesystem::exists()` check first), and must NOT fail the whole
  backup just because no `_Editor.dll` exists; it MUST fail (return `false`)
  if `_Game.dll` itself is missing (a project with no `_Game.dll` at all is
  not a valid, currently-loaded Project Assembly to be backing up in the
  first place). Create the `.hotreload_backup/` sub-directory first via
  `std::filesystem::create_directories()` if missing.

### 3.5 — Wire `EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames()`

Per Step 2's own analysis above: a constructor parameter is IMPOSSIBLE here,
so give `EditorHotReloadDebugCapability` a new private pointer member,
`ProjectAssemblyHost* m_projectAssemblyHost = nullptr;` (defaulted null, so
the class's existing default, no-argument constructor is completely
untouched), plus one new public setter:

```cpp
// editor-core-separation-13 campaign, PHASE4 - called exactly once, from
// EditorHost's own constructor BODY (mirrors Core::SetEditorLayerHook()'s
// existing "setter called once after construction" precedent exactly -
// Core.h/.cpp), strictly AFTER m_core already exists but BEFORE
// m_networkServer.Start() ever accepts a real HTTP request. NOT a
// constructor parameter (see this phase's own Step 2 analysis for why that
// is impossible for a namespace-scope static).
void SetProjectAssemblyHost(ProjectAssemblyHost& projectAssemblyHost) noexcept;
```

`EditorHotReloadDebugCapability.h` forward-declares `class ProjectAssemblyHost;`
(a pointer member needs no complete type) - mirrors this header's own existing
"zero dependency on the complete type, only the .cpp needs it" convention.
`EditorHotReloadDebugCapability.cpp` needs a NEW
`#include "../Core/Plugins/ProjectAssemblyHost.h"` added to its own include
list (it does not include this header today - confirmed, its current include
list is `ProjectRootPath.h`, `../Assets/AssetDatabase.h`,
`../Core/Plugins/HotReloadEngineStateMutex.h`,
`../Core/Plugins/ProjectAssemblyBuildRunner.h`,
`../Core/Plugins/ProjectAssemblyHotReloadDebugStatus.h`,
`../ECS/Reflection/ComponentTypeRegistry.h`, `../Game/Game.h`,
`../Scene/SceneBuilder.h`, `../Scene/SceneJsonFormat.h`, none of which is
`ProjectAssemblyHost.h`) - needed both for `SetProjectAssemblyHost()`'s own
parameter type and for dereferencing `m_projectAssemblyHost->GetLoadedAssemblyFileNames()`
below, both of which need the COMPLETE type.

Then, in `EditorHost.cpp`'s constructor BODY (NOT the member-initializer
list — `s_editorHotReloadDebugCapability` is a namespace-scope static that
already exists by the time the constructor body runs, so this is a completely
ordinary call), add, immediately after `m_core.SetEditorLayerHook(...)` and
strictly BEFORE `m_networkServer.Start(8080)`:

```cpp
s_editorHotReloadDebugCapability.SetProjectAssemblyHost(m_core.GetProjectAssemblyHost());
```

(`Core::GetProjectAssemblyHost()` — a new, small, public accessor this phase
adds to `Core.h`/`.cpp`, mirroring `Core::GetRenderer()`'s own existing
precedent exactly: `ProjectAssemblyHost& GetProjectAssemblyHost() noexcept { return m_projectAssemblyHost; }`.
`m_projectAssemblyHost` is currently a PRIVATE `Core` member with no public
accessor at all — confirmed, `search_in_dir` for `GetProjectAssemblyHost`
across `src/Core/` finds zero matches today.) This call is placed
UNCONDITIONALLY (never inside the `#if GTE_ENABLE_PROJECT_ASSEMBLIES` guard
that wraps `m_core.LoadProjectAssemblies()` above it) because
`Core::m_projectAssemblyHost` itself is an unconditional `Core` member
(confirmed, `CMakeLists.txt`'s `add_library(gte_core STATIC ...)` list
compiles `Core.h`/`.cpp` unconditionally, and `Core::LoadProjectAssemblies()`'s
own doc comment confirms only the EditorHost.cpp CALL SITE is gated, never
the member itself) - this keeps `GetLoadedAssemblyFileNames()` behaving
identically (a real, always-valid pointer, simply reporting an empty vector
whenever nothing has ever been loaded) whether `GTE_ENABLE_PROJECT_ASSEMBLIES`
is ON or OFF, exactly like every other "always compiles, only the runtime
call site is gated" precedent in this codebase.

```cpp
std::vector<std::string> EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames() const
{
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
    // m_projectAssemblyHost is guaranteed non-null by the time any real HTTP
    // request can reach this method (see the setter-call-ordering guarantee
    // above) - a null check here would only ever hide a genuine construction-
    // order regression, never a legitimate runtime state, so this
    // deliberately dereferences directly rather than defensively branching.
    return m_projectAssemblyHost->GetLoadedAssemblyFileNames();
}
```

**Do not change this method's signature or the `IHotReloadDebugCapability`
interface itself** — only this concrete class gains a new private member +
setter; the abstract interface in `EditorCapabilities.h` is untouched, and
this class's own constructor stays the exact same default, no-argument one it
has today (see this phase's own Step 2 analysis for why a constructor
parameter is not just unnecessary here but actually IMPOSSIBLE).

### 3.6 — New Tier-1 test coverage

Create `tests/Core/Plugins/ProjectAssemblyHostTests.cpp` - confirmed, this
file does not exist yet anywhere in this repo (see this phase's own note a
few paragraphs below listing every file that DOES currently exist under
`tests/Core/Plugins/`), so this is a brand new file, not an extension of an
existing one. Since a REAL `.dll` load
requires a real, built Project Assembly on disk (heavier than a pure unit
test should need), scope these tests to what's testable WITHOUT a real
`.dll`:

1. `GetLoadedAssemblyFileNames()` on a freshly-constructed, never-loaded
   `ProjectAssemblyHost` returns an empty vector.
2. `UnloadProjectAssembly("NeverLoaded", core, renderer)` on a
   never-loaded host is a safe no-op (requires a minimal/headless `Core`
   and `Renderer` — reuse `CoreHeadlessConstructionTest`'s own existing
   construction precedent; if a headless `Renderer` cannot be constructed
   at all outside a real Vulkan context, this specific test may need to be
   scoped out with an explicit, documented reason — do not fake a
   `Renderer` in a way that could hide a real bug).

New file, `tests/Core/Plugins/ProjectAssemblyBuildRunnerBackupRestoreTests.cpp`
- confirmed, no `ProjectAssemblyBuildRunnerTests.cpp` (or any similarly-named
file) exists anywhere under `tests/Core/Plugins/` today (that directory's
real, current contents are exactly `PluginHostFailurePathTests.cpp`,
`PluginRenderPassBuilderAdapterV3ValidationTests.cpp`,
`PluginRenderResourceTranslationTests.cpp`,
`ProjectAssemblyHotReloadDebugStatusTests.cpp`) - this is a genuinely brand
new file, not an extension of anything pre-existing. Using a throwaway temp
directory (never the real `project_assemblies/` folder — this must not touch
real project output):

1. Write two fake files (`Foo_Game.dll`, `Foo_Editor.dll`, arbitrary byte
   content) into a temp "output directory", call
   `BackupProjectAssemblyBinaries("Foo", tempOutputDirectory)` - the
   `outputDirectory` parameter is REQUIRED (see 3.4's own layering note for
   why it is never defaulted to an internal `gte::ExecutableDirectory()`
   call), so injecting a temp path here needs no special-casing or deviation
   from the header sketch - confirm both `.bak` files now exist with
   identical byte content.
2. Delete the ORIGINAL `Foo_Game.dll` (simulating a failed compile leaving
   nothing at that path), call
   `RestoreProjectAssemblyBinariesFromBackup("Foo", tempOutputDirectory)`, confirm
   `Foo_Game.dll` exists again with the original byte content.
3. Only `_Game.dll` present (no `_Editor.dll`) — confirm
   `BackupProjectAssemblyBinaries()` still succeeds (returns `true`),
   backing up only the one file.
4. `_Game.dll` itself missing — confirm `BackupProjectAssemblyBinaries()`
   returns `false`.
5. `RestoreProjectAssemblyBinariesFromBackup()` for a project with no
   existing backup — confirm it returns `false`, logs an error, does not
   crash.

Add both new/extended test file paths to `tests/CMakeLists.txt`'s list if
new files were created.

### 3.7 — Compile check (incremental)

```
cmake --build build --target gte_core
cmake --build build --target gte_editor
cmake --build build --target GreatTamanaEngineTests
```
Fix any error.

### 3.8 — Live verification

`run_app_background` the built `GreatTamanaEditor.exe`, then, via
`gte_send_request`:

1. `GET /project_assembly/debug/loaded_assemblies` → confirm
   `["ProjectAssemblyProbe_Game.dll","ProjectAssemblyProbe_Editor.dll"]`
   (or whatever the real, current file set is — this is now REAL, live
   data, not a placeholder; if it is unexpectedly still `{"dll_file_names":[]}`,
   that is a bug in this phase's own wiring — investigate via `GET
   /get_logs` before declaring done).
2. Confirm `GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe`
   is unaffected by this phase (still whatever PHASE3 established as the
   real baseline).
3. `GET /list_tabs`/`GET /get_swapchain` unchanged — this phase adds
   capability, nothing calls `UnloadProjectAssembly()` from any production
   code path yet (that's still a manual, direct test-only call, PHASE5's
   job) — the running engine's own visible behavior must be identical to
   before this phase.

`stop_app_background` once confirmed. **Do NOT call
`UnloadProjectAssembly()` against the live, running engine in this phase** —
that live isolation test, with full before/after verification across all
five OBSERVE routes, is explicitly PHASE5's own job, once the probe fixture
also has a real custom component type to exercise Hazard 1 for real.

### 3.9 — Completion report

Write `PHASE4_COMPLETION_REPORT.md`: exact final signatures, how
`EditorHotReloadDebugCapability` gained its `SetProjectAssemblyHost()` setter
and how EditorHost.cpp's construction site now calls it, backup/restore test
results, compile results, live verification results (items 1-3 above). Commit.

## Definition of Done

- [ ] `Renderer::WaitForGpuIdle()`'s doc comment lists this feature as a
      second sanctioned caller.
- [ ] `ProjectAssemblyHost::UnloadProjectAssembly()` exists, calls
      `WaitForGpuIdle()` → ledger teardown → `FreeLibrary()` in that exact
      order, Editor-before-Game.
- [ ] `ProjectAssemblyHost::GetLoadedAssemblyFileNames()` exists.
- [ ] `BackupProjectAssemblyBinaries()`/`RestoreProjectAssemblyBinariesFromBackup()`
      exist in `ProjectAssemblyBuildRunner`, both proven by isolated
      (temp-directory) tests, including the "no `_Editor.dll`" and
      "missing `_Game.dll`" edge cases.
- [ ] `EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames()`'s body
      is real, not a placeholder; `IHotReloadDebugCapability`'s own
      interface signature is unchanged.
- [ ] Live `GET /project_assembly/debug/loaded_assemblies` shows the real,
      current `.dll` file set.
- [ ] `PHASE4_COMPLETION_REPORT.md` exists; changes committed to git.
