#pragma once

#include "EditorContext.h"

namespace gte {

class IProjectLifecycleCapability;

// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE4. A single, floating, NON-DOCKABLE ImGui utility
// window (mirrors BoneViewerWindow.h's Open()/Build() shape, but with NO
// GPU resources at all - this window only ever manipulates a text buffer
// and calls the SAME capability method the HTTP route calls, LDD-PW5's
// "one function, two callers" rule).
class NewProjectWindow {
public:
    // Clears the text buffer + any previous error message - called the
    // frame ctx.newProjectWindowOpen first flips false->true (mirrors
    // FrameDebuggerPanel's own "the bool IS the open state, Open()-shaped
    // reset happens on the rising edge" precedent).
    void Open();

    // No-op whenever ctx.newProjectWindowOpen is false - safe to call
    // every frame unconditionally.
    void Build(EditorContext& ctx, IProjectLifecycleCapability* capability);

private:
    bool m_wasOpenLastFrame = false;
    char m_nameBuffer[128] = {};
    std::string m_errorMessage; // "" = no error currently shown.
};

} // namespace gte
