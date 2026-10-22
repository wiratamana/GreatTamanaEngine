// Tier-1 tests for the pure parts of the cluster draw verb: grouping, slot assignment, the cone/pipeline
// pairing and the claim-after-proof flow with a refusing pass declarer. No device involved.

#include "Core/FeatureKit/ClusterPartPlanning.h"
#include "Renderer/Culling/ClusterCullMath.h"

#include <gtest/gtest.h>

#include <array>

namespace gte::fx {
namespace {

Entity MakeEntity(std::uint32_t index)
{
    Entity e;
    e.index = index;
    e.generation = 1;
    return e;
}

ClusterInstance MakeInstance(std::uint32_t entity, std::uint32_t meshIndex)
{
    ClusterInstance instance;
    instance.entity = MakeEntity(entity);
    instance.mesh = MeshToken{ meshIndex, 1 };
    return instance;
}

TEST(ClusterPartPlanningTest, GroupingKeepsFirstUseOrderAndInstanceOrder)
{
    const std::array<ClusterInstance, 5> candidates = { MakeInstance(1, 7), MakeInstance(2, 3), MakeInstance(3, 7),
        MakeInstance(4, 3), MakeInstance(5, 9) };
    const std::vector<ClusterMeshGroup> groups = GroupInstancesByMesh(candidates);
    ASSERT_EQ(groups.size(), 3u);
    EXPECT_EQ(groups[0].mesh.index, 7u);
    EXPECT_EQ(groups[1].mesh.index, 3u);
    EXPECT_EQ(groups[2].mesh.index, 9u);
    EXPECT_EQ(groups[0].instances, (std::vector<std::uint32_t>{ 0, 2 }));
    EXPECT_EQ(groups[1].instances, (std::vector<std::uint32_t>{ 1, 3 }));
}

TEST(ClusterPartPlanningTest, SlotsGoToTheMostWorkFirstAndTiesGoToTheLowerToken)
{
    const std::array<ClusterPartSize, 4> parts = {
        ClusterPartSize{ MeshToken{ 5, 1 }, 2, 10 }, // 20
        ClusterPartSize{ MeshToken{ 4, 1 }, 1, 20 }, // 20, lower token than the first
        ClusterPartSize{ MeshToken{ 6, 1 }, 100, 1 }, // 100
        ClusterPartSize{ MeshToken{ 7, 1 }, 1, 1 }, // 1
    };
    const ClusterSlotAssignment assignment = AssignClusterPartSlots(parts, 3);
    EXPECT_EQ(assignment.assigned, (std::vector<std::uint32_t>{ 2, 1, 0 }));
    EXPECT_EQ(assignment.dropped, (std::vector<std::uint32_t>{ 3 }));
}

TEST(ClusterPartPlanningTest, NoSlotsDropsEveryPart)
{
    const std::array<ClusterPartSize, 2> parts = { ClusterPartSize{ MeshToken{ 1, 1 }, 1, 1 },
        ClusterPartSize{ MeshToken{ 2, 1 }, 1, 1 } };
    const ClusterSlotAssignment assignment = AssignClusterPartSlots(parts, 0);
    EXPECT_TRUE(assignment.assigned.empty());
    EXPECT_EQ(assignment.dropped.size(), 2u);
}

TEST(ClusterPartPlanningTest, ADepthOnlyLaneNeverGetsTheConeTestOrBackFaceCulling)
{
    const ClusterCullFlags requested = ClusterCullFlags::Frustum | ClusterCullFlags::BackfaceCone;
    const ClusterCullFlags effective = EffectiveClusterCullFlags(requested, true);
    EXPECT_FALSE(HasClusterCullFlag(effective, ClusterCullFlags::BackfaceCone));
    EXPECT_FALSE(ClusterPipelineKeyFor(effective, true, false).cullBackFaces);
    EXPECT_EQ(ClusterPipelineKeyFor(effective, true, true).variant, ClusterPipelineVariant::DepthOnly);
}

// A declarer that refuses one position of the chain.
struct FakeDeclarer {
    enum class Refuse { Nothing, Reset, Cull, Draw };
    Refuse refuse = Refuse::Nothing;
    int calls = 0;

