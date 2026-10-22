// Tier-1 tests for the cluster draw state: name pool, layout, parameter blocks and budgets. No device involved.

#include "Core/FeatureKit/ClusterDrawState.h"
#include "Core/FeatureKit/ClusterNamePool.h"

#include <gtest/gtest.h>

#include <limits>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace gte::fx {
namespace {

constexpr rg::ViewScope kGame = rg::ViewScope::GameView;
constexpr rg::ViewScope kScene = rg::ViewScope::SceneView;

TEST(ClusterNamePoolTests, SameTextGivesSamePointer)
{
    ClusterNamePool pool(8);
    const char* a = pool.Intern("alpha");
    const char* b = pool.Intern(std::string("alpha"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a, b);
    EXPECT_EQ(pool.Size(), 1u);
    EXPECT_NE(pool.Intern("beta"), a);
    EXPECT_EQ(pool.Size(), 2u);
}

TEST(ClusterNamePoolTests, PointersSurviveGrowth)
{
    ClusterNamePool pool(64);
    const char* first = pool.Intern("first");
    for (int i = 0; i < 50; ++i) {
        ASSERT_NE(pool.Intern("name" + std::to_string(i)), nullptr);
    }
    EXPECT_STREQ(first, "first");
    EXPECT_EQ(pool.Intern("first"), first);
}

TEST(ClusterNamePoolTests, FullPoolRefusesNewNamesButKnowsOldOnes)
{
    ClusterNamePool pool(2);
    const char* a = pool.Intern("a");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(pool.Intern("b"), nullptr);
    EXPECT_EQ(pool.Intern("c"), nullptr);
    EXPECT_EQ(pool.Intern("a"), a);
    EXPECT_EQ(pool.Size(), 2u);
}

TEST(ClusterNamePoolTests, EmptyNameIsRefused)
{
    ClusterNamePool pool(2);
    EXPECT_EQ(pool.Intern(""), nullptr);
}

TEST(ClusterDrawLayoutTests, PassNameFormat)
{
    EXPECT_EQ(ClusterPassNameText("Base", 2, 3, ClusterPassKind::Cull), "Base.L2.P3.Cull");
    EXPECT_EQ(ClusterPassNameText("Base", 0, 0, ClusterPassKind::Reset), "Base.L0.P0.Reset");
    EXPECT_EQ(ClusterPassNameText("Base", 0, 7, ClusterPassKind::Stats), "Base.L0.P7.Stats");
}

TEST(ClusterDrawLayoutTests, ChainIndexIsDense)
{
    const ClusterDrawShape shape{ 4, 4 };
    std::set<std::size_t> seen;
    for (std::size_t scope = 0; scope < rg::kViewScopeCount; ++scope) {
        for (std::uint32_t lane = 0; lane < shape.lanes; ++lane) {
            for (std::uint32_t slot = 0; slot < shape.partSlots; ++slot) {
                seen.insert(ClusterChainIndex(scope, lane, slot, shape));
            }
        }
    }
    EXPECT_EQ(seen.size(), rg::kViewScopeCount * 16u);
    EXPECT_EQ(*seen.begin(), 0u);
    EXPECT_EQ(*seen.rbegin(), rg::kViewScopeCount * 16u - 1);
}

TEST(ClusterDrawLayoutTests, ShapeLimits)
{
    EXPECT_TRUE(ClusterShapeIsValid({ kMainViewLanes, kMainViewPartSlots }));
    EXPECT_TRUE(ClusterShapeIsValid({ kDepthOnlyLanes, kDepthOnlyPartSlots }));
    EXPECT_FALSE(ClusterShapeIsValid({ 0, 1 }));
    EXPECT_FALSE(ClusterShapeIsValid({ 1, 0 }));
    EXPECT_FALSE(ClusterShapeIsValid({ 5, 1 }));
    EXPECT_FALSE(ClusterShapeIsValid({ 1, 9 }));
}

TEST(ClusterDrawStateTests, InvalidShapeIsRefusedWithMessage)
{
    ClusterDrawState bad("TestBad", { 0, 1 });
    EXPECT_FALSE(bad.Valid());
    EXPECT_FALSE(bad.Error().empty());
    EXPECT_TRUE(bad.AllPassNames().empty());
    EXPECT_EQ(bad.Instances(0), nullptr);
    EXPECT_EQ(bad.PassName(0, 0, ClusterPassKind::Cull), nullptr);

    ClusterDrawState noName("", { 1, 1 });
    EXPECT_FALSE(noName.Valid());
}

TEST(ClusterDrawStateTests, PassNamesAreUniqueAndStable)
{
    ClusterDrawState state("TestNames", { 4, 4 });
    ASSERT_TRUE(state.Valid());
    const std::vector<std::string> names = state.AllPassNames();
    EXPECT_EQ(names.size(), 4u * 4u * kClusterPassKindCount);
    const std::unordered_set<std::string> unique(names.begin(), names.end());
    EXPECT_EQ(unique.size(), names.size());
    EXPECT_STREQ(state.PassName(1, 2, ClusterPassKind::Draw), "TestNames.L1.P2.Draw");
    EXPECT_EQ(state.PassName(4, 0, ClusterPassKind::Draw), nullptr);
    EXPECT_EQ(state.PassName(0, 4, ClusterPassKind::Draw), nullptr);
}

TEST(ClusterDrawStateTests, RebuiltStateReusesInternedNames)
{
    ClusterDrawState first("TestRebuild", { 1, 2 });
    const char* name = first.PassName(0, 1, ClusterPassKind::Cull);
    ASSERT_NE(name, nullptr);
    {
        ClusterDrawState second("TestRebuild", { 1, 2 });
        EXPECT_EQ(second.PassName(0, 1, ClusterPassKind::Cull), name);
    }
    EXPECT_EQ(first.PassName(0, 1, ClusterPassKind::Cull), name);
}

TEST(ClusterDrawStateTests, DifferentBasesNeverShareNames)
{
    ClusterDrawState a("TestBaseA", { 1, 2 });
    ClusterDrawState b("TestBaseB", { 1, 2 });
    std::unordered_set<std::string> all;
    for (const std::string& n : a.AllPassNames()) {
        EXPECT_TRUE(all.insert(n).second);
    }
    for (const std::string& n : b.AllPassNames()) {
        EXPECT_TRUE(all.insert(n).second);
    }
    EXPECT_NE(a.ParamsName(kGame, 0, 0), b.ParamsName(kGame, 0, 0));
}

TEST(ClusterDrawStateTests, NoTwoChainsShareAParameterBlock)
{
    ClusterDrawState state("TestParams", { 4, 4 });
    ASSERT_TRUE(state.Valid());
    ClusterDrawState other("TestParamsOther", { 4, 4 });
    std::set<const void*> blocks;
    std::set<const char*> names;
    std::size_t count = 0;
    for (ClusterDrawState* s : { &state, &other }) {
        for (const rg::ViewScope scope : { kGame, kScene }) {
            for (std::uint32_t lane = 0; lane < 4; ++lane) {
                for (std::uint32_t slot = 0; slot < 4; ++slot) {
                    ASSERT_NE(s->Params(scope, lane, slot), nullptr);
                    blocks.insert(s->Params(scope, lane, slot));
                    names.insert(s->ParamsName(scope, lane, slot));
                    ++count;
                }
            }
        }
    }
    EXPECT_EQ(blocks.size(), count);
    EXPECT_EQ(names.size(), count);
}

TEST(ClusterDrawStateTests, BuffersAreDistinctPerLaneAndSlot)
{
    ClusterDrawState state("TestBuffers", { 2, 3 });
    ASSERT_TRUE(state.Valid());
    std::set<const void*> buffers;
    std::size_t count = 0;
    for (std::uint32_t slot = 0; slot < 3; ++slot) {
        buffers.insert(state.Instances(slot));
        ++count;
        for (std::uint32_t lane = 0; lane < 2; ++lane) {
            buffers.insert(state.Visible(lane, slot));
            buffers.insert(state.Commands(lane, slot));
            buffers.insert(state.Count(lane, slot));
            count += 3;
        }
    }
    EXPECT_EQ(buffers.count(nullptr), 0u);
    EXPECT_EQ(buffers.size(), count);
    EXPECT_EQ(state.Visible(2, 0), nullptr);
    EXPECT_EQ(state.Instances(3), nullptr);
}

TEST(ClusterDrawStateTests, BindingIndexCoversTwoScopesDensely)
{
    ClusterDrawState state("TestBindings", { 2, 3 });
    ASSERT_TRUE(state.Valid());
    std::set<std::size_t> seen;
    for (const rg::ViewScope scope : { kGame, kScene }) {
        for (std::uint32_t lane = 0; lane < 2; ++lane) {
            for (std::uint32_t slot = 0; slot < 3; ++slot) {
                seen.insert(state.BindingIndex(scope, lane, slot));
            }
        }
    }
    EXPECT_EQ(seen.size(), 12u);
    EXPECT_EQ(*seen.rbegin(), 11u);
    EXPECT_EQ(state.BindingIndex(rg::ViewScope::Shared, 0, 0), std::numeric_limits<std::size_t>::max());
}

TEST(ClusterDrawStateTests, ReserveRejectsBadIndices)
{
    ClusterDrawState state("TestReserveBad", { 1, 2 });
    EXPECT_EQ(state.TryReserveChainBytes(rg::ViewScope::Shared, 0, 0, 1, 1), ClusterReserveResult::BadIndex);
    EXPECT_EQ(state.TryReserveChainBytes(kGame, 1, 0, 1, 1), ClusterReserveResult::BadIndex);
    EXPECT_EQ(state.TryReserveChainBytes(kGame, 0, 2, 1, 1), ClusterReserveResult::BadIndex);
    EXPECT_EQ(state.HeldBytes(kGame), 0u);
}

TEST(ClusterDrawStateTests, StateBudgetThenProcessCeiling)
{
    constexpr std::uint64_t kInstances = 1;
    constexpr std::uint64_t kWork = 1000000;
    const std::uint64_t chain = ClusterChainBytes(kInstances, kWork);
    ASSERT_GT(chain, 0u);
    const std::uint64_t perScope = kClusterDrawBudgetBytes / chain; // Chains that fit one scope.
    ASSERT_GE(perScope, 1u);
    ASSERT_LT(perScope, kMainViewPartSlots);

    {
        ClusterDrawState a("TestBudgetA", { 1, kMainViewPartSlots });
        ClusterDrawState b("TestBudgetB", { 1, kMainViewPartSlots });
        for (std::uint32_t slot = 0; slot < perScope; ++slot) {
            ASSERT_EQ(a.TryReserveChainBytes(kGame, 0, slot, kInstances, kWork), ClusterReserveResult::Ok);
        }
        EXPECT_EQ(a.TryReserveChainBytes(kGame, 0, static_cast<std::uint32_t>(perScope), kInstances, kWork),
            ClusterReserveResult::OverStateBudget);
        EXPECT_EQ(a.HeldBytes(kGame), perScope * chain);
        EXPECT_LE(a.HeldBytes(kGame), kClusterDrawBudgetBytes);

        // Re-reserving a chain at the same size changes nothing.
        EXPECT_EQ(a.TryReserveChainBytes(kGame, 0, 0, kInstances, kWork), ClusterReserveResult::Ok);
        EXPECT_EQ(a.HeldBytes(kGame), perScope * chain);

        // Fill the process ceiling with both scopes of `a` and the game scope of `b`.
        std::uint32_t scene = 0;
        while (a.TryReserveChainBytes(kScene, 0, scene, kInstances, kWork) == ClusterReserveResult::Ok) {
            ++scene;
        }
        ClusterReserveResult last = ClusterReserveResult::Ok;
        for (std::uint32_t slot = 0; slot < kMainViewPartSlots && last == ClusterReserveResult::Ok; ++slot) {
            last = b.TryReserveChainBytes(kGame, 0, slot, kInstances, kWork);
        }
        EXPECT_NE(last, ClusterReserveResult::Ok);
        EXPECT_LE(ProcessClusterLedger().Total(), kClusterDrawProcessBudgetBytes);
    }

    // Destroying a state returns its bytes to the ledger.
    ClusterDrawState c("TestBudgetC", { 1, kMainViewPartSlots });
    EXPECT_EQ(c.TryReserveChainBytes(kGame, 0, 0, kInstances, kWork), ClusterReserveResult::Ok);
}

} // namespace
} // namespace gte::fx
