// Unit tests for src/Scene/SceneJsonFormat.h - SerializeSceneDocument()/
// DeserializeSceneDocument() round-tripping the *.gtscene v2 JSON format
// (src/Scene/SceneDocument.h). Replaces the deleted
// tests/Scene/SceneTextFormatTests.cpp - see
// task_manager/scene-serialization-2/PHASE3_JSON_SCENE_DOCUMENT_AND_HIERARCHY_SAVE_PLUS_GENERIC_LOAD.md.
// Pure logic - no ECS/Renderer/filesystem I/O involved at all - "Tier 1" per
// tests/CMakeLists.txt's own taxonomy. Always built - src/Scene/ has no
// GTE_ENABLE_EDITOR/GTE_ENABLE_PROJECT_PANEL dependency.

#include "Scene/SceneJsonFormat.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(SceneJsonFormatTest, RoundTripsEmptyDocument)
{
    const SceneDocument document;
    const std::string text = SerializeSceneDocument(document);

    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->entities.empty());
}

TEST(SceneJsonFormatTest, RoundTripsOneEntityWithNoParentNoComponents)
{
    SceneDocument document;
    SceneEntityRecord record;
    record.siblingIndex = 3;
    document.entities.push_back(record);

    const std::string text = SerializeSceneDocument(document);
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->entities.size(), 1u);

    const SceneEntityRecord& parsed = result->entities[0];
    EXPECT_FALSE(parsed.parentIndex.has_value());
    EXPECT_EQ(parsed.siblingIndex, 3u);
    EXPECT_TRUE(parsed.assetGuid.empty());
    EXPECT_TRUE(parsed.components.empty());
}

TEST(SceneJsonFormatTest, RoundTripsThreeLevelDeepParentChainByIndex)
{
    SceneDocument document;

    SceneEntityRecord root;
    document.entities.push_back(root); // index 0

    SceneEntityRecord child;
    child.parentIndex = 0;
    document.entities.push_back(child); // index 1

    SceneEntityRecord grandchild;
    grandchild.parentIndex = 1;
    document.entities.push_back(grandchild); // index 2

    const std::string text = SerializeSceneDocument(document);
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->entities.size(), 3u);

    EXPECT_FALSE(result->entities[0].parentIndex.has_value());
    ASSERT_TRUE(result->entities[1].parentIndex.has_value());
    EXPECT_EQ(*result->entities[1].parentIndex, 0u);
    ASSERT_TRUE(result->entities[2].parentIndex.has_value());
    EXPECT_EQ(*result->entities[2].parentIndex, 1u);
}

TEST(SceneJsonFormatTest, RoundTripsArbitraryComponentsBag)
{
    SceneDocument document;
    SceneEntityRecord record;
    record.components["Transform"] = nlohmann::json::object(
        { { "position", nlohmann::json::array({ 1.0, 2.0, 3.0 }) }, { "rotation", nlohmann::json::array({ 0.0, 0.0, 0.0, 1.0 }) },
            { "scale", nlohmann::json::array({ 1.0, 1.0, 1.0 }) } });
    record.components["Camera"] = nlohmann::json::object(
        { { "fovYDegrees", 60.0 }, { "nearZ", 0.1 }, { "farZ", 1000.0 }, { "active", true } });
    document.entities.push_back(record);

    const std::string text = SerializeSceneDocument(document);
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->entities.size(), 1u);

    const nlohmann::json& components = result->entities[0].components;
    ASSERT_TRUE(components.contains("Transform"));
    ASSERT_TRUE(components.contains("Camera"));
    EXPECT_DOUBLE_EQ(components["Camera"]["nearZ"].get<double>(), 0.1);
    EXPECT_DOUBLE_EQ(components["Camera"]["farZ"].get<double>(), 1000.0);
    EXPECT_EQ(components["Transform"]["position"][1].get<double>(), 2.0);
}

TEST(SceneJsonFormatTest, RoundTripsAssetGuidWhenPresent)
{
    SceneDocument document;
    SceneEntityRecord record;
    record.assetGuid = "0123456789abcdef0123456789abcdef";
    document.entities.push_back(record);

    const std::string text = SerializeSceneDocument(document);
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->entities.size(), 1u);
    EXPECT_EQ(result->entities[0].assetGuid, "0123456789abcdef0123456789abcdef");
}

TEST(SceneJsonFormatTest, RejectsInvalidJsonText)
{
    EXPECT_FALSE(DeserializeSceneDocument("{ not valid json").has_value());
}

TEST(SceneJsonFormatTest, RejectsNonObjectTopLevel)
{
    EXPECT_FALSE(DeserializeSceneDocument("[1,2,3]").has_value());
}

TEST(SceneJsonFormatTest, RejectsMissingGtsceneVersion)
{
    EXPECT_FALSE(DeserializeSceneDocument(R"({"entities":[]})").has_value());
}

TEST(SceneJsonFormatTest, RejectsWrongGtsceneVersion)
{
    EXPECT_FALSE(DeserializeSceneDocument(R"({"gtscene_version":1,"entities":[]})").has_value());
    EXPECT_FALSE(DeserializeSceneDocument(R"({"gtscene_version":3,"entities":[]})").has_value());
}

TEST(SceneJsonFormatTest, RejectsNonArrayEntities)
{
    EXPECT_FALSE(DeserializeSceneDocument(R"({"gtscene_version":2,"entities":{}})").has_value());
}

TEST(SceneJsonFormatTest, RejectsMissingEntities)
{
    EXPECT_FALSE(DeserializeSceneDocument(R"({"gtscene_version":2})").has_value());
}

TEST(SceneJsonFormatTest, RejectsOutOfRangeParentIndex)
{
    const std::string text = R"({"gtscene_version":2,"entities":[{"parent":5,"components":{}}]})";
    EXPECT_FALSE(DeserializeSceneDocument(text).has_value());
}

TEST(SceneJsonFormatTest, RejectsNegativeParentIndex)
{
    const std::string text = R"({"gtscene_version":2,"entities":[{"parent":-1,"components":{}}]})";
    EXPECT_FALSE(DeserializeSceneDocument(text).has_value());
}

TEST(SceneJsonFormatTest, RejectsNonObjectComponents)
{
    const std::string text = R"({"gtscene_version":2,"entities":[{"components":[1,2,3]}]})";
    EXPECT_FALSE(DeserializeSceneDocument(text).has_value());
}

TEST(SceneJsonFormatTest, RejectsNonObjectEntity)
{
    const std::string text = R"({"gtscene_version":2,"entities":[42]})";
    EXPECT_FALSE(DeserializeSceneDocument(text).has_value());
}

TEST(SceneJsonFormatTest, TolerantOfUnrecognizedExtraTopLevelKey)
{
    const std::string text = R"({"gtscene_version":2,"entities":[],"future_field":"whatever"})";
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->entities.empty());
}

TEST(SceneJsonFormatTest, TolerantOfUnrecognizedExtraPerEntityKey)
{
    const std::string text = R"({"gtscene_version":2,"entities":[{"future_field":123}]})";
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->entities.size(), 1u);
    EXPECT_TRUE(result->entities[0].components.empty());
}

TEST(SceneJsonFormatTest, AbsentSiblingIndexDefaultsToZero)
{
    const std::string text = R"({"gtscene_version":2,"entities":[{}]})";
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->entities.size(), 1u);
    EXPECT_EQ(result->entities[0].siblingIndex, 0u);
}

} // namespace
} // namespace gte
