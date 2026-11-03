// Tier-1 tests for the toon material classifier, built from a table shaped like the 32-material Furina model.

#include "Game/Instantiation/ToonMaterialClass.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace gte {
namespace {

constexpr std::int32_t kSkinRamp = 0;
constexpr std::int32_t kHairRamp = 1;
constexpr std::int32_t kDefaultRamp = 2;

std::vector<MaterialTextureRef> MakeTextures()
{
    return { { "C:\\model\\toon\\skin.bmp", Guid::Invalid() }, { "C:\\model\\toon\\Hair.bmp", Guid::Invalid() },
        { "C:\\model\\toon\\toon_defo.bmp", Guid::Invalid() }, { "C:\\model\\tex\\face.png", Guid::Invalid() } };
}

struct Row {
    const char* name;
    const char* englishName;
    std::int32_t toonTexture; // -1 = none.
    bool sharedToon;
    SphereTextureMode sphere;
    ToonClass expected;
};

Material MakeMaterial(const Row& row)
{
    Material m;
    m.name = row.name;
    m.englishName = row.englishName;
    m.toonTextureIndex = row.sharedToon ? -1 : row.toonTexture;
    m.useSharedToon = row.sharedToon;
    m.sphereMode = row.sphere;
    m.drawEdge = true;
    m.edgeSize = 0.54f;
    return m;
}

constexpr auto kNone = SphereTextureMode::Disabled;
constexpr auto kAdd = SphereTextureMode::Add;

// Index = material index of the model.
const std::array<Row, 32> kFurina = { {
    { "face", "Mat_Face", kSkinRamp, false, kNone, ToonClass::FaceSkin },        // 0
    { "face2", "Mat_Face", kSkinRamp, false, kNone, ToonClass::FaceSkin },       // 1
    { "double_lid", "Mat_Face", kSkinRamp, false, kNone, ToonClass::FaceSkin },  // 2
    { "lash", "Mat_Face", kHairRamp, false, kNone, ToonClass::FaceLine },        // 3
    { "mouth", "Mat_Face", kSkinRamp, false, kNone, ToonClass::FaceSkin },       // 4
    { "teeth", "Mat_Face", 0, true, kNone, ToonClass::FaceSkin },                // 5
    { "brow", "Mat_Face", kHairRamp, false, kNone, ToonClass::FaceLine },        // 6
    { "white_eye", "Mat_Eye", -1, false, kNone, ToonClass::Eye },                // 7
    { "eye", "Mat_Eye", -1, false, kNone, ToonClass::Eye },                      // 8
    { "star_eye", "Mat_Eye", -1, false, kNone, ToonClass::Eye },                 // 9
    { "hair", "Mat_Hair", kHairRamp, false, kNone, ToonClass::Hair },            // 10
    { "effect_hair", "Mat_EffectHair", kHairRamp, false, kNone, ToonClass::Hair }, // 11
    { "cloth1", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },     // 12
    { "cloth2", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },     // 13
    { "cloth3", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },     // 14
    { "cloth4", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },     // 15
    { "cloth5", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },     // 16
    { "cloth6", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },     // 17
    { "gem", "Mat_Cloth", kDefaultRamp, false, kAdd, ToonClass::Gem },           // 18
    { "cloth7", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },     // 19
    { "skin", "Mat_Hair", kSkinRamp, false, kNone, ToonClass::Skin },            // 20
    { "cloth8", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },     // 21
    { "cloth9", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },     // 22
    { "skirt1", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },     // 23
    { "skirt2", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },     // 24
    { "skirt3", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },     // 25
    { "trim1", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },      // 26
    { "trim2", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::Cloth },      // 27
    { "skirt1+", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::ClothBack }, // 28
    { "skirt2+", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::ClothBack }, // 29
    { "skirt3+", "Mat_Cloth", kDefaultRamp, false, kNone, ToonClass::ClothBack }, // 30
    { "hair+", "Mat_Hair", kHairRamp, false, kAdd, ToonClass::HairOverlay },     // 31
} };

TEST(ToonMaterialClass, AllFurinaMaterialsAreClassified)
{
    const std::vector<MaterialTextureRef> textures = MakeTextures();
    for (std::size_t i = 0; i < kFurina.size(); ++i) {
        EXPECT_EQ(ClassifyToonMaterial(MakeMaterial(kFurina[i]), textures), kFurina[i].expected) << "material " << i;
    }
}

TEST(ToonMaterialClass, ClassIdsAreStable)
{
    EXPECT_EQ(static_cast<std::uint32_t>(ToonClass::Cloth), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(ToonClass::Skin), 1u);
    EXPECT_EQ(static_cast<std::uint32_t>(ToonClass::FaceSkin), 2u);
    EXPECT_EQ(static_cast<std::uint32_t>(ToonClass::FaceLine), 3u);
    EXPECT_EQ(static_cast<std::uint32_t>(ToonClass::Eye), 4u);
    EXPECT_EQ(static_cast<std::uint32_t>(ToonClass::Hair), 5u);
    EXPECT_EQ(static_cast<std::uint32_t>(ToonClass::HairOverlay), 6u);
    EXPECT_EQ(static_cast<std::uint32_t>(ToonClass::Gem), 7u);
    EXPECT_EQ(static_cast<std::uint32_t>(ToonClass::ClothBack), 8u);
}

TEST(ToonMaterialClass, EyeRuleBeatsEveryOtherRule)
{
    const std::vector<MaterialTextureRef> textures = MakeTextures();
    Material m = MakeMaterial({ "eye+", "Mat_Face", -1, false, kAdd, ToonClass::Eye });
    EXPECT_EQ(ClassifyToonMaterial(m, textures), ToonClass::Eye);
}

TEST(ToonMaterialClass, UnknownModelFallsBackToCloth)
{
    Material m;
    m.name = "body";
    m.toonTextureIndex = 0;
    EXPECT_EQ(ClassifyToonMaterial(m, {}), ToonClass::Cloth); // Ramp index outside an empty texture list.
}

TEST(ToonMaterialClass, EdgeRule)
{
    EXPECT_TRUE(ToonClassDrawsEdge(ToonClass::Cloth, true));
    EXPECT_FALSE(ToonClassDrawsEdge(ToonClass::Cloth, false));
    EXPECT_TRUE(ToonClassDrawsEdge(ToonClass::Hair, true));
    EXPECT_TRUE(ToonClassDrawsEdge(ToonClass::Skin, true));
    EXPECT_TRUE(ToonClassDrawsEdge(ToonClass::Gem, true));
    EXPECT_FALSE(ToonClassDrawsEdge(ToonClass::Eye, true));
    EXPECT_FALSE(ToonClassDrawsEdge(ToonClass::FaceLine, true));
    EXPECT_FALSE(ToonClassDrawsEdge(ToonClass::HairOverlay, true));
    EXPECT_FALSE(ToonClassDrawsEdge(ToonClass::ClothBack, true));
}

TEST(ToonMaterialClass, ParamsCarryClassEdgeScaleAndFlag)
{
    const std::vector<MaterialTextureRef> textures = MakeTextures();
    const MaterialParams hair = MakeToonParamsFor(MakeMaterial(kFurina[10]), textures);
    EXPECT_FLOAT_EQ(hair.toonClass, 5.0f);
    EXPECT_FLOAT_EQ(hair.edgeWidthScale, 1.0f);
    EXPECT_FLOAT_EQ(hair.drawEdge, 1.0f);

    const MaterialParams eye = MakeToonParamsFor(MakeMaterial(kFurina[8]), textures);
    EXPECT_FLOAT_EQ(eye.toonClass, 4.0f);
    EXPECT_FLOAT_EQ(eye.drawEdge, 0.0f);
}

} // namespace
} // namespace gte
