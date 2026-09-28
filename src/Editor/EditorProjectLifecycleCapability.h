// src/Editor/EditorProjectLifecycleCapability.h
#pragma once

#include "../Core/EditorCapabilities.h"

namespace gte {

class Core;
class EditorHost;
class ProjectLifecycleLoadCommandBridge;

// The real, gte_editor-owned implementation of IProjectLifecycleCapability
// (Core/EditorCapabilities.h). Constructed once, as a namespace-scope
// static inside EditorHost.cpp (mirrors s_editorHotReloadDebugCapability's
// exact precedent).
class EditorProjectLifecycleCapability : public IProjectLifecycleCapability {
public:
    CreateProjectOutcome CreateNewProjectAssembly(const std::string& name) override;

    // editor-core-separation-17 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 3), PHASE3. See PHASE0_MASTER_STRATEGY.md Section 2.2 and
    // this class's own .cpp top-of-file comment for the full
    // deadlock-avoidance reasoning: these two methods are NOT
    // interchangeable.
    OpenProjectOutcome OpenProjectAssembly(const std::string& name) override;
    OpenProjectOutcome OpenProjectAssemblyOnMainThread(const std::string& name) override;
    std::vector<ProjectListEntry> ListProjectAssemblies() override;

    // Called once, from EditorHost's own constructor body - mirrors
    // EditorHotReloadDebugCapability's own 3-setter precedent exactly
    // (setter, not a constructor parameter, since this object is
    // constructed via static initialization before Core/EditorHost exist).
    void SetLoadCommandBridge(ProjectLifecycleLoadCommandBridge& bridge) noexcept;

    // Gives this capability direct, main-thread-only access to the real
    // Core&/EditorHost* needed by OpenProjectAssemblyOnMainThread()'s own
    // Tier-3 load step (LoadOneProjectAssemblyFromExactPath[IfExists]()
    // needs both). `editorHost` may be nullptr (mirrors every existing
    // "*_Editor.dll load is optional" precedent in this codebase) but
    // `core` must never be.
    void SetEngineReferences(Core& core, EditorHost* editorHost) noexcept;

private:
    // Shared by BOTH OpenProjectAssembly() and OpenProjectAssemblyOnMainThread()
    // - everything EXCEPT the actual Tier-3 load call (name validation,
    // resolving the source root, classifying the tier, marking
    // ActiveProjectAssemblyState active, composing the per-tier
    // statusMessage). Returns the tier it classified, plus a
    // partially-filled OpenProjectOutcome - the caller fills in
    // loadAttempted/loadSucceeded itself after deciding HOW to perform the
    // Tier-3 load (bridge vs. direct).
    struct PreLoadResult {
        OpenProjectOutcome outcome;
        bool needsLoad = false; // true only when tier == Compiled
        std::string resolvedProjectName;
    };
    PreLoadResult ClassifyAndMarkActive(const std::string& name);

    ProjectLifecycleLoadCommandBridge* m_loadCommandBridge = nullptr;
    Core* m_core = nullptr;
    EditorHost* m_editorHost = nullptr;
};

} // namespace gte
