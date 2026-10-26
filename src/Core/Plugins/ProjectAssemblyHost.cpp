// src/Core/Plugins/ProjectAssemblyHost.cpp
// Loads and unloads per-project game/editor DLLs via LoadLibraryW/GetProcAddress.
#include "ProjectAssemblyHost.h"
#include "../Logging.h"
#include "ProjectAssemblyRegistrationLedger.h"
#include "HotReloadEngineStateMutex.h"
#include "../../ECS/Reflection/ComponentTypeRegistry.h"
#include "../../Renderer/Renderer.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <windows.h>

namespace gte {

namespace {
// Strips the "_Game.dll" / "_Editor.dll" suffix to recover the project name.
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
} // namespace

ProjectAssemblyHost::~ProjectAssemblyHost()
{
    // Assemblies are never FreeLibrary()'d here: the OS unmaps them at process exit.
    // Only UnloadProjectAssembly() frees a module.
}

void ProjectAssemblyHost::LoadProjectAssemblies(
    const std::filesystem::path& outputDirectory, Core& core, EditorHost* editorHost)
{
    // Force the registry's lazy built-in bootstrap here, outside any project's recording bracket, so built-in
    // component types are never attributed to a project's ledger entry.
    ComponentTypeRegistry::Instance();

    if (!std::filesystem::exists(outputDirectory)) {
        GTE_LOG_INFO("ProjectAssembly", "no project_assemblies directory found at " + outputDirectory.string() + ", skipping");
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(outputDirectory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        TryLoadOneAssembly(entry.path(), core, editorHost);
    }
}

bool ProjectAssemblyHost::TryLoadOneAssembly(
    const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost)
{
    // Serializes loads against hot reload and unload. Nesting is safe: the ledger only takes its own mutex.
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());

    const std::string fileName = dllPath.filename().string();
    const bool isEditorAssembly = fileName.ends_with("_Editor.dll");
    const bool isGameAssembly = fileName.ends_with("_Game.dll");
    if (!isEditorAssembly && !isGameAssembly) {
        return false; // Not a Project Assembly output - ignore silently.
    }

    // Suppress the OS "entry point not found" dialog; restore the mode right after.
    UINT previousErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    HMODULE module = LoadLibraryW(dllPath.c_str());
    SetErrorMode(previousErrorMode);
    if (module == nullptr) {
        GTE_LOG_ERROR_BLOCKING("ProjectAssembly", "Failed to load Project Assembly '" + fileName + "' (GetLastError=" + std::to_string(GetLastError()) + "). This usually means it was built against an older/incompatible engine version - rebuild it and relaunch.");
        return false;
    }

    // Both assembly kinds export "GTE_RegisterProject" with different signatures; the filename suffix alone
    // picks the call shape. A wrong shape is undefined behavior.
    if (isEditorAssembly) {
        if (editorHost == nullptr) {
            GTE_LOG_WARNING("ProjectAssembly", fileName + " is an _Editor assembly but no EditorHost exists in this process - skipped.");
            FreeLibrary(module); // Safe here - its export was never called.
            return false;
        }
        using EditorEntryFn = void (*)(Core&, EditorHost&);
        auto entry = reinterpret_cast<EditorEntryFn>(reinterpret_cast<void*>(GetProcAddress(module, "GTE_RegisterProject")));
        if (entry == nullptr) {
            GTE_LOG_WARNING("ProjectAssembly", fileName + " is missing export GTE_RegisterProject, not a valid Project Assembly, skipping");
            FreeLibrary(module);
            return false;
        }
        // Attribute this project's registrations to its ledger entry.
        const std::string projectName = DeriveProjectNameFromDllFileName(fileName);
        ProjectAssemblyRegistrationLedger::Instance().BeginRecordingFor(projectName);
        entry(core, *editorHost);
        ProjectAssemblyRegistrationLedger::Instance().EndRecording();
    } else { // isGameAssembly
        using GameEntryFn = void (*)(Core&);
        auto entry = reinterpret_cast<GameEntryFn>(reinterpret_cast<void*>(GetProcAddress(module, "GTE_RegisterProject")));
        if (entry == nullptr) {
            GTE_LOG_WARNING("ProjectAssembly", fileName + " is missing export GTE_RegisterProject, not a valid Project Assembly, skipping");
            FreeLibrary(module);
            return false;
        }
        // Attribute this project's registrations to its ledger entry, as in the _Editor branch.
        const std::string projectName = DeriveProjectNameFromDllFileName(fileName);
        ProjectAssemblyRegistrationLedger::Instance().BeginRecordingFor(projectName);
        entry(core);
        ProjectAssemblyRegistrationLedger::Instance().EndRecording();
    }

    GTE_LOG_INFO("ProjectAssembly", "Loaded Project Assembly '" + fileName + "' from " + dllPath.string());

    LoadedAssembly loaded;
    loaded.moduleHandle = module;
    loaded.dllFileName = fileName;
    m_loadedAssemblies.push_back(loaded);
    return true;
}

// Loads one assembly from an exact path; used by hot reload for both the fresh build and the backup restore.
bool ProjectAssemblyHost::LoadOneProjectAssemblyFromExactPath(
    const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost)
{
    return TryLoadOneAssembly(dllPath, core, editorHost);
}

bool ProjectAssemblyHost::LoadOneProjectAssemblyFromExactPathIfExists(
    const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost)
{
    if (!std::filesystem::exists(dllPath)) {
        GTE_LOG_INFO("ProjectAssembly",
            "LoadOneProjectAssemblyFromExactPathIfExists: " + dllPath.string() +
            " does not exist - treating as a normal 'no Editor assembly for this project' case.");
        return true;
    }
    return LoadOneProjectAssemblyFromExactPath(dllPath, core, editorHost);
}

bool ProjectAssemblyHost::UnloadProjectAssembly(const std::string& projectName, Core& core, Renderer& renderer)
{
    // Held for the whole call so readers of the loaded list and the ledger never see a half-finished unload.
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());

