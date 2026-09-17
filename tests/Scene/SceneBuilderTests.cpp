// Unit tests for src/Scene/SceneBuilder.h - BuildSceneDocumentFromRegistry()
// (Registry + AssetDatabase -> SceneDocument, including PHASE4's own
// MeshAssetSource -> asset_guid resolution pass) and ClearEntireScene()
// (unconditionally wipes EVERY entity before a Load - see PHASE4, section
// 3.3, which replaced the old, narrower ClearSerializableSceneObjects()).
// Rewritten in
// task_manager/scene-serialization-2/PHASE3_JSON_SCENE_DOCUMENT_AND_HIERARCHY_SAVE_PLUS_GENERIC_LOAD.md
// (section 3.6) against the NEW SceneDocument/SceneEntityRecord generic-
// component-bag shape - BuildSceneDocumentFromRegistry() now walks the
// ENTIRE hierarchy (every entity reachable from the root list, recursively),
// not just PrimitiveSource/MeshAssetSource-tagged roots. Touches a real temp
// directory (to build a real AssetDatabase against real *.gta files, same
// pattern as tests/Assets/AssetDatabaseTests.cpp) but no GPU/SDL/ImGui at
// all - "Tier 1" per tests/CMakeLists.txt's own taxonomy. Always built -
// src/Scene/ has no GTE_ENABLE_EDITOR/GTE_ENABLE_PROJECT_PANEL dependency.

#include "Scene/SceneBuilder.h"

#include "ECS/Components/Camera.h"
#include "ECS/Components/MeshAssetSource.h"
#include "ECS/Components/Name.h"
#include "ECS/Components/PrimitiveSource.h"
#include "ECS/Components/Transform.h"
#include "ECS/TransformHierarchy.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

class SceneBuilderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteSceneBuilderTest_") + info->test_suite_name() + "_" + info->name());

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    std::filesystem::path m_root;
    AssetDatabase m_db;
};

// --- BuildSceneDocumentFromRegistry() ---------------------------------------

TEST_F(SceneBuilderTest, PrimitiveRootProducesOnePrimitiveRecord)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    Transform& transform = registry.AddComponent<Transform>(entity);
    transform.position = Vec3(1.0f, 2.0f, 3.0f);
    transform.rotation = Quat(0.0f, 0.0f, 0.0f, 1.0f);
    transform.scale = Vec3(2.0f, 2.0f, 2.0f);
    registry.AddComponent<PrimitiveSource>(entity).type = PrimitiveType::Sphere;
    registry.AddComponent<Name>(entity).value = "MySphere";

    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, m_db);
    ASSERT_EQ(document.entities.size(), 1u);

    const SceneEntityRecord& record = document.entities[0];
    EXPECT_FALSE(record.parentIndex.has_value());
    ASSERT_TRUE(record.components.contains("PrimitiveSource"));
    EXPECT_EQ(record.components["PrimitiveSource"]["type"].get<std::string>(), "Sphere");
    ASSERT_TRUE(record.components.contains("Name"));
    EXPECT_EQ(record.components["Name"]["value"].get<std::string>(), "MySphere");
    ASSERT_TRUE(record.components.contains("Transform"));
    EXPECT_EQ(record.components["Transform"]["position"][0].get<float>(), 1.0f);
    EXPECT_EQ(record.components["Transform"]["position"][1].get<float>(), 2.0f);
    EXPECT_EQ(record.components["Transform"]["position"][2].get<float>(), 3.0f);
    EXPECT_EQ(record.components["Transform"]["scale"][0].get<float>(), 2.0f);
}

