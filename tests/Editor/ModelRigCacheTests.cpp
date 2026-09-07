// Unit tests for the Editor's shared rig-data cache
// (src/Editor/ModelRigCache.h) - GetOrLoad()'s mtime-gated load/reload
// behavior against real *.gta files on disk. Deliberately Tier 1 despite
// touching the real filesystem (a real temp directory this fixture creates/
// tears down, plus real *.gta files written via GtaFile.h's own
// WriteGtaFile()) - no ImGui/Renderer/live-Vulkan-device/SDL-video involved
// at all, mirroring tests/Editor/ProjectPanelDataTests.cpp's own convention
// exactly. Fixture construction style (a RigFileData with one Bone/
// RigidBody/Joint, distinct hand-picked field values) mirrors
// tests/Assets/RigFileTests.cpp's own BuildSampleRigData().

#include "Editor/ModelRigCache.h"

#include "Assets/GtaFile.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

RigFileData BuildFixtureRigData(const std::string& boneName)
{
    RigFileData rig;

    Bone bone;
    bone.name = boneName;
    bone.position = Vec3(1.0f, 2.0f, 3.0f);
    bone.parentBoneIndex = -1;
    rig.skeleton.bones.push_back(bone);

    RigidBody body;
    body.name = "RB_" + boneName;
    body.boneIndex = 0;
    body.shape = RigidBodyShape::Sphere;
    body.shapeSize = Vec3(0.25f, 0.0f, 0.0f);
    body.mass = 2.5f;
    body.motionType = RigidBodyMotionType::Dynamic;
    rig.physics.rigidBodies.push_back(body);

    Joint joint;
    joint.name = "J_" + boneName;
    joint.type = JointType::Hinge;
    joint.rigidBodyAIndex = 0;
    joint.rigidBodyBIndex = -1;
    rig.physics.joints.push_back(joint);

    return rig;
}

bool WriteMeshGta(const std::filesystem::path& path, const RigFileData& rig)
{
    const std::vector<std::uint8_t> metadata = EncodeRigDataToBytes(rig);
    return WriteGtaFile(path, AssetType::Mesh, Guid::Generate(), AssetFlags::None, metadata, /*payload=*/{});
}

class ModelRigCacheTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteModelRigCacheTest_") + info->test_suite_name() + "_" + info->name());

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec); // Leftover from a previous crashed run, if any.
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    std::filesystem::path m_root;
};

TEST_F(ModelRigCacheTest, ReturnsNullptrForANonexistentPath)
{
    ModelRigCache cache;
    const std::string path = (m_root / "does_not_exist.gta").string();
    EXPECT_EQ(cache.GetOrLoad(path), nullptr);
}

TEST_F(ModelRigCacheTest, ReturnsNullptrForAFileThatIsNotAMeshGta)
{
    const std::filesystem::path path = m_root / "texture.gta";
    ASSERT_TRUE(WriteGtaFile(path, AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, {}));

    ModelRigCache cache;
    EXPECT_EQ(cache.GetOrLoad(path.string()), nullptr);
}

TEST_F(ModelRigCacheTest, LoadsBonesRigidBodiesAndJointsFromARealMeshGta)
{
    const std::filesystem::path path = m_root / "model.gta";
    const RigFileData fixture = BuildFixtureRigData("A");
    ASSERT_TRUE(WriteMeshGta(path, fixture));

    ModelRigCache cache;
    const RigFileData* loaded = cache.GetOrLoad(path.string());
    ASSERT_NE(loaded, nullptr);

    ASSERT_EQ(loaded->skeleton.bones.size(), 1u);
    EXPECT_EQ(loaded->skeleton.bones[0].name, "A");
    EXPECT_EQ(loaded->skeleton.bones[0].position, Vec3(1.0f, 2.0f, 3.0f));

    ASSERT_EQ(loaded->physics.rigidBodies.size(), 1u);
    EXPECT_EQ(loaded->physics.rigidBodies[0].name, "RB_A");
    EXPECT_EQ(loaded->physics.rigidBodies[0].shape, RigidBodyShape::Sphere);
    EXPECT_FLOAT_EQ(loaded->physics.rigidBodies[0].mass, 2.5f);

    ASSERT_EQ(loaded->physics.joints.size(), 1u);
    EXPECT_EQ(loaded->physics.joints[0].name, "J_A");
    EXPECT_EQ(loaded->physics.joints[0].type, JointType::Hinge);
}

