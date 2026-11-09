// Tier-1 tests for the VolumetricCloud settings, the layer math and the shader names.

#include "Features/VolumetricCloud/VolumetricCloudMath.h"
#include "Features/VolumetricCloud/VolumetricCloudTypes.h"
#include "Renderer/Vulkan/ShaderReflection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <span>
#include <string>

namespace gte {
namespace {

TEST(VolumetricCloudMathTest, DefaultsAreAlreadySane)
{
    const VolumetricCloudSettings defaults;
    const VolumetricCloudSettings sanitized = SanitizeVolumetricCloudSettings(defaults);
    EXPECT_EQ(sanitized.debugView, VolumetricCloudDebugView::Off);
    EXPECT_FLOAT_EQ(sanitized.slice, defaults.slice);
    EXPECT_FLOAT_EQ(sanitized.tiling, defaults.tiling);
    EXPECT_EQ(sanitized.channel, defaults.channel);
    EXPECT_FALSE(sanitized.enabled);
}

TEST(VolumetricCloudMathTest, ClampsEveryRange)
{
    VolumetricCloudSettings s;
    s.slice = 7.0f;
    s.tiling = 1000.0f;
    s.channel = 99;
    const VolumetricCloudSettings high = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(high.slice, 1.0f);
    EXPECT_FLOAT_EQ(high.tiling, 16.0f);
    EXPECT_EQ(high.channel, kCloudDebugChannelRgb);

    s.slice = -3.0f;
    s.tiling = 0.0f;
    const VolumetricCloudSettings low = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(low.slice, 0.0f);
    EXPECT_FLOAT_EQ(low.tiling, 0.25f);
}

TEST(VolumetricCloudMathTest, NonFiniteNumbersFallBackToTheDefault)
{
    VolumetricCloudSettings s;
    s.slice = std::numeric_limits<float>::quiet_NaN();
    s.tiling = std::numeric_limits<float>::infinity();
    const VolumetricCloudSettings out = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(out.slice, VolumetricCloudSettings{}.slice);
    EXPECT_FLOAT_EQ(out.tiling, VolumetricCloudSettings{}.tiling);
}

TEST(VolumetricCloudMathTest, UnknownDebugViewTurnsOff)
{
    VolumetricCloudSettings s;
    s.debugView = static_cast<VolumetricCloudDebugView>(200);
    EXPECT_EQ(SanitizeVolumetricCloudSettings(s).debugView, VolumetricCloudDebugView::Off);
}

TEST(VolumetricCloudMathTest, PushCarriesTheSettings)
{
    VolumetricCloudSettings s;
    s.slice = 0.25f;
    s.tiling = 3.0f;
    s.channel = 2;
    const VolumetricCloudSlicePush push = MakeVolumetricCloudSlicePush(s);
    EXPECT_FLOAT_EQ(push.slice, 0.25f);
    EXPECT_FLOAT_EQ(push.tiling, 3.0f);
    EXPECT_EQ(push.channel, 2u);
}

TEST(VolumetricCloudMathTest, AssetNamesLiveUnderTheCloudsFolder)
{
    for (const char* asset : { kCloudShapeAsset, kCloudDetailAsset, kCloudWeatherAsset, kCloudBlueNoiseAsset }) {
        EXPECT_EQ(std::string(asset).rfind("clouds/", 0), 0u) << asset;
    }
}

TEST(VolumetricCloudMathTest, EverySettingHasAUniqueNameAndADefaultInsideItsRange)
{
    const VolumetricCloudSettings defaults;
    std::set<std::string> names;
    for (const VolumetricCloudFloatSetting& entry : kVolumetricCloudFloatSettings) {
        EXPECT_TRUE(names.insert(entry.name).second) << entry.name;
        EXPECT_LT(entry.low, entry.high) << entry.name;
        EXPECT_GE(defaults.*(entry.member), entry.low) << entry.name;
        EXPECT_LE(defaults.*(entry.member), entry.high) << entry.name;
    }
}

TEST(VolumetricCloudMathTest, SanitizingIsIdempotentAndKeepsTheLayerThick)
{
    VolumetricCloudSettings s;
    s.layerBaseM = 5000.0f;
    s.layerTopM = 100.0f;
    s.coverage = std::numeric_limits<float>::quiet_NaN();
    const VolumetricCloudSettings once = SanitizeVolumetricCloudSettings(s);
    EXPECT_GT(once.layerTopM, once.layerBaseM);
    EXPECT_FLOAT_EQ(once.coverage, VolumetricCloudSettings{}.coverage);
    const VolumetricCloudSettings twice = SanitizeVolumetricCloudSettings(once);
    EXPECT_FLOAT_EQ(twice.layerTopM, once.layerTopM);
    EXPECT_FLOAT_EQ(twice.layerBaseM, once.layerBaseM);
}

TEST(VolumetricCloudLayerTest, RemapIsLinearAndSurvivesAZeroWidthRange)
{
    EXPECT_FLOAT_EQ(CloudRemap(0.5f, 0.0f, 1.0f, 10.0f, 20.0f), 15.0f);
    EXPECT_FLOAT_EQ(CloudRemap(2.0f, 1.0f, 0.0f, 0.0f, 1.0f), -1.0f);
    EXPECT_TRUE(std::isfinite(CloudRemap(0.3f, 1.0f, 1.0f, 0.0f, 1.0f)));
    EXPECT_LT(CloudRemap(0.3f, 1.0f, 1.0f, 0.0f, 1.0f), -100.0f);
}

TEST(VolumetricCloudLayerTest, AltitudeMatchesTheDoublePrecisionSphere)
{
    const double radius = 6371000.0;
    for (const double horizontal : { 0.0, 1000.0, 20000.0, 90000.0 }) {
        for (const double y : { 0.0, 1500.0, 4000.0 }) {
            const double exact = std::sqrt(horizontal * horizontal + (radius + y) * (radius + y)) - radius;
            const float got = CloudAltitudeM(static_cast<float>(horizontal * horizontal), static_cast<float>(y),
                static_cast<float>(radius));
            EXPECT_NEAR(got, exact, 0.5) << horizontal << " " << y;
        }
    }
}

TEST(VolumetricCloudLayerTest, HeightFractionIsClamped)
{
    EXPECT_FLOAT_EQ(CloudHeightFraction(1500.0f, 1500.0f, 4000.0f), 0.0f);
    EXPECT_FLOAT_EQ(CloudHeightFraction(2750.0f, 1500.0f, 4000.0f), 0.5f);
    EXPECT_FLOAT_EQ(CloudHeightFraction(9000.0f, 1500.0f, 4000.0f), 1.0f);
    EXPECT_FLOAT_EQ(CloudHeightFraction(0.0f, 1500.0f, 4000.0f), 0.0f);
}

TEST(VolumetricCloudLayerTest, GradientsRoundTheBaseAndTheTop)
{
    EXPECT_FLOAT_EQ(CloudShapeGradient(0.0f, 0.8f, 0.07f, 0.12f), 0.0f);
    EXPECT_FLOAT_EQ(CloudShapeGradient(0.1f, 0.8f, 0.07f, 0.12f), 1.0f);
    EXPECT_FLOAT_EQ(CloudShapeGradient(0.95f, 0.5f, 0.07f, 0.12f), 0.0f);
    EXPECT_FLOAT_EQ(CloudDensityGradient(0.0f, 0.2f, 0.9f), 0.0f);
    EXPECT_FLOAT_EQ(CloudDensityGradient(0.5f, 0.2f, 0.9f), 0.5f);
    EXPECT_FLOAT_EQ(CloudDensityGradient(1.0f, 0.2f, 0.9f), 0.0f);
}

// Double-precision twin of the shell intersection: the first and last parameter of the ray inside the shell.
struct ExactSegment {
    bool hit = false;
    double start = 0.0;
    double end = 0.0;
};

ExactSegment ExactShell(double cameraY, double dirY, double radius, double base, double top, double maxDistance)
{
    const double oy = radius + cameraY;
    const auto roots = [&](double height, double& nearT, double& farT) {
        const double r = radius + height;
        const double b = oy * dirY;
        const double disc = b * b - (oy * oy - r * r);
        if (disc < 0.0) {
            return false;
        }
        nearT = -b - std::sqrt(disc);
        farT = -b + std::sqrt(disc);
        return true;
    };
    double tn = 0.0, tf = 0.0, bn = 0.0, bf = 0.0, gn = 0.0, gf = 0.0;
    const bool topHit = roots(top, tn, tf);
    const bool baseHit = roots(base, bn, bf);
    ExactSegment seg;
    double start = 0.0;
    double end = 0.0;
    if (cameraY < base) {
        if (dirY < 0.0 && roots(0.0, gn, gf) && gn > -0.5) {
            return seg;
        }
        start = bf;
        end = tf;
    } else if (cameraY < top) {
        end = tf;
        if (baseHit && bn > 0.0) {
            end = std::min(end, bn);
        }
    } else {
        if (!topHit || tf <= 0.0) {
            return seg;
        }
        start = std::max(tn, 0.0);
        end = (baseHit && bn > 0.0) ? bn : tf;
    }
    end = std::min(end, maxDistance);
    if (!(end > start)) {
        return seg;
    }
    seg.hit = true;
    seg.start = start;
    seg.end = end;
    return seg;
}

TEST(VolumetricCloudLayerTest, ShellSegmentMatchesTheDoublePrecisionReference)
{
    const float radius = 6371000.0f;
    for (const float y : { 0.0f, 2.0f, 1400.0f, 1500.0f, 2500.0f, 4000.0f, 7000.0f }) {
        for (const float dirY : { -0.5f, -0.05f, -0.001f, 0.0f, 0.001f, 0.01f, 0.1f, 0.5f, 0.99f, 1.0f }) {
            const CloudShellSegment got = ComputeCloudShellSegment(y, dirY, radius, 1500.0f, 4000.0f, 1.0e7f);
            const ExactSegment want = ExactShell(y, dirY, radius, 1500.0, 4000.0, 1.0e7);
            ASSERT_EQ(got.hit, want.hit) << "y " << y << " dirY " << dirY;
            if (want.hit) {
                // Float rounding at 6371 km limits the far, almost horizontal rays to a few metres.
                const double tolerance = 5.0 + 1.0e-4 * want.end;
                EXPECT_NEAR(got.tStart, want.start, tolerance) << "y " << y << " dirY " << dirY;
                EXPECT_NEAR(got.tEnd, want.end, tolerance) << "y " << y << " dirY " << dirY;
            }
        }
    }
}

TEST(VolumetricCloudLayerTest, StraightUpAndDownSeeTheLayerThickness)
{
    const float r = 6371000.0f;
    const CloudShellSegment up = ComputeCloudShellSegment(0.0f, 1.0f, r, 1500.0f, 4000.0f, 1.0e6f);
    ASSERT_TRUE(up.hit);
    EXPECT_NEAR(up.tStart, 1500.0f, 1.0f);
    EXPECT_NEAR(up.tEnd, 4000.0f, 1.0f);

    const CloudShellSegment inside = ComputeCloudShellSegment(2500.0f, 1.0f, r, 1500.0f, 4000.0f, 1.0e6f);
    ASSERT_TRUE(inside.hit);
    EXPECT_FLOAT_EQ(inside.tStart, 0.0f);
    EXPECT_NEAR(inside.tEnd, 1500.0f, 1.0f);

    const CloudShellSegment above = ComputeCloudShellSegment(7000.0f, -1.0f, r, 1500.0f, 4000.0f, 1.0e6f);
    ASSERT_TRUE(above.hit);
    EXPECT_NEAR(above.tStart, 3000.0f, 1.0f);
    EXPECT_NEAR(above.tEnd, 5500.0f, 1.0f);
}

TEST(VolumetricCloudLayerTest, RaysThatCanNotSeeTheLayerMiss)
{
    const float r = 6371000.0f;
    // Below the layer looking at the ground, and above the layer looking away from it.
    EXPECT_FALSE(ComputeCloudShellSegment(2.0f, -0.2f, r, 1500.0f, 4000.0f, 1.0e6f).hit);
    EXPECT_FALSE(ComputeCloudShellSegment(7000.0f, 0.3f, r, 1500.0f, 4000.0f, 1.0e6f).hit);
}

TEST(VolumetricCloudLayerTest, MaxDistanceCutsTheSegment)
{
    const float r = 6371000.0f;
    const CloudShellSegment cut = ComputeCloudShellSegment(2.0f, 0.02f, r, 1500.0f, 4000.0f, 90000.0f);
    ASSERT_TRUE(cut.hit);
    EXPECT_FLOAT_EQ(cut.tEnd, 90000.0f);
    EXPECT_FALSE(ComputeCloudShellSegment(2.0f, 0.02f, r, 1500.0f, 4000.0f, 1000.0f).hit);
}

TEST(VolumetricCloudLayerTest, WindMovesAlongTheDirection)
{
    float offset[2] = {};
    AdvanceCloudWind(offset, 10.0f, 0.0f, 2.0f);
    EXPECT_NEAR(offset[0], 20.0f, 1e-4f);
    EXPECT_NEAR(offset[1], 0.0f, 1e-4f);
    AdvanceCloudWind(offset, 10.0f, 90.0f, 1.0f);
    EXPECT_NEAR(offset[1], 10.0f, 1e-4f);
}

TEST(VolumetricCloudParamsTest, BlockLayoutMatchesTheShader)
{
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, eyeAndPlanetRadius), 64u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, sunDirectionAndIntensity), 80u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, sunColorAndAmbient), 96u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, ambientColor), 112u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, layer), 128u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, weather), 144u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, noise), 160u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, density), 176u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, heightShape), 192u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, march), 208u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, counts), 224u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, skip), 240u);
}

