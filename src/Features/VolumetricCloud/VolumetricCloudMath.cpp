#include "VolumetricCloudMath.h"

#include "../../Core/FeatureKit/ScreenSpaceView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

namespace gte {

namespace {

constexpr float kMinLayerThicknessM = 50.0f;
constexpr float kMinStepM = 1e-4f;
// A camera standing on the ground looks into it, even with rounding at 6371 km.
constexpr float kGroundSlackM = 0.5f;
constexpr float kMinPhaseDenominator = 1e-4f;
// Radiance of the blue sky light, relative to the sun's luminance.
constexpr float kAmbientTint[3] = { 0.55f, 0.7f, 1.0f };

float ClampOrDefault(float value, float low, float high, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

float Saturate(float v) noexcept
{
    return std::clamp(v, 0.0f, 1.0f);
}

float SmoothStep(float lo, float hi, float v) noexcept
{
    const float t = Saturate((v - lo) / std::max(hi - lo, kMinStepM));
    return t * t * (3.0f - 2.0f * t);
}

// Both roots of the ray against a sphere of altitude `heightM`, centred below the eye.
struct SphereRoots {
    bool hit = false;
    float tNear = 0.0f;
    float tFar = 0.0f;
};

// The discriminant is a product of two sums, so it keeps its digits for a radius of 6371 km.
SphereRoots IntersectShell(float cameraY, float dirY, float radiusM, float heightM) noexcept
{
    const float ro = radiusM + cameraY;
    const float s = std::sqrt(std::max(1.0f - dirY * dirY, 0.0f));
    const float inner = (heightM - cameraY) + ro * dirY * dirY / (1.0f + s);
    const float outer = radiusM + heightM + ro * s;
    const float disc = inner * outer;
    SphereRoots roots;
    if (disc < 0.0f) {
        return roots;
    }
    const float root = std::sqrt(disc);
    const float b = ro * dirY;
    roots.hit = true;
    roots.tNear = -b - root;
    roots.tFar = -b + root;
    return roots;
}

using Matrix64 = std::array<double, 16>; // Column-major, like Mat4.

Matrix64 ToDouble(const Mat4& m) noexcept
{
    Matrix64 out{};
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out[static_cast<std::size_t>(c * 4 + r)] = static_cast<double>(m(r, c));
        }
    }
    return out;
}

Mat4 ToFloat(const Matrix64& m) noexcept
{
    Mat4 out = Mat4::Identity();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out(r, c) = static_cast<float>(m[static_cast<std::size_t>(c * 4 + r)]);
        }
    }
    return out;
}

// Gauss-Jordan with partial pivoting. False when the matrix is singular.
bool Invert(const Matrix64& in, Matrix64& out) noexcept
{
    double a[4][8] = {};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            a[r][c] = in[static_cast<std::size_t>(c * 4 + r)];
        }
        a[r][4 + r] = 1.0;
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r) {
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) {
                pivot = r;
            }
        }
        if (!(std::abs(a[pivot][col]) > 1e-300)) {
            return false;
        }
        for (int c = 0; c < 8; ++c) {
            std::swap(a[col][c], a[pivot][c]);
        }
        const double scale = 1.0 / a[col][col];
        for (int c = 0; c < 8; ++c) {
            a[col][c] *= scale;
        }
        for (int r = 0; r < 4; ++r) {
            if (r == col) {
                continue;
            }
            const double factor = a[r][col];
            for (int c = 0; c < 8; ++c) {
                a[r][c] -= factor * a[col][c];
            }
        }
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[static_cast<std::size_t>(c * 4 + r)] = a[r][4 + c];
        }
    }
    return true;
}

} // namespace

CloudRelativeCamera MakeCloudRelativeCamera(const Mat4& viewProjection, const Vec3& eye) noexcept
{
    CloudRelativeCamera camera;
    Matrix64 vp = ToDouble(viewProjection);
    // The projection of the eye itself becomes the translation column: A = VP * Translation(eye).
    for (int r = 0; r < 4; ++r) {
        const std::size_t row = static_cast<std::size_t>(r);
        vp[12 + row] += vp[row] * static_cast<double>(eye.x) + vp[4 + row] * static_cast<double>(eye.y)
            + vp[8 + row] * static_cast<double>(eye.z);
    }
    Matrix64 inverse{};
    if (!Invert(vp, inverse)) {
        return camera;
    }
    for (const double value : vp) {
        if (!std::isfinite(value)) {
            return camera;
        }
    }
    camera.valid = true;
    camera.projection = ToFloat(vp);
    camera.inverse = ToFloat(inverse);
    return camera;
}

