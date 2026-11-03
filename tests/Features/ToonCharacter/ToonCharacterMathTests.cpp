// Tests for the ToonCharacter settings, look record, tint table, debug arguments and part collection.

#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/ToonShading.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "ECS/TransformHierarchy.h"
#include "Features/SceneLighting/SceneLightingTypes.h"
#include "Features/ToonCharacter/ToonCharacterDebugEndpoint.h"
#include "Features/ToonCharacter/ToonCharacterMath.h"
#include "Features/ToonCharacter/ToonPartCollector.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace gte {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

TEST(ToonCharacterMathTest, DefaultsAreInsideTheirOwnRanges)
{
    const ToonCharacterSettings d{};
    const ToonCharacterSettings s = SanitizeToonCharacterSettings(d);
    EXPECT_EQ(s.enabled, d.enabled);
    EXPECT_FLOAT_EQ(s.bandThreshold, d.bandThreshold);
    EXPECT_FLOAT_EQ(s.bandSoftness, d.bandSoftness);
    EXPECT_FLOAT_EQ(s.deepThreshold, d.deepThreshold);
    EXPECT_FLOAT_EQ(s.shadowStrength, d.shadowStrength);
    EXPECT_FLOAT_EQ(s.shadeBlend, d.shadeBlend);
    EXPECT_FLOAT_EQ(s.rimStrength, d.rimStrength);
    EXPECT_FLOAT_EQ(s.rimWidth, d.rimWidth);
    EXPECT_FLOAT_EQ(s.hairHighlight, d.hairHighlight);
    EXPECT_FLOAT_EQ(s.emissiveBoost, d.emissiveBoost);
    EXPECT_FLOAT_EQ(s.faceFlatten, d.faceFlatten);
    EXPECT_FLOAT_EQ(s.outlineWidthPx, d.outlineWidthPx);
    EXPECT_FLOAT_EQ(s.outlineDarkness, d.outlineDarkness);
    EXPECT_EQ(s.debugView, d.debugView);
}

TEST(ToonCharacterMathTest, SanitizeClampsEveryField)
{
    ToonCharacterSettings high;
    high.bandThreshold = 5.0f;
    high.bandSoftness = 5.0f;
    high.deepThreshold = 5.0f;
    high.shadowStrength = 5.0f;
    high.shadeBlend = 5.0f;
    high.rimStrength = 50.0f;
    high.rimWidth = 5.0f;
    high.hairHighlight = 50.0f;
    high.emissiveBoost = 500.0f;
    high.faceFlatten = 5.0f;
    high.outlineWidthPx = 50.0f;
    high.outlineDarkness = 5.0f;
    high.debugView = 99;
    const ToonCharacterSettings h = SanitizeToonCharacterSettings(high);
    EXPECT_FLOAT_EQ(h.bandThreshold, 0.95f);
    EXPECT_FLOAT_EQ(h.bandSoftness, 0.25f);
    EXPECT_FLOAT_EQ(h.deepThreshold, 0.95f);
    EXPECT_FLOAT_EQ(h.shadowStrength, 1.0f);
    EXPECT_FLOAT_EQ(h.shadeBlend, 1.0f);
    EXPECT_FLOAT_EQ(h.rimStrength, 2.0f);
    EXPECT_FLOAT_EQ(h.rimWidth, 1.0f);
    EXPECT_FLOAT_EQ(h.hairHighlight, 4.0f);
    EXPECT_FLOAT_EQ(h.emissiveBoost, 16.0f);
    EXPECT_FLOAT_EQ(h.faceFlatten, 1.0f);
    EXPECT_FLOAT_EQ(h.outlineWidthPx, 8.0f);
    EXPECT_FLOAT_EQ(h.outlineDarkness, 1.0f);
    EXPECT_EQ(h.debugView, kToonDebugViewCount - 1);

    ToonCharacterSettings low;
    low.bandThreshold = -1.0f;
    low.bandSoftness = -1.0f;
    low.deepThreshold = -1.0f;
    low.shadowStrength = -1.0f;
    low.shadeBlend = -1.0f;
    low.rimStrength = -1.0f;
    low.rimWidth = -1.0f;
    low.hairHighlight = -1.0f;
    low.emissiveBoost = -1.0f;
    low.faceFlatten = -1.0f;
    low.outlineWidthPx = -1.0f;
    low.outlineDarkness = -1.0f;
    low.debugView = -4;
    const ToonCharacterSettings l = SanitizeToonCharacterSettings(low);
    EXPECT_FLOAT_EQ(l.bandThreshold, 0.05f);
    EXPECT_FLOAT_EQ(l.bandSoftness, 0.001f);
    EXPECT_FLOAT_EQ(l.deepThreshold, 0.0f);
    EXPECT_FLOAT_EQ(l.shadowStrength, 0.0f);
    EXPECT_FLOAT_EQ(l.outlineWidthPx, 0.0f);
    EXPECT_EQ(l.debugView, 0);
}

