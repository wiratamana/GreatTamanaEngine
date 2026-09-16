// Unit tests for RecomputeAndSaveMeshNormalsToGtaFile()
// (src/Assets/MeshNormalRecomputePersistence.h) - task_manager/stl-parser-1,
// PHASE3. Real-temp-file round-trip tests, following
// tests/Assets/AssetImporterTests.cpp's/
// tests/Game/Physics/DynamicChainPhysicsPersistenceTests.cpp's own
// temp-directory convention.

#include "Assets/MeshNormalRecomputePersistence.h"

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

// Builds a real, on-disk Mesh *.gta - a single triangle with deliberately
// WRONG normals - and returns the path written to.
std::filesystem::path WriteSampleMeshGtaFileWithWrongNormals(const std::filesystem::path& path)
{
    MeshData mesh;
    mesh.positions = { Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f) };
    // Deliberately WRONG normals (should end up recomputed as (0,0,1) after
    // RecomputeAndSaveMeshNormalsToGtaFile()).
    mesh.normals = { Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f) };
    mesh.uvs = { Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f) };
    mesh.indices = { 0, 1, 2 };
    const std::vector<std::uint8_t> payload = EncodeMeshDataToBytes(mesh);

    RigFileData rig;
    Bone rootBone;
    rootBone.name = "Root";
    rootBone.parentBoneIndex = -1;
    rig.skeleton.bones.push_back(rootBone);
    const std::vector<std::uint8_t> metadata = EncodeRigDataToBytes(rig);

    const bool wrote = WriteGtaFile(path, AssetType::Mesh, Guid::Generate(), AssetFlags::None, metadata, payload);
    EXPECT_TRUE(wrote);
    return path;
}

TEST(MeshNormalRecomputePersistenceTest, RecomputesAndOverwritesNormalsInAnExistingMeshGtaFile)
{
    const std::filesystem::path path
        = WriteSampleMeshGtaFileWithWrongNormals(TempGtaPath("MeshNormalRecomputePersistenceTest_Basic.gta"));

    const std::optional<GtaFileData> before = ReadGtaFile(path);
    ASSERT_TRUE(before.has_value());

    std::string error;
    const bool ok = RecomputeAndSaveMeshNormalsToGtaFile(path.string(), &error);
    ASSERT_TRUE(ok) << error;

    const std::optional<GtaFileData> after = ReadGtaFile(path);
    ASSERT_TRUE(after.has_value());

    // GUID/flags/version/metadata preserved byte-for-byte/value-for-value.
    EXPECT_EQ(after->header.Id(), before->header.Id());
    EXPECT_EQ(after->header.Flags(), before->header.Flags());
    EXPECT_EQ(after->header.version, before->header.version);
    EXPECT_EQ(after->metadata, before->metadata);

    const std::optional<MeshData> meshAfter = DecodeMeshDataFromBytes(after->payload);
    ASSERT_TRUE(meshAfter.has_value());
    const Vec3 expected = Normalize(
        Cross(meshAfter->positions[1] - meshAfter->positions[0], meshAfter->positions[2] - meshAfter->positions[0]));
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(ApproximatelyEqual(meshAfter->normals[i], expected)) << "vertex " << i;
    }
}

TEST(MeshNormalRecomputePersistenceTest, FailsGracefullyWhenTheFileDoesNotExist)
{
    const std::filesystem::path path = TempGtaPath("MeshNormalRecomputePersistenceTest_DoesNotExist.gta");
    ASSERT_FALSE(std::filesystem::exists(path));

    std::string error;
    EXPECT_FALSE(RecomputeAndSaveMeshNormalsToGtaFile(path.string(), &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(MeshNormalRecomputePersistenceTest, FailsGracefullyWhenTheGtaIsNotAMeshAsset)
{
    const std::filesystem::path path = TempGtaPath("MeshNormalRecomputePersistenceTest_NotAMesh.gta");
    ASSERT_TRUE(WriteGtaFile(path, AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, {}));

    std::string error;
    EXPECT_FALSE(RecomputeAndSaveMeshNormalsToGtaFile(path.string(), &error));
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("Mesh"), std::string::npos);
}

TEST(MeshNormalRecomputePersistenceTest, FailsGracefullyWhenThePayloadIsUndecodable)
{
    const std::filesystem::path path = TempGtaPath("MeshNormalRecomputePersistenceTest_UndecodablePayload.gta");
    const std::vector<std::uint8_t> garbagePayload = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    ASSERT_TRUE(WriteGtaFile(path, AssetType::Mesh, Guid::Generate(), AssetFlags::None, {}, garbagePayload));

    const std::optional<GtaFileData> before = ReadGtaFile(path);
    ASSERT_TRUE(before.has_value());

    std::string error;
    EXPECT_FALSE(RecomputeAndSaveMeshNormalsToGtaFile(path.string(), &error));
    EXPECT_FALSE(error.empty());

    // The original file on disk must be left completely untouched.
    const std::optional<GtaFileData> after = ReadGtaFile(path);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->header.Id(), before->header.Id());
    EXPECT_EQ(after->metadata, before->metadata);
    EXPECT_EQ(after->payload, before->payload);
}

} // namespace
} // namespace gte
