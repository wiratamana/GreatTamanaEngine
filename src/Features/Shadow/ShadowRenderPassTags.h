#pragma once

// This feature's own RenderPassTag bit - lives here, in the Shadow
// feature's own header, never in a Core RenderGraph file. Every Shadow
// pass stamps this tag via AddRenderPass()'s trailing `tags` argument, and
// ShadowFeature's own constructor gives this bit its "Shadow" Render Graph
// panel grouping heading.

#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

namespace gte {

inline constexpr rg::RenderPassTag kShadowPassTag{ 1ull << 2 };

} // namespace gte
