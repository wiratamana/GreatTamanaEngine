// Tier-1 tests: TranslateLegacyViewScope maps named views to ViewScope.

#include "Application/RenderPassViewData.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(TranslateLegacyViewScopeTest, GameMapsToGameView)
{
    EXPECT_EQ(TranslateLegacyViewScope(rg::RenderViewId::Named("Game")), rg::ViewScope::GameView);
}

TEST(TranslateLegacyViewScopeTest, SceneMapsToSceneView)
{
    EXPECT_EQ(TranslateLegacyViewScope(rg::RenderViewId::Named("Scene")), rg::ViewScope::SceneView);
}

TEST(TranslateLegacyViewScopeTest, SharedViewMapsToShared)
{
    EXPECT_EQ(TranslateLegacyViewScope(rg::RenderViewId::Shared()), rg::ViewScope::Shared);
}

TEST(TranslateLegacyViewScopeTest, UnknownNamedViewMapsToShared)
{
    EXPECT_EQ(TranslateLegacyViewScope(rg::RenderViewId::Named("Shadow.Map.GameView")), rg::ViewScope::Shared);
    EXPECT_EQ(TranslateLegacyViewScope(rg::RenderViewId::Named("game")), rg::ViewScope::Shared);
}

} // namespace
} // namespace gte
