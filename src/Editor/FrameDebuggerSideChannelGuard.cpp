#include "FrameDebuggerSideChannelGuard.h"

namespace gte {

std::string FrameDebuggerSideChannelGuardTag::FormatMessage(const std::string& keyDebugName)
{
    return "RenderPassBlackboard key '" + keyDebugName
        + "' was published this frame even though its own gating pass is DISABLED - a disabled pass's own side "
          "effect is still visible to whichever other pass reads this key (Clause C violation).";
}

} // namespace gte
