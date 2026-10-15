// Loads *_Game.dll / *_Editor.dll project assemblies and calls each one's GTE_RegisterProject export.
// The kind is told apart by filename suffix only, so gte_add_project() must keep the _Game/_Editor target names.
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

    // Loads every "*_Game.dll"/"*_Editor.dll" directly inside `outputDirectory` (no recursion) and calls
    // each GTE_RegisterProject export once. A missing directory is fine. `core` must outlive the process.
    // `editorHost` is nullptr for a standalone host; a "*_Editor.dll" is then skipped with a logged warning.
    void LoadProjectAssemblies(const std::filesystem::path& outputDirectory,
        Core& core, EditorHost* editorHost);

    std::size_t LoadedAssemblyCount() const noexcept { return m_loadedAssemblies.size(); }

    // Unloads a project's _Game.dll and _Editor.dll in a fixed order: idle the GPU, unregister every pass/panel/
    // component it registered, then FreeLibrary (Editor first). A no-op for an unknown project.
    void UnloadProjectAssembly(const std::string& projectName, Core& core, Renderer& renderer);

    // File names of the currently loaded .dlls (never the raw HMODULE).
    std::vector<std::string> GetLoadedAssemblyFileNames() const;

    // Loads exactly one .dll at `dllPath` (same _Game/_Editor suffix dispatch as above). False (logged) on failure.
    bool LoadOneProjectAssemblyFromExactPath(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);

    // Same, but a non-existent `dllPath` is a normal "nothing to do" and returns true.
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
