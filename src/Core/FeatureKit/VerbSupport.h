#pragma once

// Kit-private helpers shared by the frame verbs.

#include "Frame.h"
#include "FrameContext.h"
#include "../../Renderer/RenderGraph/RenderPassToggleGuard.h"

#include <string>
#include <string_view>

namespace gte::fx::detail {

// True when the verb may declare its pass: the pass is not toggled off.
inline bool PassEnabled(const FrameContext& context, rg::StaticName passName)
{
    return rg::ShouldDeclareBuiltInPassThisFrame(context.toggles, passName.value);
}

// True when `texture` belongs to this frame callback; otherwise logs once and returns false.
inline bool CheckOwned(Frame& frame, const Texture& texture, rg::StaticName passName, std::string_view what)
{
    if (frame.Owns(texture)) {
        return true;
    }
    frame.ReportOnce(std::string("Pass '") + passName.value + "': " + std::string(what)
        + " is invalid or was made by another frame callback");
    return false;
}

} // namespace gte::fx::detail