TEST(ToonCharacterMathTest, NonFiniteValuesFallBackToTheDefaults)
{
    const ToonCharacterSettings d{};
    ToonCharacterSettings in;
    in.bandThreshold = kNaN;
    in.bandSoftness = kInf;
    in.deepThreshold = -kInf;
    in.shadowStrength = kNaN;
    in.rimStrength = kInf;
    in.emissiveBoost = kNaN;
    in.outlineWidthPx = kInf;
    in.outlineDarkness = kNaN;
    const ToonCharacterSettings s = SanitizeToonCharacterSettings(in);
    EXPECT_FLOAT_EQ(s.bandThreshold, d.bandThreshold);
    EXPECT_FLOAT_EQ(s.bandSoftness, d.bandSoftness);
    EXPECT_FLOAT_EQ(s.deepThreshold, d.deepThreshold);
    EXPECT_FLOAT_EQ(s.shadowStrength, d.shadowStrength);
    EXPECT_FLOAT_EQ(s.rimStrength, d.rimStrength);
    EXPECT_FLOAT_EQ(s.emissiveBoost, d.emissiveBoost);
    EXPECT_FLOAT_EQ(s.outlineWidthPx, d.outlineWidthPx);
    EXPECT_FLOAT_EQ(s.outlineDarkness, d.outlineDarkness);
}

TEST(ToonCharacterMathTest, DeepThresholdNeverPassesTheBandThreshold)
{
    ToonCharacterSettings in;
    in.bandThreshold = 0.3f;
    in.deepThreshold = 0.8f;
    const ToonCharacterSettings s = SanitizeToonCharacterSettings(in);
    EXPECT_FLOAT_EQ(s.bandThreshold, 0.3f);
    EXPECT_FLOAT_EQ(s.deepThreshold, 0.3f);
}

TEST(ToonCharacterMathTest, SanitizedBandThresholdStaysAboveTheUnsetMarker)
{
    ToonCharacterSettings in;
    in.bandThreshold = 0.0f;
    const ToonLook look = PackToonLook(SanitizeToonCharacterSettings(in), Extent2D{ 1280, 720 });
    EXPECT_GT(look.v0[0], 0.0f);
    EXPECT_GT(PackToonLook(ToonCharacterSettings{}, Extent2D{ 1280, 720 }).v0[0], 0.0f);
}

TEST(ToonCharacterMathTest, PackCarriesTheSettings)
{
    ToonCharacterSettings s;
    s.bandThreshold = 0.4f;
    s.bandSoftness = 0.03f;
    s.deepThreshold = 0.2f;
    s.shadowStrength = 0.7f;
    s.rimStrength = 0.5f;
    s.rimWidth = 0.55f;
    s.hairHighlight = 0.9f;
    s.emissiveBoost = 2.5f;
    s.faceFlatten = 0.6f;
    s.outlineDarkness = 0.25f;
    s.debugView = 3;
    s.shadeBlend = 0.75f;
    s.gemHeuristic = false;
    s.goldGlint = true;
    const ToonLook look = PackToonLook(s, Extent2D{ 1920, 1080 });

    EXPECT_FLOAT_EQ(look.v0[0], 0.4f);
    EXPECT_FLOAT_EQ(look.v0[1], 0.03f);
    EXPECT_FLOAT_EQ(look.v0[2], 0.2f);
    EXPECT_FLOAT_EQ(look.v0[3], 0.7f);
    EXPECT_FLOAT_EQ(look.v1[0], 0.5f);
    EXPECT_FLOAT_EQ(look.v1[1], 0.55f);
    EXPECT_FLOAT_EQ(look.v1[2], 0.9f);
    EXPECT_FLOAT_EQ(look.v1[3], 2.5f);
    EXPECT_FLOAT_EQ(look.v2[0], 0.6f);
    EXPECT_FLOAT_EQ(look.v2[2], 0.25f);
    EXPECT_FLOAT_EQ(look.v2[3], 3.0f);
    EXPECT_FLOAT_EQ(look.v3[0], 0.75f);
    EXPECT_FLOAT_EQ(look.v3[1], 0.0f);
    EXPECT_FLOAT_EQ(look.v3[2], 1.0f);
    EXPECT_FLOAT_EQ(look.v3[3], 1920.0f / 1080.0f);
}

