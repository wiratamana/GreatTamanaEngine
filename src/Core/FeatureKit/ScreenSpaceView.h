#pragma once

#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"
#include "../Extent.h"

#include <cstdint>
#include <type_traits>

namespace gte::fx {

// Camera data derived from invViewProjection once per view and frame. Perspective cameras only.
struct CameraBasis {
    bool valid = false;
    Vec3 eye;
    Vec3 right;              // Unit, world direction of increasing pixel x.
    Vec3 down;               // Unit, world direction of increasing pixel y.
    Vec3 forward;            // Unit, optical axis toward the scene.
    float pixelScale = 0.0f; // World size of one pixel at view depth 1.
};

// Camera axes and pixel scale from the inverse view-projection alone. Invalid for non-perspective or skewed cameras.
CameraBasis DeriveCameraBasis(const Vec3& eye, const Mat4& invViewProjection, Extent2D extent) noexcept;

// World position of one depth sample through a camera's inverse view-projection.
Vec3 UnprojectDepth(const Mat4& invViewProjection, float ndcX, float ndcY, float depthZeroToOne) noexcept;

// ceil(size / divisor), at least 1 per axis.
Extent2D ScaledExtent(Extent2D extent, std::uint32_t divisor) noexcept;

// Clamps into [lo, hi]. NaN or infinity gives `fallback`.
float SanitizeFinite(float value, float lo, float hi, float fallback) noexcept;

// Stores xyz and w into a 4-float GPU member.
void PackVec3(float (&dst)[4], const Vec3& v, float w = 0.0f) noexcept;

// Previous-frame data a temporal filter reprojects with. Valid only if the resolve pass wrote it.
struct TemporalHistory {
    bool valid = false;
    std::uint64_t frame = 0;
    Extent2D extent{};
    std::uint32_t divisor = 0;
    std::uint32_t stableFrames = 0; // Frames in a row the extent and divisor did not change.
    Mat4 viewProjection = Mat4::Identity();
    Vec3 eye;
    Vec3 forward;
};

// True when `previous` may be reprojected into this frame: written last frame, same size and divisor,
// stable for two frames, and no camera cut.
bool HistoryUsable(const TemporalHistory& previous, const CameraBasis& basis, std::uint64_t frame, Extent2D extent,
    std::uint32_t divisor) noexcept;

// History record of a frame whose resolve pass ran.
TemporalHistory MakeHistory(const CameraBasis& basis, const Mat4& viewProjection, std::uint64_t frame, Extent2D extent,
    std::uint32_t divisor, const TemporalHistory& previous) noexcept;

// std430 block of a temporal resolve shader; member order matches the shader.
struct TemporalResolveParamsGpu {
    Mat4 invViewProjection;
    Mat4 prevViewProjection;
    float eye[4] = {};
    float forward[4] = {};
    float prevEye[4] = {};
    float prevForward[4] = {};
    float filter[4] = {};         // x depth sigma, y history weight, z history depth tolerance, w spatial on.
    std::uint32_t counts[4] = {}; // x divisor, y history valid, z depth width, w depth height.
};
static_assert(sizeof(TemporalResolveParamsGpu) == 224);
static_assert(std::is_trivially_copyable_v<TemporalResolveParamsGpu>);

struct TemporalResolveSettings {
    float depthSigma = 0.03f;
    float historyWeight = 0.9f;
    bool spatial = true;
    std::uint32_t divisor = 1;
};

// `settings` must be sanitized by the caller.
TemporalResolveParamsGpu MakeTemporalResolveParams(const CameraBasis& basis, const TemporalHistory& previous,
    const TemporalResolveSettings& settings, Extent2D viewExtent, const Mat4& invViewProjection,
    bool historyValid) noexcept;

} // namespace gte::fx