TEST(VolumetricCloudParamsTest, PacksTheSettingsAndTheFrame)
{
    VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    VolumetricCloudFrameFacts facts;
    facts.eye = Vec3(1.0f, 2.0f, 3.0f);
    facts.sunDirection = Vec3(0.0f, 1.0f, 0.0f);
    facts.sunIlluminance = Vec3(3.0f, 3.0f, 3.0f);
    facts.windOffset[0] = 5.0f;
    facts.windOffset[1] = -6.0f;
    facts.frameIndex = 77;
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(s, facts);
    EXPECT_FLOAT_EQ(p.eyeAndPlanetRadius[0], 1.0f);
    EXPECT_FLOAT_EQ(p.eyeAndPlanetRadius[1], 2.0f);
    EXPECT_FLOAT_EQ(p.eyeAndPlanetRadius[3], 6371000.0f);
    EXPECT_FLOAT_EQ(p.layer[0], s.layerBaseM);
    EXPECT_FLOAT_EQ(p.layer[1], s.layerTopM);
    EXPECT_FLOAT_EQ(p.weather[1], 5.0f);
    EXPECT_FLOAT_EQ(p.weather[2], -6.0f);
    EXPECT_FLOAT_EQ(p.weather[3], s.coverage);
    EXPECT_FLOAT_EQ(p.sunColorAndAmbient[0], 3.0f);
    EXPECT_EQ(p.counts[0], static_cast<std::uint32_t>(s.maxSteps));
    EXPECT_EQ(p.counts[1], 77u);
}

