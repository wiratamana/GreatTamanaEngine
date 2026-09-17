// Unit tests for the real, built-in component reflection registrations
// (src/ECS/Reflection/BuiltinComponentReflection.cpp) - see
// task_manager/scene-serialization-2/PHASE2_BUILTIN_COMPONENT_REFLECTION_REGISTRATION.md.
//
// ComponentTypeRegistry::Instance() self-bootstraps
// RegisterBuiltinComponentReflections() the very first time it's ever
// called (ComponentTypeRegistry.cpp) - simply calling Instance() anywhere
// below is enough to guarantee Transform/Name/Camera/DirectionalLight/
// PrimitiveSource are registered, with no explicit setup call needed.

#include "ECS/Reflection/ComponentTypeRegistry.h"
#include "ECS/Reflection/MathJsonAdapters.h"
#include "ECS/Registry.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/DirectionalLight.h"
#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/MeshAssetSource.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Name.h"
#include "ECS/Components/PrimitiveSource.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Components/SkeletalAnimator.h"
#include "ECS/Components/Transform.h"
#include "Renderer/Primitives/PrimitiveMeshGenerator.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gte {
namespace {

const FieldDescriptor* FindField(const ComponentTypeDescriptor& descriptor, const std::string& name)
{
    for (const FieldDescriptor& field : descriptor.fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

TEST(BuiltinComponentReflectionTest, TransformIsRegisteredWithExactlyPositionRotationScale)
{
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("Transform");
    ASSERT_NE(descriptor, nullptr);
    ASSERT_EQ(descriptor->fields.size(), 3u);
    EXPECT_NE(FindField(*descriptor, "position"), nullptr);
    EXPECT_NE(FindField(*descriptor, "rotation"), nullptr);
    EXPECT_NE(FindField(*descriptor, "scale"), nullptr);
    // Deliberately NOT reflected - see BuiltinComponentReflection.cpp's own
    // comment: `parent`/`siblingIndex` are captured at the SceneDocument
    // entity-record level instead (PHASE0 Appendix A), never inside the
    // generic component bag.
    EXPECT_EQ(FindField(*descriptor, "parent"), nullptr);
    EXPECT_EQ(FindField(*descriptor, "siblingIndex"), nullptr);
}

TEST(BuiltinComponentReflectionTest, NameIsRegisteredWithExactlyValue)
{
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("Name");
    ASSERT_NE(descriptor, nullptr);
    ASSERT_EQ(descriptor->fields.size(), 1u);
    EXPECT_NE(FindField(*descriptor, "value"), nullptr);
}

TEST(BuiltinComponentReflectionTest, CameraIsRegisteredWithExactlyFourFields)
{
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("Camera");
    ASSERT_NE(descriptor, nullptr);
    ASSERT_EQ(descriptor->fields.size(), 4u);
    EXPECT_NE(FindField(*descriptor, "fovYDegrees"), nullptr);
    EXPECT_NE(FindField(*descriptor, "nearZ"), nullptr);
    EXPECT_NE(FindField(*descriptor, "farZ"), nullptr);
    EXPECT_NE(FindField(*descriptor, "active"), nullptr);
}

TEST(BuiltinComponentReflectionTest, DirectionalLightIsRegisteredWithExactlyThreeFields)
{
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("DirectionalLight");
    ASSERT_NE(descriptor, nullptr);
    ASSERT_EQ(descriptor->fields.size(), 3u);
    EXPECT_NE(FindField(*descriptor, "color"), nullptr);
    EXPECT_NE(FindField(*descriptor, "illuminanceLux"), nullptr);
    EXPECT_NE(FindField(*descriptor, "active"), nullptr);
}

TEST(BuiltinComponentReflectionTest, PrimitiveSourceIsRegisteredWithExactlyType)
{
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("PrimitiveSource");
    ASSERT_NE(descriptor, nullptr);
    ASSERT_EQ(descriptor->fields.size(), 1u);
    EXPECT_NE(FindField(*descriptor, "type"), nullptr);
}

TEST(BuiltinComponentReflectionTest, HandleBearingOrDerivedOrRuntimeComponentsAreNeverRegistered)
{
    // Regression guard (PHASE2's own Definition of Done) - a future
    // accidental registration of one of these must be caught here
    // immediately, not discovered later as a silent GPU-resource-pool
    // corruption bug or a stale-physics-state bug in the field.
    EXPECT_EQ(ComponentTypeRegistry::Instance().Find("MeshRenderer"), nullptr);
    EXPECT_EQ(ComponentTypeRegistry::Instance().Find("MeshAssetSource"), nullptr);
    EXPECT_EQ(ComponentTypeRegistry::Instance().Find("SkeletalAnimator"), nullptr);
    EXPECT_EQ(ComponentTypeRegistry::Instance().Find("DynamicChainRig"), nullptr);
    EXPECT_EQ(ComponentTypeRegistry::Instance().Find("ResolvedAnimationPose"), nullptr);
}

TEST(BuiltinComponentReflectionTest, CameraRoundTripsNearFarThroughARealRegistry)
{
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("Camera");
    ASSERT_NE(descriptor, nullptr);

    Registry registry;
    const Entity source = registry.CreateEntity();
    registry.AddComponent<Camera>(source);
    Camera& sourceCamera = registry.GetComponent<Camera>(source);
    sourceCamera.fovYDegrees = 45.0f;
    sourceCamera.nearZ = 0.25f;
    sourceCamera.farZ = 2500.0f;
    sourceCamera.active = false;

    // Serialize, exactly the way Phase 3/4's generic Save will: via
    // tryGetConstComponent + every field's own writeJson.
    const void* sourceComponent = descriptor->tryGetConstComponent(registry, source);
    ASSERT_NE(sourceComponent, nullptr);
    nlohmann::json fields;
    for (const FieldDescriptor& field : descriptor->fields) {
        field.writeJson(sourceComponent, fields);
    }

    // Apply back onto a FRESH entity's fresh, default Camera, exactly the
    // way Phase 3/4's generic Load will: via ensureDefaultComponent + every
    // field's own readJson.
    const Entity target = registry.CreateEntity();
    descriptor->ensureDefaultComponent(registry, target);
    void* targetComponent = descriptor->tryGetMutableComponent(registry, target);
    ASSERT_NE(targetComponent, nullptr);
    std::string errorMessage;
    for (const FieldDescriptor& field : descriptor->fields) {
        EXPECT_TRUE(field.readJson(targetComponent, fields, errorMessage)) << errorMessage;
    }

    const Camera& targetCamera = registry.GetComponent<Camera>(target);
    EXPECT_FLOAT_EQ(targetCamera.fovYDegrees, 45.0f);
    EXPECT_FLOAT_EQ(targetCamera.nearZ, 0.25f);
    EXPECT_FLOAT_EQ(targetCamera.farZ, 2500.0f);
    EXPECT_FALSE(targetCamera.active);
}

TEST(BuiltinComponentReflectionTest, PrimitiveSourceEnumRoundTripsForEveryPrimitiveType)
{
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("PrimitiveSource");
    ASSERT_NE(descriptor, nullptr);
    const FieldDescriptor* typeField = FindField(*descriptor, "type");
    ASSERT_NE(typeField, nullptr);

    const PrimitiveType allTypes[] = {
        PrimitiveType::Cube,
        PrimitiveType::Sphere,
        PrimitiveType::Capsule,
        PrimitiveType::Cone,
        PrimitiveType::Plane,
    };

    for (PrimitiveType type : allTypes) {
        PrimitiveSource source;
        source.type = type;
        nlohmann::json fields;
        typeField->writeJson(&source, fields);

        PrimitiveSource target;
        target.type = (type == PrimitiveType::Cube) ? PrimitiveType::Sphere : PrimitiveType::Cube; // deliberately different, to prove readJson actually wrote it
        std::string errorMessage;
        EXPECT_TRUE(typeField->readJson(&target, fields, errorMessage)) << errorMessage;
        EXPECT_EQ(target.type, type);
    }
}

TEST(BuiltinComponentReflectionTest, PrimitiveSourceEnumRejectsAnUnrecognizedStringWithoutMutating)
{
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("PrimitiveSource");
    ASSERT_NE(descriptor, nullptr);
    const FieldDescriptor* typeField = FindField(*descriptor, "type");
    ASSERT_NE(typeField, nullptr);

    nlohmann::json fields;
    fields["type"] = "NotAShape";

    PrimitiveSource target;
    target.type = PrimitiveType::Sphere; // a deliberately non-default value, to prove it survives a failed readJson untouched.
    std::string errorMessage;
    bool ok = true;
    EXPECT_NO_THROW(ok = typeField->readJson(&target, fields, errorMessage));
    EXPECT_FALSE(ok);
    EXPECT_FALSE(errorMessage.empty());
    EXPECT_EQ(target.type, PrimitiveType::Sphere); // unchanged - never a partial/corrupt write.
}

} // namespace
} // namespace gte
