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
            continue; // Did not run this frame.
        }
        if (pass.category == rg::RenderPassCategory::FrameDebuggerInternal) {
            continue; // Debugger-owned scaffolding is never shown.
        }
        if (pass.viewScope == rg::ViewScope::Secondary) {
            continue; // The tree covers the primary view only.
        }
        if (presentNames.find(pass.name) == presentNames.end()) {
            missing.push_back(pass.name);
        }
    }
    return missing;
}

} // namespace gte