TEST(VolumetricCloudParamsTest, ASunBelowTheHorizonGivesNoSunLight)
{
    VolumetricCloudFrameFacts facts;
    facts.sunDirection = Vec3(0.0f, -1.0f, 0.0f);
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(VolumetricCloudSettings{}, facts);
    EXPECT_FLOAT_EQ(p.sunColorAndAmbient[0], 0.0f);
    EXPECT_GT(p.ambientColor[2], 0.0f);
}

const ReflectedDescriptorBinding* Find(const ShaderReflectionResult& result, const std::string& name)
{
    const auto it = std::find_if(result.descriptorBindings.begin(), result.descriptorBindings.end(),
        [&](const ReflectedDescriptorBinding& binding) { return binding.name == name; });
    return it == result.descriptorBindings.end() ? nullptr : &*it;
}

ShaderReflectionResult ReflectCompute(const char* path)
{
    const ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, path };
    return ReflectShader(std::span<const ShaderStageFile>(&stage, 1));
}

void ExpectViewerShader(const char* path, const char* sampler)
{
    const ShaderReflectionResult result = ReflectCompute(path);
    const ReflectedDescriptorBinding* source = Find(result, sampler);
    ASSERT_NE(source, nullptr) << sampler;
    EXPECT_EQ(source->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    const ReflectedDescriptorBinding* output = Find(result, "destinationImage");
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(output->access, ReflectedAccess::WriteOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 2u);
    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.offset, 0u);
    EXPECT_EQ(result.pushConstantRange.size, sizeof(VolumetricCloudSlicePush));
}

TEST(VolumetricCloudShaderNamesTest, MarchExposesBoundNames)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_CLOUD_MARCH_SPV_PATH);
    const ReflectedDescriptorBinding* params = Find(result, "params");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);
    for (const char* sampler : { "sourceColor", "sourceDepth", "shapeVolume", "detailVolume", "weatherMap", "blueNoise" }) {
        const ReflectedDescriptorBinding* binding = Find(result, sampler);
        ASSERT_NE(binding, nullptr) << sampler;
        EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) << sampler;
    }
    const ReflectedDescriptorBinding* output = Find(result, "destinationImage");
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(output->access, ReflectedAccess::WriteOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 8u);
    EXPECT_FALSE(result.hasPushConstantRange);
}

TEST(VolumetricCloudShaderNamesTest, Slice3dExposesBoundNamesAndPushBlock)
{
    ExpectViewerShader(GTE_TEST_CLOUD_SLICE3D_SPV_PATH, "noiseVolume");
}

TEST(VolumetricCloudShaderNamesTest, Slice2dExposesBoundNamesAndPushBlock)
{
    ExpectViewerShader(GTE_TEST_CLOUD_SLICE2D_SPV_PATH, "noiseTexture");
}

} // namespace
} // namespace gte
