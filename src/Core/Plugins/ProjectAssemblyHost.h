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

private:
    struct LoadedAssembly {
        void* moduleHandle = nullptr; // HMODULE, stored as void* - mirrors PluginHost's identical convention.
        std::string dllFileName;
    };

    void TryLoadOneAssembly(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);

    std::vector<LoadedAssembly> m_loadedAssemblies;
};

} // namespace gte