    bool Reset()
    {
        ++calls;
        return refuse != Refuse::Reset;
    }
    bool Cull()
    {
        ++calls;
        return refuse != Refuse::Cull;
    }
    bool Draw()
    {
        ++calls;
        return refuse != Refuse::Draw;
    }
};

TEST(ClusterPartPlanningTest, AChainStopsAtTheFirstRefusal)
{
    struct Case {
        FakeDeclarer::Refuse refuse;
        ClusterChainProgress progress;
        int calls;
    };
    const Case cases[] = {
        { FakeDeclarer::Refuse::Reset, ClusterChainProgress::None, 1 },
        { FakeDeclarer::Refuse::Cull, ClusterChainProgress::Reset, 2 },
        { FakeDeclarer::Refuse::Draw, ClusterChainProgress::Cull, 3 },
        { FakeDeclarer::Refuse::Nothing, ClusterChainProgress::Draw, 3 },
    };
    for (const Case& c : cases) {
        FakeDeclarer declarer;
        declarer.refuse = c.refuse;
        EXPECT_EQ(RecordClusterChain(declarer), c.progress);
        EXPECT_EQ(declarer.calls, c.calls);
    }
}

const std::array<Entity, 2> kPartA = { MakeEntity(1), MakeEntity(2) };
const std::array<Entity, 1> kPartB = { MakeEntity(3) };

ClusterClaimContext GameContext()
{
    ClusterClaimContext context;
    context.scope = rg::ViewScope::GameView;
    return context;
}

TEST(ClusterPartPlanningTest, ARefusalAtAnyPositionLeavesThatPartUnclaimed)
{
    for (const FakeDeclarer::Refuse refuse :
        { FakeDeclarer::Refuse::Reset, FakeDeclarer::Refuse::Cull, FakeDeclarer::Refuse::Draw }) {
        const std::array<ClusterPartPlan, 2> parts = { ClusterPartPlan{ kPartA }, ClusterPartPlan{ kPartB } };
        const ClusterRecordOutcome outcome = RecordClusterParts(parts, GameContext(), [&](std::size_t index) {
            FakeDeclarer declarer;
            declarer.refuse = index == 0 ? refuse : FakeDeclarer::Refuse::Nothing;
            return RecordClusterChain(declarer);
        });
        ASSERT_EQ(outcome.claimed.size(), 1u);
        EXPECT_EQ(outcome.claimed[0], MakeEntity(3));
        EXPECT_NE(outcome.progress[0], ClusterChainProgress::Draw);
        EXPECT_EQ(outcome.progress[1], ClusterChainProgress::Draw);
    }
}

TEST(ClusterPartPlanningTest, AnAcceptingDeclarerClaimsEveryPart)
{
    const std::array<ClusterPartPlan, 2> parts = { ClusterPartPlan{ kPartA }, ClusterPartPlan{ kPartB } };
    const ClusterRecordOutcome outcome = RecordClusterParts(parts, GameContext(), [](std::size_t) {
        FakeDeclarer declarer;
        return RecordClusterChain(declarer);
    });
    EXPECT_EQ(outcome.claimed.size(), 3u);
}

TEST(ClusterPartPlanningTest, ASharedScopeFrameClaimsNothing)
{
    ClusterClaimContext context;
    context.scope = rg::ViewScope::Shared;
    const std::array<ClusterPartPlan, 1> parts = { ClusterPartPlan{ kPartA } };
    const ClusterRecordOutcome outcome = RecordClusterParts(parts, context, [](std::size_t) {
        FakeDeclarer declarer;
        return RecordClusterChain(declarer);
    });
    EXPECT_TRUE(outcome.claimed.empty());
}

TEST(ClusterPartPlanningTest, AFrameWithACaptureArmedClaimsNothing)
{
    ClusterClaimContext context = GameContext();
    context.captureArmed = true;
    const std::array<ClusterPartPlan, 1> parts = { ClusterPartPlan{ kPartA } };
    const ClusterRecordOutcome outcome = RecordClusterParts(parts, context, [](std::size_t) {
        FakeDeclarer declarer;
        return RecordClusterChain(declarer);
    });
    EXPECT_TRUE(outcome.claimed.empty());
}

TEST(ClusterPartPlanningTest, APartThatIsNotReadyIsNeverDeclared)
{
    ClusterPartPlan notReady{ kPartA };
    notReady.planReady = false;
    const std::array<ClusterPartPlan, 1> parts = { notReady };
    int declared = 0;
    const ClusterRecordOutcome outcome = RecordClusterParts(parts, GameContext(), [&](std::size_t) {
        ++declared;
        return ClusterChainProgress::Draw;
    });
    EXPECT_EQ(declared, 0);
    EXPECT_TRUE(outcome.claimed.empty());
}

// A declarer that also has the stats steps; each step can be refused.
struct StatsFakeDeclarer : FakeDeclarer {
    enum class RefuseStats { Nothing, Clear, Compute, Copy };
    RefuseStats refuseStats = RefuseStats::Nothing;
    int statsCalls = 0;

