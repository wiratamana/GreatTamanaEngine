// Tier-1 tests for the side-channel detector and its curated key table (pure logic, no GPU).

#include "Core/FeatureKit/ChannelKey.h"
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
    // A disabled toggle whose key was still published is reported.
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

// The curated table keeps every documented entry.
TEST(FrameDebuggerSideChannelCheckerTest, RealKnownRiskBlackboardKeyRulesContainsEveryDocumentedEntry)
{
    const std::vector<KnownRiskBlackboardKeyRule>& rules = KnownRiskBlackboardKeyRules();

    std::unordered_set<std::string> gatingToggleNames;
    for (const KnownRiskBlackboardKeyRule& rule : rules) {
        gatingToggleNames.insert(rule.gatingToggleName);
    }

    EXPECT_NE(gatingToggleNames.find("DrawSkyBackground"), gatingToggleNames.end());
    EXPECT_NE(gatingToggleNames.find("GpuSkinning"), gatingToggleNames.end());
    EXPECT_GE(rules.size(), 3u);
}

bool HasRuleWithKey(const std::vector<KnownRiskBlackboardKeyRule>& rules, std::uint64_t key, const char* toggle)
{
    for (const KnownRiskBlackboardKeyRule& rule : rules) {
        if (rule.key.hash == key && rule.gatingToggleName == toggle) {
            return true;
        }
    }
    return false;
}

// The channel rules use the same key function the kit publishes with.
TEST(FrameDebuggerSideChannelCheckerTest, ChannelRulesUseTheKitsOwnKeyFunction)
{
    const std::vector<KnownRiskBlackboardKeyRule>& rules = KnownRiskBlackboardKeyRules();
    for (const char* view : { "Game", "Scene" }) {
        const rg::RenderViewId id = rg::RenderViewId::Named(view);
        EXPECT_TRUE(HasRuleWithKey(rules, fx::ChannelKey("Shadow", "Map", id), "Shadow.Cascade0.Draw")) << view;
        EXPECT_TRUE(HasRuleWithKey(rules, fx::ChannelKey("Shadow", "Receiver", id), "Shadow.Cascade0.Draw")) << view;
        EXPECT_TRUE(HasRuleWithKey(rules, fx::ChannelKey("Shadow", "Mask", id), "Shadow.Mask.Dispatch")) << view;
        EXPECT_TRUE(HasRuleWithKey(
            rules, fx::ChannelKey("VolumetricLight", "Result", id), "VolumetricLight.Inject.Dispatch")) << view;
        EXPECT_TRUE(HasRuleWithKey(
            rules, fx::ChannelKey("VolumetricLight", "Result", id), "VolumetricLight.Integrate.Dispatch")) << view;
        EXPECT_TRUE(HasRuleWithKey(
            rules, fx::ChannelKey("Atmosphere", "AerialVolume", id), "AtmosphereAerialPerspectiveVolumePass")) << view;
    }
}

TEST(FrameDebuggerSideChannelCheckerTest, APublishedChannelWithItsToggleOffIsReported)
{
    const std::vector<KnownRiskBlackboardKeyRule>& rules = KnownRiskBlackboardKeyRules();
    const rg::RenderPassId published{ fx::ChannelKey("Shadow", "Map", rg::RenderViewId::Named("Game")) };

    const std::vector<std::string> leaks = DetectDisabledPassBlackboardKeyLeaks(
        rules, [](const std::string& toggle) { return toggle != "Shadow.Cascade0.Draw"; },
        [published](rg::RenderPassId key) { return key == published; });

    ASSERT_EQ(leaks.size(), 1u);
    EXPECT_EQ(leaks[0], "Shadow.Map (Game)");
}

} // namespace
} // namespace gte
