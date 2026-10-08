#pragma once

// Render Graph panel grouping tag for RenderFeatureCompositor's own blend-
// chain compute dispatches. Registered once, in
// RenderFeatureCompositor's own constructor.

#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

namespace gte {

inline constexpr rg::RenderPassTag kPostProcessingPassTag{ 1ull << 4 };

} // namespace gte