TEST_F(SceneBuilderTest, AssetRootWithTrackedPathProducesOneAssetRecord)
{
    const std::filesystem::path gtaPath = m_root / "model.gta";
    const Guid knownGuid = Guid::Generate();
    ASSERT_TRUE(WriteGtaFile(gtaPath, AssetType::Mesh, knownGuid, AssetFlags::None, {}, {}));
    ASSERT_EQ(m_db.RefreshFromDirectory(m_root), 1u);

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity);
    registry.AddComponent<MeshAssetSource>(entity).gtaPath = gtaPath.string();

    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, m_db);
    ASSERT_EQ(document.entities.size(), 1u);
    // task_manager/scene-serialization-2/
    // PHASE4_RECIPE_SPAWN_RECONCILIATION_AND_LOAD_CORRECTNESS.md, section
    // 3.1 - assetGuid is now actually resolved (a tracked path resolves to
    // its own AssetRecord's stable Guid::ToString()) - PHASE3 left this
    // field empty for every entity; PHASE4 is what wires this resolution
    // pass in. MeshAssetSource itself is still not a registered reflectable
    // component (see PHASE2) so its own "components" bag still has no
    // "MeshAssetSource" key either - only its sibling Transform does.
    EXPECT_EQ(document.entities[0].assetGuid, knownGuid.ToString());
    EXPECT_TRUE(document.entities[0].components.contains("Transform"));
    EXPECT_FALSE(document.entities[0].components.contains("MeshAssetSource"));
}

// (v2) Path-normalization invariant retained from scene-serialization-1 -
// PHASE4 now actually ACTS on this (see above), so this test also doubles
// as a regression guard that the underlying AssetDatabase path-resolution
// behavior this feature depends on hasn't drifted.
TEST_F(SceneBuilderTest, AssetRootResolvesViaNormalizedEquivalentPath)
{
    const std::filesystem::path gtaPath = m_root / "Sub" / "model.gta";
    std::filesystem::create_directories(m_root / "Sub");
    const Guid knownGuid = Guid::Generate();
    ASSERT_TRUE(WriteGtaFile(gtaPath, AssetType::Mesh, knownGuid, AssetFlags::None, {}, {}));
    ASSERT_EQ(m_db.RefreshFromDirectory(m_root), 1u);

    std::filesystem::path equivalentPath = m_root;
    equivalentPath /= "Sub";
    equivalentPath /= "model.gta";
    ASSERT_NE(m_db.FindByPath(equivalentPath.string()), nullptr);

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity);
    registry.AddComponent<MeshAssetSource>(entity).gtaPath = equivalentPath.string();

    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, m_db);
    ASSERT_EQ(document.entities.size(), 1u);
    EXPECT_TRUE(document.entities[0].components.contains("Transform"));
}

TEST_F(SceneBuilderTest, AssetRootWithUntrackedPathStillProducesARecordWithNoGuid)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    Transform& transform = registry.AddComponent<Transform>(entity);
    transform.position = Vec3(5.0f, 6.0f, 7.0f);
    registry.AddComponent<MeshAssetSource>(entity).gtaPath = (m_root / "NeverImported.gta").string();

    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, m_db);
    // PHASE0's Locked Design Decision #2 - scope widens to EVERY entity, so
    // this root is no longer SKIPPED the way scene-serialization-1 used to
    // - its Transform/Name are still captured generically, only its
    // (not-yet-implemented-this-phase) asset_guid stays empty.
    ASSERT_EQ(document.entities.size(), 1u);
    EXPECT_TRUE(document.entities[0].assetGuid.empty());
    ASSERT_TRUE(document.entities[0].components.contains("Transform"));
    EXPECT_EQ(document.entities[0].components["Transform"]["position"][0].get<float>(), 5.0f);
}

TEST_F(SceneBuilderTest, PlainCameraRootProducesACameraRecordToo)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity);
    Camera& camera = registry.AddComponent<Camera>(entity);
    camera.nearZ = 0.25f;
    camera.farZ = 500.0f;

    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, m_db);
    // PHASE0's Locked Design Decision #2 - a Camera-only root is no longer
    // skipped at all (scene-serialization-1's old "neither tag present"
    // skip is gone).
    ASSERT_EQ(document.entities.size(), 1u);
    ASSERT_TRUE(document.entities[0].components.contains("Camera"));
    ASSERT_TRUE(document.entities[0].components.contains("Transform"));
    EXPECT_EQ(document.entities[0].components["Camera"]["nearZ"].get<float>(), 0.25f);
    EXPECT_EQ(document.entities[0].components["Camera"]["farZ"].get<float>(), 500.0f);
}

