// Unit tests for src/Scene/SceneBuilder.h - BuildSceneDocumentFromRegistry()
// (Registry + AssetDatabase -> SceneDocument) and
// ClearSerializableSceneObjects() (wipes only what this feature owns before
// a Load). Touches a real temp directory (to build a real AssetDatabase
// against real *.gta files, same pattern as tests/Assets/AssetDatabaseTests.cpp)
// but no GPU/SDL/ImGui at all - "Tier 1" per tests/CMakeLists.txt's own
// taxonomy. Always built - src/Scene/ has no GTE_ENABLE_EDITOR/
// GTE_ENABLE_PROJECT_PANEL dependency.

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
    ASSERT_EQ(document.objects.size(), 1u);

    const SceneObjectRecord& record = document.objects[0];
    EXPECT_EQ(record.kind, SceneObjectKind::Primitive);
    EXPECT_EQ(record.primitiveType, PrimitiveType::Sphere);
    EXPECT_EQ(record.name, "MySphere");
    EXPECT_TRUE(ApproximatelyEqual(record.position, transform.position));
    EXPECT_TRUE(ApproximatelyEqual(record.rotation, transform.rotation));
    EXPECT_TRUE(ApproximatelyEqual(record.scale, transform.scale));
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
    ASSERT_EQ(document.objects.size(), 1u);
    EXPECT_EQ(document.objects[0].kind, SceneObjectKind::Asset);
    EXPECT_EQ(document.objects[0].assetGuid, knownGuid);
}

// (v2) Path-normalization invariant: a differently-spelled-but-equivalent
// path (built via std::filesystem::path segment joining rather than a raw
// hand-concatenated string) still resolves correctly, proving this bridge's
// reliance on AssetDatabase's own std::filesystem::absolute()-based
// normalization actually holds.
TEST_F(SceneBuilderTest, AssetRootResolvesViaNormalizedEquivalentPath)
{
    const std::filesystem::path gtaPath = m_root / "Sub" / "model.gta";
    std::filesystem::create_directories(m_root / "Sub");
    const Guid knownGuid = Guid::Generate();
    ASSERT_TRUE(WriteGtaFile(gtaPath, AssetType::Mesh, knownGuid, AssetFlags::None, {}, {}));
    ASSERT_EQ(m_db.RefreshFromDirectory(m_root), 1u);

    // Build an equivalent path independently, by appending segments one at a
    // time via std::filesystem::path::operator/= (the same way
    // SDL_GetBasePath()-derived code builds a path today) rather than reusing
    // the exact same path object constructed above - std::filesystem::
    // absolute() does not collapse lexical segments like "." or ".." (only
    // canonical() does), so this deliberately does NOT introduce one; it only
    // proves two INDEPENDENTLY-CONSTRUCTED path objects that are already
    // equivalent still resolve correctly through FindByPath()'s own
    // absolute()-based normalization.
    std::filesystem::path equivalentPath = m_root;
    equivalentPath /= "Sub";
    equivalentPath /= "model.gta";

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity);
    registry.AddComponent<MeshAssetSource>(entity).gtaPath = equivalentPath.string();

    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, m_db);
    ASSERT_EQ(document.objects.size(), 1u);
    EXPECT_EQ(document.objects[0].kind, SceneObjectKind::Asset);
    EXPECT_EQ(document.objects[0].assetGuid, knownGuid);
}

TEST_F(SceneBuilderTest, AssetRootWithUntrackedPathIsSkipped)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity);
    registry.AddComponent<MeshAssetSource>(entity).gtaPath = (m_root / "NeverImported.gta").string();

    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, m_db);
    EXPECT_TRUE(document.objects.empty());
}

TEST_F(SceneBuilderTest, RootWithNeitherTagIsSkipped)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity);
    registry.AddComponent<Camera>(entity);

    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, m_db);
    EXPECT_TRUE(document.objects.empty());
}

TEST_F(SceneBuilderTest, ChildEntityIsNeverIndependentlyVisited)
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
    registry.AddComponent<MeshAssetSource>(child).gtaPath = gtaPath.string(); // Even if it also carries the tag.

    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, m_db);
    ASSERT_EQ(document.objects.size(), 1u);
    EXPECT_EQ(document.objects[0].kind, SceneObjectKind::Asset);
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

    const Entity skippedEntity = registry.CreateEntity();
    registry.AddComponent<Transform>(skippedEntity);
    registry.AddComponent<Camera>(skippedEntity);

    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, m_db);
    ASSERT_EQ(document.objects.size(), 2u);

    bool sawPrimitive = false;
    bool sawAsset = false;
    for (const SceneObjectRecord& record : document.objects) {
        if (record.kind == SceneObjectKind::Primitive) {
            sawPrimitive = true;
            EXPECT_EQ(record.primitiveType, PrimitiveType::Cube);
        } else if (record.kind == SceneObjectKind::Asset) {
            sawAsset = true;
            EXPECT_EQ(record.assetGuid, knownGuid);
        }
    }
    EXPECT_TRUE(sawPrimitive);
    EXPECT_TRUE(sawAsset);
}

// --- ClearSerializableSceneObjects() -----------------------------------------

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

    ClearSerializableSceneObjects(registry);

    EXPECT_FALSE(registry.IsAlive(primitiveRoot));
    EXPECT_FALSE(registry.IsAlive(primitiveChild));
    EXPECT_FALSE(registry.IsAlive(assetRoot));
    EXPECT_FALSE(registry.IsAlive(assetChild1));
    EXPECT_FALSE(registry.IsAlive(assetChild2));
}

TEST_F(SceneBuilderTest, ClearLeavesUntaggedEntitiesUntouched)
{
    Registry registry;

    const Entity primitiveRoot = registry.CreateEntity();
    registry.AddComponent<Transform>(primitiveRoot);
    registry.AddComponent<PrimitiveSource>(primitiveRoot);

    const Entity cameraEntity = registry.CreateEntity();
    Transform& cameraTransform = registry.AddComponent<Transform>(cameraEntity);
    cameraTransform.position = Vec3(1.0f, 2.0f, 3.0f);
    registry.AddComponent<Camera>(cameraEntity);

    ClearSerializableSceneObjects(registry);

    EXPECT_FALSE(registry.IsAlive(primitiveRoot));
    ASSERT_TRUE(registry.IsAlive(cameraEntity));
    EXPECT_TRUE(registry.HasComponent<Transform>(cameraEntity));
    EXPECT_TRUE(registry.HasComponent<Camera>(cameraEntity));
    EXPECT_TRUE(ApproximatelyEqual(registry.GetComponent<Transform>(cameraEntity).position, Vec3(1.0f, 2.0f, 3.0f)));
}

TEST_F(SceneBuilderTest, ClearOnEmptyRegistryIsASafeNoOp)
{
    Registry registry;
    ClearSerializableSceneObjects(registry);
    EXPECT_EQ(registry.AliveEntityCount(), 0u);
}

} // namespace
} // namespace gte
