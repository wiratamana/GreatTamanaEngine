// Tier-1 tests for the cluster draw state: name pool, layout, parameter blocks and budgets. No device involved.

#include "Core/FeatureKit/ClusterDrawState.h"
#include "Core/FeatureKit/ClusterNamePool.h"
#include "Core/FeatureKit/OncePerFrame.h"
#include "Core/FeatureKit/HizPyramid.h"
#include "Renderer/GpuStampPlan.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace gte::fx {
namespace {

constexpr rg::ViewScope kGame = rg::ViewScope::Primary;
constexpr rg::ViewScope kScene = rg::ViewScope::Secondary;

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
    EXPECT_TRUE(ClusterShapeIsValid({ 1, 12 }));
    EXPECT_FALSE(ClusterShapeIsValid({ 0, 1 }));
    EXPECT_FALSE(ClusterShapeIsValid({ 1, 0 }));
    EXPECT_FALSE(ClusterShapeIsValid({ 5, 1 }));
    EXPECT_FALSE(ClusterShapeIsValid({ 1, 13 }));
}

// Stands in for a persistent resource that refuses a second request within one frame.
struct OneRequestPerFrame {
    std::uint64_t lastFrame = ~std::uint64_t{ 0 };
    int Acquire(std::uint64_t frame)
    {
        if (frame == lastFrame) {
            return 0;
        }
        lastFrame = frame;
        return 7;
    }
};

TEST(OncePerFrameTests, TwoPartsOfOneLaneInOneFrameBothGetTheResource)
{
    OneRequestPerFrame resource;
    OncePerFrame<int> once;
    for (int part = 0; part < 12; ++part) {
        EXPECT_EQ(once.Get(5, nullptr, [&] { return resource.Acquire(5); }), 7) << "part " << part;
    }
}

TEST(OncePerFrameTests, ANewFrameOrOwnerAcquiresAgain)
{
    OneRequestPerFrame resource;
    OncePerFrame<int> once;
    int builderA = 0;
    int builderB = 0;
    EXPECT_EQ(once.Get(1, &builderA, [&] { return resource.Acquire(1); }), 7);
    EXPECT_EQ(once.Get(2, &builderA, [&] { return resource.Acquire(2); }), 7);
    EXPECT_EQ(once.Get(2, &builderA, [&] { return resource.Acquire(2); }), 7);
    int calls = 0;
    once.Get(2, &builderB, [&] { return ++calls; });
    EXPECT_EQ(calls, 1);
}

TEST(OncePerFrameTests, AFailedAcquireIsNotRetriedInTheSameFrame)
{
    OncePerFrame<int> once;
    int calls = 0;
    EXPECT_EQ(once.Get(3, nullptr, [&] { ++calls; return 0; }), 0);
    EXPECT_EQ(once.Get(3, nullptr, [&] { ++calls; return 9; }), 0);
    EXPECT_EQ(calls, 1);
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
    EXPECT_NE(a.TableName(kGame, 0, 0), b.TableName(kGame, 0, 0));
}

TEST(ClusterDrawStateTests, NoTwoChainsShareAParameterBlock)
{
    ClusterDrawState state("TestParams", { 4, 4 });
    ASSERT_TRUE(state.Valid());
    ClusterDrawState other("TestParamsOther", { 4, 4 });
    std::set<const void*> blocks;
    std::size_t count = 0;
    for (ClusterDrawState* s : { &state, &other }) {
        for (const rg::ViewScope scope : { kGame, kScene }) {
            for (std::uint32_t lane = 0; lane < 4; ++lane) {
                for (std::uint32_t slot = 0; slot < 4; ++slot) {
                    ASSERT_NE(s->Params(scope, lane, slot), nullptr);
                    blocks.insert(s->Params(scope, lane, slot));
                    ++count;
                }
            }
        }
    }
    EXPECT_EQ(blocks.size(), count);
}