TEST_F(SceneBuilderTest, ChildEntityIsSerializedWithParentIndex)
{
    const std::filesystem::path gtaPath = m_root / "model.gta";
    const Guid knownGuid = Guid::Generate();
    ASSERT_TRUE(WriteGtaFile(gtaPath, AssetType::Mesh, knownGuid, AssetFlags::None, {}, {}));
    ASSERT_EQ(m_db.RefreshFromDirectory(m_root), 1u);

    Registry registry;
    const Entity root = registry.CreateEntity();
    registry.AddComponent<Transform>(root);
    registry.AddComponent<MeshAssetSource>(root).gtaPath = gtaPath.string();

    const Entity child = registry.CreateEntity();
    Transform& childTransform = registry.AddComponent<Transform>(child);
    childTransform.parent = root;
    registry.AddComponent<Name>(child).value = "ChildPart";

    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, m_db);
    // A child entity IS now independently visited/serialized (as its own
    // SceneEntityRecord with a non-null parentIndex) - this is the OPPOSITE
    // of scene-serialization-1's old "child entity is never independently
    // visited" behavior (PHASE0's Locked Design Decision #2 supersedes that
    // campaign's own Design Decision #3).
    ASSERT_EQ(document.entities.size(), 2u);

    // Find the root record (no parent) and the child record (has a parent).
    const SceneEntityRecord* rootRecord = nullptr;
    const SceneEntityRecord* childRecord = nullptr;
    for (std::size_t i = 0; i < document.entities.size(); ++i) {
        if (!document.entities[i].parentIndex.has_value()) {
            rootRecord = &document.entities[i];
        } else {
            childRecord = &document.entities[i];
        }
    }
    ASSERT_NE(rootRecord, nullptr);
    ASSERT_NE(childRecord, nullptr);

    // childRecord's own parentIndex must point at rootRecord's own array
    // index.
    const std::size_t rootIndex = static_cast<std::size_t>(rootRecord - document.entities.data());
    EXPECT_EQ(*childRecord->parentIndex, rootIndex);
    ASSERT_TRUE(childRecord->components.contains("Name"));
    EXPECT_EQ(childRecord->components["Name"]["value"].get<std::string>(), "ChildPart");
}

TEST_F(SceneBuilderTest, MultipleIndependentRootsAllResolveCorrectly)
{
    const std::filesystem::path gtaPath = m_root / "model.gta";
    const Guid knownGuid = Guid::Generate();
    ASSERT_TRUE(WriteGtaFile(gtaPath, AssetType::Mesh, knownGuid, AssetFlags::None, {}, {}));
    ASSERT_EQ(m_db.RefreshFromDirectory(m_root), 1u);

    Registry registry;

    const Entity primitiveEntity = registry.CreateEntity();
    registry.AddComponent<Transform>(primitiveEntity);
    registry.AddComponent<PrimitiveSource>(primitiveEntity).type = PrimitiveType::Cube;

    const Entity assetEntity = registry.CreateEntity();
    registry.AddComponent<Transform>(assetEntity);
    registry.AddComponent<MeshAssetSource>(assetEntity).gtaPath = gtaPath.string();

    const Entity cameraEntity = registry.CreateEntity();
    registry.AddComponent<Transform>(cameraEntity);
    registry.AddComponent<Camera>(cameraEntity);

    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, m_db);
    // Every root is now in scope - Primitive, Asset, AND Camera (3, not 2 -
    // the old test only expected the first two, since a Camera-only root
    // used to be skipped).
    ASSERT_EQ(document.entities.size(), 3u);

    bool sawPrimitive = false;
    bool sawCamera = false;
    for (const SceneEntityRecord& record : document.entities) {
        if (record.components.contains("PrimitiveSource")) {
            sawPrimitive = true;
            EXPECT_EQ(record.components["PrimitiveSource"]["type"].get<std::string>(), "Cube");
        } else if (record.components.contains("Camera")) {
            sawCamera = true;
        }
    }
    EXPECT_TRUE(sawPrimitive);
    EXPECT_TRUE(sawCamera);
}

