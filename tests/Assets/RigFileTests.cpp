// Unit tests for src/Assets/RigFile.h - EncodeRigDataToBytes()/
// DecodeRigDataFromBytes() round-tripping skin weights/bones/morphs/physics
// binary layout (the *.gta AssetType::Mesh METADATA section - see
// GtaFile.h). No GPU/SDL/ImGui involved - "Tier 1" per tests/CMakeLists.txt's
// own taxonomy.

#include "Assets/RigFile.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

RigFileData BuildSampleRigData()
{
    RigFileData rig;

    VertexSkinWeights sw0;
    sw0.type = VertexWeightType::BDEF1;
    sw0.boneIndices[0] = 0;
    sw0.boneWeights[0] = 1.0f;
    rig.skinWeights.push_back(sw0);

    VertexSkinWeights sw1;
    sw1.type = VertexWeightType::SDEF;
    sw1.boneIndices[0] = 0;
    sw1.boneIndices[1] = 1;
    sw1.boneWeights[0] = 0.6f;
    sw1.boneWeights[1] = 0.4f;
    sw1.sdefC = Vec3(0.1f, 0.2f, 0.3f);
    sw1.sdefR0 = Vec3(0.4f, 0.5f, 0.6f);
    sw1.sdefR1 = Vec3(0.7f, 0.8f, 0.9f);
    rig.skinWeights.push_back(sw1);

    Bone root;
    root.name = "Root";
    root.englishName = "Root_en";
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    root.rotatable = true;
    root.translatable = true;
    root.visible = true;
    root.controllable = true;
    root.tailOffset = Vec3(0.0f, 1.0f, 0.0f);
    rig.skeleton.bones.push_back(root);

    Bone tip;
    tip.name = "Tip";
    tip.parentBoneIndex = 0;
    tip.deformDepth = 1;
    tip.isIk = true;
    tip.ikTargetBoneIndex = 0;
    tip.ikIterationCount = 10;
    tip.ikAngleLimitRadians = 0.5f;
    Bone::IkLink link;
    link.boneIndex = 0;
    link.hasAngleLimit = true;
    link.angleLimitMin = Vec3(-1.0f, -1.0f, -1.0f);
    link.angleLimitMax = Vec3(1.0f, 1.0f, 1.0f);
    tip.ikLinks.push_back(link);
    rig.skeleton.bones.push_back(tip);

    Morph brow;
    brow.name = "Brow";
    brow.englishName = "Brow_en";
    brow.controlPanel = 1;
    brow.type = MorphType::Position;
    brow.positionOffsets.push_back({ 0, Vec3(0.1f, 0.0f, 0.0f) });
    brow.positionOffsets.push_back({ 1, Vec3(0.0f, 0.1f, 0.0f) });
    rig.morphs.morphs.push_back(brow);

    Morph mat;
    mat.name = "MatTweak";
    mat.type = MorphType::Material;
    Morph::MaterialOffset matOffset;
    matOffset.materialIndex = -1;
    matOffset.op = Morph::MaterialOffset::OpType::Add;
    matOffset.diffuse = Vec4(0.1f, 0.2f, 0.3f, 0.4f);
    matOffset.specularPower = 5.0f;
    mat.materialOffsets.push_back(matOffset);
    rig.morphs.morphs.push_back(mat);

    RigidBody body;
    body.name = "RB0";
    body.boneIndex = 1;
    body.group = 3;
    body.collisionGroupMask = 0xFFFF;
    body.shape = RigidBodyShape::Box;
    body.shapeSize = Vec3(0.5f, 0.5f, 0.5f);
    body.translate = Vec3(0.0f, 1.0f, 0.0f);
    body.mass = 1.0f;
    body.restitution = 0.1f;
    body.friction = 0.2f;
    body.motionType = RigidBodyMotionType::Dynamic;
    rig.physics.rigidBodies.push_back(body);

    Joint joint;
    joint.name = "J0";
    joint.type = JointType::Hinge;
    joint.rigidBodyAIndex = 0;
    joint.rigidBodyBIndex = -1;
    joint.rotateLowerLimit = Vec3(-1.0f, 0.0f, 0.0f);
    joint.rotateUpperLimit = Vec3(1.0f, 0.0f, 0.0f);
    rig.physics.joints.push_back(joint);

    // --- Materials / textures (Guid-referenced texture slots) ---
    MaterialTextureRef importedTexture;
    importedTexture.sourcePath = "C:\\Source\\body.png"; // Diagnostic only - never read at load time.
    importedTexture.guid = Guid::Parse("0123456789abcdef0123456789abcdef");
    rig.materials.textures.push_back(importedTexture);

    MaterialTextureRef unresolvedTexture; // Never actually imported - guid stays Guid::Invalid().
    unresolvedTexture.sourcePath = "C:\\Source\\missing.png";
    rig.materials.textures.push_back(unresolvedTexture);

    Material material;
    material.name = "Body";
    material.textureIndex = 0;
    material.indexCount = 6;
    rig.materials.materials.push_back(material);

    return rig;
}

