#include "RenderFeatureCameraData.h"

#include "../../Application/RenderPassViewData.h"
#include "../Logging.h"

namespace gte {

void RenderFeatureCameraDataWarningTracker::Reset()
{
    m_warnedViews.clear();
}

bool RenderFeatureCameraDataWarningTracker::ShouldWarnOnce(rg::RenderViewId view)
{
    for (const rg::RenderViewId& warned : m_warnedViews) {
        if (warned == view) {
            return false;
        }
    }
    m_warnedViews.push_back(view);
    return true;
}

namespace {

// Process-lifetime singleton backing ResolveRenderFeatureCameraData()'s own
// log-once-per-view behavior - never consulted by this class's own Tier-1
// tests, which construct their own, independent tracker instances instead.
RenderFeatureCameraDataWarningTracker& WarningTrackerInstance()
{
    static RenderFeatureCameraDataWarningTracker tracker;
    return tracker;
}

} // namespace

RenderFeatureCameraData ResolveRenderFeatureCameraData(rg::RenderViewId view, const RenderPassViewData* viewData)
{
    if (viewData == nullptr) {
        return RenderFeatureCameraData{};
    }

    RenderFeatureCameraData result;
    result.eyeWorldPosition = viewData->eyeWorldPosition;
    result.relative = viewData->eyeRelativeViewProjection.has_value()
        ? MakeEyeRelativeCameraFromMatrix(*viewData->eyeRelativeViewProjection)
        : MakeEyeRelativeCamera(viewData->viewProjection, viewData->eyeWorldPosition);

    if (!result.relative.valid && WarningTrackerInstance().ShouldWarnOnce(view)) {
        GTE_LOG_ERROR("RenderFeatureCameraData",
            "ResolveRenderFeatureCameraData() - view-projection matrix is singular; the view has no camera.");
    }
    return result;
}

} // namespace gte
