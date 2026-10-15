#pragma once

// Per-view camera data for a PostComposite/PreUI render-feature callback: what a depth-based or
// camera-aware effect needs beyond ScenePassReadHandles' color/depth read handles.

#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"
#include "../../Renderer/RenderGraph/RenderGraphHandles.h"

#include <vector>

namespace gte {

// Only used by pointer here; defined in Application/RenderPassViewData.h.
struct RenderPassViewData;

struct RenderFeatureCameraData {
    Vec3 eyeWorldPosition;
    Mat4 invViewProjection = Mat4::Identity();
    bool invViewProjectionValid = false;
};

// Log-once-per-view bookkeeping for singular matrices; a resettable class so tests start clean.
class RenderFeatureCameraDataWarningTracker {
public:
    void Reset();

    // Records `view` as warned and returns true only the first time this is
    // called for that exact view since the last Reset().
    bool ShouldWarnOnce(rg::RenderViewId view);

private:
    std::vector<rg::RenderViewId> m_warnedViews;
};

// Resolves `view`'s eye position and a checked inverse view-projection. Returns an invalid result when
// `viewData` is null; a singular matrix falls back to identity and warns once per view.
RenderFeatureCameraData ResolveRenderFeatureCameraData(rg::RenderViewId view, const RenderPassViewData* viewData);

} // namespace gte
