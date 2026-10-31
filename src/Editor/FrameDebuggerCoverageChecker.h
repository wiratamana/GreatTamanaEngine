#pragma once

#include "FrameDebuggerData.h"
#include "../Renderer/RenderGraph/RenderGraphSnapshot.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace gte {

// Pure coverage detector: a pass that ran must appear as a leaf in the Frame Debugger tree.
// Compares the raw render-graph snapshot with the tree built from the same capture.

// Collects `details->passName` of every draw-call leaf at any depth. Group nodes add nothing.
std::unordered_set<std::string> CollectPassNamesPresentInFrameDebuggerTree(const FrameDebuggerSnapshot& tree);

// Returns the names of passes that ran but have no leaf in `tree`.
// Culled passes, FrameDebuggerInternal passes and secondary-view passes are excluded by design.
std::vector<std::string> DetectPassesMissingFromFrameDebuggerTree(
    const std::vector<rg::RenderGraphPassSnapshot>& passes, const FrameDebuggerSnapshot& tree);

} // namespace gte