TEST(ToonCharacterMathTest, OutlineWidthScalesWithViewHeightAndIsConstantInNdc)
{
    ToonCharacterSettings s;
    s.outlineWidthPx = 1.6f;
    const float ndc720 = PackToonLook(s, Extent2D{ 1280, 720 }).v2[1];
    const float ndc1440 = PackToonLook(s, Extent2D{ 2560, 1440 }).v2[1];
    EXPECT_FLOAT_EQ(ndc720, 1.6f * 2.0f / 720.0f);
    EXPECT_FLOAT_EQ(ndc1440, ndc720);

    // Thickness in pixels = ndc * height / 2.
    EXPECT_NEAR(ndc720 * 720.0f / 2.0f, 1.6f, 1e-5f);
    EXPECT_NEAR(ndc1440 * 1440.0f / 2.0f, 3.2f, 1e-4f);

    s.outlineWidthPx = 0.0f;
    EXPECT_FLOAT_EQ(PackToonLook(s, Extent2D{ 1280, 720 }).v2[1], 0.0f);
}

TEST(ToonCharacterMathTest, EmptyExtentFallsBackToTheReferenceView)
{
    ToonCharacterSettings s;
    const ToonLook look = PackToonLook(s, Extent2D{ 0, 0 });
    EXPECT_FLOAT_EQ(look.v3[3], 1.0f);
    EXPECT_FLOAT_EQ(look.v2[1], s.outlineWidthPx * 2.0f / kToonOutlineReferenceHeightPx);
}

TEST(ToonCharacterMathTest, LookRecordLayoutMatchesTheUniformTail)
{
    EXPECT_EQ(sizeof(ToonLook), 64u);
    EXPECT_EQ(alignof(ToonLook), 16u);
    EXPECT_EQ(offsetof(ToonLook, v0), 0u);
    EXPECT_EQ(offsetof(ToonLook, v1), 16u);
    EXPECT_EQ(offsetof(ToonLook, v2), 32u);
    EXPECT_EQ(offsetof(ToonLook, v3), 48u);
    EXPECT_TRUE(std::is_trivially_copyable_v<ToonLook>);
    EXPECT_EQ(sizeof(SceneLightingUniformData), 512u);
    EXPECT_EQ(offsetof(SceneLightingUniformData, toonLook), 448u);
}

TEST(ToonCharacterMathTest, UniformTailCopiesTheLookAndStaysZeroWithout)
{
    const ResolvedDirectionalLight sun{};
    const SceneLightingSettings settings{};
    const SceneLightingUniformData none = PackSceneLightingUniformData(sun, settings, Vec3{});
    for (const auto& row : none.toonLook) {
        for (const float value : row) {
            EXPECT_EQ(value, 0.0f);
        }
    }

    ToonLook look;
    for (int i = 0; i < 4; ++i) {
        look.v0[i] = 1.0f + i;
        look.v1[i] = 10.0f + i;
        look.v2[i] = 20.0f + i;
        look.v3[i] = 30.0f + i;
    }
    const SceneLightingUniformData packed = PackSceneLightingUniformData(sun, settings, Vec3{}, nullptr, nullptr, &look);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(packed.toonLook[0][i], look.v0[i]);
        EXPECT_EQ(packed.toonLook[1][i], look.v1[i]);
        EXPECT_EQ(packed.toonLook[2][i], look.v2[i]);
        EXPECT_EQ(packed.toonLook[3][i], look.v3[i]);
    }
}

