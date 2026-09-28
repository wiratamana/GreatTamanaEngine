#pragma once

#include "../Core/EditorCapabilities.h"

namespace gte {

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
};

} // namespace gte
