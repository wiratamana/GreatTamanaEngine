#pragma once

// Per-view camera data for a PostComposite/PreUI render-feature callback -
// everything a depth-based or camera-aware effect (fog, outlines, custom
// post-processing) needs about the camera that is NOT already covered by
// ScenePassReadHandles' color/depth read handles. Deliberately a separate
// struct/file pair - never folded onto ScenePassReadHandles.
#include "../../Application/RenderPassViewData.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"
#include "../../Renderer/RenderGraph/RenderPipeline.h"

#include <vector>

namespace gte {

struct RenderFeatureCameraData {
    Vec3 eyeWorldPosition;
    Mat4 invViewProjection = Mat4::Identity();
    bool invViewProjectionValid = false;
};

// Pure, Tier-1-testable "already warned" bookkeeping for the singular-matrix
// log-once-per-view behavior below - a linear-scanned std::vector wrapped in
// a resettable class (mirrors ImGuiIdConflictTracker's own shape) rather than
// a bare function-local static, so a test can construct a fresh instance and
// never contaminate another test case's own expectations.
class RenderFeatureCameraDataWarningTracker {
public:
    void Reset();

    // Records `view` as warned and returns true only the first time this is
    // called for that exact view since the last Reset().
    bool ShouldWarnOnce(rg::RenderViewId view);

private:
    std::vector<rg::RenderViewId> m_warnedViews;
};

// Resolves `view`'s current eye position and a checked inverse
// view-projection matrix (Mat4::TryInverse(), never the unchecked
// Mat4::Inverse()). Returns a quiet, default-constructed, invalid result
// when `viewData` is null (no data for this view yet - not an error). On a
// singular source matrix, falls back to Mat4::Identity() and logs a warning
// at most once per distinct `view` for the process lifetime.
RenderFeatureCameraData ResolveRenderFeatureCameraData(rg::RenderViewId view, const RenderPassViewData* viewData);

} // namespace gte
