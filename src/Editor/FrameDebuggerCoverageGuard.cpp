#include "FrameDebuggerCoverageGuard.h"

namespace gte {

std::string FrameDebuggerCoverageGuardTag::FormatMessage(const std::string& passName)
{
    return "Pass '" + passName
        + "' genuinely executed this frame (non-culled, non-FrameDebuggerInternal, non-SceneView) but has NO "
          "corresponding leaf anywhere in the captured Frame Debugger event tree - the render pass ran, but the "
          "Frame Debugger shows nothing for it (Clause B violation).";
}

} // namespace gte
