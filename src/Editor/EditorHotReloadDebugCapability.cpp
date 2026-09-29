#include "EditorHotReloadDebugCapability.h"

#include "ProjectRootPath.h"
#include "../Assets/AssetDatabase.h"
#include "../Core/Plugins/HotReloadEngineStateMutex.h"
#include "../Core/Plugins/ProjectAssemblyBuildRunner.h"
#include "../Core/Plugins/ProjectAssemblyHost.h"
#include "../Core/Plugins/ProjectAssemblyHotReloadDebugStatus.h"
#include "../Core/Plugins/ProjectAssemblyRegistrationLedger.h"
// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE3.
#include "../Application/ProjectAssemblyHotReloadCommandBridge.h"
// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE4.
#include "../Application/EngineCommandBridge.h"
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

bool EditorHotReloadDebugCapability::IsCompileInFlight(const std::string& projectName) const
{
    return IsProjectAssemblyBuildInFlight(projectName);
}

bool EditorHotReloadDebugCapability::TriggerHotReload(const std::string& projectName)
{
    // editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 3), PHASE3 - REAL now (submits into the bridge and blocks
    // until the main thread's drain point services it), no longer the
    // editor-core-separation-12 PHASE2 permanent `return false;` placeholder.
    // This method's own signature never changed for this.
    if (m_hotReloadCommandBridge == nullptr) {
        return false; // Should never happen in real production wiring - see SetHotReloadCommandBridge()'s own call-ordering guarantee.
    }
    const ProjectAssemblyHotReloadCommandBridge::SubmitResult submit = m_hotReloadCommandBridge->SubmitAndWait(projectName);
    if (submit.alreadyPending) {
        return false; // Mirrors TriggerCompileOnly()'s own "already in progress -> false" contract.
    }
    if (submit.timedOut) {
        // The cycle is still running on the main thread (see
        // SubmitAndWait()'s own doc comment) - this HTTP caller simply
        // stopped waiting. Report this honestly as "not yet done from this
        // caller's point of view"; GET /project_assembly/hot_reload/status
        // is the correct way to observe the real, eventual outcome.
        return false;
    }
    return true;
}

bool EditorHotReloadDebugCapability::SetProbeHotReloadMarkerValueForTesting(int value)
{
    // Called from the NETWORK thread (a route handler) - the live
    // Registry has no synchronization of its own (see this whole class's
    // own header comment), so this submits a new EngineCommandKind
    // through EditorHost's general EngineCommandBridge and blocks until
    // the main thread's own per-frame drain point services it - mirrors
    // GetSceneSnapshot's identical bridge-based precedent (Application/
    // EngineCommandDispatch.cpp), NOT TriggerHotReload()'s own separate,
    // dedicated ProjectAssemblyHotReloadCommandBridge (that bridge is only
    // for a FULL hot-reload cycle, never for a plain, fast, single-frame
    // Registry mutation like this one).
    if (m_engineCommandBridge == nullptr) {
        return false; // Should never happen in real production wiring - see SetEngineCommandBridge()'s own call-ordering guarantee.
    }
    EngineCommandRequest request;
    request.kind = EngineCommandKind::SetProbeHotReloadMarkerValueForTesting;
    request.setProbeHotReloadMarkerValueForTesting.value = value;
    const EngineCommandBridge::SubmitResult submit = m_engineCommandBridge->SubmitAndWait(request);
    if (submit.alreadyPending || submit.timedOut || !submit.result.has_value()) {
        // Mirrors every other bool-returning method on this same
        // interface (TriggerCompileOnly/TriggerHotReload): a submission
        // failure collapses to a plain, honest `false`, never a crash and
        // never a dereference of an empty std::optional.
        return false;
    }
    return submit.result->setProbeHotReloadMarkerValueForTesting.success;
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

// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE3 - see this method's own doc comment in
// EditorHotReloadDebugCapability.h. Called exactly once, from EditorHost's
// own constructor body.
void EditorHotReloadDebugCapability::SetHotReloadCommandBridge(ProjectAssemblyHotReloadCommandBridge& bridge) noexcept
{
    m_hotReloadCommandBridge = &bridge;
}

// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE4 - see this method's own doc comment in
// EditorHotReloadDebugCapability.h. Called exactly once, from EditorHost's
// own constructor body.
void EditorHotReloadDebugCapability::SetEngineCommandBridge(EngineCommandBridge& bridge) noexcept
{
    m_engineCommandBridge = &bridge;
}

} // namespace gte