VolumetricCloudSettings SanitizeVolumetricCloudSettings(const VolumetricCloudSettings& candidate) noexcept
{
    const VolumetricCloudSettings defaults;
    VolumetricCloudSettings out = candidate;
    for (const VolumetricCloudFloatSetting& entry : kVolumetricCloudFloatSettings) {
        out.*(entry.member) = ClampOrDefault(candidate.*(entry.member), entry.low, entry.high, defaults.*(entry.member));
    }
    out.layerTopM = std::max(out.layerTopM, out.layerBaseM + kMinLayerThicknessM);
    out.channel = std::min(candidate.channel, kCloudDebugChannelRgb);
    if (static_cast<std::uint8_t>(candidate.debugView) > static_cast<std::uint8_t>(VolumetricCloudDebugView::BlueNoise)) {
        out.debugView = VolumetricCloudDebugView::Off;
    }
    return out;
}

VolumetricCloudSettings ApplyVolumetricCloudPreset(
    const VolumetricCloudSettings& current, VolumetricCloudPreset preset) noexcept
{
    const VolumetricCloudSettings defaults;
    VolumetricCloudSettings out = current;
    out.coverage = defaults.coverage;
    out.density = defaults.density;
    switch (preset) {
    case VolumetricCloudPreset::Clear:
        out.coverage = 0.2f;
        out.density = 0.9f;
        break;
    case VolumetricCloudPreset::Scattered:
        break;
    case VolumetricCloudPreset::Broken:
        out.coverage = 0.8f;
        break;
    case VolumetricCloudPreset::Overcast:
        out.coverage = 1.0f;
        out.density = 1.4f;
        break;
    }
    return SanitizeVolumetricCloudSettings(out);
}

VolumetricCloudSlicePush MakeVolumetricCloudSlicePush(const VolumetricCloudSettings& settings) noexcept
{
    return VolumetricCloudSlicePush{ settings.slice, settings.tiling, settings.channel, 0u };
}

float CloudRemap(float v, float lo0, float hi0, float lo1, float hi1) noexcept
{
    float width = hi0 - lo0;
    if (std::abs(width) < kMinStepM) {
        width = width < 0.0f ? -kMinStepM : kMinStepM;
    }
    return lo1 + (v - lo0) / width * (hi1 - lo1);
}

float CloudAltitudeM(float horizontalSquared, float y, float radiusM) noexcept
{
    const float s = horizontalSquared + y * (2.0f * radiusM + y);
    return s / (std::sqrt(std::max(s + radiusM * radiusM, 0.0f)) + radiusM);
}

float CloudHeightFraction(float altitudeM, float baseM, float topM) noexcept
{
    return Saturate((altitudeM - baseM) / std::max(topM - baseM, kMinStepM));
}

float CloudShapeGradient(float heightFraction, float peak, float baseRoundness, float topRoundness) noexcept
{
    const float stop = Saturate(peak + topRoundness);
    const float bottom = Saturate(CloudRemap(heightFraction, 0.0f, baseRoundness, 0.0f, 1.0f));
    const float top = Saturate(CloudRemap(heightFraction, stop * 0.2f, stop, 1.0f, 0.0f));
    return bottom * top;
}

float CloudDensityGradient(float heightFraction, float ramp, float topFade) noexcept
{
    const float bottom = heightFraction * Saturate(CloudRemap(heightFraction, 0.0f, ramp, 0.0f, 1.0f));
    const float top = Saturate(CloudRemap(heightFraction, topFade, 1.0f, 1.0f, 0.0f));
    return bottom * top;
}

CloudShellSegment ComputeCloudShellSegment(
    float cameraY, float dirY, float radiusM, float baseM, float topM, float maxDistanceM) noexcept
{
    CloudShellSegment segment;
    const float y = std::max(cameraY, 0.0f);
    const SphereRoots top = IntersectShell(y, dirY, radiusM, topM);
    const SphereRoots base = IntersectShell(y, dirY, radiusM, baseM);

    float start = 0.0f;
    float end = 0.0f;
    if (y < baseM) {
        // The base sphere also holds the ground: a ray that leaves it only far away passes the layer.
        const SphereRoots ground = IntersectShell(y, dirY, radiusM, 0.0f);
        if (dirY < 0.0f && ground.hit && ground.tNear > -kGroundSlackM) {
            return segment;
        }
        start = base.tFar;
        end = top.tFar;
    } else if (y < topM) {
        end = top.tFar;
        if (base.hit && base.tNear > 0.0f) {
            end = std::min(end, base.tNear);
        }
    } else {
        if (!top.hit || top.tFar <= 0.0f) {
            return segment;
        }
        start = std::max(top.tNear, 0.0f);
        end = (base.hit && base.tNear > 0.0f) ? base.tNear : top.tFar;
    }
    end = std::min(end, maxDistanceM);
    if (!(end > start)) {
        return segment;
    }
    segment.hit = true;
    segment.tStart = start;
    segment.tEnd = end;
    return segment;
}