    std::vector<std::size_t> matchingIndices;
    for (std::size_t i = 0; i < m_loadedAssemblies.size(); ++i) {
        if (DeriveProjectNameFromDllFileName(m_loadedAssemblies[i].dllFileName) == projectName) {
            matchingIndices.push_back(i);
        }
    }
    if (matchingIndices.empty()) {
        GTE_LOG_INFO("ProjectAssembly", "UnloadProjectAssembly('" + projectName + "') - nothing currently loaded for this project, no-op.");
        return true;
    }

    // The GPU must be idle before any project code is freed; otherwise the project stays loaded.
    if (renderer.DrainGpuForCodeUnload() != BlockOutcome::Completed) {
        GTE_LOG_ERROR("ProjectAssembly", "UnloadProjectAssembly('" + projectName + "') skipped: the GPU did not go idle.");
        return false;
    }

    ProjectAssemblyRegistrationLedger::Instance().UnregisterEverythingFor(projectName, core);

    // Editor assemblies are freed first (reverse of load order).
    std::sort(matchingIndices.begin(), matchingIndices.end(), [this](std::size_t a, std::size_t b) {
        const bool aIsEditor = m_loadedAssemblies[a].dllFileName.ends_with("_Editor.dll");
        const bool bIsEditor = m_loadedAssemblies[b].dllFileName.ends_with("_Editor.dll");
        return aIsEditor && !bIsEditor;
    });
    for (const std::size_t index : matchingIndices) {
        GTE_LOG_INFO("ProjectAssembly", "Unloading '" + m_loadedAssemblies[index].dllFileName + "'.");
        FreeLibrary(static_cast<HMODULE>(m_loadedAssemblies[index].moduleHandle));
    }
    // Erase in descending index order so earlier indices stay valid.
    std::sort(matchingIndices.begin(), matchingIndices.end(), std::greater<std::size_t>());
    for (const std::size_t index : matchingIndices) {
        m_loadedAssemblies.erase(m_loadedAssemblies.begin() + static_cast<std::ptrdiff_t>(index));
    }
    GTE_LOG_INFO("ProjectAssembly", "UnloadProjectAssembly('" + projectName + "') complete.");
    return true;
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

} // namespace gte
