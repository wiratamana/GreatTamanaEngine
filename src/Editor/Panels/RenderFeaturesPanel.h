#pragma once

#include "../../Core/Plugins/RenderFeatureDebugEntry.h"

#include <vector>

namespace gte {

struct EditorContext;
class RenderFeatureCompositor;

// Live render-feature authoring window (priority and enabled state).
// Toggled via EditorContext::renderFeaturesWindowOpen.
class RenderFeaturesPanel {
public:
    void Build(EditorContext& ctx, const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
        RenderFeatureCompositor* renderFeatureCompositor);
};

} // namespace gte
