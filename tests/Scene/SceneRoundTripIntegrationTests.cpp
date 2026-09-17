// task_manager/scene-serialization-2/PHASE3_JSON_SCENE_DOCUMENT_AND_HIERARCHY_SAVE_PLUS_GENERIC_LOAD.md's
// own Definition of Done requires "a manual/local sanity check ... save a
// scene containing a Camera with a non-default nearZ/farZ, plus a plain
// empty Transform+Name child node parented under it, reload, and confirm
// both values AND the parent/child relationship are restored exactly".
//
// Editor/SceneIO.cpp's real LoadScene() needs a live Game + Renderer (a
// GPU-dependent, Tier 2 dependency this test suite deliberately avoids -
// see AGENTS.md's "Testability & Regression Safety") - but its own
// reconstruction algorithm (Pass A: create bare entities; Pass B1: wire
// hierarchy; Pass B2: apply every reflected component's fields generically;
// Pass B3: restore sibling ordering) only ever touches a plain Registry -
// `renderer` is accepted but unused by this phase's own generic
// reconstruction (see SceneIO.cpp's own comment). This test reproduces
// that EXACT same sequence against a plain Registry, directly against
// Scene/SceneBuilder.h + Scene/SceneJsonFormat.h + ECS/Reflection - a real,
// automated stand-in for the phase's own required manual check, and a
// permanent regression guard for the whole round trip composing correctly.
//
// task_manager/scene-serialization-2/PHASE6_TESTS_DOCS_CLEANUP_AND_FULL_REGRESSION.md,
// section 3.4 - extended with a THIRD, independent root - a PrimitiveSource -
// alongside the original Camera+child pair, so this test also proves a
// second, unrelated root resolves correctly side-by-side with a parented
// hierarchy in the SAME document, end to end through the real
// BuildSceneDocumentFromRegistry() -> SerializeSceneDocument() ->
// DeserializeSceneDocument() -> reconstruction pipeline (not just the
// reflection primitives in isolation - Phase 1/2's own tests already cover
// those; this test's job is to catch an integration mistake between
// SceneBuilder.cpp and the registry that neither phase's own isolated tests
// could).

#include "Scene/SceneBuilder.h"
#include "Scene/SceneJsonFormat.h"

