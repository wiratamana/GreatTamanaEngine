// src/Core/Plugins/ProjectAssemblyHost.h
//
// editor-core-separation-11 campaign (Project Assembly system), PHASE5.
// Scans GTE_PROJECT_ASSEMBLY_OUTPUT_DIR (PHASE3) for *_Game.dll/*_Editor.dll,
// LoadLibraryW()s each, resolves the ONE fixed export (GTE_RegisterProject),
// and calls it once per .dll, passing the real, live gte::Core& (and, for
// an _Editor assembly, the real, live gte::EditorHost&).
//
// DELIBERATE DIFFERENCES from PluginHost (read that file's own header
// comment first for the shape this mirrors):
//   1. No ABI fingerprint struct is generated or checked here at all - this
//      is unnecessary by construction (same-toolchain/same-build guaranteed,
//      since a Project Assembly .dll links GreatTamanaEditor directly -
//      PHASE2/3).
//   2. TWO different function-pointer signatures are resolved, depending on
//      whether the .dll's own filename ends in "_Game.dll" or
//      "_Editor.dll" - see LoadProjectAssemblies()'s own doc comment below
//      for exactly how this is distinguished, and why a single, uniform
//      resolution (like PluginHost's single IPluginModule* shape) does not
//      work here.
//   3. This class does NOT keep a live IPluginModule*-equivalent handle per
//      loaded assembly after registration - GTE_RegisterProject is a
//      one-shot call, there is no ongoing QueryCapability()-style
//      interaction afterward. This class's only ongoing job after
//      LoadProjectAssemblies() returns is keeping every loaded HMODULE
//      alive (never FreeLibrary()'d) for the rest of the process lifetime
//      (LDD4 - no hot reload, ever).
//
// LOAD-BEARING COUPLING, flagged loudly (see TryLoadOneAssembly()'s own
// .cpp-side comment for the full reasoning): the exported symbol name is
// ALWAYS "GTE_RegisterProject" for both a _Game.dll and an _Editor.dll -
// this class tells them apart ONLY by filename suffix, resolved BEFORE the
// export is looked up, never by inspecting the export's own signature (no
// such runtime introspection exists). This is why cmake/GteProject.cmake's
// gte_add_project() unconditionally names its targets exactly
// "${NAME}_Game"/"${NAME}_Editor" - if that naming convention is ever
// loosened, this class breaks silently (calling a 2-argument function
// through a 1-argument pointer is undefined behavior, with no guaranteed
// crash at the call site).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gte {

class Core;
class EditorHost;
class Renderer;

class ProjectAssemblyHost {
public:
    ProjectAssemblyHost() = default;
    ~ProjectAssemblyHost();

    ProjectAssemblyHost(const ProjectAssemblyHost&) = delete;
    ProjectAssemblyHost& operator=(const ProjectAssemblyHost&) = delete;
    ProjectAssemblyHost(ProjectAssemblyHost&&) = delete;
    ProjectAssemblyHost& operator=(ProjectAssemblyHost&&) = delete;

    // Enumerates every "*_Game.dll"/"*_Editor.dll" directly inside
    // `outputDirectory` (no recursion - mirrors PluginHost::LoadPlugins()'s
    // own "directly inside, no subfolder recursion" rule), loads each, and
    // calls its GTE_RegisterProject export exactly once. Safe to call with
    // a non-existent directory (a fresh checkout with no Projects/ folder
    // yet must never crash - mirrors PluginHost's own identical guarantee).
    // `core` must outlive this call by the engine's entire remaining
    // process lifetime. `editorHost` is `nullptr` for a Player-shaped host
    // that never constructs one (mirrors IEditorPanelModule_v1's own "never
    // queried by a Player host" precedent) - a "*_Editor.dll" found while
    // `editorHost == nullptr` is skipped with a loud, logged warning, never
    // crashed on.
    void LoadProjectAssemblies(const std::filesystem::path& outputDirectory,
        Core& core, EditorHost* editorHost);

    std::size_t LoadedAssemblyCount() const noexcept { return m_loadedAssemblies.size(); }

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

    // editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 3), PHASE1. Loads exactly ONE .dll at an EXACT, caller-supplied
    // path (never scanning a directory) and calls its GTE_RegisterProject
    // export - reused by PerformProjectAssemblyHotReload() (PHASE4) for BOTH
    // the success-path fresh-compile load and the failure-path backup-restore
    // load, since both are mechanically identical: "load this exact .dll file
    // and call its export". Thin wrapper over the now-public-facing
    // TryLoadOneAssembly() - same suffix-based _Game/_Editor dispatch, same
    // ProjectAssemblyRegistrationLedger bracketing, same LoadedAssembly
    // bookkeeping. Returns false (logged) if LoadLibraryW fails or the export
    // is missing - never crashes.
    bool LoadOneProjectAssemblyFromExactPath(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);

    // Same as above, but a NON-EXISTENT dllPath is a normal, valid, TRUE
    // ("nothing to do here") result, not an error - mirrors
    // ProjectAssemblyBuildRunner's own "no Editor target is a normal, valid
    // case" precedent, for a project with no _Editor.dll at all.
    bool LoadOneProjectAssemblyFromExactPathIfExists(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);

private:
    struct LoadedAssembly {
        void* moduleHandle = nullptr; // HMODULE, stored as void* - mirrors PluginHost's identical convention.
        std::string dllFileName;
    };

    bool TryLoadOneAssembly(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);

    std::vector<LoadedAssembly> m_loadedAssemblies;
};

} // namespace gte