TEST(ClusterDrawStateTests, BuffersAreDistinctPerLaneAndSlot)
{
    ClusterDrawState state("TestBuffers", { 2, 3 });
    ASSERT_TRUE(state.Valid());
    std::set<const void*> buffers;
    std::size_t count = 0;
    for (std::uint32_t slot = 0; slot < 3; ++slot) {
        buffers.insert(state.Instances(slot));
        buffers.insert(state.History(slot));
        count += 2;
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
    EXPECT_EQ(state.History(3), nullptr);
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
    EXPECT_EQ(state.TryReserveChainBytes(rg::ViewScope::Shared, 0, 0, 1, 1, false), ClusterReserveResult::BadIndex);
    EXPECT_EQ(state.TryReserveChainBytes(kGame, 1, 0, 1, 1, false), ClusterReserveResult::BadIndex);
    EXPECT_EQ(state.TryReserveChainBytes(kGame, 0, 2, 1, 1, false), ClusterReserveResult::BadIndex);
    EXPECT_EQ(state.HeldBytes(kGame), 0u);
}

TEST(ClusterDrawStateTests, ATwoPhaseChainNeedsASecondLaneAndIsRecordedOnTheFirst)
{
    ClusterDrawState oneLane("TestTwoPhaseOneLane", { 1, 2 });
    EXPECT_EQ(oneLane.TryReserveChainBytes(kGame, 0, 0, 1, 1000, true), ClusterReserveResult::BadIndex);

    ClusterDrawState twoLanes("TestTwoPhaseTwoLanes", { 2, 2 });
    EXPECT_EQ(twoLanes.TryReserveChainBytes(kGame, 1, 0, 1, 1000, true), ClusterReserveResult::BadIndex);
    ASSERT_EQ(twoLanes.TryReserveChainBytes(kGame, 0, 0, 1, 1000, true), ClusterReserveResult::Ok);
    EXPECT_EQ(twoLanes.HeldBytes(kGame), ClusterChainBytes(1, 1000, true));
    EXPECT_GT(twoLanes.HeldBytes(kGame), ClusterChainBytes(1, 1000, false));
}

TEST(ClusterDrawStateTests, TheInstanceBufferOfASlotIsChargedOnceForAllLanes)
{
    constexpr std::uint64_t kInstances = 50000;
    constexpr std::uint64_t kWork = 1100000;
    ClusterDrawState state("TestInstancesOnce", { 4, 2 });
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        ASSERT_EQ(state.TryReserveChainBytes(kGame, lane, 0, kInstances, kWork, false), ClusterReserveResult::Ok);
    }
    const std::uint64_t oneChain = ClusterChainBytes(kInstances, kWork, false);
    const std::uint64_t instances = ClusterBufferCapacity(kInstances * sizeof(ClusterInstanceGpu));
    EXPECT_EQ(state.HeldBytes(kGame), 4 * (oneChain - instances) + instances);

    // A second slot has its own instance buffer.
    ASSERT_EQ(state.TryReserveChainBytes(kGame, 0, 1, kInstances, kWork, false), ClusterReserveResult::Ok);
    EXPECT_EQ(state.HeldBytes(kGame), 5 * (oneChain - instances) + 2 * instances);
}

TEST(ClusterDrawStateTests, AGrowingInstanceCountIsChargedEvenWhenAnotherLaneHoldsTheBuffer)
{
    ClusterDrawState state("TestInstanceGrowth", { 2, 1 });
    ASSERT_EQ(state.TryReserveChainBytes(kGame, 0, 0, 1000, 20000, false), ClusterReserveResult::Ok);
    ASSERT_EQ(state.TryReserveChainBytes(kGame, 1, 0, 1000, 20000, false), ClusterReserveResult::Ok);
    const std::uint64_t before = state.HeldBytes(kGame);
    ASSERT_EQ(state.TryReserveChainBytes(kGame, 0, 0, 100000, 20000, false), ClusterReserveResult::Ok);
    EXPECT_EQ(state.HeldBytes(kGame), before - ClusterBufferCapacity(1000 * sizeof(ClusterInstanceGpu))
        + ClusterBufferCapacity(100000 * sizeof(ClusterInstanceGpu)));
}

TEST(ClusterDrawStateTests, CanReserveAnswersLikeTryReserveAndRecordsNothing)
{
    ClusterDrawState state("TestCanReserve", { 2, 1 });
    EXPECT_EQ(state.CanReserveChainBytes(kGame, 0, 0, 50000, 1100000, true), ClusterReserveResult::Ok);
    EXPECT_EQ(state.HeldBytes(kGame), 0u);
    EXPECT_EQ(state.CanReserveChainBytes(rg::ViewScope::Shared, 0, 0, 1, 1, false), ClusterReserveResult::BadIndex);

    // Over the state budget: a chain far bigger than 256 MiB.
    EXPECT_EQ(state.CanReserveChainBytes(kGame, 0, 0, 1, 20000000, false), ClusterReserveResult::OverStateBudget);
    EXPECT_EQ(state.HeldBytes(kGame), 0u);
}

