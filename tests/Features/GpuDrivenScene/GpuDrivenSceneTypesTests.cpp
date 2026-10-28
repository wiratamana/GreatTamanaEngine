// Settings, cull plan, camera choice, count history and debug keys of the GpuDrivenScene feature.

#include "Features/GpuDrivenScene/GpuDrivenSceneTypes.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

namespace gte {
namespace {

template <class E, std::size_t N>
void ExpectRoundTrip(const std::array<fx::EnumEntry<E>, N>& table)
{
    for (const fx::EnumEntry<E>& entry : table) {
        EXPECT_EQ(fx::EnumName(table, entry.value), entry.name);
        const std::optional<E> parsed = fx::ParseEnum(table, entry.name);
        ASSERT_TRUE(parsed.has_value()) << entry.name;
        EXPECT_EQ(*parsed, entry.value);
    }
}

TEST(GpuDrivenSceneTypesTest, EveryEnumTableRoundTrips)
{
    ExpectRoundTrip(kCullModeTable);
    ExpectRoundTrip(kOcclusionStateTable);
    ExpectRoundTrip(kHizReasonTable);
    ExpectRoundTrip(kCullCameraSourceTable);
    ExpectRoundTrip(kCpuCullStateTable);
    ExpectRoundTrip(kGpuDrivenSceneDebugViewTable);
}

TEST(GpuDrivenSceneTypesTest, CullModeNamesAreExact)
{
    EXPECT_EQ(fx::EnumName(kCullModeTable, CullMode::NoCull), "NoCull");
    EXPECT_EQ(fx::EnumName(kCullModeTable, CullMode::Frustum), "Frustum");
    EXPECT_EQ(fx::EnumName(kCullModeTable, CullMode::HiZ), "HiZ");
    EXPECT_EQ(kCullModeTable.size(), 3u);
}

TEST(GpuDrivenSceneTypesTest, DefaultsCullWithTheFrustum)
{
    const GpuDrivenSceneSettings s;
    EXPECT_TRUE(s.enabled);
    EXPECT_EQ(s.mode, CullMode::Frustum);
    EXPECT_FALSE(s.cone);
    EXPECT_FALSE(s.collectStats);
    EXPECT_EQ(s.debugView, GpuDrivenSceneDebugView::Off);
}

TEST(GpuDrivenSceneTypesTest, SanitizeResetsOutOfRangeEnums)
{
    GpuDrivenSceneSettings s;
    s.debugView = static_cast<GpuDrivenSceneDebugView>(200);
    s.mode = static_cast<CullMode>(200);
    const GpuDrivenSceneSettings clean = SanitizeGpuDrivenSceneSettings(s);
    EXPECT_EQ(clean.debugView, GpuDrivenSceneDebugView::Off);
    EXPECT_EQ(clean.mode, CullMode::Frustum);

    s = GpuDrivenSceneSettings{};
    s.debugView = GpuDrivenSceneDebugView::ClusterId;
    s.mode = CullMode::HiZ;
    EXPECT_EQ(SanitizeGpuDrivenSceneSettings(s).debugView, GpuDrivenSceneDebugView::ClusterId);
    EXPECT_EQ(SanitizeGpuDrivenSceneSettings(s).mode, CullMode::HiZ);
}

TEST(GpuDrivenSceneTypesTest, ResolveCullTestsPerModeAndCone)
{
    for (const bool cone : { false, true }) {
        GpuDrivenSceneSettings s;
        s.cone = cone;

        s.mode = CullMode::NoCull;
        ViewCullTests t = ResolveCullTests(s);
        EXPECT_FALSE(t.frustum);
        EXPECT_FALSE(t.cone);
        EXPECT_FALSE(t.occlusion);

        s.mode = CullMode::Frustum;
        t = ResolveCullTests(s);
        EXPECT_TRUE(t.frustum);
        EXPECT_EQ(t.cone, cone);
        EXPECT_FALSE(t.occlusion);

        s.mode = CullMode::HiZ;
        t = ResolveCullTests(s);
        EXPECT_TRUE(t.frustum);
        EXPECT_EQ(t.cone, cone);
        EXPECT_TRUE(t.occlusion);
    }
}

TEST(GpuDrivenSceneTypesTest, OnlyThePrimaryViewWantsHiz)
{
    GpuDrivenSceneSettings s;
    s.mode = CullMode::HiZ;
    EXPECT_TRUE(ViewWantsHiz(s, false));
    EXPECT_FALSE(ViewWantsHiz(s, true));

    s.enabled = false;
    EXPECT_FALSE(ViewWantsHiz(s, false));

    s.enabled = true;
    s.mode = CullMode::Frustum;
    EXPECT_FALSE(ViewWantsHiz(s, false));
    s.mode = CullMode::NoCull;
    EXPECT_FALSE(ViewWantsHiz(s, false));
}

TEST(GpuDrivenSceneTypesTest, ChooseCullCameraTable)
{
    for (const CullMode mode : { CullMode::NoCull, CullMode::Frustum, CullMode::HiZ }) {
        for (const bool secondary : { false, true }) {
            for (const bool primaryValid : { false, true }) {
                for (const bool liveValid : { false, true }) {
                    const CullCameraSource source = ChooseCullCamera(secondary, mode, primaryValid, liveValid);
                    if (mode == CullMode::NoCull) {
                        EXPECT_EQ(source, CullCameraSource::None);
                    } else if (secondary) {
                        EXPECT_EQ(source, primaryValid ? CullCameraSource::Primary : CullCameraSource::None);
                    } else {
                        EXPECT_EQ(source, liveValid ? CullCameraSource::Own : CullCameraSource::None);
                    }
                    if (secondary) {
                        EXPECT_NE(source, CullCameraSource::Own);
                    }
                }
            }
        }
    }
}

TEST(GpuDrivenSceneTypesTest, OcclusionStateFollowsRequestCameraAndResult)
{
    EXPECT_EQ(OcclusionStateOf(false, true, false), OcclusionState::NotRequested);
    EXPECT_EQ(OcclusionStateOf(false, false, false), OcclusionState::NotRequested);
    EXPECT_EQ(OcclusionStateOf(true, false, true), OcclusionState::NoCullCamera);
    EXPECT_EQ(OcclusionStateOf(true, true, false), OcclusionState::Unusable);
    EXPECT_EQ(OcclusionStateOf(true, true, true), OcclusionState::Applied);
}

TEST(GpuDrivenSceneTypesTest, HiddenCountNeverWraps)
{
    EXPECT_EQ(HiddenClusterCount(100, 40), 60u);
    EXPECT_EQ(HiddenClusterCount(100, 100), 0u);
    EXPECT_FALSE(HiddenClusterCount(100, 101).has_value());
    EXPECT_FALSE(HiddenClusterCount(0, 1).has_value());
    EXPECT_EQ(HiddenClusterCount(0, 0), 0u);
}

TEST(GpuDrivenSceneTypesTest, SettingKeysApplyOrReject)
{
    GpuDrivenSceneSettings s;
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "mode", "HiZ"), GpuDrivenSceneSetResult::Applied);
    EXPECT_EQ(s.mode, CullMode::HiZ);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "mode", "NoCull"), GpuDrivenSceneSetResult::Applied);
    EXPECT_EQ(s.mode, CullMode::NoCull);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "cone", "true"), GpuDrivenSceneSetResult::Applied);
    EXPECT_TRUE(s.cone);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "collectStats", "1"), GpuDrivenSceneSetResult::Applied);
    EXPECT_TRUE(s.collectStats);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "enabled", "false"), GpuDrivenSceneSetResult::Applied);
    EXPECT_FALSE(s.enabled);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "debugView", "ClusterId"), GpuDrivenSceneSetResult::Applied);
    EXPECT_EQ(s.debugView, GpuDrivenSceneDebugView::ClusterId);

    const GpuDrivenSceneSettings before = s;
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "mode", "Bogus"), GpuDrivenSceneSetResult::BadValue);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "mode", "frustum"), GpuDrivenSceneSetResult::BadValue);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "cone", "maybe"), GpuDrivenSceneSetResult::BadValue);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "debugView", "Nope"), GpuDrivenSceneSetResult::BadValue);
    EXPECT_EQ(s.mode, before.mode);
    EXPECT_EQ(s.cone, before.cone);
    EXPECT_EQ(s.debugView, before.debugView);
}

