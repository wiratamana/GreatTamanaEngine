// Unit tests for src/Assets/MotionFile.h - EncodeMotionDataToBytes()/
// DecodeMotionDataFromBytes() round-tripping the *.gta AssetType::Animation
// payload's binary layout. No GPU/SDL/ImGui involved - "Tier 1" per
// tests/CMakeLists.txt's own taxonomy.

#include "Assets/MotionFile.h"

#include <cstring>

#include <gtest/gtest.h>

namespace gte {
namespace {

MotionData BuildSampleMotionData()
{
    MotionData motion;
    motion.modelName = "SampleModel";

    BoneKeyframe bone1;
    bone1.boneName = "Bone1";
    bone1.frame = 0;
    bone1.translation = Vec3(0.0f, 0.0f, 0.0f);
    bone1.rotation = Quat::Identity();
    for (std::size_t i = 0; i < bone1.interpolation.size(); ++i) {
        bone1.interpolation[i] = static_cast<std::uint8_t>(i);
    }
    motion.boneKeyframes.push_back(bone1);

    BoneKeyframe bone2;
    bone2.boneName = "Bone2";
    bone2.frame = 10;
    bone2.translation = Vec3(1.5f, -2.5f, 0.5f);
    bone2.rotation = Quat(0.0f, 0.70710678f, 0.0f, 0.70710678f);
    bone2.interpolation.fill(200);
    motion.boneKeyframes.push_back(bone2);

    MorphKeyframe morph;
    morph.morphName = "MorphA";
    morph.frame = 3;
    morph.weight = 0.75f;
    motion.morphKeyframes.push_back(morph);

    CameraKeyframe cam;
    cam.frame = 7;
    cam.distance = -45.0f;
    cam.interest = Vec3(0.0f, 10.0f, 0.0f);
    cam.rotateRadians = Vec3(0.1f, 0.2f, 0.3f);
    cam.interpolation.fill(5);
    cam.fieldOfViewDegrees = 30;
    cam.isPerspective = true;
    motion.cameraKeyframes.push_back(cam);

    LightKeyframe light;
    light.frame = 8;
    light.color = Vec3(1.0f, 1.0f, 1.0f);
    light.direction = Vec3(0.0f, -1.0f, 0.0f);
    motion.lightKeyframes.push_back(light);

    ShadowKeyframe shadow;
    shadow.frame = 9;
    shadow.shadowType = 1;
    shadow.distance = 6.5f;
    motion.shadowKeyframes.push_back(shadow);

    IkKeyframe ik;
    ik.frame = 11;
    ik.visible = true;
    IkEnableState state1;
    state1.ikBoneName = "LeftLegIK";
    state1.enabled = true;
    ik.states.push_back(state1);
    IkEnableState state2;
    state2.ikBoneName = "RightLegIK";
    state2.enabled = false;
    ik.states.push_back(state2);
    motion.ikKeyframes.push_back(ik);

    return motion;
}

TEST(MotionFileTest, EncodeThenDecodeRoundTripsEveryTrackExactly)
{
    const MotionData original = BuildSampleMotionData();
    const std::vector<std::uint8_t> encoded = EncodeMotionDataToBytes(original);

    const std::optional<MotionData> decoded = DecodeMotionDataFromBytes(encoded);
    ASSERT_TRUE(decoded.has_value());

    EXPECT_EQ(decoded->modelName, original.modelName);

    ASSERT_EQ(decoded->boneKeyframes.size(), original.boneKeyframes.size());
    for (std::size_t i = 0; i < original.boneKeyframes.size(); ++i) {
        const auto& a = original.boneKeyframes[i];
        const auto& b = decoded->boneKeyframes[i];
        EXPECT_EQ(a.boneName, b.boneName);
        EXPECT_EQ(a.frame, b.frame);
        EXPECT_EQ(a.translation, b.translation);
        EXPECT_EQ(a.rotation.x, b.rotation.x);
        EXPECT_EQ(a.rotation.y, b.rotation.y);
        EXPECT_EQ(a.rotation.z, b.rotation.z);
        EXPECT_EQ(a.rotation.w, b.rotation.w);
        EXPECT_EQ(a.interpolation, b.interpolation);
    }

    ASSERT_EQ(decoded->morphKeyframes.size(), original.morphKeyframes.size());
    EXPECT_EQ(decoded->morphKeyframes[0].morphName, original.morphKeyframes[0].morphName);
    EXPECT_EQ(decoded->morphKeyframes[0].frame, original.morphKeyframes[0].frame);
    EXPECT_FLOAT_EQ(decoded->morphKeyframes[0].weight, original.morphKeyframes[0].weight);

    ASSERT_EQ(decoded->cameraKeyframes.size(), original.cameraKeyframes.size());
    EXPECT_EQ(decoded->cameraKeyframes[0].frame, original.cameraKeyframes[0].frame);
    EXPECT_FLOAT_EQ(decoded->cameraKeyframes[0].distance, original.cameraKeyframes[0].distance);
    EXPECT_EQ(decoded->cameraKeyframes[0].interest, original.cameraKeyframes[0].interest);
    EXPECT_EQ(decoded->cameraKeyframes[0].rotateRadians, original.cameraKeyframes[0].rotateRadians);
    EXPECT_EQ(decoded->cameraKeyframes[0].interpolation, original.cameraKeyframes[0].interpolation);
    EXPECT_EQ(decoded->cameraKeyframes[0].fieldOfViewDegrees, original.cameraKeyframes[0].fieldOfViewDegrees);
    EXPECT_EQ(decoded->cameraKeyframes[0].isPerspective, original.cameraKeyframes[0].isPerspective);

    ASSERT_EQ(decoded->lightKeyframes.size(), original.lightKeyframes.size());
    EXPECT_EQ(decoded->lightKeyframes[0].color, original.lightKeyframes[0].color);
    EXPECT_EQ(decoded->lightKeyframes[0].direction, original.lightKeyframes[0].direction);

    ASSERT_EQ(decoded->shadowKeyframes.size(), original.shadowKeyframes.size());
    EXPECT_EQ(decoded->shadowKeyframes[0].shadowType, original.shadowKeyframes[0].shadowType);
    EXPECT_FLOAT_EQ(decoded->shadowKeyframes[0].distance, original.shadowKeyframes[0].distance);

    ASSERT_EQ(decoded->ikKeyframes.size(), original.ikKeyframes.size());
    ASSERT_EQ(decoded->ikKeyframes[0].states.size(), original.ikKeyframes[0].states.size());
    EXPECT_EQ(decoded->ikKeyframes[0].visible, original.ikKeyframes[0].visible);
    EXPECT_EQ(decoded->ikKeyframes[0].states[0].ikBoneName, original.ikKeyframes[0].states[0].ikBoneName);
    EXPECT_EQ(decoded->ikKeyframes[0].states[0].enabled, original.ikKeyframes[0].states[0].enabled);
    EXPECT_EQ(decoded->ikKeyframes[0].states[1].enabled, original.ikKeyframes[0].states[1].enabled);
}

TEST(MotionFileTest, EncodesAnEmptyMotionAsAHeaderOnlyBlobThatDecodesBackToEmpty)
{
    const MotionData empty;
    const std::vector<std::uint8_t> encoded = EncodeMotionDataToBytes(empty);

    const std::optional<MotionData> decoded = DecodeMotionDataFromBytes(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->modelName.empty());
    EXPECT_TRUE(decoded->boneKeyframes.empty());
    EXPECT_TRUE(decoded->morphKeyframes.empty());
    EXPECT_TRUE(decoded->cameraKeyframes.empty());
    EXPECT_TRUE(decoded->lightKeyframes.empty());
    EXPECT_TRUE(decoded->shadowKeyframes.empty());
    EXPECT_TRUE(decoded->ikKeyframes.empty());
}

TEST(MotionFileTest, DecodeFailsOnEmptyBytes)
{
    EXPECT_FALSE(DecodeMotionDataFromBytes(std::vector<std::uint8_t>{}).has_value());
}

TEST(MotionFileTest, DecodeFailsOnGarbageBytes)
{
    const std::vector<std::uint8_t> garbage(64, 0xAB);
    EXPECT_FALSE(DecodeMotionDataFromBytes(garbage).has_value());
}

TEST(MotionFileTest, DecodeFailsOnATruncatedButOtherwiseValidBlob)
{
    const MotionData original = BuildSampleMotionData();
    std::vector<std::uint8_t> encoded = EncodeMotionDataToBytes(original);
    encoded.resize(encoded.size() - 4); // Chop off the last few bytes of the IK section.

    EXPECT_FALSE(DecodeMotionDataFromBytes(encoded).has_value());
}

TEST(MotionFileTest, DecodeFailsWhenMagicIsWrongEvenIfSizeWouldOtherwiseFit)
{
    MotionData original = BuildSampleMotionData();
    std::vector<std::uint8_t> encoded = EncodeMotionDataToBytes(original);
    encoded[0] = static_cast<std::uint8_t>(~encoded[0]); // Corrupt the magic's first byte.

    EXPECT_FALSE(DecodeMotionDataFromBytes(encoded).has_value());
}

} // namespace
} // namespace gte