// --- ClearEntireScene() -------------------------------------------------
// task_manager/scene-serialization-2/
// PHASE4_RECIPE_SPAWN_RECONCILIATION_AND_LOAD_CORRECTNESS.md, section 3.3 -
// ClearSerializableSceneObjects() (Primitive/Asset-root-only) is deleted and
// replaced by a genuinely unconditional ClearEntireScene(). The first two
// tests below only needed their call site renamed (their existing
// assertions already hold true for the new unconditional behavior); the
// third test's assertion is INVERTED (a Camera-only root is no longer left
// untouched - see that test's own updated name/body).

TEST_F(SceneBuilderTest, ClearDestroysPrimitiveAndAssetRootsPlusTheirChildren)
{
    Registry registry;

    const Entity primitiveRoot = registry.CreateEntity();
    registry.AddComponent<Transform>(primitiveRoot);
    registry.AddComponent<PrimitiveSource>(primitiveRoot);
    const Entity primitiveChild = registry.CreateEntity();
    Transform& primitiveChildTransform = registry.AddComponent<Transform>(primitiveChild);
    primitiveChildTransform.parent = primitiveRoot;

    const Entity assetRoot = registry.CreateEntity();
    registry.AddComponent<Transform>(assetRoot);
    registry.AddComponent<MeshAssetSource>(assetRoot);
    const Entity assetChild1 = registry.CreateEntity();
    Transform& assetChild1Transform = registry.AddComponent<Transform>(assetChild1);
    assetChild1Transform.parent = assetRoot;
    const Entity assetChild2 = registry.CreateEntity();
    Transform& assetChild2Transform = registry.AddComponent<Transform>(assetChild2);
    assetChild2Transform.parent = assetRoot;

    ClearEntireScene(registry);

    EXPECT_FALSE(registry.IsAlive(primitiveRoot));
    EXPECT_FALSE(registry.IsAlive(primitiveChild));
    EXPECT_FALSE(registry.IsAlive(assetRoot));
    EXPECT_FALSE(registry.IsAlive(assetChild1));
    EXPECT_FALSE(registry.IsAlive(assetChild2));
}

TEST_F(SceneBuilderTest, ClearDestroysEveryEntityIncludingUntaggedOnes)
{
    Registry registry;

    const Entity primitiveRoot = registry.CreateEntity();
    registry.AddComponent<Transform>(primitiveRoot);
    registry.AddComponent<PrimitiveSource>(primitiveRoot);

    const Entity cameraEntity = registry.CreateEntity();
    Transform& cameraTransform = registry.AddComponent<Transform>(cameraEntity);
    cameraTransform.position = Vec3(1.0f, 2.0f, 3.0f);
    registry.AddComponent<Camera>(cameraEntity);

    ClearEntireScene(registry);

    // ClearEntireScene() is UNCONDITIONAL (unlike the old, deleted
    // ClearSerializableSceneObjects() this test used to exercise) - a
    // Camera-only root is NO LONGER left untouched, since PHASE0's Locked
    // Design Decision #2 means there is no longer any entity kind this
    // feature does not own.
    EXPECT_FALSE(registry.IsAlive(primitiveRoot));
    EXPECT_FALSE(registry.IsAlive(cameraEntity));
}

TEST_F(SceneBuilderTest, ClearOnEmptyRegistryIsASafeNoOp)
{
    Registry registry;
    ClearEntireScene(registry);
    EXPECT_EQ(registry.AliveEntityCount(), 0u);
}

} // namespace
} // namespace gte