TEST(ToonCharacterMathTest, SceneUniformBlockInTheShaderEndsWithTheToonLookVectors)
{
#ifndef GTE_REPO_SRC_DIR
    GTEST_SKIP() << "GTE_REPO_SRC_DIR is not defined";
#else
    std::ifstream file(std::string(GTE_REPO_SRC_DIR) + "/Shaders/DirectionalLighting.glsl", std::ios::binary);
    ASSERT_TRUE(file.is_open());
    std::ostringstream text;
    text << file.rdbuf();
    const std::string source = text.str();
    const std::size_t cascade3 = source.find("cascadeViewProjection3");
    const std::size_t look0 = source.find("toonLook0");
    const std::size_t look1 = source.find("toonLook1");
    const std::size_t look2 = source.find("toonLook2");
    const std::size_t look3 = source.find("toonLook3");
    ASSERT_NE(cascade3, std::string::npos);
    ASSERT_NE(look0, std::string::npos);
    EXPECT_LT(cascade3, look0);
    EXPECT_LT(look0, look1);
    EXPECT_LT(look1, look2);
    EXPECT_LT(look2, look3);
#endif
}

TEST(ToonCharacterMathTest, TintTableHoldsTheSpecValues)
{
    const ToonClassTint& cloth = ToonTintOf(ToonClass::Cloth);
    EXPECT_FLOAT_EQ(cloth.shade.r, 0.42f);
    EXPECT_FLOAT_EQ(cloth.shade.g, 0.48f);
    EXPECT_FLOAT_EQ(cloth.shade.b, 0.82f);
    EXPECT_FLOAT_EQ(cloth.deep.r, 0.80f);
    EXPECT_FLOAT_EQ(cloth.outline.b, 0.62f);

    const ToonClassTint& hair = ToonTintOf(ToonClass::Hair);
    EXPECT_FLOAT_EQ(hair.shade.r, 0.62f);
    EXPECT_FLOAT_EQ(hair.shade.b, 0.93f);
    EXPECT_FLOAT_EQ(hair.outline.g, 0.55f);

    const ToonClassTint& skin = ToonTintOf(ToonClass::Skin);
    EXPECT_FLOAT_EQ(skin.shade.g, 0.74f);
    EXPECT_FLOAT_EQ(skin.outline.r, 0.55f);

    EXPECT_FLOAT_EQ(ToonTintOf(ToonClass::FaceSkin).shade.r, 0.97f);
    EXPECT_FLOAT_EQ(ToonTintOf(ToonClass::ClothBack).shade.b, cloth.shade.b);
}

TEST(ToonCharacterMathTest, UnknownClassUsesTheClothTint)
{
    const ToonClassTint& unknown = ToonTintOf(static_cast<ToonClass>(77));
    EXPECT_FLOAT_EQ(unknown.shade.b, ToonTintOf(ToonClass::Cloth).shade.b);
}

TEST(ToonCharacterMathTest, ClassRules)
{
    EXPECT_TRUE(ToonClassIsUnlit(ToonClass::Eye));
    EXPECT_TRUE(ToonClassIsUnlit(ToonClass::FaceLine));
    EXPECT_FALSE(ToonClassIsUnlit(ToonClass::Cloth));
    EXPECT_FALSE(ToonClassIsUnlit(ToonClass::FaceSkin));

    EXPECT_TRUE(ToonClassHasDeepBand(ToonClass::Cloth));
    EXPECT_TRUE(ToonClassHasDeepBand(ToonClass::Hair));
    EXPECT_FALSE(ToonClassHasDeepBand(ToonClass::Skin));
    EXPECT_FALSE(ToonClassHasDeepBand(ToonClass::FaceSkin));
}