    bool StatsClear()
    {
        ++statsCalls;
        return refuseStats != RefuseStats::Clear;
    }
    bool StatsCompute()
    {
        ++statsCalls;
        return refuseStats != RefuseStats::Compute;
    }
    bool StatsCopy()
    {
        ++statsCalls;
        return refuseStats != RefuseStats::Copy;
    }
};

TEST(ClusterPartPlanningTest, AStatsRefusalDoesNotRemoveTheClaim)
{
    for (const StatsFakeDeclarer::RefuseStats refuse :
        { StatsFakeDeclarer::RefuseStats::Clear, StatsFakeDeclarer::RefuseStats::Compute,
            StatsFakeDeclarer::RefuseStats::Copy }) {
        const std::array<ClusterPartPlan, 2> parts = { ClusterPartPlan{ kPartA }, ClusterPartPlan{ kPartB } };
        const ClusterRecordOutcome outcome = RecordClusterParts(parts, GameContext(), [&](std::size_t) {
            StatsFakeDeclarer declarer;
            declarer.refuseStats = refuse;
            return RecordClusterPart(declarer, true);
        });
        EXPECT_EQ(outcome.claimed.size(), 3u);
        EXPECT_EQ(outcome.progress[0], ClusterChainProgress::Draw);
    }
}

TEST(ClusterPartPlanningTest, StatsRunOnlyAfterADeclaredDrawAndOnlyWhenWanted)
{
    StatsFakeDeclarer wanted;
    EXPECT_EQ(RecordClusterPart(wanted, true), ClusterChainProgress::Draw);
    EXPECT_EQ(wanted.statsCalls, 3);

    StatsFakeDeclarer notWanted;
    EXPECT_EQ(RecordClusterPart(notWanted, false), ClusterChainProgress::Draw);
    EXPECT_EQ(notWanted.statsCalls, 0);

    StatsFakeDeclarer drawRefused;
    drawRefused.refuse = FakeDeclarer::Refuse::Draw;
    EXPECT_EQ(RecordClusterPart(drawRefused, true), ClusterChainProgress::Cull);
    EXPECT_EQ(drawRefused.statsCalls, 0);
}

TEST(ClusterPartPlanningTest, StatsStopAtTheFirstRefusedStep)
{
    StatsFakeDeclarer declarer;
    declarer.refuseStats = StatsFakeDeclarer::RefuseStats::Clear;
    EXPECT_FALSE(RecordClusterStats(declarer));
    EXPECT_EQ(declarer.statsCalls, 1);
}

TEST(ClusterPartPlanningTest, StatsAreNeverWantedForADepthOnlyTargetOrALaterLane)
{
    EXPECT_TRUE(ClusterStatsWanted(true, false, 0));
    EXPECT_FALSE(ClusterStatsWanted(true, true, 0));
    EXPECT_FALSE(ClusterStatsWanted(true, false, 1));
    EXPECT_FALSE(ClusterStatsWanted(false, false, 0));
}

TEST(ClusterPartPlanningTest, VisibleCountsSumTheValidCellsAndKeepTheOldestFrame)
{
    const std::array<ClusterCountSample, 3> samples = { ClusterCountSample{ true, 10, 7 },
        ClusterCountSample{ false, 999, 1 }, ClusterCountSample{ true, 5, 6 } };
    const ClusterVisibleCount total = SumClusterCounts(samples);
    EXPECT_TRUE(total.valid);
    EXPECT_EQ(total.value, 15u);
    EXPECT_EQ(total.frame, 6u);
}

TEST(ClusterPartPlanningTest, VisibleCountsAreInvalidWithoutAValidCell)
{
    const std::array<ClusterCountSample, 2> none = { ClusterCountSample{}, ClusterCountSample{} };
    EXPECT_FALSE(SumClusterCounts(none).valid);
    EXPECT_FALSE(SumClusterCounts({}).valid);
}

} // namespace
} // namespace gte::fx
