#pragma once

#include "EditorContext.h"

namespace gte {

class IAssetScaffoldingCapability;

// editor-core-separation-18 campaign (On-Engine Project Workflow plan,
// BIG-STEP 4). A single, floating, NON-DOCKABLE ImGui utility window
// (mirrors NewProjectWindow.h's Open()/Build() shape exactly), parameterized
// by whichever AssetScaffoldKind the Project panel's own "Create" submenu
// most recently opened it with (ctx.createAssetWindowPendingKind).
class CreateAssetWindow {
public:
    void Open();
    void Build(EditorContext& ctx, IAssetScaffoldingCapability* capability);

private:
    bool m_wasOpenLastFrame = false;
    char m_nameBuffer[128] = {};
    std::string m_errorMessage;
};

} // namespace gte
