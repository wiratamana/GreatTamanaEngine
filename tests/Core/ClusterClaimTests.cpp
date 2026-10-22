// Tier-1 tests for the pure claim rule in Core/ClusterClaim.h. No device involved.

#include "Core/ClusterClaim.h"

#include <gtest/gtest.h>

#include <array>

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
    part.planReady = true;
    part.progress = ClusterChainProgress::Draw;
    return part;
}

ClusterClaimContext GameContext()
{
    ClusterClaimContext context;
    context.scope = rg::ViewScope::GameView;
    return context;
}

TEST(ClusterClaimTest, ACompleteChainClaimsEveryEntityOfEveryPart)
{
    const std::array<ClusterPartClaim, 2> parts = { CompletePart(kPartA), CompletePart(kPartB) };
    const std::vector<Entity> claimed = ClaimedEntitiesOf(parts, GameContext());
    ASSERT_EQ(claimed.size(), 3u);
    EXPECT_EQ(claimed[0], MakeEntity(1));
    EXPECT_EQ(claimed[2], MakeEntity(3));
}

TEST(ClusterClaimTest, AToggledOffPassClaimsNothingForThatPart)
{
    for (int which = 0; which < 3; ++which) {
        ClusterPartClaim part = CompletePart(kPartA);
        (which == 0 ? part.toggles.reset : which == 1 ? part.toggles.cull : part.toggles.draw) = false;
        const std::array<ClusterPartClaim, 2> parts = { part, CompletePart(kPartB) };
        const std::vector<Entity> claimed = ClaimedEntitiesOf(parts, GameContext());
        ASSERT_EQ(claimed.size(), 1u) << "toggle " << which;
        EXPECT_EQ(claimed[0], MakeEntity(3));
    }
}

TEST(ClusterClaimTest, APlanThatIsNotReadyClaimsNothing)
{
    ClusterPartClaim part = CompletePart(kPartA);
    part.planReady = false;
    const std::array<ClusterPartClaim, 1> parts = { part };
    EXPECT_TRUE(ClaimedEntitiesOf(parts, GameContext()).empty());
}

TEST(ClusterClaimTest, AChainThatStoppedBeforeTheDrawClaimsNothing)
{
    for (const ClusterChainProgress progress :
        { ClusterChainProgress::None, ClusterChainProgress::Reset, ClusterChainProgress::Cull }) {
        ClusterPartClaim part = CompletePart(kPartA);
        part.progress = progress;
        const std::array<ClusterPartClaim, 1> parts = { part };
        EXPECT_TRUE(ClaimedEntitiesOf(parts, GameContext()).empty());
    }
}

TEST(ClusterClaimTest, ACapturedGameViewFrameClaimsNothing)
{
    ClusterClaimContext context = GameContext();
    context.captureArmed = true;
    const std::array<ClusterPartClaim, 1> parts = { CompletePart(kPartA) };
    EXPECT_TRUE(ClaimedEntitiesOf(parts, context).empty());
}

TEST(ClusterClaimTest, ACaptureOfTheGameViewDoesNotBlockTheSceneView)
{
    ClusterClaimContext context;
    context.scope = rg::ViewScope::SceneView;
    context.captureArmed = true;
    const std::array<ClusterPartClaim, 1> parts = { CompletePart(kPartA) };
    EXPECT_EQ(ClaimedEntitiesOf(parts, context).size(), 2u);
}

TEST(ClusterClaimTest, TheSharedScopeNeverClaims)
{
    ClusterClaimContext context;
    context.scope = rg::ViewScope::Shared;
    const std::array<ClusterPartClaim, 1> parts = { CompletePart(kPartA) };
    EXPECT_TRUE(ClaimedEntitiesOf(parts, context).empty());
}

TEST(ClusterClaimTest, AClaimIsAPureFunctionOfItsScope)
{
    const std::array<ClusterPartClaim, 1> gameParts = { CompletePart(kPartA) };
    const std::array<ClusterPartClaim, 1> sceneParts = { CompletePart(kPartB) };
    ClusterClaimContext sceneContext;
    sceneContext.scope = rg::ViewScope::SceneView;

    const std::vector<Entity> game = ClaimedEntitiesOf(gameParts, GameContext());
    const std::vector<Entity> scene = ClaimedEntitiesOf(sceneParts, sceneContext);
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
    EXPECT_TRUE(ClaimedEntitiesOf({}, GameContext()).empty());
}

} // namespace
} // namespace gte
