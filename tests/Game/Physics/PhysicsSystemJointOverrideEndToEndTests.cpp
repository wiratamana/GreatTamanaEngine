// End-to-end regression for task_manager/verlet-integration-11 (PHASES 1-4):
// proves a user's live joint-physics edit survives a real save-to-*.gta ->
// reload-from-*.gta round trip and is genuinely re-applied by
// PhysicsSystem::RegisterDynamicChains() on the NEXT load, not merely held in
// the SAME PhysicsSystem's own in-memory cache. No GPU/SDL/ImGui involved -
// real disk I/O only (matches GtaFileTests.cpp/RigFileTests.cpp's own
// established "Tier 1 + real temp file" precedent).
//
// task_manager/verlet-integration-11, PHASE5 - written directly against this
// this diagnostic pass - see that phase's own Step 3.3/3.4).
//
#include "Game/Physics/PhysicsSystem.h"
#include "Game/Physics/DynamicChainPhysicsPersistence.h"
#include "Game/Animation/SkeletalRigCache.h"
#include "Assets/GtaFile.h"
#include "Assets/MeshFile.h"
#include "Assets/RigFile.h"

#include <gtest/gtest.h>
#include <filesystem>

namespace gte {
namespace {

// Builds and writes a real, minimal but genuinely chain-detectable Mesh
// *.gta to `path`: a mesh payload (EncodeMeshDataToBytes) plus a RigFileData
// (EncodeRigDataToBytes) whose skeleton + PhysicsData::rigidBodies/joints are
// shaped so DetectDynamicChains() produces AT LEAST ONE chain with a known,
// asserted-on bone index - one Static RigidBody anchor bone plus three
// Dynamic RigidBody bones connected in a straight line, mirroring
// DynamicChainDetectionTests.cpp's own minimal fixture shape.
void WriteDetectableSkinnedMeshGtaFile(const std::filesystem::path& path, std::int32_t* outJointBoneIndex)
{
    // --- Mesh payload: a single triangle, just enough to pass
    // DecodeMeshDataFromBytes()'s own "at least 3 indices" sanity checks
    // downstream (MeshAssetGpuCatalog.cpp) - not itself checked by this test.
    MeshData mesh;
    mesh.positions = { Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f) };
    mesh.normals = { Vec3::Up(), Vec3::Up(), Vec3::Up() };
    mesh.uvs = { Vec2::Zero(), Vec2::Zero(), Vec2::Zero() };
    mesh.indices = { 0, 1, 2 };

    // --- Rig metadata: 0 = static anchor, 1/2/3 = a straight dynamic chain -
    // exactly DynamicChainDetectionTests.cpp's own
    // "SimpleLinearRigWithOneStaticAnchorAndThreeDynamicBodiesDetectsOneLinearChain"
    // fixture shape.
    RigFileData rig;
    Bone anchor;
    anchor.position = Vec3(0.0f, 0.0f, 0.0f);
    anchor.parentBoneIndex = -1;
    rig.skeleton.bones.push_back(anchor);

    Bone joint1;
    joint1.position = Vec3(0.0f, 1.0f, 0.0f);
    joint1.parentBoneIndex = 0;
    rig.skeleton.bones.push_back(joint1);

    Bone joint2;
    joint2.position = Vec3(0.0f, 2.5f, 0.0f);
    joint2.parentBoneIndex = 1;
    rig.skeleton.bones.push_back(joint2);

    Bone joint3;
    joint3.position = Vec3(0.0f, 4.5f, 0.0f);
    joint3.parentBoneIndex = 2;
    rig.skeleton.bones.push_back(joint3);

    RigidBody anchorBody;
    anchorBody.boneIndex = 0;
    anchorBody.motionType = RigidBodyMotionType::Static;
    rig.physics.rigidBodies.push_back(anchorBody);

    for (std::int32_t boneIndex = 1; boneIndex <= 3; ++boneIndex) {
        RigidBody body;
        body.boneIndex = boneIndex;
        body.motionType = RigidBodyMotionType::Dynamic;
        body.mass = 1.0f;
        // DetectDynamicChains() seeds DynamicJointSettings::damping straight
        // from RigidBody::linearDamping (see DynamicChainDetection.cpp) - set
        // to match DynamicJointSettings{}'s own default (0.4f) so this test's
        // own "before any save, this joint has pure defaults" sanity check
        // below is actually true, rather than accidentally comparing against
        // a stiffness/mass-only default while damping was really seeded as 0.
        body.linearDamping = 0.4f;
        rig.physics.rigidBodies.push_back(body);
    }

