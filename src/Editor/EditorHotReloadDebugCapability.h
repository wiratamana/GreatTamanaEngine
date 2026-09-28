#pragma once

#include "../Core/EditorCapabilities.h"

namespace gte {

class ProjectAssemblyHost;
// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE3.
class ProjectAssemblyHotReloadCommandBridge;
// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE4.
class EngineCommandBridge;

// editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1), PHASE2 - the real, gte_editor-owned implementation of
// IHotReloadDebugCapability (Core/EditorCapabilities.h). Constructed once,
// as a namespace-scope static inside EditorHost.cpp (mirrors
// s_editorLogQueryCapability's exact precedent - see that file's own
// PHASE3 comment for why a namespace-scope static, not a function-local
// one, is required here: its address is needed inside EditorHost's own
// member-initializer list, which runs before the constructor body).
//
// This HEADER deliberately carries ZERO dependency on ComponentTypeRegistry.h/
// ProjectRootPath.h/SceneBuilder.h/SceneJsonFormat.h/
// ProjectAssemblyBuildRunner.h/HotReloadEngineStateMutex.h themselves (only
// this class's own .cpp does) - mirrors EditorSceneIOCapability.h's own
// identical "the real body needing the complete type lives in a
// gte_editor-only .cpp" precedent exactly.
class EditorHotReloadDebugCapability : public IHotReloadDebugCapability {
public:
    Status GetHotReloadStatus() const override;
    LedgerEntry GetLedgerEntry(const std::string& projectName) const override;
    std::vector<std::string> GetLoadedAssemblyFileNames() const override;
    std::vector<std::string> GetRegisteredComponentTypeNames() const override;
    std::string BuildSceneSnapshotJson(Game& game) override;
    bool TriggerCompileOnly(const std::string& projectName) override;
    bool TriggerHotReload(const std::string& projectName) override;
    bool SetProbeHotReloadMarkerValueForTesting(int value) override;

    // editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 2), PHASE4 - called exactly once, from EditorHost's own
    // constructor BODY (mirrors Core::SetEditorLayerHook()'s existing
    // "setter called once after construction" precedent exactly -
    // Core.h/.cpp), strictly AFTER m_core already exists but BEFORE
    // m_networkServer.Start() ever accepts a real HTTP request. NOT a
    // constructor parameter: `s_editorHotReloadDebugCapability` is a
    // namespace-scope static, constructed via ordinary C++ static
    // initialization BEFORE main() (and therefore strictly before any
    // Core/ProjectAssemblyHost object is ever constructed anywhere in the
    // process) - a constructor parameter of type ProjectAssemblyHost& would
    // require an already-constructed object to exist at that point, which
    // is impossible (see PHASE4_PROJECT_ASSEMBLY_HOST_UNLOAD_GPU_SAFETY_AND_BINARY_BACKUP.md,
    // Step 2, for the full reasoning).
    void SetProjectAssemblyHost(ProjectAssemblyHost& projectAssemblyHost) noexcept;

    // editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 3), PHASE3 - gives this capability a live
    // ProjectAssemblyHotReloadCommandBridge& to submit a hot-reload request
    // through (see TriggerHotReload()'s own .cpp body). Same "setter, not a
    // constructor parameter" reasoning as SetProjectAssemblyHost() above -
    // called exactly once, from EditorHost's own constructor body.
    void SetHotReloadCommandBridge(ProjectAssemblyHotReloadCommandBridge& bridge) noexcept;

    // editor-core-separation-15 campaign, PHASE4 - gives this capability a
    // live EngineCommandBridge& to submit the one new
    // SetProbeHotReloadMarkerValueForTesting engine command through (this is
    // EditorHost's own GENERAL m_commandBridge - the SAME bridge
    // GetSceneSnapshot/SaveScene/LoadScene/etc already share - NOT
    // m_hotReloadCommandBridge, which is a separate, dedicated bridge only
    // for a full hot-reload CYCLE). Same "setter, not a constructor
    // parameter" reasoning as SetProjectAssemblyHost()/
    // SetHotReloadCommandBridge() above - called exactly once, from
    // EditorHost's own constructor body.
    void SetEngineCommandBridge(EngineCommandBridge& bridge) noexcept;

private:
    // editor-core-separation-13 campaign, PHASE4 - defaulted null so this
    // class's existing default, no-argument constructor is completely
    // untouched. Guaranteed non-null by the time any real HTTP request can
    // reach GetLoadedAssemblyFileNames() (see that method's own .cpp-side
    // comment for the setter-call-ordering guarantee).
    ProjectAssemblyHost* m_projectAssemblyHost = nullptr;
    // editor-core-separation-14 campaign, PHASE3 - defaulted null so this
    // class's existing default, no-argument constructor is completely
    // untouched; TriggerHotReload() defensively returns false if this is
    // still null (should never happen in real production wiring).
    ProjectAssemblyHotReloadCommandBridge* m_hotReloadCommandBridge = nullptr;
    // editor-core-separation-15 campaign, PHASE4 - defaulted null so this
    // class's existing default, no-argument constructor is completely
    // untouched; SetProbeHotReloadMarkerValueForTesting() defensively returns
    // false if this is still null (should never happen in real production
    // wiring).
    EngineCommandBridge* m_engineCommandBridge = nullptr;
};

} // namespace gte
