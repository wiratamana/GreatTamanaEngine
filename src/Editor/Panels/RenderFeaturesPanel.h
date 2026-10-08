#pragma once

#include "../../Core/Plugins/RenderFeatureDebugEntry.h"
#include "../../Renderer/Culling/GpuDrivenBatchDebugInfo.h"

#include <vector>

namespace gte {

struct EditorContext;
class RenderFeatureCompositor;

// Live render-feature authoring + GPU-driven-batch culling readout - split
// out of the Render Graph panel (that panel's one job is inspecting a
// selected pass; authoring a render feature's priority/enabled state is a
// different, unrelated tool that used to share its widget by accident).
//
// A floating window, toggled via EditorContext::renderFeaturesWindowOpen.
class RenderFeaturesPanel {
public:
    void Build(EditorContext& ctx, const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
        const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries, RenderFeatureCompositor* renderFeatureCompositor);
};

} // namespace gte