TEST(ToonCharacterMathTest, ShadeTonesKeepTheLuminanceFloorAndStayBelowWhite)
{
    for (std::uint32_t id = 0; id < kToonClassCount; ++id) {
        const auto shadeClass = static_cast<ToonClass>(id);
        for (const bool deep : { false, true }) {
            const ToonRgb tone = ToonShadeTone(shadeClass, deep);
            EXPECT_GE(ToonLuminance(tone), kToonMinShadeLuminance - 1e-5f) << "class " << id;
            EXPECT_LE(std::max({ tone.r, tone.g, tone.b }), 1.0f + 1e-6f) << "class " << id;
        }
    }
}

TEST(ToonCharacterMathTest, ClothShadeIsColouredAndTheDeepBandIsDarker)
{
    const ToonRgb shade = ToonShadeTone(ToonClass::Cloth, false);
    const ToonRgb deep = ToonShadeTone(ToonClass::Cloth, true);
    EXPECT_FLOAT_EQ(shade.r, 0.42f);
    EXPECT_GT(shade.b - shade.r, 0.15f);
    EXPECT_LT(ToonLuminance(deep), ToonLuminance(shade));
    // A class without a deep band ignores the deep request.
    const ToonRgb skin = ToonShadeTone(ToonClass::Skin, true);
    EXPECT_FLOAT_EQ(skin.g, 0.74f);
}

TEST(ToonCharacterMathTest, UnlitClassesKeepAWhiteTone)
{
    // An unlit class has a white tint, so the luminance floor leaves it unchanged.
    const ToonRgb unlit = ToonShadeTone(ToonClass::Eye, true);
    EXPECT_FLOAT_EQ(unlit.r, 1.0f);
    EXPECT_FLOAT_EQ(unlit.g, 1.0f);
    EXPECT_FLOAT_EQ(unlit.b, 1.0f);
}

std::vector<fx::FeatureDebugArg> Args(std::initializer_list<std::pair<const char*, const char*>> pairs)
{
    std::vector<fx::FeatureDebugArg> out;
    for (const auto& [key, value] : pairs) {
        out.push_back(fx::FeatureDebugArg{ key, value });
    }
    return out;
}

TEST(ToonCharacterDebugArgsTest, ParsesEveryKindOfKey)
{
    ToonCharacterSettings s;
    const std::vector<fx::FeatureDebugArg> args = Args({ { "outlineWidthPx", "2.5" }, { "enabled", "false" },
        { "goldGlint", "0" }, { "gemHeuristic", "true" }, { "debugView", "4" }, { "rimStrength", "0.75" } });
    EXPECT_FALSE(ParseToonCharacterDebugArgs(s, args).has_value());
    EXPECT_FLOAT_EQ(s.outlineWidthPx, 2.5f);
    EXPECT_FALSE(s.enabled);
    EXPECT_FALSE(s.goldGlint);
    EXPECT_TRUE(s.gemHeuristic);
    EXPECT_EQ(s.debugView, 4);
    EXPECT_FLOAT_EQ(s.rimStrength, 0.75f);
}

TEST(ToonCharacterDebugArgsTest, DebugViewIsClamped)
{
    ToonCharacterSettings s;
    const std::vector<fx::FeatureDebugArg> args = Args({ { "debugView", "99" } });
    EXPECT_FALSE(ParseToonCharacterDebugArgs(s, args).has_value());
    EXPECT_EQ(s.debugView, kToonDebugViewCount - 1);
}

TEST(ToonCharacterDebugArgsTest, BadKeysAndValuesAreNamed)
{
    ToonCharacterSettings s;
    const auto unknown = ParseToonCharacterDebugArgs(s, Args({ { "nope", "1" } }));
    ASSERT_TRUE(unknown.has_value());
    EXPECT_EQ(unknown->key, "nope");
    EXPECT_EQ(unknown->message, "unknown key");

    const auto readOnly = ParseToonCharacterDebugArgs(s, Args({ { "game.status", "Ok" } }));
    ASSERT_TRUE(readOnly.has_value());
    EXPECT_EQ(readOnly->message, "key is read-only");

    const auto badFloat = ParseToonCharacterDebugArgs(s, Args({ { "rimWidth", "abc" } }));
    ASSERT_TRUE(badFloat.has_value());
    EXPECT_EQ(badFloat->key, "rimWidth");

    const auto notFinite = ParseToonCharacterDebugArgs(s, Args({ { "rimWidth", "nan" } }));
    EXPECT_TRUE(notFinite.has_value());

    const auto badBool = ParseToonCharacterDebugArgs(s, Args({ { "enabled", "maybe" } }));
    ASSERT_TRUE(badBool.has_value());
    EXPECT_EQ(badBool->key, "enabled");

    const auto badView = ParseToonCharacterDebugArgs(s, Args({ { "debugView", "-1" } }));
    EXPECT_TRUE(badView.has_value());
}

