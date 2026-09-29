// Unit tests for DetectRenderPassHonestyMismatches
// (src/Editor/RenderPassHonestyChecker.h) - task_manager/editor-core-separation-21
// campaign, PHASE5 (PHASE5_IRON_RULE_PERMANENT_MISMATCH_DETECTOR.md). Genuinely
// Tier 1: exercises pure logic over hand-fabricated rg::RenderGraphPassSnapshot
// values and a plain lambda lookup - no live ImGui context, Logger sink, or
// RenderGraph/RenderPassToggleRegistry object needed at all (see
// RenderPassHonestyChecker.h's own class comment for why this function is
// deliberately this narrow), mirroring
// tests/Editor/ImGuiIdConflictTrackerTests.cpp's own "pure detector, hand-built
// fixture" precedent.

#include "Editor/RenderPassHonestyChecker.h"

#include <gtest/gtest.h>

#include <unordered_map>

namespace gte {
namespace {

rg::RenderGraphPassSnapshot MakePass(const std::string& name, bool isCulled = false)
{
    rg::RenderGraphPassSnapshot pass;
    pass.name = name;
    pass.isCulled = isCulled;
    return pass;
}

// A trivial std::function-compatible lookup, built from a plain map - passes
// not present in the map default to `true` (mirroring
// RenderPassToggleRegistry::IsEnabled()'s own documented "never seen -> true"
// default) so a test can express "this name was never registered" simply by
// leaving it out.
std::function<bool(const std::string&)> MakeLookup(const std::unordered_map<std::string, bool>& enabledByName)
{
    return [enabledByName](const std::string& name) {
        auto it = enabledByName.find(name);
        return it == enabledByName.end() ? true : it->second;
    };
}

TEST(RenderPassHonestyCheckerTest, ConsistentCaseProducesZeroMismatches)
{
    // Every executed (non-culled) pass reports enabled == true; every
    // disabled pass is genuinely absent from the captured snapshot
    // altogether (the honest, expected shape - RenderGraphPanel.cpp's own
    // documented contract: "a disabled pass leaves ZERO trace in the render
    // graph, since it is never declared into the graph at all").
    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("RenderOpaque"));
    passes.push_back(MakePass("DrawSkyBackground"));

    const std::unordered_map<std::string, bool> enabledByName{
        {"RenderOpaque", true},
        {"DrawSkyBackground", true},
    };

    const std::vector<std::string> mismatches = DetectRenderPassHonestyMismatches(passes, MakeLookup(enabledByName));

    EXPECT_TRUE(mismatches.empty());
}

TEST(RenderPassHonestyCheckerTest, SyntheticContradictionProducesExactlyOneMismatchNamingThatPass)
{
    // One pass is present, non-culled (i.e. it genuinely executed/would be
    // shown as a real leaf in the Frame Debugger's own event tree), but the
    // toggle registry reports it disabled - a genuine contradiction, exactly
    // the reported bug's own shape
    // (AtmosphereAerialPerspectiveCompositePass still executing after being
    // unticked in the "Render Graph" panel).
    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("RenderOpaque"));
    passes.push_back(MakePass("AtmosphereAerialPerspectiveCompositePass"));

    const std::unordered_map<std::string, bool> enabledByName{
        {"RenderOpaque", true},
        {"AtmosphereAerialPerspectiveCompositePass", false},
    };

    const std::vector<std::string> mismatches = DetectRenderPassHonestyMismatches(passes, MakeLookup(enabledByName));

    ASSERT_EQ(mismatches.size(), 1u);
    EXPECT_EQ(mismatches[0], "AtmosphereAerialPerspectiveCompositePass");
}

TEST(RenderPassHonestyCheckerTest, CulledPassIsNeverReportedEvenIfItsToggleEntryReadsFalse)
{
    // A CULLED pass (RenderGraphCompiler::Compile()'s own dead-code
    // elimination) is a real, legitimate, UNRELATED reason a pass never ran
    // this frame - never a "the toggle registry is being ignored"
    // contradiction, regardless of what its own toggle entry reports.
    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("RenderOpaque"));
    passes.push_back(MakePass("SomeCulledPass", /*isCulled=*/true));

    const std::unordered_map<std::string, bool> enabledByName{
        {"RenderOpaque", true},
        {"SomeCulledPass", false},
    };

    const std::vector<std::string> mismatches = DetectRenderPassHonestyMismatches(passes, MakeLookup(enabledByName));

    EXPECT_TRUE(mismatches.empty());
}

TEST(RenderPassHonestyCheckerTest, PassNameNeverSeenByTheRegistryIsNeverReported)
{
    // A pass the toggle registry has never heard of at all defaults to
    // enabled == true (RenderPassToggleRegistry::IsEnabled()'s own
    // documented default) - MakeLookup() above models this exactly by
    // returning true for any name missing from its map.
    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("BrandNewPassNobodyRegisteredYet"));

    const std::unordered_map<std::string, bool> enabledByName; // deliberately empty

    const std::vector<std::string> mismatches = DetectRenderPassHonestyMismatches(passes, MakeLookup(enabledByName));

    EXPECT_TRUE(mismatches.empty());
}

TEST(RenderPassHonestyCheckerTest, MultipleMismatchesAreAllReported)
{
    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("RenderOpaque"));
    passes.push_back(MakePass("DemoRenderFeaturePlugin_Clear"));
    passes.push_back(MakePass("DemoRenderFeatureSecondPlugin_Clear"));

    const std::unordered_map<std::string, bool> enabledByName{
        {"RenderOpaque", true},
        {"DemoRenderFeaturePlugin_Clear", false},
        {"DemoRenderFeatureSecondPlugin_Clear", false},
    };

    const std::vector<std::string> mismatches = DetectRenderPassHonestyMismatches(passes, MakeLookup(enabledByName));

    ASSERT_EQ(mismatches.size(), 2u);
    EXPECT_EQ(mismatches[0], "DemoRenderFeaturePlugin_Clear");
    EXPECT_EQ(mismatches[1], "DemoRenderFeatureSecondPlugin_Clear");
}

TEST(RenderPassHonestyCheckerTest, EmptyLookupReturnsEmptyMismatchListDefensively)
{
    std::vector<rg::RenderGraphPassSnapshot> passes;
    passes.push_back(MakePass("RenderOpaque"));

    const std::function<bool(const std::string&)> emptyLookup;

    const std::vector<std::string> mismatches = DetectRenderPassHonestyMismatches(passes, emptyLookup);

    EXPECT_TRUE(mismatches.empty());
}

} // namespace
} // namespace gte