TEST(ClusterDrawStateTests, ATwoPhaseChainCanBeRefusedWhileTheSingleChainFits)
{
    // 3 frustum chains of 52 MiB leave 100 MiB of the 256 MiB state budget: room for one more of them, not for a Hi-Z chain.
    constexpr std::uint64_t kInstances = 50000;
    constexpr std::uint64_t kWork = 1100000;
    constexpr std::uint64_t kHistoryWords = 100000; // 50000 instances x 2 chunks.
    ASSERT_EQ(ClusterHistoryWordCount(kInstances, 22), kHistoryWords);
    ClusterDrawState state("TestLadder", { 2, kMainViewPartSlots });
    for (std::uint32_t slot = 0; slot < 3; ++slot) {
        ASSERT_EQ(state.TryReserveChainBytes(kGame, 0, slot, kInstances, kWork, false), ClusterReserveResult::Ok);
    }
    EXPECT_EQ(state.CanReserveChainBytes(kGame, 0, 3, kInstances, kWork, false), ClusterReserveResult::Ok);
    EXPECT_EQ(state.CanReserveChainBytes(kGame, 0, 3, kInstances, kWork, true, kHistoryWords),
        ClusterReserveResult::OverStateBudget);
    EXPECT_EQ(state.HeldBytes(kGame), 3 * ClusterChainBytes(kInstances, kWork, false));
}

TEST(ClusterDrawStateTests, ThePackedHistoryChargesAboutAChunkOfTheOldSize)
{
    constexpr std::uint64_t kInstances = 50000;
    constexpr std::uint64_t kWork = 1100000;
    const std::uint64_t old = ClusterChainBytes(kInstances, kWork, true);
    const std::uint64_t packed = ClusterChainBytes(
        kInstances, kWork, ClusterChainShape{ true, false, ClusterHistoryWordCount(kInstances, 22) });
    EXPECT_EQ(old - packed, ClusterHistoryCapacity(kWork) - ClusterHistoryCapacity(ClusterHistoryWordCount(kInstances, 22)));
    EXPECT_EQ(ClusterHistoryCapacity(kWork), 8u << 20);
    EXPECT_EQ(ClusterHistoryCapacity(ClusterHistoryWordCount(kInstances, 22)), 512u << 10);
}

TEST(ClusterDrawStateTests, StateBudgetThenProcessCeiling)
{
    constexpr std::uint64_t kInstances = 1;
    constexpr std::uint64_t kWork = 1000000;
    const std::uint64_t chain = ClusterChainBytes(kInstances, kWork, false);
    ASSERT_GT(chain, 0u);
    const std::uint64_t perScope = kClusterDrawBudgetBytes / chain; // Chains that fit one scope.
    ASSERT_GE(perScope, 1u);
    ASSERT_LT(perScope, kMainViewPartSlots);

    {
        ClusterDrawState a("TestBudgetA", { 1, kMainViewPartSlots });
        ClusterDrawState b("TestBudgetB", { 1, kMainViewPartSlots });
        for (std::uint32_t slot = 0; slot < perScope; ++slot) {
            ASSERT_EQ(a.TryReserveChainBytes(kGame, 0, slot, kInstances, kWork, false), ClusterReserveResult::Ok);
        }
        EXPECT_EQ(a.TryReserveChainBytes(kGame, 0, static_cast<std::uint32_t>(perScope), kInstances, kWork, false),
            ClusterReserveResult::OverStateBudget);
        EXPECT_EQ(a.HeldBytes(kGame), perScope * chain);
        EXPECT_LE(a.HeldBytes(kGame), kClusterDrawBudgetBytes);

        // Re-reserving a chain at the same size changes nothing.
        EXPECT_EQ(a.TryReserveChainBytes(kGame, 0, 0, kInstances, kWork, false), ClusterReserveResult::Ok);
        EXPECT_EQ(a.HeldBytes(kGame), perScope * chain);

        // Fill the process ceiling with both scopes of `a` and the game scope of `b`.
        std::uint32_t scene = 0;
        while (a.TryReserveChainBytes(kScene, 0, scene, kInstances, kWork, false) == ClusterReserveResult::Ok) {
            ++scene;
        }
        ClusterReserveResult last = ClusterReserveResult::Ok;
        for (std::uint32_t slot = 0; slot < kMainViewPartSlots && last == ClusterReserveResult::Ok; ++slot) {
            last = b.TryReserveChainBytes(kGame, 0, slot, kInstances, kWork, false);
        }
        EXPECT_NE(last, ClusterReserveResult::Ok);
        EXPECT_LE(ProcessClusterLedger().Total(), kClusterDrawProcessBudgetBytes);
    }

    // Destroying a state returns its bytes to the ledger.
    ClusterDrawState c("TestBudgetC", { 1, kMainViewPartSlots });
    EXPECT_EQ(c.TryReserveChainBytes(kGame, 0, 0, kInstances, kWork, false), ClusterReserveResult::Ok);
}

