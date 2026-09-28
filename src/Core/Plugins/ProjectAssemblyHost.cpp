// src/Core/Plugins/ProjectAssemblyHost.cpp
//
// editor-core-separation-11 campaign (Project Assembly system), PHASE5.
// See ProjectAssemblyHost.h's own header comment for the full design
// rationale (mirrors PluginHost.cpp's own LoadLibraryW/GetProcAddress/
// logging idiom, minus the ABI-fingerprint check, which is unnecessary here
// by construction - PHASE0_MASTER_STRATEGY.md, section 2.2).
#include "ProjectAssemblyHost.h"

#include "../Logging.h"
#include "ProjectAssemblyRegistrationLedger.h"

#include <cstring>
#include <windows.h>

namespace gte {

namespace {
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE3 - strips a known Project Assembly .dll suffix to
// recover the plain project name - the ONE place this derivation is needed
// (TryLoadOneAssembly() already has `fileName` computed locally, right
// before this).
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
    // Never FreeLibrary()'d before process exit - LDD4 (no hot reload,
    // ever). Mirrors PluginHost's own reverse-destruction-order shape only
    // in spirit; there is no per-assembly destroy export to call here (a
    // Project Assembly's GTE_RegisterProject is a one-shot registration
    // call, not a live capability object this class owns afterward), so
    // this destructor intentionally does nothing but let the process itself
    // tear down - the OS unmaps every loaded module at process exit
    // regardless.
}

void ProjectAssemblyHost::LoadProjectAssemblies(
    const std::filesystem::path& outputDirectory, Core& core, EditorHost* editorHost)
{
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

void ProjectAssemblyHost::TryLoadOneAssembly(
    const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost)
{
    const std::string fileName = dllPath.filename().string();
    const bool isEditorAssembly = fileName.ends_with("_Editor.dll");
    const bool isGameAssembly = fileName.ends_with("_Game.dll");
    if (!isEditorAssembly && !isGameAssembly) {
        return; // Not a Project Assembly output - ignore silently.
    }

    HMODULE module = LoadLibraryW(dllPath.c_str());
    if (module == nullptr) {
        GTE_LOG_WARNING("ProjectAssembly", "Failed to LoadLibraryW: " + dllPath.string() + " (GetLastError=" + std::to_string(GetLastError()) + ")");
        return;
    }

    // LOAD-BEARING: the exported symbol name is the SAME,
    // "GTE_RegisterProject", for both a _Game.dll and an _Editor.dll -
    // _Game vs. _Editor is decided ONLY by the filename suffix check
    // above, BEFORE the export is ever resolved. See this file's own header
    // comment (ProjectAssemblyHost.h) for why: there is no way to introspect
    // a resolved function pointer's own parameter count at runtime, so
    // getting this filename-based dispatch wrong (e.g. calling a
    // 2-argument _Editor export through a 1-argument _Game-shaped function
    // pointer) is undefined behavior with no guaranteed crash at the call
    // site.
    if (isEditorAssembly) {
        if (editorHost == nullptr) {
            GTE_LOG_WARNING("ProjectAssembly", fileName + " is an _Editor assembly but no EditorHost exists in this process - skipped.");
            FreeLibrary(module); // Safe here - its export was never called.
            return;
        }
        using EditorEntryFn = void (*)(Core&, EditorHost&);
        auto entry = reinterpret_cast<EditorEntryFn>(reinterpret_cast<void*>(GetProcAddress(module, "GTE_RegisterProject")));
        if (entry == nullptr) {
            GTE_LOG_WARNING("ProjectAssembly", fileName + " is missing export GTE_RegisterProject, not a valid Project Assembly, skipping");
            FreeLibrary(module);
            return;
        }
        // editor-core-separation-13 campaign (Project Assembly Hot Reload
        // plan, BIG-STEP 2), PHASE3 - brackets this GTE_RegisterProject call
        // so every RegisterDescriptor()/RegisterPluginPanel()/
        // RegisterProjectRenderPassProvider() call it makes gets attributed
        // to this project's own ledger entry, additive/safe - does not
        // change what gets loaded, in what order, or with what arguments.
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
            return;
        }
        // editor-core-separation-13 campaign, PHASE3 - same bracket as the
        // _Editor branch above (see that comment for the full reasoning).
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
}

} // namespace gte
