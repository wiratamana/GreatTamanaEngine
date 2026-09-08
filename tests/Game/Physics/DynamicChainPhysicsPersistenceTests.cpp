#include "Game/Physics/DynamicChainPhysicsPersistence.h"
#include "Assets/GtaFile.h"
#include "Assets/MeshFile.h"
#include "Assets/RigFile.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace gte {
namespace {

std::filesystem::path TempGtaPath(const char* name)
{
    return std::filesystem::temp_directory_path() / name;
}

// Builds a real, on-disk Mesh *.gta with a NON-trivial RigFileData (a
// skeleton with >= 3 bones, so it can carry a couple of
// jointPhysicsOverrides-worthy bone indices) plus a real mesh payload, and
// returns the path written to.
std::filesystem::path WriteSampleMeshGtaFile(const std::filesystem::path& path)
{
    MeshData mesh;
    mesh.positions = { Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f) };
    mesh.normals = { Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, 1.0f) };
    mesh.uvs = { Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f) };
    mesh.indices = { 0, 1, 2 };
    const std::vector<std::uint8_t> payload = EncodeMeshDataToBytes(mesh);

    RigFileData rig;
    Bone rootBone;
    rootBone.name = "Root";
    rootBone.parentBoneIndex = -1;
    rig.skeleton.bones.push_back(rootBone);
    Bone childBone;
    childBone.name = "Child1";
    childBone.parentBoneIndex = 0;
    rig.skeleton.bones.push_back(childBone);
    Bone grandchildBone;
    grandchildBone.name = "Child2";
    grandchildBone.parentBoneIndex = 1;
    rig.skeleton.bones.push_back(grandchildBone);

    Material material;
    material.name = "Body";
    material.indexCount = 3;
    rig.materials.materials.push_back(material);

    const std::vector<std::uint8_t> metadata = EncodeRigDataToBytes(rig);

    const bool wrote = WriteGtaFile(path, AssetType::Mesh, Guid::Generate(), AssetFlags::None, metadata, payload);
    EXPECT_TRUE(wrote);
    return path;
}

TEST(DynamicChainPhysicsPersistenceTest, SavesOverridesWithoutDisturbingAnyOtherData)
{
    const std::filesystem::path path = WriteSampleMeshGtaFile(TempGtaPath("JointOverridePersistenceTest.gta"));
    const std::optional<GtaFileData> before = ReadGtaFile(path);
    ASSERT_TRUE(before.has_value());
    const std::optional<RigFileData> rigBefore = DecodeRigDataFromBytes(before->metadata);
    ASSERT_TRUE(rigBefore.has_value());

    DynamicChainDefinition chain;
    chain.rootBoneIndex = 0;
    chain.jointBoneIndices = { 1, 2 };
    chain.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(2);
    chain.jointSettings = { DynamicJointSettings{ 0.6f, 0.03f, 2.0f }, DynamicJointSettings{ 0.8f, 0.01f, 0.5f } };

    std::string error;
    const bool ok = SaveJointPhysicsOverridesToGtaFile(path.string(), { chain }, &error);
    ASSERT_TRUE(ok) << error;

    const std::optional<GtaFileData> after = ReadGtaFile(path);
    ASSERT_TRUE(after.has_value());
    // --- Header fields unchanged ---
    EXPECT_EQ(after->header.Id(), before->header.Id());
    EXPECT_EQ(after->header.Flags(), before->header.Flags());
    EXPECT_EQ(after->header.version, before->header.version);
    EXPECT_EQ(after->header.Type(), AssetType::Mesh);
    // --- Payload (mesh geometry) byte-for-byte unchanged ---
    EXPECT_EQ(after->payload, before->payload);

    const std::optional<RigFileData> rigAfter = DecodeRigDataFromBytes(after->metadata);
    ASSERT_TRUE(rigAfter.has_value());
    // --- Everything besides jointPhysicsOverrides unchanged ---
    EXPECT_EQ(rigAfter->skeleton.bones.size(), rigBefore->skeleton.bones.size());
    EXPECT_EQ(rigAfter->skinWeights.size(), rigBefore->skinWeights.size());
    EXPECT_EQ(rigAfter->materials.materials.size(), rigBefore->materials.materials.size());
    // --- The new overrides ARE present, matching exactly what was passed in ---
    ASSERT_EQ(rigAfter->jointPhysicsOverrides.size(), 2u);
    EXPECT_EQ(rigAfter->jointPhysicsOverrides[0].boneIndex, 1);
    EXPECT_FLOAT_EQ(rigAfter->jointPhysicsOverrides[0].damping, 0.6f);
    EXPECT_EQ(rigAfter->jointPhysicsOverrides[1].boneIndex, 2);
    EXPECT_FLOAT_EQ(rigAfter->jointPhysicsOverrides[1].mass, 0.5f);
}

TEST(DynamicChainPhysicsPersistenceTest, FailsGracefullyWhenFileDoesNotExist)
{
    std::string error;
    EXPECT_FALSE(SaveJointPhysicsOverridesToGtaFile(
        (std::filesystem::temp_directory_path() / "DoesNotExist.gta").string(), {}, &error));
    EXPECT_FALSE(error.empty());
}

TEST(DynamicChainPhysicsPersistenceTest, FailsGracefullyOnANonMeshAssetType)
{
    const std::filesystem::path path = TempGtaPath("NotAMesh.gta");
    ASSERT_TRUE(WriteGtaFile(path, AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, {}));

    std::string error;
    EXPECT_FALSE(SaveJointPhysicsOverridesToGtaFile(path.string(), {}, &error));
    EXPECT_FALSE(error.empty());
}

TEST(DynamicChainPhysicsPersistenceTest, OverwritingASecondTimeReplacesRatherThanAccumulatingOverrides)
{
    const std::filesystem::path path = WriteSampleMeshGtaFile(TempGtaPath("JointOverrideReplaceTest.gta"));

    DynamicChainDefinition chain;
    chain.jointBoneIndices = { 1 };
    chain.jointSettings = { DynamicJointSettings{ 0.1f, 0.1f, 1.0f } };
    ASSERT_TRUE(SaveJointPhysicsOverridesToGtaFile(path.string(), { chain }, nullptr));

    chain.jointSettings = { DynamicJointSettings{ 0.9f, 0.9f, 9.0f } }; // Same joint, new values.
    ASSERT_TRUE(SaveJointPhysicsOverridesToGtaFile(path.string(), { chain }, nullptr));

    const std::optional<GtaFileData> gta = ReadGtaFile(path);
    ASSERT_TRUE(gta.has_value());
    const std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    ASSERT_TRUE(rig.has_value());
    ASSERT_EQ(rig->jointPhysicsOverrides.size(), 1u); // NOT 2 - the second save REPLACED, not appended.
    EXPECT_FLOAT_EQ(rig->jointPhysicsOverrides[0].damping, 0.9f);
}

} // namespace
} // namespace gte