TEST(ClusterDrawStateTests, TheMainViewRunsOneLanePerPhaseAndStaysInsideTheGpuRecordBudget)
{
    EXPECT_EQ(kMainViewLanes, 2u);
    EXPECT_EQ(kMainViewPartSlots, 12u);
    EXPECT_EQ(kClusterPassNameCount, 2u * 12u * 6u + 4u * 4u * 3u);
    EXPECT_EQ(kGpuBaseRecordCount + 2 * kClusterPassNameCount + kHizPassCount, 450u);
    EXPECT_LE(kGpuBaseRecordCount + 2 * kClusterPassNameCount + kHizPassCount, kGpuRecordBudgetSynchronous);

    ClusterDrawState state("TestMainShape", { kMainViewLanes, kMainViewPartSlots });
    ASSERT_TRUE(state.Valid());
    EXPECT_EQ(state.AllPassNames().size(), static_cast<std::size_t>(kMainViewLanes) * kMainViewPartSlots * kClusterPassKindCount);
    EXPECT_STREQ(state.PassName(1, 7, ClusterPassKind::Cull), "TestMainShape.L1.P7.Cull");
}

constexpr ClusterDrawShape kSharedShape{ 4, 1, true };

TEST(ClusterDrawStateTests, SharedLanesUseTheBuffersOfLaneZeroButKeepOwnNamesAndParameters)
{
    ClusterDrawState state("TestShareBuffers", { 4, 2, true });
    ASSERT_TRUE(state.Valid());
    std::set<const void*> params;
    for (std::uint32_t slot = 0; slot < 2; ++slot) {
        for (std::uint32_t lane = 0; lane < 4; ++lane) {
            EXPECT_EQ(state.Visible(lane, slot), state.Visible(0, slot));
            EXPECT_EQ(state.Commands(lane, slot), state.Commands(0, slot));
            EXPECT_EQ(state.Count(lane, slot), state.Count(0, slot));
            EXPECT_NE(state.Params(kGame, lane, slot), state.Params(kScene, lane, slot));
            params.insert(state.Params(kGame, lane, slot));
            params.insert(state.Params(kScene, lane, slot));
        }
    }
    EXPECT_NE(state.Visible(0, 0), state.Visible(0, 1));
    EXPECT_NE(state.Commands(0, 0), state.Commands(0, 1));
    EXPECT_NE(state.Count(0, 0), state.Count(0, 1));
    EXPECT_NE(state.Visible(0, 0), state.Commands(0, 0));
    EXPECT_EQ(params.size(), 2u * 4u * 2u);
    EXPECT_EQ(params.count(nullptr), 0u);
    EXPECT_EQ(state.Visible(4, 0), nullptr);
    EXPECT_EQ(state.Count(0, 2), nullptr);

    const std::vector<std::string> names = state.AllPassNames();
    EXPECT_EQ(names.size(), 4u * 2u * kClusterPassKindCount);
    EXPECT_EQ(std::unordered_set<std::string>(names.begin(), names.end()).size(), names.size());
    EXPECT_STREQ(state.PassName(3, 1, ClusterPassKind::Draw), "TestShareBuffers.L3.P1.Draw");
    EXPECT_NE(state.TableName(kGame, 1, 0), state.TableName(kGame, 0, 0));
}

TEST(ClusterDrawStateTests, UnsharedLanesKeepOneBufferEach)
{
    ClusterDrawState state("TestNoShare", { 4, 1 });
    ASSERT_TRUE(state.Valid());
    std::set<const void*> buffers;
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        buffers.insert(state.Visible(lane, 0));
        buffers.insert(state.Commands(lane, 0));
        buffers.insert(state.Count(lane, 0));
    }
    EXPECT_EQ(buffers.size(), 12u);
}