TEST(GpuDrivenSceneTypesTest, RemovedKeysAreUnknown)
{
    GpuDrivenSceneSettings s;
    for (const std::string_view key : { "frustum", "occlusion", "freezeCulling", "sceneCull", "sceneCone", "sceneSource",
             "freezeSerial", "ghost", "bogus" }) {
        EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, key, "true"), GpuDrivenSceneSetResult::UnknownKey) << key;
    }
}

TEST(GpuDrivenSceneTypesTest, WritableKeyListIsPinned)
{
    constexpr std::array<std::string_view, 5> expected{ "enabled", "mode", "cone", "collectStats", "debugView" };
    ASSERT_EQ(kGpuDrivenSceneSettingKeys.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(kGpuDrivenSceneSettingKeys[i].name, expected[i]);
    }
    const GpuDrivenSceneSettingKey* mode = FindGpuDrivenSceneSettingKey("mode");
    ASSERT_NE(mode, nullptr);
    EXPECT_EQ(mode->allowed, "NoCull, Frustum or HiZ");
}

TEST(GpuDrivenSceneTypesTest, EveryKeyHasAnErrorText)
{
    for (const GpuDrivenSceneSettingKey& key : kGpuDrivenSceneSettingKeys) {
        EXPECT_FALSE(key.name.empty());
        EXPECT_FALSE(key.allowed.empty()) << key.name;
        GpuDrivenSceneSettings s;
        EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, key.name, "###"), GpuDrivenSceneSetResult::BadValue) << key.name;
    }
}