void AdvanceCloudWind(double (&offset)[2], double (&step)[2], float speed, float directionDegrees, float dt,
    double periodM) noexcept
{
    const double radians = static_cast<double>(directionDegrees) * kPi / 180.0;
    const double distance = static_cast<double>(speed) * static_cast<double>(dt);
    step[0] = std::cos(radians) * distance;
    step[1] = std::sin(radians) * distance;
    for (int axis = 0; axis < 2; ++axis) {
        offset[axis] += step[axis];
        if (periodM > 0.0) {
            offset[axis] = std::fmod(offset[axis], periodM);
            offset[axis] += offset[axis] < 0.0 ? periodM : 0.0;
        }
    }
}

double CloudWindPeriodM(const VolumetricCloudSettings& safe) noexcept
{
    constexpr std::uint64_t kMaxPeriodM = 100000000ull;
    std::uint64_t period = 1;
    for (const float tile : { safe.weatherScaleM, safe.shapeScaleM, safe.detailScaleM }) {
        const std::uint64_t metres = static_cast<std::uint64_t>(std::max(std::llround(tile), 1ll));
        period = period / std::gcd(period, metres) * metres;
        if (period > kMaxPeriodM) {
            return 0.0;
        }
    }
    return static_cast<double>(period);
}

float CloudHenyeyGreenstein(float cosTheta, float g) noexcept
{
    const float g2 = g * g;
    const float denominator = std::max(1.0f + g2 - 2.0f * g * cosTheta, kMinPhaseDenominator);
    return (1.0f - g2) / (4.0f * kPi * denominator * std::sqrt(denominator));
}

float CloudDraine(float cosTheta, float g, float alpha) noexcept
{
    const float shape = (1.0f + alpha * cosTheta * cosTheta) / (1.0f + alpha * (1.0f + 2.0f * g * g) / 3.0f);
    return CloudHenyeyGreenstein(cosTheta, g) * shape;
}

float CloudPhase(const VolumetricCloudSettings& safe, float cosTheta, float eccentricity) noexcept
{
    if (safe.phaseModel >= 0.5f) {
        const float hg = CloudHenyeyGreenstein(cosTheta, kCloudDraineHgG * eccentricity);
        const float draine = CloudDraine(cosTheta, kCloudDraineG * eccentricity, kCloudDraineAlpha);
        return (1.0f - kCloudDraineWeight) * hg + kCloudDraineWeight * draine;
    }
    const float forward = CloudHenyeyGreenstein(cosTheta, safe.forwardG * eccentricity);
    const float backward = CloudHenyeyGreenstein(cosTheta, safe.backwardG * eccentricity);
    return forward + (backward - forward) * safe.backwardMix;
}

float CloudBeer(float opticalDepth) noexcept
{
    return std::exp(-std::max(opticalDepth, 0.0f));
}

float CloudSunScatter(const VolumetricCloudSettings& safe, float cosTheta, float opticalDepth) noexcept
{
    float sum = 0.0f;
    float attenuation = 1.0f;
    float contribution = 1.0f;
    float eccentricity = 1.0f;
    const int octaves = static_cast<int>(std::lround(safe.msOctaves));
    for (int i = 0; i < octaves; ++i) {
        sum += contribution * CloudPhase(safe, cosTheta, eccentricity) * CloudBeer(opticalDepth * attenuation);
        attenuation *= safe.msAttenuation;
        contribution *= safe.msContribution;
        eccentricity *= safe.msEccentricity;
    }
    return sum + safe.silverIntensity * CloudHenyeyGreenstein(cosTheta, safe.silverG) * CloudBeer(opticalDepth);
}

float CloudStepOpacity(float extinction, float stepLength) noexcept
{
    return 1.0f - std::exp(-std::max(extinction, 0.0f) * std::max(stepLength, 0.0f));
}

void CloudAccumulateStep(float& light, float& transmittance, float source, float opacity) noexcept
{
    light += transmittance * source * opacity;
    transmittance *= 1.0f - opacity;
}

