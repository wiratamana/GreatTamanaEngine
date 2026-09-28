// src/Editor/EditorProjectLifecycleCapability.h
#pragma once

#include "../Core/EditorCapabilities.h"

namespace gte {

// The real, gte_editor-owned implementation of IProjectLifecycleCapability
// (Core/EditorCapabilities.h). Constructed once, as a namespace-scope
// static inside EditorHost.cpp (mirrors s_editorHotReloadDebugCapability's
// exact precedent).
class EditorProjectLifecycleCapability : public IProjectLifecycleCapability {
public:
    CreateProjectOutcome CreateNewProjectAssembly(const std::string& name) override;
};

} // namespace gte