TEST(GpuDrivenSceneTypesTest, TestedHistoryPairsAFrameWithItsOwnValue)
{
    TestedHistory history;
    EXPECT_FALSE(history.At(0).has_value());
    for (std::uint64_t frame = 10; frame < 14; ++frame) {
        history.Record(frame, frame * 100);
    }
    for (std::uint64_t frame = 10; frame < 14; ++frame) {
        ASSERT_TRUE(history.At(frame).has_value());
        EXPECT_EQ(*history.At(frame), frame * 100);
    }
    history.Record(14, 1400);
    EXPECT_FALSE(history.At(10).has_value()); // The oldest entry was overwritten.
    EXPECT_FALSE(history.At(18).has_value()); // Same slot, other frame.
    EXPECT_EQ(*history.At(14), 1400u);
}

TEST(GpuDrivenSceneTypesTest, ClusterCountAgeIsFramesSinceReadbackAndSaturates)
{
    EXPECT_EQ(ClusterCountAgeFrames(100, 97), 3u);
    EXPECT_EQ(ClusterCountAgeFrames(100, 100), 0u);
    EXPECT_EQ(ClusterCountAgeFrames(100, 101), 0u); // Count from a later frame.
    EXPECT_EQ(ClusterCountAgeFrames(100, 0), 0u);   // No count yet.
    EXPECT_EQ(ClusterCountAgeFrames(0, 0), 0u);
}

TEST(GpuDrivenSceneTypesTest, OutcomeTextNamesInertReasonAndRefusal)
{
    fx::ClusterStats stats;
    EXPECT_TRUE(DescribeClusterOutcome(0, stats).empty());
    EXPECT_FALSE(DescribeClusterOutcome(3, stats).empty());
    EXPECT_TRUE(ClusterOutcomeRefused(3, stats));

    stats.declared = true;
    EXPECT_TRUE(DescribeClusterOutcome(3, stats).empty());
    EXPECT_FALSE(ClusterOutcomeRefused(3, stats));

    stats.inertReason = fx::ClusterInertReason::CaptureArmed;
    EXPECT_EQ(DescribeClusterOutcome(3, stats), "capture armed");
}

} // namespace
} // namespace gte
