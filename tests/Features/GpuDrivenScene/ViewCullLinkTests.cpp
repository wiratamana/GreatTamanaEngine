// Enum tables, count pairing and link ownership of the cull view link.

#include "Core/FeatureKit/ViewCullLink.h"

#include <gtest/gtest.h>

#include <memory>

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
        EXPECT_TRUE(fx::EnumKnown(table, entry.value));
    }
    EXPECT_FALSE(fx::ParseEnum(table, "").has_value());
    EXPECT_FALSE(fx::ParseEnum(table, "NoSuchName").has_value());
    EXPECT_FALSE(fx::EnumKnown(table, static_cast<E>(250)));
}

TEST(ViewCullLinkTest, EveryEnumTableRoundTrips)
{
    ExpectRoundTrip(fx::kViewCullChoiceTable);
    ExpectRoundTrip(fx::kViewCullSourceTable);
    ExpectRoundTrip(fx::kGhostStyleTable);
    ExpectRoundTrip(fx::kViewCullModeTable);
    ExpectRoundTrip(fx::kOcclusionStateTable);
    ExpectRoundTrip(fx::kHizReasonTable);
}

TEST(ViewCullLinkTest, UnknownEnumValueGetsTheFirstName)
{
    EXPECT_EQ(fx::EnumName(fx::kViewCullChoiceTable, static_cast<fx::ViewCullChoice>(250)), "FollowSettings");
}

TEST(ViewCullLinkTest, HiddenCountNeverWraps)
{
    EXPECT_EQ(fx::HiddenClusterCount(100, 40), 60u);
    EXPECT_EQ(fx::HiddenClusterCount(100, 100), 0u);
    EXPECT_FALSE(fx::HiddenClusterCount(100, 101).has_value());
    EXPECT_FALSE(fx::HiddenClusterCount(0, 1).has_value());
    EXPECT_EQ(fx::HiddenClusterCount(0, 0), 0u);
}

TEST(ViewCullLinkTest, FreshLinkIsIdle)
{
    const fx::ViewCullLink link;
    EXPECT_FALSE(link.request.pending);
    EXPECT_FALSE(link.request.hudShown);
    EXPECT_FALSE(link.report.valid);
    EXPECT_FALSE(link.report.rendered);
}

TEST(ViewCullLinkTest, EitherSideMayOutliveTheOther)
{
    std::shared_ptr<fx::ViewCullLink> owner = std::make_shared<fx::ViewCullLink>();
    std::shared_ptr<fx::ViewCullLink> editor = owner;
    owner.reset();
    editor->report.valid = true;
    EXPECT_TRUE(editor->report.valid);
    EXPECT_EQ(editor.use_count(), 1);
}

TEST(ViewCullLinkTest, SourceNamesAreLiveAndFrozen)
{
    EXPECT_EQ(fx::EnumName(fx::kViewCullSourceTable, fx::ViewCullSource::Live), "Live");
    EXPECT_EQ(fx::ParseEnum(fx::kViewCullSourceTable, "Frozen"), fx::ViewCullSource::Frozen);
    EXPECT_FALSE(fx::ParseEnum(fx::kViewCullSourceTable, "SceneCamera").has_value());
}

TEST(HizReasonTest, NamesTheFirstFailedGuard)
{
    HizValidity state;
    EXPECT_EQ(HizUnusableReason(state, 6, 100, 50), HizReason::NoHistory);

    state.hasBuilt = true;
    state.lastFrame = 3;
    state.width = 100;
    state.height = 50;
    state.framesSinceValid = 5;
    EXPECT_EQ(HizUnusableReason(state, 6, 100, 50), HizReason::BuildOff);

    state.lastFrame = 5;
    EXPECT_EQ(HizUnusableReason(state, 6, 200, 50), HizReason::Resized);

    state.framesSinceValid = 1;
    EXPECT_EQ(HizUnusableReason(state, 6, 100, 50), HizReason::WarmingUp);

    state.framesSinceValid = kHizValidFramesRequired;
    EXPECT_EQ(HizUnusableReason(state, 6, 100, 50), HizReason::None);
}

} // namespace
} // namespace gte
