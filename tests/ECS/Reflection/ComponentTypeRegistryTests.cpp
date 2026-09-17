// Unit tests for the brand-new generic field-reflection primitives
// (src/ECS/Reflection/{ComponentTypeDescriptor.h, ComponentTypeRegistry.h/.inl/.cpp,
// ReflectFieldMacros.h, MathJsonAdapters.h}) - see
// task_manager/scene-serialization-2/PHASE1_REFLECTION_CORE_AND_MATH_JSON_ADAPTERS.md.
//
// Deliberately exercised against a small, PRIVATE, test-file-local struct
// (DummyReflectedComponent) - NOT a real ECS component - so this phase's own
// tests stay fully independent of a later phase's real component
// registrations (Transform/Camera/etc.).
//
// ComponentTypeRegistry::Instance() is a genuine process-wide singleton, so
// every TEST() below that needs DummyReflectedComponent registered goes
// through EnsureDummyReflectedComponentRegistered() (idempotent - registering
// the exact same typeName twice would trip the registry's own debug-only
// duplicate-registration assert), and every test that verifies "unknown
// before registration" behavior uses its OWN distinct, nowhere-else-used
// typeName so it can never be polluted by another test in this same binary
// having already registered something.

#include "ECS/Reflection/ComponentTypeRegistry.h"
#include "ECS/Reflection/MathJsonAdapters.h"
#include "ECS/Reflection/ReflectFieldMacros.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gte {
namespace {

struct DummyReflectedComponent {
    float value = 1.0f;
    std::string label;
    Vec3 offset;
};

void EnsureDummyReflectedComponentRegistered()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    RegisterComponentType<DummyReflectedComponent>("DummyReflectedComponent", {
        GTE_REFLECT_FIELD(DummyReflectedComponent, value),
        GTE_REFLECT_FIELD(DummyReflectedComponent, label),
        GTE_REFLECT_FIELD(DummyReflectedComponent, offset),
    });
}

