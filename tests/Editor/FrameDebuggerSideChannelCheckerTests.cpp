// Unit tests for DetectDisabledPassBlackboardKeyLeaks()/KnownRiskBlackboardKeyRules()
// (src/Editor/FrameDebuggerSideChannelChecker.h) - task_manager/editor-core-separation-22
// campaign, PHASE6 (PHASE6_IRON_RULE_V2_BIDIRECTIONAL_DETECTOR.md, Step 3.3,
// Clause C). Genuinely Tier 1: exercises pure logic over hand-fabricated
// rules and plain lambda lookups - no live RenderPassToggleRegistry/
// RenderPassBlackboard object needed at all, mirroring
// tests/Editor/RenderPassHonestyCheckerTests.cpp's own "pure detector,
// hand-built fixture" precedent exactly.

#include "Editor/FrameDebuggerSideChannelChecker.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace gte {
namespace {

using rg::operator""_passId;

std::function<bool(const std::string&)> MakeEnabledLookup(const std::unordered_map<std::string, bool>& enabledByName)
{
    return [enabledByName](const std::string& name) {
        auto it = enabledByName.find(name);
        return it == enabledByName.end() ? true : it->second;
    };
}

std::function<bool(rg::RenderPassId)> MakePublishedLookup(const std::unordered_set<std::uint64_t>& publishedHashes)
{
    return [publishedHashes](rg::RenderPassId key) { return publishedHashes.count(key.hash) != 0; };
}

TEST(FrameDebuggerSideChannelCheckerTest, EnabledToggleNeverReportsEvenIfPublished)
{
    // The gating toggle is enabled - publishing this key this frame is
    // expected/honest, regardless of whether it actually was.
    const std::vector<KnownRiskBlackboardKeyRule> rules{
        { "Test.Key"_passId, "Test.Key", "SomePass" },
    };

    const std::unordered_map<std::string, bool> enabledByName{ { "SomePass", true } };
    const std::unordered_set<std::uint64_t> published{ ("Test.Key"_passId).hash };

    const std::vector<std::string> leaks =
        DetectDisabledPassBlackboardKeyLeaks(rules, MakeEnabledLookup(enabledByName), MakePublishedLookup(published));

    EXPECT_TRUE(leaks.empty());
}

TEST(FrameDebuggerSideChannelCheckerTest, DisabledToggleWithKeyPublishedIsReported)
{
    // The exact side-channel-leak shape PHASE1/PHASE3 of this campaign
    // fixed: the gating toggle reports DISABLED, yet the key was still
    // published this frame.
    const std::vector<KnownRiskBlackboardKeyRule> rules{
        { "Atmosphere.GameSkyBackgroundCallback"_passId, "Atmosphere.GameSkyBackgroundCallback", "DrawSkyBackground" },
    };

    const std::unordered_map<std::string, bool> enabledByName{ { "DrawSkyBackground", false } };
    const std::unordered_set<std::uint64_t> published{ ("Atmosphere.GameSkyBackgroundCallback"_passId).hash };

    const std::vector<std::string> leaks =
        DetectDisabledPassBlackboardKeyLeaks(rules, MakeEnabledLookup(enabledByName), MakePublishedLookup(published));

    ASSERT_EQ(leaks.size(), 1u);
    EXPECT_EQ(leaks[0], "Atmosphere.GameSkyBackgroundCallback");
}

TEST(FrameDebuggerSideChannelCheckerTest, DisabledToggleWithKeyNotPublishedIsNotReported)
{
    // The honest, fixed shape: the toggle is disabled AND the key was
    // correctly never published this frame either.
    const std::vector<KnownRiskBlackboardKeyRule> rules{
        { "Test.Key"_passId, "Test.Key", "SomePass" },
    };

    const std::unordered_map<std::string, bool> enabledByName{ { "SomePass", false } };
    const std::unordered_set<std::uint64_t> published; // deliberately empty

    const std::vector<std::string> leaks =
        DetectDisabledPassBlackboardKeyLeaks(rules, MakeEnabledLookup(enabledByName), MakePublishedLookup(published));

    EXPECT_TRUE(leaks.empty());
}

TEST(FrameDebuggerSideChannelCheckerTest, MultipleRulesOnlyGenuinelyLeakingOnesAreReported)
{
    const std::vector<KnownRiskBlackboardKeyRule> rules{
        { "Key.A"_passId, "Key.A", "PassA" }, // leaking
        { "Key.B"_passId, "Key.B", "PassB" }, // honest: toggle enabled
        { "Key.C"_passId, "Key.C", "PassC" }, // honest: toggle disabled, never published
    };

    const std::unordered_map<std::string, bool> enabledByName{
        { "PassA", false },
        { "PassB", true },
        { "PassC", false },
    };
    const std::unordered_set<std::uint64_t> published{ ("Key.A"_passId).hash, ("Key.B"_passId).hash };

    const std::vector<std::string> leaks =
        DetectDisabledPassBlackboardKeyLeaks(rules, MakeEnabledLookup(enabledByName), MakePublishedLookup(published));

    ASSERT_EQ(leaks.size(), 1u);
    EXPECT_EQ(leaks[0], "Key.A");
}

TEST(FrameDebuggerSideChannelCheckerTest, EmptyCallablesReturnEmptyDefensively)
{
    const std::vector<KnownRiskBlackboardKeyRule> rules{
        { "Test.Key"_passId, "Test.Key", "SomePass" },
    };

    const std::function<bool(const std::string&)> emptyEnabledLookup;
    const std::function<bool(rg::RenderPassId)> emptyPublishedLookup;

    EXPECT_TRUE(DetectDisabledPassBlackboardKeyLeaks(rules, emptyEnabledLookup, MakePublishedLookup({})).empty());
    EXPECT_TRUE(
        DetectDisabledPassBlackboardKeyLeaks(rules, MakeEnabledLookup({}), emptyPublishedLookup).empty());
}

// Sanity check on the REAL, curated allowlist itself (Step 3.3 item 1) -
// confirms every entry PHASE6_COMPLETION_REPORT.md documents is actually
// present, so a future accidental deletion of an entry is caught here
// instead of silently shrinking the detector's own coverage.
TEST(FrameDebuggerSideChannelCheckerTest, RealKnownRiskBlackboardKeyRulesContainsEveryDocumentedEntry)
{
    const std::vector<KnownRiskBlackboardKeyRule>& rules = KnownRiskBlackboardKeyRules();

    std::unordered_set<std::string> gatingToggleNames;
    for (const KnownRiskBlackboardKeyRule& rule : rules) {
        gatingToggleNames.insert(rule.gatingToggleName);
    }

    EXPECT_NE(gatingToggleNames.find("DrawSkyBackground"), gatingToggleNames.end());
    EXPECT_NE(gatingToggleNames.find("GpuSkinning"), gatingToggleNames.end());
    EXPECT_NE(gatingToggleNames.find("AtmosphereComposite"), gatingToggleNames.end());
    EXPECT_GE(rules.size(), 4u); // Sky callback + GPU Skinning + Game/Scene composited output.
}

} // namespace
} // namespace gte
