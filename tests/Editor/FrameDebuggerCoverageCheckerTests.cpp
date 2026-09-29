// Unit tests for DetectPassesMissingFromFrameDebuggerTree()/
// CollectPassNamesPresentInFrameDebuggerTree() (src/Editor/FrameDebuggerCoverageChecker.h)
// - task_manager/editor-core-separation-22 campaign, PHASE6
// (PHASE6_IRON_RULE_V2_BIDIRECTIONAL_DETECTOR.md, Step 3.1). Genuinely
// Tier 1: exercises pure logic over hand-fabricated
// rg::RenderGraphPassSnapshot/FrameDebuggerSnapshot values - no live ImGui
// context, Logger sink, or RenderGraph object needed at all, mirroring
// tests/Editor/RenderPassHonestyCheckerTests.cpp's own "pure detector,
// hand-built fixture" precedent exactly.

#include "Editor/FrameDebuggerCoverageChecker.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

rg::RenderGraphPassSnapshot MakePass(const std::string& name, bool isCulled = false,
    rg::RenderPassCategory category = rg::RenderPassCategory::General,
    rg::ViewScope viewScope = rg::ViewScope::Shared)
{
    rg::RenderGraphPassSnapshot pass;
    pass.name = name;
    pass.isCulled = isCulled;
    pass.category = category;
    pass.viewScope = viewScope;
    return pass;
}

// A real pass-level leaf, exactly like BuildRealFrameDebuggerSnapshot()'s
// own BuildComputeDispatchLeaf()/BuildGraphicsPassLeaf()/BuildRenderOpaqueLeaf()
// always produce: isDrawCall == true, details populated, details->passName
// == the real, raw pass name.
FrameDebuggerEventNode MakeRealPassLeaf(const std::string& passName)
{
    FrameDebuggerEventNode node;
    node.isDrawCall = true;
    node.name = passName;
    FrameDebuggerEventDetails details;
    details.passName = passName;
    node.details = details;
    return node;
}

// A group node (e.g. "Compute Dispatches (Pre-GameView)") - isDrawCall ==
// false, no details, owning children - contributes no pass identity of its
// own, exactly like the tree's own real group nodes.
FrameDebuggerEventNode MakeGroup(const std::string& groupName, std::vector<FrameDebuggerEventNode> children)
{
    FrameDebuggerEventNode node;
    node.isDrawCall = false;
    node.name = groupName;
    node.children = std::move(children);
    return node;
}

FrameDebuggerSnapshot MakeTree(std::vector<FrameDebuggerEventNode> rootChildren)
{
    FrameDebuggerEventNode root;
    root.name = "Game View";
    root.isDrawCall = false;
    root.children = std::move(rootChildren);

    FrameDebuggerSnapshot snapshot;
    snapshot.rootNodes.push_back(std::move(root));
    return snapshot;
}

// (a) - a synthetic snapshot + a synthetic tree that DOES contain every
// survivor -> empty result.
TEST(FrameDebuggerCoverageCheckerTest, EveryNonCulledSurvivorPresentInTreeProducesEmptyResult)
{
    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("RenderOpaque"));
    passes.push_back(MakePass("DrawSkyBackground"));

    const FrameDebuggerSnapshot tree =
        MakeTree({ MakeRealPassLeaf("RenderOpaque"), MakeRealPassLeaf("DrawSkyBackground") });

    const std::vector<std::string> missing = DetectPassesMissingFromFrameDebuggerTree(passes, tree);

    EXPECT_TRUE(missing.empty());
}

// (b) - a synthetic snapshot with one survivor deliberately omitted from
// the tree -> that one name reported. This is the exact regression shape
// PHASE0_MASTER_STRATEGY.md's Root Cause #2 ("DemoRenderFeaturePlugin_Clear
// produces zero visible nodes") describes.
TEST(FrameDebuggerCoverageCheckerTest, OneSurvivorMissingFromTreeIsReported)
{
    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("RenderOpaque"));
    passes.push_back(MakePass("DemoRenderFeaturePlugin_Clear"));

    // The tree only contains "RenderOpaque" - "DemoRenderFeaturePlugin_Clear"
    // genuinely ran (non-culled, present in `passes`) but has no leaf.
    const FrameDebuggerSnapshot tree = MakeTree({ MakeRealPassLeaf("RenderOpaque") });

    const std::vector<std::string> missing = DetectPassesMissingFromFrameDebuggerTree(passes, tree);

    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing[0], "DemoRenderFeaturePlugin_Clear");
}

