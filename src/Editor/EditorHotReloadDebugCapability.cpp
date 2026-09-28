#include "EditorHotReloadDebugCapability.h"

#include "ProjectRootPath.h"
#include "../Assets/AssetDatabase.h"
#include "../Core/Plugins/HotReloadEngineStateMutex.h"
#include "../Core/Plugins/ProjectAssemblyBuildRunner.h"
#include "../Core/Plugins/ProjectAssemblyHost.h"
#include "../Core/Plugins/ProjectAssemblyHotReloadDebugStatus.h"
#include "../Core/Plugins/ProjectAssemblyRegistrationLedger.h"
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
    const std::string& projectName) const
{
    // editor-core-separation-13 campaign, PHASE3 - REAL now, no longer a
    // placeholder. ProjectAssemblyRegistrationLedger is the real class a
    // future BIG-STEP 2 campaign (this one) built; this method is a
    // trivial 1-to-1 field copy of its own Entry struct into this
    // interface's own identically-shaped LedgerEntry struct (see
    // ProjectAssemblyRegistrationLedger.h's own header comment for why the
    // field names/order are guaranteed identical).
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
    const ProjectAssemblyRegistrationLedger::Entry entry =
        ProjectAssemblyRegistrationLedger::Instance().PeekEntry(projectName);
    LedgerEntry result;
    result.renderPassNames = entry.renderPassNames;
    result.panelNames = entry.panelNames;
    result.componentTypeNames = entry.componentTypeNames;
    return result;
}

std::vector<std::string> EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames() const
{
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
    // editor-core-separation-13 campaign, PHASE4 - REAL now, no longer a
    // placeholder. m_projectAssemblyHost is guaranteed non-null by the time
    // any real HTTP request can reach this method THROUGH THE REAL PRODUCTION
    // WIRING (see SetProjectAssemblyHost()'s own doc comment for the
    // setter-call-ordering guarantee) - but PHASE5's own full ctest
    // regression pass found and fixed a genuine null-pointer crash here: a
    // pre-existing BIG-STEP1 test
    // (tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp,
    // `LoadedAssembliesReturnsHonestEmptyPlaceholderList`) constructs a bare
    // `EditorHotReloadDebugCapability` directly and legitimately never calls
    // SetProjectAssemblyHost() at all - this class's own doc comment's prior
    // claim ("never a legitimate runtime state") was simply wrong, confirmed
    // by a real SEGFAULT the very first time this whole campaign's own full
    // `ctest` pass ran this test alongside PHASE4's real body. Fixed with a
    // defensive null check, mirroring this whole engine's own "unknown/
    // unset state degrades to an honest empty result, never a crash"
    // philosophy used everywhere else in this class and its siblings.
    if (m_projectAssemblyHost == nullptr) {
        return {};
    }
    return m_projectAssemblyHost->GetLoadedAssemblyFileNames();
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

// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE4 - see this method's own doc comment in
// EditorHotReloadDebugCapability.h for the full "why a setter, not a
// constructor parameter" reasoning. Called exactly once, from EditorHost's
// own constructor body.
void EditorHotReloadDebugCapability::SetProjectAssemblyHost(ProjectAssemblyHost& projectAssemblyHost) noexcept
{
    m_projectAssemblyHost = &projectAssemblyHost;
}

} // namespace gte
