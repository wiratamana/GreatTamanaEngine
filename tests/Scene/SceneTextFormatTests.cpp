// Unit tests for src/Scene/SceneTextFormat.h - SerializeSceneDocument()/
// DeserializeSceneDocument() round-tripping the new *.gtscene hand-rolled
// text format (src/Scene/SceneDocument.h). Pure logic - no ECS/Renderer/
// filesystem I/O involved at all - "Tier 1" per tests/CMakeLists.txt's own
// taxonomy. Always built - src/Scene/ has no GTE_ENABLE_EDITOR/
// GTE_ENABLE_PROJECT_PANEL dependency, mirroring src/Physics/'s/src/Jobs/'s
// own unconditional bootstrap.

#include "Scene/SceneTextFormat.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(SceneTextFormatTest, RoundTripsEmptyDocument)
{
    const SceneDocument document;
    const std::string text = SerializeSceneDocument(document);

    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->objects.empty());
}

TEST(SceneTextFormatTest, RoundTripsOnePrimitiveRecord)
{
    SceneDocument document;
    SceneObjectRecord record;
    record.kind = SceneObjectKind::Primitive;
    record.name = "MyCube";
    record.position = Vec3(1.5f, -2.25f, 3.0f);
    record.rotation = Quat(0.1f, 0.2f, 0.3f, 0.9f);
    record.scale = Vec3(2.0f, 2.0f, 2.0f);
    record.primitiveType = PrimitiveType::Sphere;
    document.objects.push_back(record);

    const std::string text = SerializeSceneDocument(document);
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->objects.size(), 1u);

    const SceneObjectRecord& parsed = result->objects[0];
    EXPECT_EQ(parsed.kind, SceneObjectKind::Primitive);
    EXPECT_EQ(parsed.name, "MyCube");
    EXPECT_TRUE(ApproximatelyEqual(parsed.position, record.position));
    EXPECT_TRUE(ApproximatelyEqual(parsed.rotation, record.rotation));
    EXPECT_TRUE(ApproximatelyEqual(parsed.scale, record.scale));
    EXPECT_EQ(parsed.primitiveType, PrimitiveType::Sphere);
}

TEST(SceneTextFormatTest, RoundTripsOneAssetRecord)
{
    SceneDocument document;
    SceneObjectRecord record;
    record.kind = SceneObjectKind::Asset;
    record.name = "ImportedModel";
    record.position = Vec3(0.0f, 1.0f, 0.0f);
    record.rotation = Quat::Identity();
    record.scale = Vec3::One();
    record.assetGuid = Guid::Generate();
    document.objects.push_back(record);

    const std::string text = SerializeSceneDocument(document);
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->objects.size(), 1u);

    const SceneObjectRecord& parsed = result->objects[0];
    EXPECT_EQ(parsed.kind, SceneObjectKind::Asset);
    EXPECT_EQ(parsed.name, "ImportedModel");
    EXPECT_EQ(parsed.assetGuid, record.assetGuid);
}

TEST(SceneTextFormatTest, RoundTripsMultipleMixedRecords)
{
    SceneDocument document;

    SceneObjectRecord primitive;
    primitive.kind = SceneObjectKind::Primitive;
    primitive.primitiveType = PrimitiveType::Cone;
    document.objects.push_back(primitive);

    SceneObjectRecord asset;
    asset.kind = SceneObjectKind::Asset;
    asset.assetGuid = Guid::Generate();
    document.objects.push_back(asset);

    const std::string text = SerializeSceneDocument(document);
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->objects.size(), 2u);
    EXPECT_EQ(result->objects[0].kind, SceneObjectKind::Primitive);
    EXPECT_EQ(result->objects[0].primitiveType, PrimitiveType::Cone);
    EXPECT_EQ(result->objects[1].kind, SceneObjectKind::Asset);
    EXPECT_EQ(result->objects[1].assetGuid, asset.assetGuid);
}

TEST(SceneTextFormatTest, RejectsEmptyString)
{
    EXPECT_FALSE(DeserializeSceneDocument("").has_value());
}