// (c) - a culled pass omitted from the tree -> NOT reported (an honest,
// unrelated reason: RenderGraphCompiler::Compile()'s own dead-code
// elimination, never a "the tree hid a real survivor" contradiction).
TEST(FrameDebuggerCoverageCheckerTest, CulledPassOmittedFromTreeIsNeverReported)
{
    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("RenderOpaque"));
    passes.push_back(MakePass("SomeCulledPass", /*isCulled=*/true));

    const FrameDebuggerSnapshot tree = MakeTree({ MakeRealPassLeaf("RenderOpaque") });

    const std::vector<std::string> missing = DetectPassesMissingFromFrameDebuggerTree(passes, tree);

    EXPECT_TRUE(missing.empty());
}

// (d) - a ViewScope::SceneView pass omitted -> NOT reported (the Frame
// Debugger tree is Game-View-only, by design - BuildRealFrameDebuggerSnapshot()'s
// own documented, honest SceneView exclusion, not a contradiction).
TEST(FrameDebuggerCoverageCheckerTest, SceneViewPassOmittedFromTreeIsNeverReported)
{
    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("RenderOpaque"));
    passes.push_back(MakePass("AtmosphereAerialPerspectiveCompositePass", /*isCulled=*/false,
        rg::RenderPassCategory::General, rg::ViewScope::SceneView));

    const FrameDebuggerSnapshot tree = MakeTree({ MakeRealPassLeaf("RenderOpaque") });

    const std::vector<std::string> missing = DetectPassesMissingFromFrameDebuggerTree(passes, tree);

    EXPECT_TRUE(missing.empty());
}

// (e) - a RenderPassCategory::FrameDebuggerInternal pass omitted -> NOT
// reported (genuinely, permanently Frame-Debugger-OWNED ephemeral
// scaffolding - e.g. FrameDebuggerReplayStepN - never a real tree citizen,
// by design).
TEST(FrameDebuggerCoverageCheckerTest, FrameDebuggerInternalCategoryPassOmittedFromTreeIsNeverReported)
{
    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("RenderOpaque"));
    passes.push_back(
        MakePass("FrameDebuggerReplayStep0", /*isCulled=*/false, rg::RenderPassCategory::FrameDebuggerInternal));

    const FrameDebuggerSnapshot tree = MakeTree({ MakeRealPassLeaf("RenderOpaque") });

    const std::vector<std::string> missing = DetectPassesMissingFromFrameDebuggerTree(passes, tree);

    EXPECT_TRUE(missing.empty());
}

// Extra coverage: a real pass-level node that ALSO owns a child event row
// (WrapPassWithOwnedChildEvent()'s own real shape - the parent keeps its
// own isDrawCall/details unchanged even once it gains a child) is still
// found correctly, and nested at any depth, not just at the tree's own
// top level.
TEST(FrameDebuggerCoverageCheckerTest, NestedGroupsAtAnyDepthAreStillWalkedCorrectly)
{
    FrameDebuggerEventNode ownedChild;
    ownedChild.isDrawCall = true;
    ownedChild.name = "Compute Dispatch";
    FrameDebuggerEventDetails childDetails;
    childDetails.passName = "AtmosphereTransmittanceLutPass"; // Not a real, separate pass name - harmless noise.
    ownedChild.details = childDetails;

    FrameDebuggerEventNode passLevelNode = MakeRealPassLeaf("AtmosphereTransmittanceLutPass");
    passLevelNode.children.push_back(std::move(ownedChild));

    const FrameDebuggerSnapshot tree = MakeTree({ MakeGroup("Compute LUT", { std::move(passLevelNode) }) });

    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("AtmosphereTransmittanceLutPass"));

    const std::vector<std::string> missing = DetectPassesMissingFromFrameDebuggerTree(passes, tree);

    EXPECT_TRUE(missing.empty());
}

TEST(FrameDebuggerCoverageCheckerTest, MultipleMissingSurvivorsAreAllReported)
{
    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("RenderOpaque"));
    passes.push_back(MakePass("DemoRenderFeaturePlugin_Clear"));
    passes.push_back(MakePass("DemoRenderFeatureSecondPlugin_Clear"));

    const FrameDebuggerSnapshot tree = MakeTree({ MakeRealPassLeaf("RenderOpaque") });

    const std::vector<std::string> missing = DetectPassesMissingFromFrameDebuggerTree(passes, tree);

    ASSERT_EQ(missing.size(), 2u);
    EXPECT_EQ(missing[0], "DemoRenderFeaturePlugin_Clear");
    EXPECT_EQ(missing[1], "DemoRenderFeatureSecondPlugin_Clear");
}

TEST(FrameDebuggerCoverageCheckerTest, EmptyTreeAndEmptyPassesProducesEmptyResult)
{
    const FrameDebuggerSnapshot tree = MakeTree({});
    const std::vector<rg::RenderGraphPassSnapshot> passes;

    const std::vector<std::string> missing = DetectPassesMissingFromFrameDebuggerTree(passes, tree);

    EXPECT_TRUE(missing.empty());
}

} // namespace
} // namespace gte
