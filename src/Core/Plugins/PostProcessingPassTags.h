#pragma once

// Render Graph panel grouping tags for RenderFeatureCompositor's own blend-
// chain compute dispatches. Registered once, in RenderFeatureCompositor's
// own constructor.

#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

namespace gte {

// Built-in (engine-authored) compositor blend passes.
inline constexpr rg::RenderPassTag kPostProcessingPassTag{ 1ull << 4 };

// Project Assembly-authored render feature blend passes (entry.projectCallback
// set) - kept distinct from kPostProcessingPassTag so the Render Graph panel
// groups built-in post-processing separately from project feature output.
inline constexpr rg::RenderPassTag kProjectFeatureGroupTag{ 1ull << 5 };

} // namespace gte
