// ToonShading is registered for scenes and survives a save and load.

#include "ECS/Components/Name.h"
#include "ECS/Components/ToonShading.h"
#include "ECS/Components/Transform.h"
#include "ECS/Reflection/ComponentTypeRegistry.h"
#include "Scene/SceneBuilder.h"
#include "Scene/SceneJsonFormat.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace gte {
namespace {

TEST(ToonShadingReflectionTest, IsRegisteredWithEnabledField)
{
    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("ToonShading");
    ASSERT_NE(descriptor, nullptr);
    ASSERT_EQ(descriptor->fields.size(), 1u);
    EXPECT_EQ(descriptor->fields[0].name, "enabled");
}

TEST(ToonShadingReflectionTest, SceneRoundTripKeepsTheValues)
{
    Registry source;
    const Entity entity = source.CreateEntity();
    source.AddComponent<Transform>(entity);
    source.AddComponent<Name>(entity).value = "Model";
    ToonShading& toon = source.AddComponent<ToonShading>(entity);
    toon.enabled = false;

    AssetDatabase noAssets;
    const std::string text = SerializeSceneDocument(BuildSceneDocumentFromRegistry(source, noAssets));
    const std::optional<SceneDocument> parsed = DeserializeSceneDocument(text);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->entities.size(), 1u);
    ASSERT_TRUE(parsed->entities[0].components.contains("ToonShading"));

    const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("ToonShading");
    ASSERT_NE(descriptor, nullptr);
    Registry loaded;
    const Entity target = loaded.CreateEntity();
    descriptor->ensureDefaultComponent(loaded, target);
    void* component = descriptor->tryGetMutableComponent(loaded, target);
    ASSERT_NE(component, nullptr);
    for (const FieldDescriptor& field : descriptor->fields) {
        std::string error;
        EXPECT_TRUE(field.readJson(component, parsed->entities[0].components.at("ToonShading"), error)) << error;
    }
    EXPECT_FALSE(loaded.GetComponent<ToonShading>(target).enabled);
}

} // namespace
} // namespace gte