TEST(SceneTextFormatTest, RejectsWrongMagicOrVersion)
{
    EXPECT_FALSE(DeserializeSceneDocument("NOT A HEADER\n").has_value());
    EXPECT_FALSE(DeserializeSceneDocument("GTSCENE 2\n").has_value());
}

TEST(SceneTextFormatTest, RejectsUnclosedObject)
{
    const std::string text = "GTSCENE 1\nOBJECT\nkind=Primitive\n";
    EXPECT_FALSE(DeserializeSceneDocument(text).has_value());
}

TEST(SceneTextFormatTest, RejectsEndWithNoPrecedingObject)
{
    const std::string text = "GTSCENE 1\nEND\n";
    EXPECT_FALSE(DeserializeSceneDocument(text).has_value());
}

TEST(SceneTextFormatTest, RejectsUnknownKindValue)
{
    const std::string text = "GTSCENE 1\nOBJECT\nkind=Bogus\nEND\n";
    EXPECT_FALSE(DeserializeSceneDocument(text).has_value());
}

TEST(SceneTextFormatTest, RejectsUnknownPrimitiveTypeValue)
{
    const std::string text = "GTSCENE 1\nOBJECT\nkind=Primitive\nprimitiveType=NotAShape\nEND\n";
    EXPECT_FALSE(DeserializeSceneDocument(text).has_value());
}

TEST(SceneTextFormatTest, RejectsWrongTokenCountForPosition)
{
    const std::string tooFew = "GTSCENE 1\nOBJECT\nkind=Primitive\nposition=1.0,2.0\nEND\n";
    EXPECT_FALSE(DeserializeSceneDocument(tooFew).has_value());

    const std::string tooMany = "GTSCENE 1\nOBJECT\nkind=Primitive\nposition=1.0,2.0,3.0,4.0\nEND\n";
    EXPECT_FALSE(DeserializeSceneDocument(tooMany).has_value());
}

TEST(SceneTextFormatTest, RejectsNonNumericPosition)
{
    const std::string text = "GTSCENE 1\nOBJECT\nkind=Primitive\nposition=a,b,c\nEND\n";
    EXPECT_FALSE(DeserializeSceneDocument(text).has_value());
}

TEST(SceneTextFormatTest, RejectsSyntacticallyMalformedAssetGuid)
{
    const std::string text = "GTSCENE 1\nOBJECT\nkind=Asset\nassetGuid=not-a-guid\nEND\n";
    EXPECT_FALSE(DeserializeSceneDocument(text).has_value());
}

TEST(SceneTextFormatTest, RejectsAssetRecordWithNoAssetGuidLineAtAll)
{
    const std::string text = "GTSCENE 1\nOBJECT\nkind=Asset\nEND\n";
    EXPECT_FALSE(DeserializeSceneDocument(text).has_value());
}

TEST(SceneTextFormatTest, RejectsAssetRecordWithInvalidGuidValue)
{
    const std::string text =
        "GTSCENE 1\nOBJECT\nkind=Asset\nassetGuid=00000000000000000000000000000000\nEND\n";
    EXPECT_FALSE(DeserializeSceneDocument(text).has_value());
}

TEST(SceneTextFormatTest, PrimitiveTolerateStrayAssetGuidLine)
{
    const std::string text =
        "GTSCENE 1\nOBJECT\nkind=Primitive\nassetGuid=00000000000000000000000000000000\nEND\n";
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->objects.size(), 1u);
    EXPECT_EQ(result->objects[0].kind, SceneObjectKind::Primitive);
}

TEST(SceneTextFormatTest, UnknownKeyIsIgnoredForForwardCompatibility)
{
    const std::string text = "GTSCENE 1\nOBJECT\nkind=Primitive\nfutureField=whatever\nEND\n";
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->objects.size(), 1u);
}

TEST(SceneTextFormatTest, AbsentOptionalNameFieldKeepsDefault)
{
    const std::string text = "GTSCENE 1\nOBJECT\nkind=Primitive\nEND\n";
    const std::optional<SceneDocument> result = DeserializeSceneDocument(text);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->objects.size(), 1u);
    EXPECT_TRUE(result->objects[0].name.empty());
}

} // namespace
} // namespace gte