// A part entity with a material texture, parented under `parent` when it is valid.
Entity AddPart(Registry& registry, Entity parent, bool textured = true)
{
    const Entity e = registry.CreateEntity();
    registry.AddComponent<Transform>(e);
    MeshRenderer& renderer = registry.AddComponent<MeshRenderer>(e);
    if (textured) {
        renderer.texture = MaterialTextureHandle{ 1, 1 };
    }
    if (parent.IsValid()) {
        EXPECT_TRUE(SetParent(registry, e, parent, false));
    }
    return e;
}

TEST(ToonPartCollectorTest, SceneWithoutToonShadingCollectsNothing)
{
    Registry registry;
    const Entity root = registry.CreateEntity();
    registry.AddComponent<Transform>(root);
    AddPart(registry, root);
    std::vector<Entity> parts;
    CollectToonParts(registry, parts);
    EXPECT_TRUE(parts.empty());
}

TEST(ToonPartCollectorTest, CollectsTexturedPartsUnderAnEnabledRoot)
{
    Registry registry;
    const Entity root = registry.CreateEntity();
    registry.AddComponent<Transform>(root);
    registry.AddComponent<ToonShading>(root);

    const Entity a = AddPart(registry, root);
    const Entity b = AddPart(registry, root);
    const Entity middle = registry.CreateEntity();
    registry.AddComponent<Transform>(middle);
    ASSERT_TRUE(SetParent(registry, middle, root, false));
    const Entity deep = AddPart(registry, middle);
    const Entity untextured = AddPart(registry, root, false);
    const Entity outsider = AddPart(registry, kInvalidEntity);

    std::vector<Entity> parts;
    CollectToonParts(registry, parts);
    EXPECT_EQ(parts.size(), 3u);
    const auto has = [&](Entity e) { return std::find(parts.begin(), parts.end(), e) != parts.end(); };
    EXPECT_TRUE(has(a));
    EXPECT_TRUE(has(b));
    EXPECT_TRUE(has(deep));
    EXPECT_FALSE(has(untextured));
    EXPECT_FALSE(has(outsider));
}

TEST(ToonPartCollectorTest, DisabledRootAndNearestMarkWin)
{
    Registry registry;
    const Entity disabledRoot = registry.CreateEntity();
    registry.AddComponent<Transform>(disabledRoot);
    registry.AddComponent<ToonShading>(disabledRoot).enabled = false;
    const Entity offPart = AddPart(registry, disabledRoot);

    const Entity enabledRoot = registry.CreateEntity();
    registry.AddComponent<Transform>(enabledRoot);
    registry.AddComponent<ToonShading>(enabledRoot);
    const Entity inner = registry.CreateEntity();
    registry.AddComponent<Transform>(inner);
    registry.AddComponent<ToonShading>(inner).enabled = false;
    ASSERT_TRUE(SetParent(registry, inner, enabledRoot, false));
    const Entity innerPart = AddPart(registry, inner);
    const Entity onPart = AddPart(registry, enabledRoot);

    std::vector<Entity> parts;
    CollectToonParts(registry, parts);
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts.front(), onPart);
    (void)offPart;
    (void)innerPart;
}

TEST(ToonPartCollectorTest, AppendsAndKeepsEarlierEntries)
{
    Registry registry;
    const Entity root = registry.CreateEntity();
    registry.AddComponent<Transform>(root);
    registry.AddComponent<ToonShading>(root);
    const Entity part = AddPart(registry, root);

    std::vector<Entity> parts{ kInvalidEntity };
    CollectToonParts(registry, parts);
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[1], part);
}

} // namespace
} // namespace gte
