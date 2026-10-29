// Tier-1 tests for the pure parts of the cluster draw verb: grouping, slot assignment, the cone/pipeline
// pairing, the round order of a two-phase chain and the claim-after-proof flow with a refusing declarer.
// No device involved.

#include "Core/FeatureKit/ClusterPartPlanning.h"
#include "Core/FeatureKit/ClusterSurvivorTable.h"
#include "Renderer/Culling/ClusterCullMath.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace gte::fx {
namespace {

Entity MakeEntity(std::uint32_t index)
{
    Entity e;
    e.index = index;
    e.generation = 1;
    return e;
}

ClusterInstance MakeInstance(std::uint32_t entity, std::uint32_t meshIndex, std::uint32_t materialIndex = 0)
{
    ClusterInstance instance;
    instance.entity = MakeEntity(entity);
    instance.mesh = MeshToken{ meshIndex, 1 };
    if (materialIndex != 0) {
        instance.material = MaterialToken{ materialIndex, 1 };
        instance.kind = ClusterMaterialKind::Textured;
    }
    return instance;
}

TEST(ClusterPartPlanningTest, GroupingKeepsFirstUseOrderAndInstanceOrder)
{
    const std::array<ClusterInstance, 5> candidates = { MakeInstance(1, 7), MakeInstance(2, 3), MakeInstance(3, 7),
        MakeInstance(4, 3), MakeInstance(5, 9) };
    const std::vector<ClusterMeshGroup> groups = GroupInstances(candidates, false);
    ASSERT_EQ(groups.size(), 3u);
    EXPECT_EQ(groups[0].mesh.index, 7u);
    EXPECT_EQ(groups[1].mesh.index, 3u);
    EXPECT_EQ(groups[2].mesh.index, 9u);
    EXPECT_EQ(groups[0].instances, (std::vector<std::uint32_t>{ 0, 2 }));
    EXPECT_EQ(groups[1].instances, (std::vector<std::uint32_t>{ 1, 3 }));
}

TEST(ClusterPartPlanningTest, GroupingSplitsByMaterialOnlyWhenAsked)
{
    const std::array<ClusterInstance, 4> candidates = { MakeInstance(1, 7, 2), MakeInstance(2, 7, 3),
        MakeInstance(3, 7, 2), MakeInstance(4, 8, 2) };

    const std::vector<ClusterMeshGroup> split = GroupInstances(candidates, true);
    ASSERT_EQ(split.size(), 3u);
    EXPECT_EQ(split[0].material.index, 2u);
    EXPECT_EQ(split[0].kind, ClusterMaterialKind::Textured);
    EXPECT_EQ(split[0].instances, (std::vector<std::uint32_t>{ 0, 2 }));
    EXPECT_EQ(split[1].material.index, 3u);
    EXPECT_EQ(split[2].mesh.index, 8u);

    const std::vector<ClusterMeshGroup> merged = GroupInstances(candidates, false);
    ASSERT_EQ(merged.size(), 2u);
    EXPECT_FALSE(merged[0].material.IsValid());
    EXPECT_EQ(merged[0].kind, ClusterMaterialKind::Textured);
    EXPECT_EQ(merged[0].instances, (std::vector<std::uint32_t>{ 0, 1, 2 }));
}

TEST(ClusterPartPlanningTest, SlotsGoToTheMostWorkFirstAndTiesGoToTheLowerToken)
{
    const MaterialToken none;
    const std::array<ClusterPartSize, 4> parts = {
        ClusterPartSize{ MeshToken{ 5, 1 }, none, 2, 10 }, // 20
        ClusterPartSize{ MeshToken{ 4, 1 }, none, 1, 20 }, // 20, lower token than the first
        ClusterPartSize{ MeshToken{ 6, 1 }, none, 100, 1 }, // 100
        ClusterPartSize{ MeshToken{ 7, 1 }, none, 1, 1 }, // 1
    };
    const ClusterSlotAssignment assignment = AssignClusterPartSlots(parts, 3);
    EXPECT_EQ(assignment.assigned, (std::vector<std::uint32_t>{ 2, 1, 0 }));
    EXPECT_EQ(assignment.dropped, (std::vector<std::uint32_t>{ 3 }));
}

TEST(ClusterPartPlanningTest, PartsOfOneMeshTieOnTheLowerMaterial)
{
    const std::array<ClusterPartSize, 2> parts = { ClusterPartSize{ MeshToken{ 5, 1 }, MaterialToken{ 9, 1 }, 1, 4 },
        ClusterPartSize{ MeshToken{ 5, 1 }, MaterialToken{ 2, 1 }, 1, 4 } };
    const ClusterSlotAssignment assignment = AssignClusterPartSlots(parts, 1);
    EXPECT_EQ(assignment.assigned, (std::vector<std::uint32_t>{ 1 }));
    EXPECT_EQ(assignment.dropped, (std::vector<std::uint32_t>{ 0 }));
}

TEST(ClusterPartPlanningTest, NoSlotsDropsEveryPart)
{
    const MaterialToken none;
    const std::array<ClusterPartSize, 2> parts = { ClusterPartSize{ MeshToken{ 1, 1 }, none, 1, 1 },
        ClusterPartSize{ MeshToken{ 2, 1 }, none, 1, 1 } };
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

// A declarer that refuses one position of the chain and logs what was asked, in order.
struct FakeDeclarer {
    enum class Refuse { Nothing, Begin, Reset, Cull, Draw, Build };
    Refuse refuse = Refuse::Nothing;
    std::size_t refusePart = 0;
    std::uint32_t refuseRound = 0;
    std::string log;

    bool Hit(Refuse at, std::size_t part, std::uint32_t round) const
    {
        return refuse == at && refusePart == part && refuseRound == round;
    }
    bool Begin(std::size_t part)
    {
        log += "B" + std::to_string(part) + " ";
        return !Hit(Refuse::Begin, part, refuseRound);
    }
    bool Reset(std::size_t part, std::uint32_t round)
    {
        log += "R" + std::to_string(part) + "." + std::to_string(round) + " ";
        return !Hit(Refuse::Reset, part, round);
    }
    bool Cull(std::size_t part, std::uint32_t round)
    {
        log += "C" + std::to_string(part) + "." + std::to_string(round) + " ";
        return !Hit(Refuse::Cull, part, round);
    }
    bool Draw(std::size_t part, std::uint32_t round)
    {
        log += "D" + std::to_string(part) + "." + std::to_string(round) + " ";
        return !Hit(Refuse::Draw, part, round);
    }
    bool Build()
    {
        log += "P ";
        return refuse != Refuse::Build;
    }
};

TEST(ClusterPartPlanningTest, AProgressStepMapsToTheClaimRule)
{
    EXPECT_EQ(ClusterProgressOf(ClusterStep::None, false), ClusterChainProgress::None);
    EXPECT_EQ(ClusterProgressOf(ClusterStep::Reset1, false), ClusterChainProgress::Reset);
    EXPECT_EQ(ClusterProgressOf(ClusterStep::Cull1, false), ClusterChainProgress::Cull);
    EXPECT_EQ(ClusterProgressOf(ClusterStep::Draw1, false), ClusterChainProgress::Draw);
    // A two-phase chain is claimable only after its last step.
    for (const ClusterStep step : { ClusterStep::Draw1, ClusterStep::Reset2, ClusterStep::Cull2 }) {
        EXPECT_NE(ClusterProgressOf(step, true), ClusterChainProgress::Draw);
    }
    EXPECT_EQ(ClusterProgressOf(ClusterStep::Draw2, true), ClusterChainProgress::Draw);
}

TEST(ClusterPartPlanningTest, ASinglePhaseChainStopsAtTheFirstRefusal)
{
    struct Case {
        FakeDeclarer::Refuse refuse;
        ClusterStep step;
        const char* log;
    };
    const Case cases[] = {
        { FakeDeclarer::Refuse::Begin, ClusterStep::None, "B0 " },
        { FakeDeclarer::Refuse::Reset, ClusterStep::None, "B0 R0.0 " },
        { FakeDeclarer::Refuse::Cull, ClusterStep::Reset1, "B0 R0.0 C0.0 " },
        { FakeDeclarer::Refuse::Draw, ClusterStep::Cull1, "B0 R0.0 C0.0 D0.0 " },
        { FakeDeclarer::Refuse::Nothing, ClusterStep::Draw1, "B0 R0.0 C0.0 D0.0 " },
    };
    for (const Case& c : cases) {
        FakeDeclarer declarer;
        declarer.refuse = c.refuse;
        const std::vector<ClusterStep> steps = RecordClusterRounds(declarer, 1, false);
        ASSERT_EQ(steps.size(), 1u);
        EXPECT_EQ(steps[0], c.step);
        EXPECT_EQ(declarer.log, c.log);
    }
}

TEST(ClusterPartPlanningTest, ATwoPhaseChainRunsRoundOneOfAllPartsThenTheBuildThenRoundTwo)
{
    FakeDeclarer declarer;
    const std::vector<ClusterStep> steps = RecordClusterRounds(declarer, 2, true);
    EXPECT_EQ(declarer.log, "B0 R0.0 C0.0 D0.0 B1 R1.0 C1.0 D1.0 P R0.1 C0.1 D0.1 R1.1 C1.1 D1.1 ");
    EXPECT_EQ(steps, (std::vector<ClusterStep>{ ClusterStep::Draw2, ClusterStep::Draw2 }));
}

TEST(ClusterPartPlanningTest, ARefusedBuildSkipsRoundTwoAndLeavesEveryPartUnclaimable)
{
    FakeDeclarer declarer;
    declarer.refuse = FakeDeclarer::Refuse::Build;
    const std::vector<ClusterStep> steps = RecordClusterRounds(declarer, 2, true);
    EXPECT_EQ(declarer.log, "B0 R0.0 C0.0 D0.0 B1 R1.0 C1.0 D1.0 P ");
    for (const ClusterStep step : steps) {
        EXPECT_EQ(step, ClusterStep::Draw1);
        EXPECT_NE(ClusterProgressOf(step, true), ClusterChainProgress::Draw);
    }
}

TEST(ClusterPartPlanningTest, NoBuildRunsWhenNoPartFinishedRoundOneOrTheChainIsSinglePhase)
{
    FakeDeclarer refusedDraw;
    refusedDraw.refuse = FakeDeclarer::Refuse::Draw;
    RecordClusterRounds(refusedDraw, 1, true);
    EXPECT_EQ(refusedDraw.log.find('P'), std::string::npos);

    FakeDeclarer single;
    RecordClusterRounds(single, 2, false);
    EXPECT_EQ(single.log.find('P'), std::string::npos);
    EXPECT_EQ(single.log.find(".1"), std::string::npos);
}

TEST(ClusterPartPlanningTest, ARefusalInOnePartNeverStopsTheOthers)
{
    FakeDeclarer roundOne;
    roundOne.refuse = FakeDeclarer::Refuse::Cull;
    roundOne.refusePart = 0;
    roundOne.refuseRound = 0;
    EXPECT_EQ(RecordClusterRounds(roundOne, 2, true), (std::vector<ClusterStep>{ ClusterStep::Reset1, ClusterStep::Draw2 }));
    EXPECT_EQ(roundOne.log.find("R0.1"), std::string::npos);

    FakeDeclarer roundTwo;
    roundTwo.refuse = FakeDeclarer::Refuse::Draw;
    roundTwo.refusePart = 0;
    roundTwo.refuseRound = 1;
    EXPECT_EQ(RecordClusterRounds(roundTwo, 2, true), (std::vector<ClusterStep>{ ClusterStep::Cull2, ClusterStep::Draw2 }));

    FakeDeclarer beginRefused;
    beginRefused.refuse = FakeDeclarer::Refuse::Begin;
    beginRefused.refusePart = 1;
    EXPECT_EQ(RecordClusterRounds(beginRefused, 2, true), (std::vector<ClusterStep>{ ClusterStep::Draw2, ClusterStep::None }));
}

const std::array<Entity, 2> kPartA = { MakeEntity(1), MakeEntity(2) };
const std::array<Entity, 1> kPartB = { MakeEntity(3) };

ClusterClaimContext GameContext()
{
    ClusterClaimContext context;
    context.scope = rg::ViewScope::Primary;
    return context;
}

// The claimed entities of an outcome in part order.
std::vector<Entity> FlatClaim(const ClusterRecordOutcome& outcome)
{
    std::vector<Entity> flat;
    for (const std::span<const Entity> span : outcome.claimed) {
        flat.insert(flat.end(), span.begin(), span.end());
    }
    return flat;
}

TEST(ClusterPartPlanningTest, OnlyPartsWithEveryStepDeclaredAreClaimed)
{
    const std::array<std::span<const Entity>, 2> parts = { std::span<const Entity>(kPartA), std::span<const Entity>(kPartB) };
    for (const bool twoPhase : { false, true }) {
        const ClusterStep last = twoPhase ? ClusterStep::Draw2 : ClusterStep::Draw1;
        const std::array<ClusterStep, 2> done = { last, last };
        EXPECT_EQ(FlatClaim(ClusterOutcomeOf(parts, GameContext(), done, twoPhase)).size(), 3u);

        const std::array<ClusterStep, 2> partial = { ClusterStep::Cull1, last };
        const ClusterRecordOutcome outcome = ClusterOutcomeOf(parts, GameContext(), partial, twoPhase);
        const std::vector<Entity> claimed = FlatClaim(outcome);
        ASSERT_EQ(claimed.size(), 1u);
        EXPECT_EQ(claimed[0], MakeEntity(3));
        EXPECT_NE(outcome.progress[0], ClusterChainProgress::Draw);
        EXPECT_EQ(outcome.progress[1], ClusterChainProgress::Draw);
    }
}

TEST(ClusterPartPlanningTest, AHalfFinishedTwoPhaseChainClaimsNothing)
{
    const std::array<std::span<const Entity>, 1> parts = { std::span<const Entity>(kPartA) };
    for (const ClusterStep step : { ClusterStep::Draw1, ClusterStep::Reset2, ClusterStep::Cull2 }) {
        const std::array<ClusterStep, 1> steps = { step };
        EXPECT_TRUE(ClusterOutcomeOf(parts, GameContext(), steps, true).claimed.empty());
    }
}

TEST(ClusterPartPlanningTest, ASharedScopeFrameClaimsNothing)
{
    ClusterClaimContext context;
    context.scope = rg::ViewScope::Shared;
    const std::array<std::span<const Entity>, 1> parts = { std::span<const Entity>(kPartA) };
    const std::array<ClusterStep, 1> steps = { ClusterStep::Draw1 };
    EXPECT_TRUE(ClusterOutcomeOf(parts, context, steps, false).claimed.empty());
}

TEST(ClusterPartPlanningTest, AFrameWithACaptureArmedClaimsNothing)
{
    ClusterClaimContext context = GameContext();
    context.captureArmed = true;
    const std::array<std::span<const Entity>, 1> parts = { std::span<const Entity>(kPartA) };
    const std::array<ClusterStep, 1> steps = { ClusterStep::Draw1 };
    EXPECT_TRUE(ClusterOutcomeOf(parts, context, steps, false).claimed.empty());
}

// A declarer that has the stats steps; each step can be refused.
struct StatsFakeDeclarer {
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

TEST(ClusterPartPlanningTest, StatsStopAtTheFirstRefusedStep)
{
    StatsFakeDeclarer declarer;
    declarer.refuseStats = StatsFakeDeclarer::RefuseStats::Clear;
    EXPECT_FALSE(RecordClusterStats(declarer));
    EXPECT_EQ(declarer.statsCalls, 1);

    StatsFakeDeclarer all;
    EXPECT_TRUE(RecordClusterStats(all));
    EXPECT_EQ(all.statsCalls, 3);
}

TEST(ClusterPartPlanningTest, StatsAreNeverWantedForADepthOnlyTargetOrALaterLane)
{
    EXPECT_TRUE(ClusterStatsWanted(true, false, 0));
    EXPECT_FALSE(ClusterStatsWanted(true, true, 0));
    EXPECT_FALSE(ClusterStatsWanted(true, false, 1));
    EXPECT_FALSE(ClusterStatsWanted(false, false, 0));
}

TEST(ClusterPartPlanningTest, HistoryIsTrustedOnlyWhenThePreviousFrameWroteItForTheSameContents)
{
    const ClusterHistoryRecord record{ 77, 10, true };
    EXPECT_TRUE(ClusterHistoryUsable(record, 77, 11));
    EXPECT_FALSE(ClusterHistoryUsable(record, 78, 11)); // Other mesh, instances or buffer.
    EXPECT_FALSE(ClusterHistoryUsable(record, 77, 10)); // Written this frame already.
    EXPECT_FALSE(ClusterHistoryUsable(record, 77, 12)); // A frame was skipped.
    EXPECT_FALSE(ClusterHistoryUsable(ClusterHistoryRecord{}, 0, 1)); // Never written or dropped.
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

TEST(ClusterPartPlanningTest, ReasonCountsSumTheValidCellsPerSlotOfTheCountBuffer)
{
    ClusterReasonSample a;
    a.valid = true;
    a.frame = 9;
    a.words.reasons[kClusterReasonFrustum] = 4;
    a.words.reasons[kClusterReasonCone] = 1;
    a.words.reasons[kClusterReasonPyramid] = 2;
    a.words.reasons[kClusterReasonHistory] = 3;
    a.words.reasons[kClusterReasonKept] = 10;
    ClusterReasonSample b = a;
    b.frame = 8;
    ClusterReasonSample stale;
    stale.words.reasons[kClusterReasonKept] = 999; // Not valid: ignored.

    const std::array<ClusterReasonSample, 3> samples = { a, stale, b };
    const ClusterReasonCounts total = SumClusterReasons(samples);
    EXPECT_TRUE(total.valid);
    EXPECT_EQ(total.frustum, 8u);
    EXPECT_EQ(total.cone, 2u);
    EXPECT_EQ(total.pyramid, 4u);
    EXPECT_EQ(total.history, 6u);
    EXPECT_EQ(total.kept, 20u);
    EXPECT_EQ(total.frame, 8u);
    EXPECT_FALSE(SumClusterReasons({}).valid);
}

TEST(ClusterSurvivorTableTest, AnEntryServesOnlyTheFrameAndGraphItWasPublishedFor)
{
    int graph = 0;
    int otherGraph = 0;
    ClusterSurvivorTable table;
    EXPECT_FALSE(table.IsFor(0, &graph)); // Empty.

    table.Begin(5, &graph, 2, 3u, Mat4::Identity(), Vec3());
    ClusterSurvivorPart part;
    part.mesh = MeshToken{ 1, 1 };
    part.setKey = 9;
    table.parts.push_back(part);

    EXPECT_TRUE(table.IsFor(5, &graph));
    EXPECT_FALSE(table.IsFor(6, &graph));
    EXPECT_FALSE(table.IsFor(5, &otherGraph));
    EXPECT_NE(table.Find(MeshToken{ 1, 1 }, 9), nullptr);
    EXPECT_EQ(table.Find(MeshToken{ 1, 1 }, 10), nullptr);
    EXPECT_EQ(table.Find(MeshToken{ 2, 1 }, 9), nullptr);

    table.Begin(6, &graph, 1, 3u, Mat4::Identity(), Vec3());
    EXPECT_EQ(table.Find(MeshToken{ 1, 1 }, 9), nullptr); // A new frame starts empty.
    table.Clear();
    EXPECT_FALSE(table.IsFor(6, &graph));
}

TEST(ClusterSurvivorTableTest, ASecondaryChainSharesOnlyWhenEverythingMatchesAndEveryPartIsCovered)
{
    EXPECT_EQ(ChooseSurvivorSource(true, true, true, 2, 2), ClusterSurvivorSource::Shared);
    EXPECT_EQ(ChooseSurvivorSource(false, true, true, 2, 2), ClusterSurvivorSource::Own);  // Not asked for.
    EXPECT_EQ(ChooseSurvivorSource(true, false, true, 2, 2), ClusterSurvivorSource::Own);  // Stale table.
    EXPECT_EQ(ChooseSurvivorSource(true, true, false, 2, 2), ClusterSurvivorSource::Own);  // Other camera or tests.
    EXPECT_EQ(ChooseSurvivorSource(true, true, true, 2, 1), ClusterSurvivorSource::Own);   // A part is missing.
    EXPECT_EQ(ChooseSurvivorSource(true, true, true, 0, 0), ClusterSurvivorSource::Own);   // Nothing to draw.
}

TEST(ClusterPartPlanningTest, SharedRoundsDrawRoundOneOfAllPartsThenRoundTwoWithoutCullOrBuild)
{
    FakeDeclarer two;
    const std::vector<ClusterStep> steps = RecordSharedRounds(two, 2, true);
    EXPECT_EQ(two.log, "B0 D0.0 B1 D1.0 D0.1 D1.1 ");
    EXPECT_EQ(steps, (std::vector<ClusterStep>{ ClusterStep::Draw2, ClusterStep::Draw2 }));

    FakeDeclarer one;
    EXPECT_EQ(RecordSharedRounds(one, 1, false), (std::vector<ClusterStep>{ ClusterStep::Draw1 }));
    EXPECT_EQ(one.log, "B0 D0.0 ");
}

TEST(ClusterPartPlanningTest, ARefusedSharedDrawLeavesThatPartUnclaimable)
{
    FakeDeclarer roundOne;
    roundOne.refuse = FakeDeclarer::Refuse::Draw;
    roundOne.refusePart = 0;
    roundOne.refuseRound = 0;
    EXPECT_EQ(RecordSharedRounds(roundOne, 2, true), (std::vector<ClusterStep>{ ClusterStep::None, ClusterStep::Draw2 }));

    FakeDeclarer roundTwo;
    roundTwo.refuse = FakeDeclarer::Refuse::Draw;
    roundTwo.refusePart = 1;
    roundTwo.refuseRound = 1;
    const std::vector<ClusterStep> steps = RecordSharedRounds(roundTwo, 2, true);
    EXPECT_EQ(steps, (std::vector<ClusterStep>{ ClusterStep::Draw2, ClusterStep::Draw1 }));
    EXPECT_NE(ClusterProgressOf(steps[1], true), ClusterChainProgress::Draw);

    FakeDeclarer begin;
    begin.refuse = FakeDeclarer::Refuse::Begin;
    begin.refusePart = 0;
    EXPECT_EQ(RecordSharedRounds(begin, 1, true), (std::vector<ClusterStep>{ ClusterStep::None }));
    EXPECT_EQ(begin.log, "B0 ");
}

} // namespace
} // namespace gte::fx