float CloudPlanetRadiusKm(const VolumetricCloudSettings& safe, const VolumetricCloudFrameFacts& facts) noexcept
{
    const bool shared = facts.atmosphereLit && std::isfinite(facts.atmospherePlanetRadiusKm) && facts.atmospherePlanetRadiusKm >= 1.0f;
    return shared ? facts.atmospherePlanetRadiusKm : safe.planetRadiusKm;
}

VolumetricCloudParamsGpu MakeVolumetricCloudParams(
    const VolumetricCloudSettings& safe, const VolumetricCloudFrameFacts& facts) noexcept
{
    VolumetricCloudParamsGpu p;
    p.invViewProjection = facts.invViewProjection;
    fx::PackVec3(p.eyeAndPlanetRadius, facts.eye, CloudPlanetRadiusKm(safe, facts) * 1000.0f);

    // A sun under the horizon lights nothing; the sky keeps a little ambient light.
    const float sunVisibility = SmoothStep(-0.05f, 0.15f, facts.sunDirection.y);
    fx::PackVec3(p.sunDirectionAndIntensity, facts.sunDirection, safe.sunIntensity);
    const Vec3 sun = facts.sunIlluminance * facts.sunTint;
    // With the atmosphere's tables the sun goes in untouched: they hold its colour and the planet's shadow.
    if (facts.atmosphereLit) {
        fx::PackVec3(p.sunColorAndAmbient, facts.sunIlluminance, safe.ambientIntensity);
    } else {
        fx::PackVec3(p.sunColorAndAmbient, sun * sunVisibility, safe.ambientIntensity);
    }

    const float luminance = 0.2126f * sun.x + 0.7152f * sun.y + 0.0722f * sun.z;
    const float ambient = luminance * facts.ambientScale * (0.15f + 0.85f * sunVisibility);
    fx::PackVec3(p.ambientColor, Vec3(kAmbientTint[0], kAmbientTint[1], kAmbientTint[2]) * ambient);

    p.layer[0] = safe.layerBaseM;
    p.layer[1] = safe.layerTopM;
    p.layer[2] = safe.maxDistanceM;
    p.layer[3] = safe.fadeStart;
    p.weather[0] = safe.weatherScaleM;
    p.weather[1] = facts.windOffset[0];
    p.weather[2] = facts.windOffset[1];
    p.weather[3] = safe.coverage;
    p.noise[0] = safe.shapeScaleM;
    p.noise[1] = safe.detailScaleM;
    p.noise[2] = safe.detailStrength;
    p.noise[3] = safe.detailFadeDistM;
    p.density[0] = safe.density;
    p.density[1] = safe.extinction;
    p.density[2] = safe.peakMin;
    p.density[3] = safe.jitter;
    p.heightShape[0] = safe.baseRoundness;
    p.heightShape[1] = safe.topRoundness;
    p.heightShape[2] = safe.densityRamp;
    p.heightShape[3] = safe.densityTopFade;
    p.march[0] = safe.stepBaseM;
    p.march[1] = safe.stepGrowth;
    p.march[2] = safe.coarseMultiplier;
    p.march[3] = safe.minTransmittance;
    p.counts[0] = static_cast<std::uint32_t>(std::lround(safe.maxSteps));
    p.counts[1] = static_cast<std::uint32_t>(facts.frameIndex & 0xFFFFFFFFu);
    p.counts[2] = static_cast<std::uint32_t>(std::lround(safe.emptyToCoarse));
    p.counts[3] = static_cast<std::uint32_t>(std::lround(safe.lightStride));
    p.skip[0] = safe.clearSkip;
    p.lightMarch[0] = std::round(safe.sunSteps);
    p.lightMarch[1] = safe.lightReach;
    p.lightMarch[2] = std::round(safe.msOctaves);
    p.lightMarch[3] = safe.powder;
    p.lightPhase[0] = safe.forwardG;
    p.lightPhase[1] = safe.backwardG;
    p.lightPhase[2] = safe.backwardMix;
    p.lightPhase[3] = safe.silverIntensity;
    p.lightScatter[0] = safe.silverG;
    p.lightScatter[1] = safe.msAttenuation;
    p.lightScatter[2] = safe.msContribution;
    p.lightScatter[3] = safe.msEccentricity;
    p.lightMisc[0] = std::round(safe.phaseModel);
    p.lightMisc[1] = safe.ambientBottom;
    p.lightMisc[2] = safe.hazeDistanceM;
    p.lightMisc[3] = safe.lightDetail;
    p.drainePhase[0] = kCloudDraineHgG;
    p.drainePhase[1] = kCloudDraineG;
    p.drainePhase[2] = kCloudDraineAlpha;
    p.drainePhase[3] = kCloudDraineWeight;
    const std::uint32_t divisor = CloudResolutionDivisor(safe);
    const CloudSampleOffset offset = CloudBayerOffset(facts.frameIndex, divisor);
    p.lowRes[0] = divisor;
    p.lowRes[1] = offset.x;
    p.lowRes[2] = offset.y;
    p.fullSize[0] = facts.viewWidth;
    p.fullSize[1] = facts.viewHeight;
    p.atmosphereMode[0] = facts.atmosphereLit ? 1.0f : 0.0f;
    p.atmosphereMode[1] = facts.skyIntensity;
    p.atmosphereMode[2] = safe.airScale;
    p.atmosphereMode[3] = safe.skyAmbient;
    fx::PackVec3(p.atmosphereCamera, facts.atmosphereCameraKm);
    return p;
}