TEST(ClusterDrawStateTests, SharedLanesChargeTheLargestLaneNotTheSum)
{
    constexpr std::uint64_t kInstances = 50000;
    constexpr std::uint64_t kWork = 1100000;
    ClusterDrawState shared("TestShareMax", kSharedShape);
    ClusterDrawState unshared("TestShareMaxOff", { 4, 1 });
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        EXPECT_EQ(shared.TryReserveChainBytes(kGame, lane, 0, kInstances, kWork, false), ClusterReserveResult::Ok);
        EXPECT_EQ(unshared.TryReserveChainBytes(kGame, lane, 0, kInstances, kWork, false), ClusterReserveResult::Ok);
    }
    EXPECT_EQ(shared.HeldBytes(kGame), 54526208u);
    EXPECT_EQ(shared.HeldBytes(kGame), ClusterChainBytes(kInstances, kWork, false));
    EXPECT_EQ(unshared.HeldBytes(kGame), 205521920u);
    // The saving is three lanes of 48 MiB + 256 B each.
    EXPECT_EQ(unshared.HeldBytes(kGame) - shared.HeldBytes(kGame), 150995712u);
}

TEST(ClusterDrawStateTests, FourSharedLanesFitAt100000InstancesWhereUnsharedLanesAreRefused)
{
    constexpr std::uint64_t kInstances = 100000;
    constexpr std::uint64_t kWork = 2200000;
    ClusterDrawState shared("TestShare100k", kSharedShape);
    ClusterDrawState unshared("TestShare100kOff", { 4, 1 });
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        EXPECT_EQ(shared.TryReserveChainBytes(kGame, lane, 0, kInstances, kWork, false), ClusterReserveResult::Ok)
            << "lane " << lane;
    }
    EXPECT_EQ(shared.HeldBytes(kGame), 109052160u);

    EXPECT_EQ(unshared.TryReserveChainBytes(kGame, 0, 0, kInstances, kWork, false), ClusterReserveResult::Ok);
    EXPECT_EQ(unshared.TryReserveChainBytes(kGame, 1, 0, kInstances, kWork, false), ClusterReserveResult::Ok);
    EXPECT_EQ(unshared.TryReserveChainBytes(kGame, 2, 0, kInstances, kWork, false), ClusterReserveResult::OverStateBudget);
    EXPECT_EQ(unshared.TryReserveChainBytes(kGame, 3, 0, kInstances, kWork, false), ClusterReserveResult::OverStateBudget);
    EXPECT_EQ(unshared.HeldBytes(kGame), 209715712u);
}

TEST(ClusterDrawStateTests, TheSharedRecordGrowsToTheLargestLaneAndNeverShrinks)
{
    ClusterDrawState state("TestShareGrow", kSharedShape);
    ASSERT_EQ(state.TryReserveChainBytes(kGame, 0, 0, 1, 1000, false), ClusterReserveResult::Ok);
    EXPECT_EQ(state.HeldBytes(kGame), ClusterChainBytes(1, 1000, false));
    ASSERT_EQ(state.TryReserveChainBytes(kGame, 1, 0, 1, 2000000, false), ClusterReserveResult::Ok);
    EXPECT_EQ(state.HeldBytes(kGame), ClusterChainBytes(1, 2000000, false));
    ASSERT_EQ(state.TryReserveChainBytes(kGame, 2, 0, 1, 1000, false), ClusterReserveResult::Ok);
    EXPECT_EQ(state.HeldBytes(kGame), ClusterChainBytes(1, 2000000, false));
}

TEST(ClusterDrawStateTests, AGrowingInstanceCountIsStillChargedOnceOnSharedLanes)
{
    ClusterDrawState state("TestShareInstances", kSharedShape);
    ASSERT_EQ(state.TryReserveChainBytes(kGame, 0, 0, 1000, 20000, false), ClusterReserveResult::Ok);
    ASSERT_EQ(state.TryReserveChainBytes(kGame, 3, 0, 100000, 20000, false), ClusterReserveResult::Ok);
    EXPECT_EQ(state.HeldBytes(kGame), ClusterChainBytes(100000, 20000, false));
}

TEST(ClusterDrawStateTests, TwoPhaseIsRefusedOnASharingState)
{
    ClusterDrawState state("TestShareTwoPhase", kSharedShape);
    EXPECT_EQ(state.TryReserveChainBytes(kGame, 0, 0, 1, 1000, true), ClusterReserveResult::BadIndex);
    EXPECT_EQ(state.CanReserveChainBytes(kGame, 0, 0, 1, 1000, true), ClusterReserveResult::BadIndex);
    EXPECT_EQ(state.HeldBytes(kGame), 0u);
    EXPECT_EQ(state.TryReserveChainBytes(kGame, 0, 0, 1, 1000, false), ClusterReserveResult::Ok);
}