const FieldDescriptor* FindField(const ComponentTypeDescriptor& descriptor, const std::string& name)
{
    for (const FieldDescriptor& field : descriptor.fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

TEST(ComponentTypeRegistryTest, FindReturnsNullForATypeNameNeverRegisteredAnywhere)
{
    EXPECT_EQ(ComponentTypeRegistry::Instance().Find("SomeTypeNameNeverRegisteredInThisTestBinary"), nullptr);
}

TEST(ComponentTypeRegistryTest, RegisterThenFindReturnsAMatchingDescriptor)
{
    EnsureDummyReflectedComponentRegistered();

    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("DummyReflectedComponent");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->typeName, "DummyReflectedComponent");
    ASSERT_EQ(descriptor->fields.size(), 3u);
    EXPECT_NE(FindField(*descriptor, "value"), nullptr);
    EXPECT_NE(FindField(*descriptor, "label"), nullptr);
    EXPECT_NE(FindField(*descriptor, "offset"), nullptr);
}

TEST(ComponentTypeRegistryTest, HasComponentTryGetAndEnsureDefaultComponentWrapARealRegistry)
{
    EnsureDummyReflectedComponentRegistered();
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("DummyReflectedComponent");
    ASSERT_NE(descriptor, nullptr);

    Registry registry;
    const Entity entity = registry.CreateEntity();

    EXPECT_FALSE(descriptor->hasComponent(registry, entity));
    EXPECT_EQ(descriptor->tryGetConstComponent(registry, entity), nullptr);

    descriptor->ensureDefaultComponent(registry, entity);
    EXPECT_TRUE(descriptor->hasComponent(registry, entity));

    void* mutableComponent = descriptor->tryGetMutableComponent(registry, entity);
    ASSERT_NE(mutableComponent, nullptr);
    EXPECT_EQ(static_cast<DummyReflectedComponent*>(mutableComponent)->value, 1.0f); // default-constructed value

    // Calling ensureDefaultComponent again on an entity that already has the
    // component must not reset it back to a fresh default (AddComponent<T>()
    // overwrites in place ONLY when not already present per ComponentStorage's
    // own Add() semantics is what ComponentTypeRegistry.inl actually relies
    // on - it explicitly checks hasComponent first).
    static_cast<DummyReflectedComponent*>(mutableComponent)->value = 55.0f;
    descriptor->ensureDefaultComponent(registry, entity);
    EXPECT_EQ(registry.GetComponent<DummyReflectedComponent>(entity).value, 55.0f);
}

TEST(ComponentTypeRegistryTest, RoundTripsEveryFieldThroughWriteJsonThenReadJson)
{
    EnsureDummyReflectedComponentRegistered();
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("DummyReflectedComponent");
    ASSERT_NE(descriptor, nullptr);

    DummyReflectedComponent source;
    source.value = 42.5f;
    source.label = "hello";
    source.offset = Vec3(1.0f, 2.0f, 3.0f);

    nlohmann::json fields;
    for (const FieldDescriptor& field : descriptor->fields) {
        field.writeJson(&source, fields);
    }
    EXPECT_TRUE(fields.contains("value"));
    EXPECT_TRUE(fields.contains("label"));
    EXPECT_TRUE(fields.contains("offset"));

    DummyReflectedComponent target; // freshly default-constructed
    std::string errorMessage;
    for (const FieldDescriptor& field : descriptor->fields) {
        EXPECT_TRUE(field.readJson(&target, fields, errorMessage)) << errorMessage;
    }

    EXPECT_FLOAT_EQ(target.value, 42.5f);
    EXPECT_EQ(target.label, "hello");
    EXPECT_TRUE(ApproximatelyEqual(target.offset, Vec3(1.0f, 2.0f, 3.0f)));
}

TEST(ComponentTypeRegistryTest, MissingKeyKeepsPreExistingValueAndStillReturnsTrue)
{
    EnsureDummyReflectedComponentRegistered();
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("DummyReflectedComponent");
    ASSERT_NE(descriptor, nullptr);

    const nlohmann::json emptyFields = nlohmann::json::object();

    DummyReflectedComponent target;
    target.value = 7.0f;
    target.label = "keep-me";
    target.offset = Vec3(9.0f, 9.0f, 9.0f);

    std::string errorMessage;
    for (const FieldDescriptor& field : descriptor->fields) {
        EXPECT_TRUE(field.readJson(&target, emptyFields, errorMessage)) << errorMessage;
    }

    EXPECT_FLOAT_EQ(target.value, 7.0f);
    EXPECT_EQ(target.label, "keep-me");
    EXPECT_TRUE(ApproximatelyEqual(target.offset, Vec3(9.0f, 9.0f, 9.0f)));
}

TEST(ComponentTypeRegistryTest, ExtraUnrecognizedKeyIsSilentlyIgnored)
{
    EnsureDummyReflectedComponentRegistered();
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("DummyReflectedComponent");
    ASSERT_NE(descriptor, nullptr);

    nlohmann::json fields;
    fields["value"] = 3.0f;
    fields["totallyUnknownFieldFromANewerBuild"] = "surprise";

    DummyReflectedComponent target;
    std::string errorMessage;
    for (const FieldDescriptor& field : descriptor->fields) {
        EXPECT_TRUE(field.readJson(&target, fields, errorMessage)) << errorMessage;
    }

    EXPECT_FLOAT_EQ(target.value, 3.0f);
}

TEST(ComponentTypeRegistryTest, WrongJsonTypeForAPlainFieldFailsCleanlyWithoutThrowing)
{
    EnsureDummyReflectedComponentRegistered();
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("DummyReflectedComponent");
    ASSERT_NE(descriptor, nullptr);
    const FieldDescriptor* valueField = FindField(*descriptor, "value");
    ASSERT_NE(valueField, nullptr);

    nlohmann::json fields;
    fields["value"] = "not-a-number";

    DummyReflectedComponent target;
    std::string errorMessage;
    bool ok = true;
    EXPECT_NO_THROW(ok = valueField->readJson(&target, fields, errorMessage));
    EXPECT_FALSE(ok);
    EXPECT_FALSE(errorMessage.empty());
}

TEST(ComponentTypeRegistryTest, MalformedVec3ArrayFailsCleanlyWithoutThrowing)
{
    EnsureDummyReflectedComponentRegistered();
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("DummyReflectedComponent");
    ASSERT_NE(descriptor, nullptr);
    const FieldDescriptor* offsetField = FindField(*descriptor, "offset");
    ASSERT_NE(offsetField, nullptr);

    nlohmann::json fields;
    fields["offset"] = { 1.0f, 2.0f }; // Vec3 needs exactly 3 elements

    DummyReflectedComponent target;
    std::string errorMessage;
    bool ok = true;
    EXPECT_NO_THROW(ok = offsetField->readJson(&target, fields, errorMessage));
    EXPECT_FALSE(ok);
    EXPECT_FALSE(errorMessage.empty());
}

struct AAlphabeticallyFirstDummyComponent { int v = 0; };
struct ZAlphabeticallyLastDummyComponent { int v = 0; };

TEST(ComponentTypeRegistryTest, AllSortedByTypeNameIsAlphabeticalRegardlessOfRegistrationOrder)
{
    static bool registered = false;
    if (!registered) {
        registered = true;
        // Deliberately registered Z before A, to prove sorting - not
        // registration order - decides AllSortedByTypeName()'s iteration order.
        RegisterComponentType<ZAlphabeticallyLastDummyComponent>("ZAlphabeticallyLastDummyComponent", {
            GTE_REFLECT_FIELD(ZAlphabeticallyLastDummyComponent, v),
        });
        RegisterComponentType<AAlphabeticallyFirstDummyComponent>("AAlphabeticallyFirstDummyComponent", {
            GTE_REFLECT_FIELD(AAlphabeticallyFirstDummyComponent, v),
        });
    }

    const std::vector<ComponentTypeDescriptor>& all = ComponentTypeRegistry::Instance().AllSortedByTypeName();

    // The whole table must be sorted ascending, not just these two entries -
    // other TESTs in this same binary have registered other typeNames too.
    for (std::size_t i = 1; i < all.size(); ++i) {
        EXPECT_LE(all[i - 1].typeName, all[i].typeName);
    }

    int indexA = -1;
    int indexZ = -1;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].typeName == "AAlphabeticallyFirstDummyComponent") { indexA = static_cast<int>(i); }
        if (all[i].typeName == "ZAlphabeticallyLastDummyComponent") { indexZ = static_cast<int>(i); }
    }
    ASSERT_NE(indexA, -1);
    ASSERT_NE(indexZ, -1);
    EXPECT_LT(indexA, indexZ);
}

} // namespace
} // namespace gte
