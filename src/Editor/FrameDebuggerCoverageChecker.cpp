#include "FrameDebuggerCoverageChecker.h"

namespace gte {

namespace {

void CollectRecursive(const std::vector<FrameDebuggerEventNode>& nodes, std::unordered_set<std::string>& out)
{
    for (const FrameDebuggerEventNode& node : nodes) {
        if (node.isDrawCall && node.details.has_value()) {
            out.insert(node.details->passName);
        }
        CollectRecursive(node.children, out);
    }
}

} // namespace

std::unordered_set<std::string> CollectPassNamesPresentInFrameDebuggerTree(const FrameDebuggerSnapshot& tree)
{
    std::unordered_set<std::string> names;
    CollectRecursive(tree.rootNodes, names);
    return names;
}

std::vector<std::string> DetectPassesMissingFromFrameDebuggerTree(
    const std::vector<rg::RenderGraphPassSnapshot>& passes, const FrameDebuggerSnapshot& tree)
{
    const std::unordered_set<std::string> presentNames = CollectPassNamesPresentInFrameDebuggerTree(tree);

    std::vector<std::string> missing;
    for (const rg::RenderGraphPassSnapshot& pass : passes) {
        if (pass.isCulled) {
            // A real, legitimate, unrelated reason a pass did not run this
            // frame - never a "the tree hid a real survivor" contradiction.
            continue;
        }
        if (pass.category == rg::RenderPassCategory::FrameDebuggerInternal) {
            // Genuinely, permanently Frame-Debugger-OWNED ephemeral
            // scaffolding (e.g. FrameDebuggerReplayStepN) - never a real
            // tree citizen, by design (RenderGraphTypes.h's own doc
            // comment) - not a contradiction.
            continue;
        }
        if (pass.viewScope == rg::ViewScope::SceneView) {
            // The Frame Debugger tree is Game-View-only, by design
            // (BuildRealFrameDebuggerSnapshot()'s own documented exclusion
            // throughout) - not a contradiction.
            continue;
        }
        if (presentNames.find(pass.name) == presentNames.end()) {
            missing.push_back(pass.name);
        }
    }
    return missing;
}

} // namespace gte
