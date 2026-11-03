#include "Frame.h"

#include "FrameContext.h"
#include "../Core.h"

namespace gte::fx {

bool PreOpaqueFrame::SetViewJitter(Vec2 ndc)
{
    detail::FrameContext* const context = Context();
    if (context == nullptr || context->core == nullptr || Scope() == rg::ViewScope::Shared) {
        return false;
    }
    return context->core->SetViewJitter(Scope(), ndc);
}

} // namespace gte::fx
