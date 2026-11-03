// Tests for the Inspector's Toon Shading mark rules and feedback. No ImGui, no device.

#include "Editor/Panels/ToonShadingInspectorLogic.h"

#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/ToonShading.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

#include <string>

namespace gte {
namespace {

Entity AddNode(Registry& registry, Entity parent = kInvalidEntity)
{
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity).parent = parent;
    return entity;
}

Entity AddMeshNode(Registry& registry, Entity parent = kInvalidEntity)
{
    const Entity entity = AddNode(registry, parent);
    registry.AddComponent<MeshRenderer>(entity);
    return entity;
}

// A model root with two mesh parts, like an imported character.
struct Model {
    Registry registry;
    Entity root;
    Entity partA;
    Entity partB;

    Model()
    {
        root = AddNode(registry);
        partA = AddMeshNode(registry, root);
        partB = AddMeshNode(registry, root);
    }
};

ToonFeatureReport DrawingReport()
{
    ToonFeatureReport report;
    report.published = true;
    report.graphEnabled = true;
    report.status = ToonCharacterFrameStatus::Ok;
    report.candidatePartCount = 32;
    report.claimedEntityCount = 32;
    return report;
}

TEST(ToonMarkTest, RootWithMeshPartsCanBeMarkedAndGetsAnEnabledComponent)
{
    Model model;
    EXPECT_EQ(EvaluateToonMark(model.registry, model.root), ToonMarkVerdict::CanMark);
    ASSERT_TRUE(EnableToonShading(model.registry, model.root));
    const ToonShading* toon = model.registry.TryGetComponent<ToonShading>(model.root);
    ASSERT_NE(toon, nullptr);
    EXPECT_TRUE(toon->enabled);
}

TEST(ToonMarkTest, MarkingTwiceKeepsOneComponent)
{
    Model model;
    ASSERT_TRUE(EnableToonShading(model.registry, model.root));
    model.registry.GetComponent<ToonShading>(model.root).enabled = false;

    EXPECT_EQ(EvaluateToonMark(model.registry, model.root), ToonMarkVerdict::AlreadyMarked);
    EXPECT_FALSE(EnableToonShading(model.registry, model.root));
    EXPECT_EQ(model.registry.Storage<ToonShading>().Size(), 1u);
    EXPECT_FALSE(model.registry.GetComponent<ToonShading>(model.root).enabled); // untouched
}

TEST(ToonMarkTest, RemoveDeletesTheMarkOnceAndAllowsMarkingAgain)
{
    Model model;
    EXPECT_FALSE(RemoveToonShading(model.registry, model.root)); // nothing to remove yet
    ASSERT_TRUE(EnableToonShading(model.registry, model.root));

    EXPECT_TRUE(RemoveToonShading(model.registry, model.root));
    EXPECT_FALSE(model.registry.HasComponent<ToonShading>(model.root));
    EXPECT_FALSE(RemoveToonShading(model.registry, model.root));
    EXPECT_EQ(EvaluateToonMark(model.registry, model.root), ToonMarkVerdict::CanMark);
}

TEST(ToonMarkTest, DeadEntityIsRefusedEverywhere)
{
    Model model;
    ASSERT_TRUE(EnableToonShading(model.registry, model.root));
    const Entity dead = model.root;
    model.registry.DestroyEntity(dead);

    EXPECT_EQ(EvaluateToonMark(model.registry, dead), ToonMarkVerdict::DeadEntity);
    EXPECT_FALSE(EnableToonShading(model.registry, dead));
    EXPECT_FALSE(RemoveToonShading(model.registry, dead));
    EXPECT_EQ(EvaluateToonMark(model.registry, kInvalidEntity), ToonMarkVerdict::DeadEntity);
}

TEST(ToonMarkTest, ChildPartIsRefusedSoItCannotOverrideTheRoot)
{
    Model model;
    EXPECT_EQ(EvaluateToonMark(model.registry, model.partA), ToonMarkVerdict::HasParent);
    EXPECT_FALSE(EnableToonShading(model.registry, model.partA));
    EXPECT_FALSE(model.registry.HasComponent<ToonShading>(model.partA));
    EXPECT_NE(ToonMarkHint(ToonMarkVerdict::HasParent), nullptr);
}

TEST(ToonMarkTest, RootWithoutAnyMeshIsRefusedQuietly)
{
    Registry registry;
    const Entity camera = AddNode(registry);
    AddNode(registry, camera);

    EXPECT_EQ(EvaluateToonMark(registry, camera), ToonMarkVerdict::NoMeshInside);
    EXPECT_FALSE(EnableToonShading(registry, camera));
    EXPECT_EQ(ToonMarkHint(ToonMarkVerdict::NoMeshInside), nullptr);
}

TEST(ToonMarkTest, SingleMeshRootAndTransformlessEntityCanBeMarked)
{
    Registry registry;
    const Entity single = AddMeshNode(registry);
    EXPECT_EQ(EvaluateToonMark(registry, single), ToonMarkVerdict::CanMark);

    const Entity bare = registry.CreateEntity();
    registry.AddComponent<MeshRenderer>(bare);
    EXPECT_EQ(EvaluateToonMark(registry, bare), ToonMarkVerdict::CanMark);
}

TEST(ToonMarkTest, MeshDeepInTheHierarchyCountsAsInside)
{
    Registry registry;
    const Entity root = AddNode(registry);
    const Entity middle = AddNode(registry, root);
    AddMeshNode(registry, middle);
    EXPECT_EQ(EvaluateToonMark(registry, root), ToonMarkVerdict::CanMark);
}

TEST(ToonMarkTest, RootWithAMarkedChildIsRefused)
{
    Model model;
    model.registry.AddComponent<ToonShading>(model.partB, ToonShading{ .enabled = false });

    EXPECT_EQ(EvaluateToonMark(model.registry, model.root), ToonMarkVerdict::MarkedPartInside);
    EXPECT_FALSE(EnableToonShading(model.registry, model.root));
    EXPECT_FALSE(model.registry.HasComponent<ToonShading>(model.root));
    EXPECT_NE(ToonMarkHint(ToonMarkVerdict::MarkedPartInside), nullptr);

    // Existing child marks can still be removed from the Inspector.
    EXPECT_TRUE(RemoveToonShading(model.registry, model.partB));
    EXPECT_EQ(EvaluateToonMark(model.registry, model.root), ToonMarkVerdict::CanMark);
}

TEST(ToonMarkTest, ADeadParentMakesTheEntityARoot)
{
    Registry registry;
    const Entity parent = AddNode(registry);
    const Entity part = AddMeshNode(registry, parent);
    registry.DestroyEntity(parent);
    EXPECT_EQ(EvaluateToonMark(registry, part), ToonMarkVerdict::CanMark);
}

TEST(ToonFeedbackTest, OkOnlyWhenThePassDrewParts)
{
    ToonFeatureReport report = DrawingReport();
    EXPECT_EQ(EvaluateToonFeedback(report, true).level, ToonFeedbackLevel::Ok);

    report.claimedEntityCount = 0;
    EXPECT_EQ(EvaluateToonFeedback(report, true).level, ToonFeedbackLevel::Warning);
}

TEST(ToonFeedbackTest, SwitchedOffOrMissingFeatureWarns)
{
    ToonFeatureReport report = DrawingReport();
    report.graphEnabled = false;
    EXPECT_EQ(EvaluateToonFeedback(report, true).level, ToonFeedbackLevel::Warning);

    report = DrawingReport();
    report.published = false;
    EXPECT_EQ(EvaluateToonFeedback(report, true).level, ToonFeedbackLevel::Warning);

    report = DrawingReport();
    report.status = ToonCharacterFrameStatus::FeatureDisabled;
    EXPECT_EQ(EvaluateToonFeedback(report, true).level, ToonFeedbackLevel::Warning);
}

TEST(ToonFeedbackTest, EveryNonOkStatusIsNeverShownAsOk)
{
    using Status = ToonCharacterFrameStatus;
    for (const Status status : { Status::NotRendered, Status::FeatureDisabled, Status::NoToonParts,
             Status::PassDisabled, Status::CaptureArmed, Status::DrawRefused }) {
        ToonFeatureReport report = DrawingReport();
        report.status = status;
        const ToonFeedback feedback = EvaluateToonFeedback(report, true);
        EXPECT_NE(feedback.level, ToonFeedbackLevel::Ok) << static_cast<int>(status);
        EXPECT_FALSE(std::string(feedback.text).empty());
    }
}

TEST(ToonFeedbackTest, ADisabledComponentIsReportedBeforeTheFeatureState)
{
    ToonFeatureReport report = DrawingReport();
    report.graphEnabled = false;
    const ToonFeedback feedback = EvaluateToonFeedback(report, false);
    EXPECT_EQ(feedback.level, ToonFeedbackLevel::Info);
}

} // namespace
} // namespace gte
