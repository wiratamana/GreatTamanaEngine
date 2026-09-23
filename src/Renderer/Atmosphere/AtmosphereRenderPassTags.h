#pragma once

// render-pass-7 campaign (task_manager/render-pass-7), PHASE3 - Core Campaign 1
// ("De-hardcode RenderPassCategory"). This feature's OWN RenderPassTag bit - lives here,
// in the Atmosphere feature's own header, NEVER in a Core RenderGraph file (see
// RenderPassTag's own doc comment, RenderGraphTypes.h, for the rule this file exists to
// follow). Every AtmosphereLutRenderer pass that should be grouped under the Editor Frame
// Debugger's "Compute LUT" heading stamps this tag via AddRenderPass()'s trailing `tags`
// argument - see AtmosphereLutRenderer.cpp's own AddXxxLutPass() methods, and this same
// feature's own RegisterPassGroupLabel() call (AtmosphereLutRenderer's constructor) that
// actually gives this bit its "Compute LUT" heading.

#include "../RenderGraph/RenderGraphTypes.h"

namespace gte {

inline constexpr rg::RenderPassTag kAtmosphereLutPassTag{ 1ull << 0 };

} // namespace gte
