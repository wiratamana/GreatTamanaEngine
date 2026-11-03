// Tests for the pure parts of the Furina showcase opener: feature plan, model measuring and reply keys. No device.

#include "Editor/FurinaShowcaseSceneOpener.h"
#include "Game/Animation/SkeletalRigCache.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace gte {
namespace {

RenderFeatureDebugEntry Feature(const char* name, bool enabled)
{
    RenderFeatureDebugEntry entry;
    entry.name = name;
    entry.enabled = enabled;
    return entry;
}

TEST(FurinaFeatureSwitchTest, TurnsOffOnlyEnabledHeavyPassesAndOnlyEnabledOnesAreNotListed)
{
    const std::vector<RenderFeatureDebugEntry> features{ Feature("Gtao.Occlusion", true), Feature("Gtao.Composite", false),
        Feature("Gtao.FrameStamp", true), Feature("AtmosphereAerialVolume", true), Feature("DrawSkyBackground", true) };
    const FurinaFeatureSwitch plan = PlanFurinaFeatureSwitch(features);
    EXPECT_TRUE(plan.turnOn.empty());
    ASSERT_EQ(plan.turnOff.size(), 2u);
    EXPECT_EQ(plan.turnOff[0], "Gtao.Occlusion");
    EXPECT_EQ(plan.turnOff[1], "AtmosphereAerialVolume");
}

TEST(FurinaFeatureSwitchTest, TurnsOnDisabledLookFeaturesByPrefix)
{
    const std::vector<RenderFeatureDebugEntry> features{ Feature("Shadow.Cascades", false), Feature("ToneMapping", false),
        Feature("ToonCharacter.Surface", false), Feature("Bloom", false), Feature("SceneLighting.Uniforms", true),
        Feature("Ssgi.Trace", true), Feature("GpuDrivenScene.Opaque", false) };
    const FurinaFeatureSwitch plan = PlanFurinaFeatureSwitch(features);
    ASSERT_EQ(plan.turnOn.size(), 4u);
    EXPECT_EQ(plan.turnOn[0], "Shadow.Cascades");
    EXPECT_EQ(plan.turnOn[1], "ToneMapping");
    EXPECT_EQ(plan.turnOn[2], "ToonCharacter.Surface");
    EXPECT_EQ(plan.turnOn[3], "Bloom");
    ASSERT_EQ(plan.turnOff.size(), 1u);
    EXPECT_EQ(plan.turnOff[0], "Ssgi.Trace");
}

TEST(FurinaFeatureSwitchTest, FeaturesThatAreNotRegisteredAreSkipped)
{
    const FurinaFeatureSwitch plan = PlanFurinaFeatureSwitch({});
    EXPECT_TRUE(plan.turnOn.empty());
    EXPECT_TRUE(plan.turnOff.empty());
}

TEST(FurinaMeasureTest, ReadsBindPoseHeightsAndTheHipBone)
{
    SkinnedMeshData skin;
    skin.bindPositions = { Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 1.0f, 19.5f, 0.0f }, Vec3{ -1.0f, 8.0f, 2.0f } };
    Bone other;
    other.name = "center";
    other.position = Vec3{ 0.0f, 9.0f, 0.0f };
    Bone hip;
    hip.englishName = "lower body";
    hip.position = Vec3{ 0.0f, 10.5f, 0.0f };
    skin.skeleton.bones = { other, hip };

    const FurinaShowcaseModel model = MeasureFurinaModel(skin);
    EXPECT_FLOAT_EQ(model.minY, 0.0f);
    EXPECT_FLOAT_EQ(model.maxY, 19.5f);
    EXPECT_FLOAT_EQ(model.hipY, 10.5f);
}

TEST(FurinaMeasureTest, AMissingHipBoneGivesNanAndTheLayoutFallsBack)
{
    SkinnedMeshData skin;
    skin.bindPositions = { Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 0.0f, 20.0f, 0.0f } };
    const FurinaShowcaseModel model = MeasureFurinaModel(skin);
    EXPECT_TRUE(std::isnan(model.hipY));
    EXPECT_TRUE(ComputeFurinaShowcaseLayout(model).valid);
}

TEST(FurinaMeasureTest, NoVerticesGivesAnInvalidLayout)
{
    EXPECT_FALSE(ComputeFurinaShowcaseLayout(MeasureFurinaModel(SkinnedMeshData{})).valid);
}

TEST(FurinaReplyTest, DetailsCarryEveryDocumentedKey)
{
    FurinaShowcaseReport report;
    report.success = true;
    report.model = Entity{ 2, 1 };
    report.camera = Entity{ 3, 0 };
    report.sun = Entity{ 4, 5 };
    report.modelHeight = 19.5f;
    report.cameraDistance = 24.0f;
    report.animationStarted = true;
    report.featuresSwitchedOn = 2;
    report.featuresSwitchedOff = 6;

    const nlohmann::json json = nlohmann::json::parse(BuildFurinaShowcaseDetailsJson(report));
    ASSERT_TRUE(json.is_object());
    EXPECT_EQ(json.size(), 6u);
    EXPECT_EQ(json["entities"].size(), 3u);
    EXPECT_EQ(json["entities"]["model"], "2/1");
    EXPECT_EQ(json["entities"]["camera"], "3/0");
    EXPECT_EQ(json["entities"]["sun"], "4/5");
    EXPECT_NEAR(json["modelHeight"].get<double>(), 19.5, 1e-3);
    EXPECT_NEAR(json["cameraDistance"].get<double>(), 24.0, 1e-3);
    EXPECT_EQ(json["animationStarted"], true);
    EXPECT_EQ(json["featuresSwitchedOn"], 2);
    EXPECT_EQ(json["featuresSwitchedOff"], 6);
    EXPECT_EQ(json["restoresOnClose"], true);
}

TEST(FurinaReleaseTest, ReleaseIsANoOpWhenTheShowcaseIsNotActive)
{
    FurinaShowcaseState state;
    FurinaShowcaseServices services;
    services.state = &state;
    EXPECT_TRUE(ReleaseFurinaShowcaseSettings(services));
    EXPECT_TRUE(ReleaseFurinaShowcaseSettings(FurinaShowcaseServices{}));
}

} // namespace
} // namespace gte