    Joint j01;
    j01.rigidBodyAIndex = 0;
    j01.rigidBodyBIndex = 1;
    rig.physics.joints.push_back(j01);
    Joint j12;
    j12.rigidBodyAIndex = 1;
    j12.rigidBodyBIndex = 2;
    rig.physics.joints.push_back(j12);
    Joint j23;
    j23.rigidBodyAIndex = 2;
    j23.rigidBodyBIndex = 3;
    rig.physics.joints.push_back(j23);

    // Skin weights - one per mesh vertex, all bound to the anchor bone (the
    // exact binding doesn't matter for this test; only its PRESENCE/COUNT
    // does, since MeshAssetGpuCatalog-equivalent code below only decides
    // "skinned" via bone/skin-weight count, never actual weight values).
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        VertexSkinWeights sw;
        sw.type = VertexWeightType::BDEF1;
        sw.boneIndices[0] = 0;
        sw.boneWeights[0] = 1.0f;
        rig.skinWeights.push_back(sw);
    }

    const std::vector<std::uint8_t> meshBytes = EncodeMeshDataToBytes(mesh);
    const std::vector<std::uint8_t> rigBytes = EncodeRigDataToBytes(rig);
    ASSERT_TRUE(WriteGtaFile(path, AssetType::Mesh, Guid::Generate(), AssetFlags::None, rigBytes, meshBytes));

    *outJointBoneIndex = 2; // The middle joint of the detected chain - arbitrary but stable.
}

// Reads `path` fresh off disk and builds a SkinnedMeshData exactly the way
// MeshAssetGpuCatalog::EnsureMeshAsset() would (read the file, decode the
// rig, copy across the same fields) - shared by BOTH tests below so the
// "fresh process" and "same session" scenarios build their input identically.
SkinnedMeshData LoadSkinnedMeshDataFromDisk(const std::filesystem::path& path)
{
    const std::optional<GtaFileData> gta = ReadGtaFile(path);
    EXPECT_TRUE(gta.has_value());
    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    EXPECT_TRUE(mesh.has_value());
    const std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    EXPECT_TRUE(rig.has_value());

    SkinnedMeshData data;
    data.bindPositions = mesh->positions;
    data.bindNormals.resize(mesh->positions.size(), Vec3::Up());
    data.uvs.resize(mesh->positions.size(), Vec2::Zero());
    data.skinWeights = rig->skinWeights;
    data.skeleton = rig->skeleton;
    data.physics = rig->physics;
    data.jointPhysicsOverrides = rig->jointPhysicsOverrides;
    return data;
}

