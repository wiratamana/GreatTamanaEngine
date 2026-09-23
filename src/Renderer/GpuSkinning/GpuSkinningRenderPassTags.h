#pragma once

// render-pass-7 campaign (task_manager/render-pass-7), PHASE3 - Core Campaign 1. This
// feature's OWN RenderPassTag bit - see AtmosphereRenderPassTags.h's identical header
// comment for the full "why". Deliberately has NO matching RegisterPassGroupLabel() call
// anywhere (see PHASE0's own Step 2.3) - a GPU Skinning compute dispatch keeps falling into
// the Editor Frame Debugger's generic "Compute Dispatches (Pre-GameView)" fallback bucket,
// exactly like it always has - this tag exists so GPU Skinning is a real, structurally
// identifiable pass group (proving the mechanism generically), without changing one pixel
// of today's actual Frame Debugger tree output.

#include "../RenderGraph/RenderGraphTypes.h"

namespace gte {

inline constexpr rg::RenderPassTag kGpuSkinningDispatchPassTag{ 1ull << 1 };

} // namespace gte
