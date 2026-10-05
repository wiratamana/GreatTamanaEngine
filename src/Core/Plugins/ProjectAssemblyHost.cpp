// src/Core/Plugins/ProjectAssemblyHost.cpp
//
// editor-core-separation-11 campaign (Project Assembly system), PHASE5.
// See ProjectAssemblyHost.h's own header comment for the full design
// rationale (mirrors PluginHost.cpp's own LoadLibraryW/GetProcAddress/
// by construction - PHASE0_MASTER_STRATEGY.md, section 2.2).
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
    // editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 2), PHASE5 - found-and-fixed regression, confirmed LIVE by
    // this phase's own isolation test. ComponentTypeRegistry::Instance()
    // self-bootstraps RegisterBuiltinComponentReflections() lazily, on ITS
    // OWN first call, from ANYWHERE in the process (ComponentTypeRegistry.cpp).
    // If that first-ever call happens to land INSIDE a Project Assembly's own
    // BeginRecordingFor()/EndRecording() bracket - which it did here, because
    // ProjectAssemblyProbe's own _Game.dll GTE_RegisterProject is the first
    // code anywhere to ever call RegisterComponentType<T>() (this phase's own
    // new ProbeHotReloadMarker) - every one of the engine's own BUILT-IN
    // component types gets misattributed to THAT project's own ledger entry.
    // Confirmed live: GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe
    // listed "Transform"/"Name"/"Camera"/"DirectionalLight"/"PrimitiveSource"
    // alongside its own real "ProbeHotReloadMarker" - none of the five
    // built-ins are ProjectAssemblyProbe's own registrations. Forcing the
    // bootstrap HERE, unconditionally, before the very first
    // TryLoadOneAssembly() call below (and therefore before any project's
    // own BeginRecordingFor() bracket can ever open), guarantees it always
    // happens OUTSIDE any such bracket, regardless of directory-iteration
    // order or which project's own registration function happens to be the
    // first in the whole process to touch ComponentTypeRegistry.
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
    // editor-core-separation-14 campaign (BIG-STEP 3), PHASE1 - closes the
    // obligation HotReloadEngineStateMutex.h's own header comment already
    // states ("a future BIG-STEP 2/3 campaign MUST lock this... around
    // every ... ProjectAssemblyHost load/unload call"). Safe to nest:
    // ProjectAssemblyRegistrationLedger's own methods (called below, via
    // GTE_RegisterProject's own registration calls) only ever take THEIR
    // OWN internal mutex, never this one (confirmed,
    // ProjectAssemblyRegistrationLedger.h's own header comment) - no
    // deadlock risk.
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
            return false;
        }
        using EditorEntryFn = void (*)(Core&, EditorHost&);
        auto entry = reinterpret_cast<EditorEntryFn>(reinterpret_cast<void*>(GetProcAddress(module, "GTE_RegisterProject")));
        if (entry == nullptr) {
            GTE_LOG_WARNING("ProjectAssembly", fileName + " is missing export GTE_RegisterProject, not a valid Project Assembly, skipping");
            FreeLibrary(module);
            return false;
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
            return false;
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
    return true;
}

// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE1. Thin wrapper over the now-public-facing
// TryLoadOneAssembly() - see ProjectAssemblyHost.h's own doc comment for the
// full reasoning (reused by PerformProjectAssemblyHotReload(), PHASE4, for
// both the success-path fresh-compile load and the failure-path
// backup-restore load).
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

// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE4 - see this method's own doc comment in
// ProjectAssemblyHost.h for the exact, non-negotiable 3-step order this
// implements.
void ProjectAssemblyHost::UnloadProjectAssembly(const std::string& projectName, Core& core, Renderer& renderer)
{
    // editor-core-separation-14 campaign (BIG-STEP 3), PHASE1 - closes the
    // obligation HotReloadEngineStateMutex.h's own header comment already
    // states. Covers the whole function, including the "nothing loaded,
    // no-op" early-return below, so a concurrent read of
    // GetLoadedAssemblyFileNames()/the registration ledger can never
    // straddle that decision.
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());

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

} // namespace gte