std::uint32_t CloudResolutionDivisor(const VolumetricCloudSettings& safe) noexcept
{
    if (safe.resolutionDivisor < 1.5f) {
        return 1;
    }
    return safe.resolutionDivisor < 3.0f ? 2 : 4;
}

CloudSampleOffset CloudBayerOffset(std::uint64_t frameIndex, std::uint32_t divisor) noexcept
{
    // Rank of each cell of the Bayer matrix; the cell holding rank k is visited on frame k.
    constexpr std::uint32_t kBayer2[4] = { 0, 2, 3, 1 };
    constexpr std::uint32_t kBayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    if (divisor != 2 && divisor != 4) {
        return CloudSampleOffset{};
    }
    const std::uint32_t cells = divisor * divisor;
    const std::uint32_t rank = static_cast<std::uint32_t>(frameIndex % cells);
    const std::uint32_t* matrix = divisor == 2 ? kBayer2 : kBayer4;
    for (std::uint32_t i = 0; i < cells; ++i) {
        if (matrix[i] == rank) {
            return CloudSampleOffset{ i % divisor, i / divisor };
        }
    }
    return CloudSampleOffset{};
}

VolumetricCloudResolveParamsGpu MakeVolumetricCloudResolveParams(const VolumetricCloudSettings& safe,
    const VolumetricCloudFrameFacts& facts, const VolumetricCloudResolveFacts& resolve) noexcept
{
    VolumetricCloudResolveParamsGpu p;
    p.invViewProjection = facts.invViewProjection;
    p.prevViewProjection = resolve.prevViewProjection;
    fx::PackVec3(p.eye, facts.eye);
    p.windDelta[0] = resolve.windDelta[0];
    p.windDelta[2] = resolve.windDelta[1];
    const bool accumulate = safe.temporal >= 0.5f;
    p.blend[0] = accumulate ? safe.historyBlend : 1.0f;
    p.blend[1] = accumulate ? safe.freshBlend : 1.0f;
    p.blend[2] = safe.clampExpand;
    p.blend[3] = safe.maxDistanceM * 0.25f;
    p.motion[0] = accumulate ? safe.motionBlend : 1.0f;
    const std::uint32_t divisor = CloudResolutionDivisor(safe);
    const CloudSampleOffset offset = CloudBayerOffset(facts.frameIndex, divisor);
    p.counts[0] = divisor;
    p.counts[1] = offset.x;
    p.counts[2] = offset.y;
    p.counts[3] = (accumulate && resolve.historyValid) ? 1u : 0u;
    p.sizes[0] = facts.viewWidth;
    p.sizes[1] = facts.viewHeight;
    p.sizes[2] = resolve.lowWidth;
    p.sizes[3] = resolve.lowHeight;
    return p;
}

float CloudAerialComposite(float scene, float cloudTransmittance, float cloudLight, float airTransmittance,
    float airLight) noexcept
{
    return scene * cloudTransmittance + cloudLight * airTransmittance + (1.0f - cloudTransmittance) * airLight;
}

bool CloudTapUsable(float tapEndKm, float tapDistanceKm, float tapTransmittance, float pixelEndKm) noexcept
{
    constexpr float kRelative = 0.03f;
    constexpr float kSlackKm = 0.02f;
    // The tap must have looked at least as far as this pixel's surface.
    if (tapEndKm < (1.0f - kRelative) * pixelEndKm - kSlackKm) {
        return false;
    }
    // A cloud that sits behind this pixel's surface must not be added to it.
    return tapTransmittance > 0.999f || tapDistanceKm <= (1.0f + kRelative) * pixelEndKm + kSlackKm;
}

} // namespace gte