#include "ECS/Components/Camera.h"
#include "ECS/Components/Name.h"
#include "ECS/Components/PrimitiveSource.h"
#include "ECS/Components/Transform.h"
#include "ECS/Reflection/ComponentTypeRegistry.h"
#include "ECS/TransformHierarchy.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(SceneRoundTripIntegrationTest, CameraWithNonDefaultNearFarPlusChildTransformNameRoundTripsExactly)
{
    // --- Build the original scene: a Camera root (non-default nearZ/farZ)
    // with a plain Transform+Name child parented under it, PLUS a completely
    // independent PrimitiveSource root sibling. ---
    Registry sourceRegistry;

    const Entity cameraEntity = sourceRegistry.CreateEntity();
    Transform& cameraTransform = sourceRegistry.AddComponent<Transform>(cameraEntity);
    cameraTransform.position = Vec3(10.0f, 20.0f, 30.0f);
    Camera& camera = sourceRegistry.AddComponent<Camera>(cameraEntity);
    camera.nearZ = 0.05f;
    camera.farZ = 2500.0f;
    camera.fovYDegrees = 45.0f;
    sourceRegistry.AddComponent<Name>(cameraEntity).value = "MainCamera";

    const Entity childEntity = sourceRegistry.CreateEntity();
    Transform& childTransform = sourceRegistry.AddComponent<Transform>(childEntity);
    childTransform.parent = cameraEntity;
    childTransform.position = Vec3(1.0f, 2.0f, 3.0f);
    sourceRegistry.AddComponent<Name>(childEntity).value = "ChildNode";

    const Entity primitiveEntity = sourceRegistry.CreateEntity();
    Transform& primitiveTransform = sourceRegistry.AddComponent<Transform>(primitiveEntity);
    primitiveTransform.position = Vec3(-4.0f, 0.5f, 8.0f);
    sourceRegistry.AddComponent<PrimitiveSource>(primitiveEntity).type = PrimitiveType::Capsule;
    sourceRegistry.AddComponent<Name>(primitiveEntity).value = "MyCapsule";

    // --- "Save": BuildSceneDocumentFromRegistry() + SerializeSceneDocument() ---
    AssetDatabase emptyAssetDatabase; // Never refreshed - no *.gta files needed for this check.
    const SceneDocument document = BuildSceneDocumentFromRegistry(sourceRegistry, emptyAssetDatabase);
    ASSERT_EQ(document.entities.size(), 3u);

    const std::string text = SerializeSceneDocument(document);

    // --- "Load" (round trip through JSON text, exactly as a real file
    // write+read would) + the exact Pass A/B1/B2/B3 sequence
    // Editor/SceneIO.cpp's LoadScene() uses against a live Registry. ---
    const std::optional<SceneDocument> parsedDocument = DeserializeSceneDocument(text);
    ASSERT_TRUE(parsedDocument.has_value());
    ASSERT_EQ(parsedDocument->entities.size(), 3u);

    Registry destinationRegistry;
    std::vector<Entity> resultEntities(parsedDocument->entities.size(), kInvalidEntity);

    // Pass A - a default Transform is added up front so Pass B1's
    // SetParent() below (which requires one on both sides) succeeds - see
    // Editor/SceneIO.cpp's own matching comment for why the phase file's
    // literal "bare CreateEntity() only" pseudocode does not actually work.
    for (std::size_t i = 0; i < parsedDocument->entities.size(); ++i) {
        resultEntities[i] = destinationRegistry.CreateEntity();
        destinationRegistry.AddComponent<Transform>(resultEntities[i]);
    }

    // Pass B1.
    for (std::size_t i = 0; i < parsedDocument->entities.size(); ++i) {
        const SceneEntityRecord& record = parsedDocument->entities[i];
        if (record.parentIndex.has_value()) {
            SetParent(destinationRegistry, resultEntities[i], resultEntities[*record.parentIndex],
                /*worldPositionStays=*/false);
        }
    }

    // Pass B2.
    for (std::size_t i = 0; i < parsedDocument->entities.size(); ++i) {
        const SceneEntityRecord& record = parsedDocument->entities[i];
        const Entity entity = resultEntities[i];
        for (auto it = record.components.begin(); it != record.components.end(); ++it) {
            const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find(it.key());
            ASSERT_NE(descriptor, nullptr) << "Every component type used by this test must be registered (Phase 2).";
            descriptor->ensureDefaultComponent(destinationRegistry, entity);
            void* component = descriptor->tryGetMutableComponent(destinationRegistry, entity);
            for (const FieldDescriptor& field : descriptor->fields) {
                std::string errorMessage;
                EXPECT_TRUE(field.readJson(component, it.value(), errorMessage)) << errorMessage;
            }
        }
    }

    // Pass B3.
    for (std::size_t i = 0; i < parsedDocument->entities.size(); ++i) {
        SetSiblingIndex(destinationRegistry, resultEntities[i], parsedDocument->entities[i].siblingIndex);
    }

    // --- Verify: find the reconstructed Camera root, its child, and the
    // independent PrimitiveSource root by Name, rather than assuming array
    // order (BuildSceneDocumentFromRegistry() never documented one). ---
    Entity reconstructedCamera = kInvalidEntity;
    Entity reconstructedChild = kInvalidEntity;
    Entity reconstructedPrimitive = kInvalidEntity;
    for (const Entity entity : resultEntities) {
        if (const Name* name = destinationRegistry.TryGetComponent<Name>(entity); name != nullptr) {
            if (name->value == "MainCamera") {
                reconstructedCamera = entity;
            } else if (name->value == "ChildNode") {
                reconstructedChild = entity;
            } else if (name->value == "MyCapsule") {
                reconstructedPrimitive = entity;
            }
        }
    }
    ASSERT_NE(reconstructedCamera, kInvalidEntity);
    ASSERT_NE(reconstructedChild, kInvalidEntity);
    ASSERT_NE(reconstructedPrimitive, kInvalidEntity);

    // Camera's own non-default nearZ/farZ/fovYDegrees round-tripped exactly.
    ASSERT_TRUE(destinationRegistry.HasComponent<Camera>(reconstructedCamera));
    const Camera& reconstructedCameraComponent = destinationRegistry.GetComponent<Camera>(reconstructedCamera);
    EXPECT_FLOAT_EQ(reconstructedCameraComponent.nearZ, 0.05f);
    EXPECT_FLOAT_EQ(reconstructedCameraComponent.farZ, 2500.0f);
    EXPECT_FLOAT_EQ(reconstructedCameraComponent.fovYDegrees, 45.0f);

    // Camera's own Transform (world-space position, since it's a root)
    // round-tripped exactly too.
    ASSERT_TRUE(destinationRegistry.HasComponent<Transform>(reconstructedCamera));
    EXPECT_TRUE(ApproximatelyEqual(
        destinationRegistry.GetComponent<Transform>(reconstructedCamera).position, Vec3(10.0f, 20.0f, 30.0f)));

    // The child's own parent/child relationship survived the round trip -
    // its live Transform::parent now points at the reconstructed Camera
    // entity (a DIFFERENT Entity handle than the original session's, by
    // design - Entity handles are session-local, never serialized
    // directly, see SceneDocument.h's own doc comment).
    ASSERT_TRUE(destinationRegistry.HasComponent<Transform>(reconstructedChild));
    EXPECT_EQ(destinationRegistry.GetComponent<Transform>(reconstructedChild).parent, reconstructedCamera);
    EXPECT_TRUE(ApproximatelyEqual(
        destinationRegistry.GetComponent<Transform>(reconstructedChild).position, Vec3(1.0f, 2.0f, 3.0f)));

    // And the hierarchy is genuinely walkable from the root down (not just
    // a raw field match) via ECS/TransformHierarchy.h's own GetChildren().
    const std::vector<Entity> cameraChildren = GetChildren(destinationRegistry, reconstructedCamera);
    ASSERT_EQ(cameraChildren.size(), 1u);
    EXPECT_EQ(cameraChildren[0], reconstructedChild);

    // The independent PrimitiveSource root resolved correctly too, entirely
    // unaffected by the unrelated Camera+child hierarchy in the same
    // document - its own type/Transform round-tripped exactly, and it is a
    // genuine root (no parent) in the reconstructed registry.
    ASSERT_TRUE(destinationRegistry.HasComponent<PrimitiveSource>(reconstructedPrimitive));
    EXPECT_EQ(destinationRegistry.GetComponent<PrimitiveSource>(reconstructedPrimitive).type, PrimitiveType::Capsule);
    ASSERT_TRUE(destinationRegistry.HasComponent<Transform>(reconstructedPrimitive));
    EXPECT_TRUE(ApproximatelyEqual(
        destinationRegistry.GetComponent<Transform>(reconstructedPrimitive).position, Vec3(-4.0f, 0.5f, 8.0f)));
    EXPECT_EQ(destinationRegistry.GetComponent<Transform>(reconstructedPrimitive).parent, kInvalidEntity);
}

} // namespace
} // namespace gte
