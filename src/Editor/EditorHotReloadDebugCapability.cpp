#include "EditorHotReloadDebugCapability.h"

#include "ProjectRootPath.h"
#include "../Assets/AssetDatabase.h"
#include "../Core/Plugins/HotReloadEngineStateMutex.h"
#include "../Core/Plugins/ProjectAssemblyBuildRunner.h"
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
    // editor-core-separation-12 campaign, PHASE2 - PLACEHOLDER. ProjectAssemblyHost
    // has no accessor for its own loaded-assembly list yet - a future
    // BIG-STEP 2 campaign adds one and replaces ONLY this method's body.
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
    return {};
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

} // namespace gte