TEST(PhysicsSystemJointOverrideEndToEndTest, EditedJointPhysicsSurvivesSaveAndReloadAcrossAFreshProcess)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "JointOverrideEndToEndTest.gta";
    std::int32_t jointBoneIndex = -1;
    WriteDetectableSkinnedMeshGtaFile(path, &jointBoneIndex);
    ASSERT_GE(jointBoneIndex, 0);

    // --- "First load": register with the auto-detected defaults - proves
    // a NEVER-edited model gets pure defaults, nothing pre-seeded. ---
    PhysicsSystem firstLoadPhysics;
    firstLoadPhysics.RegisterDynamicChains(path.string(), LoadSkinnedMeshDataFromDisk(path));

    const DynamicChainRigCache::ModelEntry* firstModel = firstLoadPhysics.GetDynamicChainRigCache().TryGet(path.string());
    ASSERT_NE(firstModel, nullptr);
    ASSERT_FALSE(firstModel->chains.empty());

    const DynamicJointSettings defaults{};
    bool foundBeforeSave = false;
    for (const DynamicChainDefinition& chain : firstModel->chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            if (chain.jointBoneIndices[i] == jointBoneIndex) {
                EXPECT_FLOAT_EQ(chain.jointSettings[i].damping, defaults.damping);
                foundBeforeSave = true;
            }
        }
    }
    ASSERT_TRUE(foundBeforeSave);

    // --- Simulate an Editor drag-edit directly on the live cache, then
    // Save (exactly what Panels/InspectorPanel.cpp's button does). ---
    DynamicChainRigCache::ModelEntry* mutableModel = firstLoadPhysics.GetDynamicChainRigCache().TryGetMutable(path.string());
    ASSERT_NE(mutableModel, nullptr);
    for (DynamicChainDefinition& chain : mutableModel->chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            if (chain.jointBoneIndices[i] == jointBoneIndex) {
                chain.jointSettings[i].damping = 0.93f;
                chain.jointSettings[i].stiffness = 0.04f;
                chain.jointSettings[i].mass = 6.25f;
            }
        }
    }
    std::string saveError;
    ASSERT_TRUE(SaveJointPhysicsOverridesToGtaFile(path.string(), mutableModel->chains, &saveError)) << saveError;

    // --- "A future session, after a full restart": a BRAND NEW
    // PhysicsSystem, re-reading the SAME file from disk from scratch. ---
    PhysicsSystem secondLoadPhysics;
    secondLoadPhysics.RegisterDynamicChains(path.string(), LoadSkinnedMeshDataFromDisk(path));

    const DynamicChainRigCache::ModelEntry* secondModel = secondLoadPhysics.GetDynamicChainRigCache().TryGet(path.string());
    ASSERT_NE(secondModel, nullptr);

    bool foundAfterReload = false;
    for (const DynamicChainDefinition& chain : secondModel->chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            if (chain.jointBoneIndices[i] == jointBoneIndex) {
                EXPECT_FLOAT_EQ(chain.jointSettings[i].damping, 0.93f);
                EXPECT_FLOAT_EQ(chain.jointSettings[i].stiffness, 0.04f);
                EXPECT_FLOAT_EQ(chain.jointSettings[i].mass, 6.25f);
                foundAfterReload = true;
            }
        }
    }
    EXPECT_TRUE(foundAfterReload);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(PhysicsSystemJointOverrideEndToEndTest, EditedJointPhysicsIsPickedUpByASecondSpawnWithinTheSameSession)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "JointOverrideSameSessionTest.gta";
    std::int32_t jointBoneIndex = -1;
    WriteDetectableSkinnedMeshGtaFile(path, &jointBoneIndex);
    ASSERT_GE(jointBoneIndex, 0);

    PhysicsSystem physics;

    physics.RegisterDynamicChains(path.string(), LoadSkinnedMeshDataFromDisk(path));
    DynamicChainRigCache::ModelEntry* mutableModel = physics.GetDynamicChainRigCache().TryGetMutable(path.string());
    ASSERT_NE(mutableModel, nullptr);
    for (DynamicChainDefinition& chain : mutableModel->chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            if (chain.jointBoneIndices[i] == jointBoneIndex) {
                chain.jointSettings[i].damping = 0.55f;
                chain.jointSettings[i].stiffness = 0.07f;
                chain.jointSettings[i].mass = 3.5f;
            }
        }
    }
    std::string saveError;
    ASSERT_TRUE(SaveJointPhysicsOverridesToGtaFile(path.string(), mutableModel->chains, &saveError)) << saveError;

    const SkinnedMeshData refreshedData = LoadSkinnedMeshDataFromDisk(path);
    ASSERT_FALSE(refreshedData.jointPhysicsOverrides.empty());

    physics.RegisterDynamicChains(path.string(), refreshedData);

    const DynamicChainRigCache::ModelEntry* secondSpawnModel = physics.GetDynamicChainRigCache().TryGet(path.string());
    ASSERT_NE(secondSpawnModel, nullptr);

    bool foundOnSecondSpawn = false;
    for (const DynamicChainDefinition& chain : secondSpawnModel->chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            if (chain.jointBoneIndices[i] == jointBoneIndex) {
                EXPECT_FLOAT_EQ(chain.jointSettings[i].damping, 0.55f);
                EXPECT_FLOAT_EQ(chain.jointSettings[i].stiffness, 0.07f);
                EXPECT_FLOAT_EQ(chain.jointSettings[i].mass, 3.5f);
                foundOnSecondSpawn = true;
            }
        }
    }
    EXPECT_TRUE(foundOnSecondSpawn);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace
} // namespace gte