TEST(RigFileTest, EncodeThenDecodeRoundTripsSkinWeightsBonesMorphsAndPhysics)
{
    const RigFileData original = BuildSampleRigData();
    const std::vector<std::uint8_t> encoded = EncodeRigDataToBytes(original);

    const std::optional<RigFileData> decoded = DecodeRigDataFromBytes(encoded);
    ASSERT_TRUE(decoded.has_value());

    // --- Skin weights ---
    ASSERT_EQ(decoded->skinWeights.size(), 2u);
    EXPECT_EQ(decoded->skinWeights[0].type, VertexWeightType::BDEF1);
    EXPECT_EQ(decoded->skinWeights[0].boneIndices[0], 0);
    EXPECT_FLOAT_EQ(decoded->skinWeights[0].boneWeights[0], 1.0f);
    EXPECT_EQ(decoded->skinWeights[1].type, VertexWeightType::SDEF);
    EXPECT_EQ(decoded->skinWeights[1].sdefC, Vec3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(decoded->skinWeights[1].sdefR0, Vec3(0.4f, 0.5f, 0.6f));
    EXPECT_EQ(decoded->skinWeights[1].sdefR1, Vec3(0.7f, 0.8f, 0.9f));

    // --- Bones ---
    ASSERT_EQ(decoded->skeleton.bones.size(), 2u);
    EXPECT_EQ(decoded->skeleton.bones[0].name, "Root");
    EXPECT_EQ(decoded->skeleton.bones[0].englishName, "Root_en");
    EXPECT_EQ(decoded->skeleton.bones[0].parentBoneIndex, -1);
    EXPECT_TRUE(decoded->skeleton.bones[0].rotatable);
    EXPECT_EQ(decoded->skeleton.bones[0].tailOffset, Vec3(0.0f, 1.0f, 0.0f));

    const Bone& tip = decoded->skeleton.bones[1];
    EXPECT_EQ(tip.name, "Tip");
    EXPECT_EQ(tip.parentBoneIndex, 0);
    EXPECT_TRUE(tip.isIk);
    EXPECT_EQ(tip.ikTargetBoneIndex, 0);
    EXPECT_EQ(tip.ikIterationCount, 10);
    ASSERT_EQ(tip.ikLinks.size(), 1u);
    EXPECT_TRUE(tip.ikLinks[0].hasAngleLimit);
    EXPECT_EQ(tip.ikLinks[0].angleLimitMin, Vec3(-1.0f, -1.0f, -1.0f));
    EXPECT_EQ(tip.ikLinks[0].angleLimitMax, Vec3(1.0f, 1.0f, 1.0f));

    // --- Morphs ---
    ASSERT_EQ(decoded->morphs.morphs.size(), 2u);
    const Morph& brow = decoded->morphs.morphs[0];
    EXPECT_EQ(brow.name, "Brow");
    EXPECT_EQ(brow.type, MorphType::Position);
    ASSERT_EQ(brow.positionOffsets.size(), 2u);
    EXPECT_EQ(brow.positionOffsets[0].vertexIndex, 0);
    EXPECT_EQ(brow.positionOffsets[0].offset, Vec3(0.1f, 0.0f, 0.0f));

    const Morph& mat = decoded->morphs.morphs[1];
    EXPECT_EQ(mat.name, "MatTweak");
    EXPECT_EQ(mat.type, MorphType::Material);
    ASSERT_EQ(mat.materialOffsets.size(), 1u);
    EXPECT_EQ(mat.materialOffsets[0].materialIndex, -1);
    EXPECT_EQ(mat.materialOffsets[0].op, Morph::MaterialOffset::OpType::Add);
    EXPECT_EQ(mat.materialOffsets[0].diffuse, Vec4(0.1f, 0.2f, 0.3f, 0.4f));
    EXPECT_FLOAT_EQ(mat.materialOffsets[0].specularPower, 5.0f);

    // --- Physics ---
    ASSERT_EQ(decoded->physics.rigidBodies.size(), 1u);
    EXPECT_EQ(decoded->physics.rigidBodies[0].name, "RB0");
    EXPECT_EQ(decoded->physics.rigidBodies[0].shape, RigidBodyShape::Box);
    EXPECT_EQ(decoded->physics.rigidBodies[0].motionType, RigidBodyMotionType::Dynamic);
    EXPECT_EQ(decoded->physics.rigidBodies[0].collisionGroupMask, 0xFFFFu);

    ASSERT_EQ(decoded->physics.joints.size(), 1u);
    EXPECT_EQ(decoded->physics.joints[0].name, "J0");
    EXPECT_EQ(decoded->physics.joints[0].type, JointType::Hinge);
    EXPECT_EQ(decoded->physics.joints[0].rigidBodyBIndex, -1);

    // --- Materials / textures ---
    ASSERT_EQ(decoded->materials.textures.size(), 2u);
    EXPECT_EQ(decoded->materials.textures[0].sourcePath, "C:\\Source\\body.png");
    EXPECT_EQ(decoded->materials.textures[0].guid, Guid::Parse("0123456789abcdef0123456789abcdef"));
    EXPECT_EQ(decoded->materials.textures[1].sourcePath, "C:\\Source\\missing.png");
    EXPECT_FALSE(decoded->materials.textures[1].guid.IsValid());

    ASSERT_EQ(decoded->materials.materials.size(), 1u);
    EXPECT_EQ(decoded->materials.materials[0].name, "Body");
    EXPECT_EQ(decoded->materials.materials[0].textureIndex, 0);
    EXPECT_EQ(decoded->materials.materials[0].indexCount, 6u);
}

TEST(RigFileTest, EncodesAllEmptyRigDataAsAHeaderOnlyBlobThatDecodesBackToEmpty)
{
    const RigFileData empty;
    const std::vector<std::uint8_t> encoded = EncodeRigDataToBytes(empty);

    const std::optional<RigFileData> decoded = DecodeRigDataFromBytes(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->skinWeights.empty());
    EXPECT_TRUE(decoded->skeleton.bones.empty());
    EXPECT_TRUE(decoded->morphs.morphs.empty());
    EXPECT_TRUE(decoded->physics.rigidBodies.empty());
    EXPECT_TRUE(decoded->physics.joints.empty());
    EXPECT_TRUE(decoded->materials.textures.empty());
    EXPECT_TRUE(decoded->materials.materials.empty());
}

TEST(RigFileTest, DecodeFailsOnEmptyBytes)
{
    EXPECT_FALSE(DecodeRigDataFromBytes(std::vector<std::uint8_t>{}).has_value());
}

TEST(RigFileTest, DecodeFailsOnGarbageBytes)
{
    const std::vector<std::uint8_t> garbage(64, 0xAB);
    EXPECT_FALSE(DecodeRigDataFromBytes(garbage).has_value());
}

TEST(RigFileTest, DecodeFailsWhenMagicIsWrongEvenIfSizeWouldOtherwiseFit)
{
    std::vector<std::uint8_t> encoded = EncodeRigDataToBytes(BuildSampleRigData());
    encoded[0] = static_cast<std::uint8_t>(~encoded[0]); // Corrupt the magic's first byte.

    EXPECT_FALSE(DecodeRigDataFromBytes(encoded).has_value());
}

TEST(RigFileTest, DecodeFailsOnATruncatedButOtherwiseValidBlob)
{
    std::vector<std::uint8_t> encoded = EncodeRigDataToBytes(BuildSampleRigData());
    encoded.resize(encoded.size() - 4); // Chop off the last few bytes of the joint array.

    EXPECT_FALSE(DecodeRigDataFromBytes(encoded).has_value());
}

TEST(RigFileTest, DecodeFailsWhenTruncatedRightAfterTheMagic)
{
    // Cuts off mid-way through the very first length-prefixed field (the
    // skin-weight count) - exercises BinaryReader's own EnsureAvailable()
    // bounds check at the earliest possible point, distinct from
    // DecodeFailsOnATruncatedButOtherwiseValidBlob above (which truncates a
    // large, otherwise-complete blob's tail instead).
    std::vector<std::uint8_t> encoded = EncodeRigDataToBytes(BuildSampleRigData());
    encoded.resize(sizeof(kRigFileMagic) + 2);
    EXPECT_FALSE(DecodeRigDataFromBytes(encoded).has_value());
}

} // namespace
} // namespace gte