TEST(ClusterDrawStateTests, SharedSlotsAreChargedSeparatelyAndScopesAreIndependent)
{
    ClusterDrawState state("TestShareScopes", { 4, 2, true });
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        ASSERT_EQ(state.TryReserveChainBytes(kGame, lane, 0, 50000, 1100000, false), ClusterReserveResult::Ok);
        ASSERT_EQ(state.TryReserveChainBytes(kScene, lane, 0, 50000, 1100000, false), ClusterReserveResult::Ok);
    }
    EXPECT_EQ(state.HeldBytes(kGame), 54526208u);
    EXPECT_EQ(state.HeldBytes(kScene), 54526208u);
    ASSERT_EQ(state.TryReserveChainBytes(kGame, 2, 1, 50000, 1100000, false), ClusterReserveResult::Ok);
    EXPECT_EQ(state.HeldBytes(kGame), 2u * 54526208u);
    EXPECT_EQ(state.HeldBytes(kScene), 54526208u);
}

TEST(ClusterDrawStateTests, TheProcessLedgerCountsASharedStateOncePerScope)
{
    constexpr std::uint64_t kInstances = 100000;
    constexpr std::uint64_t kWork = 2200000;
    constexpr std::uint64_t kOne = 109052160; // 104 MiB: one lane and the instance buffer.
    const std::uint64_t before = ProcessClusterLedger().Total();
    std::vector<std::unique_ptr<ClusterDrawState>> states;
    for (int i = 0; i < 4; ++i) {
        states.push_back(std::make_unique<ClusterDrawState>("TestShareProcess" + std::to_string(i), kSharedShape));
        for (std::uint32_t lane = 0; lane < 4; ++lane) {
            ASSERT_EQ(states.back()->TryReserveChainBytes(kGame, lane, 0, kInstances, kWork, false), ClusterReserveResult::Ok);
        }
    }
    EXPECT_EQ(ProcessClusterLedger().Total() - before, 4 * kOne);
    ClusterDrawState fifth("TestShareProcess5", kSharedShape);
    EXPECT_EQ(fifth.TryReserveChainBytes(kGame, 0, 0, kInstances, kWork, false), ClusterReserveResult::OverProcessBudget);
}

TEST(ClusterDrawStateTests, ADefaultShapeDoesNotShareLaneBuffers)
{
    EXPECT_FALSE(ClusterDrawShape{}.shareLaneBuffers);
    EXPECT_FALSE((ClusterDrawShape{ 4, 4 }).shareLaneBuffers);
    ClusterDrawState state("TestDefaultShape", { 4, 4 });
    EXPECT_FALSE(state.Shape().shareLaneBuffers);
}

TEST(HizPyramidTests, BaseAndTopPassNamesAreUniqueAndStableAcrossRebuilds)
{
    const HizPyramid pyramid("Test.Hiz");
    ASSERT_TRUE(pyramid.Valid());
    const std::vector<std::string> names = pyramid.AllPassNames();
    ASSERT_EQ(names.size(), static_cast<std::size_t>(kHizPassCount));
    EXPECT_EQ(std::set<std::string>(names.begin(), names.end()).size(), names.size());
    EXPECT_EQ(names.front(), "Test.Hiz.Base");
    EXPECT_EQ(names.back(), "Test.Hiz.Top");

    const std::size_t poolSize = [] {
        const HizPyramid again("Test.Hiz");
        return again.AllPassNames().size();
    }();
    EXPECT_EQ(poolSize, names.size());
    // The same base interns to the same pointers: a rebuilt pyramid does not grow the table.
    EXPECT_EQ(InternClusterName("Test.Hiz.Base"), InternClusterName("Test.Hiz.Base"));

    const HizPyramid other("Test.HizOther");
    for (const std::string& name : other.AllPassNames()) {
        EXPECT_EQ(std::count(names.begin(), names.end(), name), 0);
    }
}

TEST(HizPyramidTests, EmptyBaseNameIsRefused)
{
    const HizPyramid pyramid("");
    EXPECT_FALSE(pyramid.Valid());
    EXPECT_FALSE(pyramid.Error().empty());
    EXPECT_TRUE(pyramid.AllPassNames().empty());
}

} // namespace
} // namespace gte::fx