TEST_F(ModelRigCacheTest, ReturnsAValidEmptyRigFileDataForABonelessMeshGta)
{
    const std::filesystem::path path = m_root / "boneless.gta";
    ASSERT_TRUE(WriteGtaFile(path, AssetType::Mesh, Guid::Generate(), AssetFlags::None, /*metadata=*/{}, /*payload=*/{}));

    ModelRigCache cache;
    const RigFileData* loaded = cache.GetOrLoad(path.string());
    ASSERT_NE(loaded, nullptr);
    EXPECT_TRUE(loaded->skeleton.bones.empty());
    EXPECT_TRUE(loaded->physics.rigidBodies.empty());
    EXPECT_TRUE(loaded->physics.joints.empty());
}

TEST_F(ModelRigCacheTest, ReloadsWhenTheFileIsRewrittenWithANewerMtime)
{
    const std::filesystem::path path = m_root / "model.gta";
    ASSERT_TRUE(WriteMeshGta(path, BuildFixtureRigData("A")));

    ModelRigCache cache;
    const RigFileData* first = cache.GetOrLoad(path.string());
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->skeleton.bones.size(), 1u);
    EXPECT_EQ(first->skeleton.bones[0].name, "A");

    ASSERT_TRUE(WriteMeshGta(path, BuildFixtureRigData("B")));
    // Ensure the mtime actually advances even if both writes landed within
    // the same filesystem-mtime-resolution tick.
    std::error_code ec;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5), ec);

    const RigFileData* second = cache.GetOrLoad(path.string());
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(second->skeleton.bones.size(), 1u);
    EXPECT_EQ(second->skeleton.bones[0].name, "B");
}

TEST_F(ModelRigCacheTest, DoesNotReloadWhenNeitherPathNorMtimeChanged)
{
    const std::filesystem::path path = m_root / "model.gta";
    ASSERT_TRUE(WriteMeshGta(path, BuildFixtureRigData("A")));

    ModelRigCache cache;
    const RigFileData* first = cache.GetOrLoad(path.string());
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->skeleton.bones.size(), 1u);
    EXPECT_EQ(first->skeleton.bones[0].name, "A");

    const RigFileData* second = cache.GetOrLoad(path.string());
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(second->skeleton.bones.size(), 1u);
    EXPECT_EQ(second->skeleton.bones[0].name, "A");
}

TEST_F(ModelRigCacheTest, SwitchingBetweenTwoDifferentPathsReloadsCorrectlyEachTime)
{
    const std::filesystem::path pathA = m_root / "a.gta";
    const std::filesystem::path pathB = m_root / "b.gta";
    ASSERT_TRUE(WriteMeshGta(pathA, BuildFixtureRigData("A")));
    ASSERT_TRUE(WriteMeshGta(pathB, BuildFixtureRigData("B")));

    ModelRigCache cache;

    const RigFileData* loadedA1 = cache.GetOrLoad(pathA.string());
    ASSERT_NE(loadedA1, nullptr);
    EXPECT_EQ(loadedA1->skeleton.bones[0].name, "A");

    const RigFileData* loadedB = cache.GetOrLoad(pathB.string());
    ASSERT_NE(loadedB, nullptr);
    EXPECT_EQ(loadedB->skeleton.bones[0].name, "B");

    const RigFileData* loadedA2 = cache.GetOrLoad(pathA.string());
    ASSERT_NE(loadedA2, nullptr);
    EXPECT_EQ(loadedA2->skeleton.bones[0].name, "A");
}

} // namespace
} // namespace gte
