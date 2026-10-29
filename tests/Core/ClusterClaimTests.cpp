// Tier-1 tests for the pure claim and cull rules in Core/ClusterClaim.h and Core/CullRequestRules.h. No device involved.

#include "Core/ClusterClaim.h"
#include "Core/CullRequestRules.h"

#include <gtest/gtest.h>

#include <array>
#include <limits>

namespace gte {
namespace {

Entity MakeEntity(std::uint32_t index)
{
    Entity e;
    e.index = index;
    e.generation = 1;
    return e;
}

const std::array<Entity, 2> kPartA = { MakeEntity(1), MakeEntity(2) };
const std::array<Entity, 1> kPartB = { MakeEntity(3) };

ClusterPartClaim CompletePart(std::span<const Entity> entities)
{
    ClusterPartClaim part;
    part.entities = entities;
    part.progress = ClusterChainProgress::Draw;
    return part;
}

ClusterClaimContext GameContext()
{
    ClusterClaimContext context;
    context.scope = rg::ViewScope::Primary;
    return context;
}

// Spans of the claim flattened, so tests compare entities in part order.
std::vector<Entity> Claimed(std::span<const ClusterPartClaim> parts, const ClusterClaimContext& context)
{
    std::vector<Entity> flat;
    for (const std::span<const Entity> span : ClaimedSpansOf(parts, context)) {
        flat.insert(flat.end(), span.begin(), span.end());
    }
    return flat;
}

TEST(ClusterClaimTest, ACompleteChainClaimsEveryEntityOfEveryPart)
{
    const std::array<ClusterPartClaim, 2> parts = { CompletePart(kPartA), CompletePart(kPartB) };
    const std::vector<Entity> claimed = Claimed(parts, GameContext());
    ASSERT_EQ(claimed.size(), 3u);
    EXPECT_EQ(claimed[0], MakeEntity(1));
    EXPECT_EQ(claimed[2], MakeEntity(3));
}

TEST(ClusterClaimTest, TheClaimPointsIntoThePartStorageWithoutCopying)
{
    const std::array<ClusterPartClaim, 2> parts = { CompletePart(kPartA), CompletePart(kPartB) };
    const std::vector<std::span<const Entity>> spans = ClaimedSpansOf(parts, GameContext());
    ASSERT_EQ(spans.size(), 2u);
    EXPECT_EQ(spans[0].data(), kPartA.data());
    EXPECT_EQ(spans[1].data(), kPartB.data());
}

TEST(ClusterClaimTest, AChainThatStoppedBeforeTheDrawClaimsNothing)
{
    for (const ClusterChainProgress progress :
        { ClusterChainProgress::None, ClusterChainProgress::Reset, ClusterChainProgress::Cull }) {
        ClusterPartClaim part = CompletePart(kPartA);
        part.progress = progress;
        const std::array<ClusterPartClaim, 2> parts = { part, CompletePart(kPartB) };
        const std::vector<Entity> claimed = Claimed(parts, GameContext());
        ASSERT_EQ(claimed.size(), 1u);
        EXPECT_EQ(claimed[0], MakeEntity(3));
    }
}

TEST(ClusterClaimTest, APartOutsideTheViewIsClaimedWithoutAnyPass)
{
    ClusterPartClaim culled = CompletePart(kPartA);
    culled.progress = ClusterChainProgress::Culled;
    const std::array<ClusterPartClaim, 2> parts = { culled, CompletePart(kPartB) };
    EXPECT_TRUE(ClusterPartClaimable(culled));
    EXPECT_EQ(Claimed(parts, GameContext()).size(), 3u);

    ClusterClaimContext shared;
    shared.scope = rg::ViewScope::Shared;
    EXPECT_TRUE(Claimed(parts, shared).empty());
    ClusterClaimContext captured = GameContext();
    captured.captureArmed = true;
    EXPECT_TRUE(Claimed(parts, captured).empty());
}

TEST(ClusterClaimTest, ACapturedGameViewFrameClaimsNothing)
{
    ClusterClaimContext context = GameContext();
    context.captureArmed = true;
    const std::array<ClusterPartClaim, 1> parts = { CompletePart(kPartA) };
    EXPECT_TRUE(Claimed(parts, context).empty());
}

TEST(ClusterClaimTest, ACaptureOfTheGameViewDoesNotBlockTheSceneView)
{
    ClusterClaimContext context;
    context.scope = rg::ViewScope::Secondary;
    context.captureArmed = true;
    const std::array<ClusterPartClaim, 1> parts = { CompletePart(kPartA) };
    EXPECT_EQ(Claimed(parts, context).size(), 2u);
}

TEST(ClusterClaimTest, TheSharedScopeNeverClaims)
{
    ClusterClaimContext context;
    context.scope = rg::ViewScope::Shared;
    const std::array<ClusterPartClaim, 1> parts = { CompletePart(kPartA) };
    EXPECT_TRUE(Claimed(parts, context).empty());
}

TEST(ClusterClaimTest, AClaimIsAPureFunctionOfItsScope)
{
    const std::array<ClusterPartClaim, 1> gameParts = { CompletePart(kPartA) };
    const std::array<ClusterPartClaim, 1> sceneParts = { CompletePart(kPartB) };
    ClusterClaimContext sceneContext;
    sceneContext.scope = rg::ViewScope::Secondary;

    const std::vector<Entity> game = Claimed(gameParts, GameContext());
    const std::vector<Entity> scene = Claimed(sceneParts, sceneContext);
    ASSERT_EQ(game.size(), 2u);
    ASSERT_EQ(scene.size(), 1u);
    for (const Entity& sceneEntity : scene) {
        for (const Entity& gameEntity : game) {
            EXPECT_FALSE(sceneEntity == gameEntity);
        }
    }
}

TEST(ClusterClaimTest, NoPartsClaimNothing)
{
    EXPECT_TRUE(Claimed({}, GameContext()).empty());
}

TEST(SceneCullAllowedTest, MatchesTheClaimRuleForEveryScopeAndCaptureState)
{
    for (const rg::ViewScope scope : { rg::ViewScope::Shared, rg::ViewScope::Primary, rg::ViewScope::Secondary }) {
        for (const bool captureArmed : { false, true }) {
            const ClusterClaimContext context{ scope, captureArmed };
            EXPECT_EQ(SceneCullAllowed(context), ClusterClaimAllowed(context));
        }
    }
}

TEST(SceneCullAllowedTest, OnlyAPerViewScopeOutsideAPrimaryCaptureMayCull)
{
    EXPECT_FALSE(SceneCullAllowed({ rg::ViewScope::Shared, false }));
    EXPECT_FALSE(SceneCullAllowed({ rg::ViewScope::Shared, true }));
    EXPECT_TRUE(SceneCullAllowed({ rg::ViewScope::Primary, false }));
    EXPECT_FALSE(SceneCullAllowed({ rg::ViewScope::Primary, true }));
    EXPECT_TRUE(SceneCullAllowed({ rg::ViewScope::Secondary, false }));
    EXPECT_TRUE(SceneCullAllowed({ rg::ViewScope::Secondary, true }));
}

TEST(SceneCullCountsFreshTest, OnlyThePreviousAndTheCurrentFrameCount)
{
    EXPECT_TRUE(SceneCullCountsFresh(9, 10));
    EXPECT_TRUE(SceneCullCountsFresh(10, 10));
    EXPECT_FALSE(SceneCullCountsFresh(8, 10));
    EXPECT_FALSE(SceneCullCountsFresh(11, 10));
    EXPECT_TRUE(SceneCullCountsFresh(0, 0));
    EXPECT_FALSE(SceneCullCountsFresh(std::numeric_limits<std::uint64_t>::max(), 0));
}

TEST(SanitizeCullPaddingTest, ClampsToUnitRangeAndRejectsNonFinite)
{
    EXPECT_EQ(SanitizeCullPadding(0.05f), 0.05f);
    EXPECT_EQ(SanitizeCullPadding(0.0f), 0.0f);
    EXPECT_EQ(SanitizeCullPadding(1.0f), 1.0f);
    EXPECT_EQ(SanitizeCullPadding(7.0f), 1.0f);
    EXPECT_EQ(SanitizeCullPadding(-0.5f), 0.0f);
    EXPECT_EQ(SanitizeCullPadding(std::numeric_limits<float>::quiet_NaN()), 0.0f);
    EXPECT_EQ(SanitizeCullPadding(std::numeric_limits<float>::infinity()), 0.0f);
}

TEST(PrimaryAspectFallbackTest, PrefersLastPrimaryThenCallerThenOne)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_EQ(PrimaryAspectFallback(2.0f, 1.5f), 2.0f);
    EXPECT_EQ(PrimaryAspectFallback(0.0f, 1.5f), 1.5f);
    EXPECT_EQ(PrimaryAspectFallback(nan, 1.5f), 1.5f);
    EXPECT_EQ(PrimaryAspectFallback(-1.0f, 1.5f), 1.5f);
    EXPECT_EQ(PrimaryAspectFallback(inf, 1.5f), 1.5f);
    EXPECT_EQ(PrimaryAspectFallback(0.0f, 0.0f), 1.0f);
    EXPECT_EQ(PrimaryAspectFallback(nan, inf), 1.0f);
}

} // namespace
} // namespace gte
