#pragma once

// Orbit camera around a target point. Pure math, no input or UI types.
// Left-handed, +Y up. Near/far follow the distance so very small and very large meshes both stay visible.

#include "../Math/Mat4.h"
#include "../Math/Vec3.h"

namespace gte {

struct OrbitCamera {
    static constexpr float kFovYDegrees = 45.0f;
    static constexpr float kMaxPitchDegrees = 85.0f;
    static constexpr float kDegreesPerPixel = 0.3f;
    static constexpr float kPanPerPixelPerDistance = 0.0015f;
    static constexpr float kWheelStep = 0.15f;
    static constexpr float kMaxDistanceInRadii = 200.0f;

    float yawDeg = 20.0f;
    float pitchDeg = 12.0f;
    float distance = 10.0f;
    Vec3 target = Vec3::Zero();
    float radius = 1.0f; // framed bounding radius, scales the distance limits and the clip planes

    Vec3 Eye() const noexcept;
    Mat4 View() const noexcept;
    // Engine projection (flipY = true), used to render the image.
    Mat4 Projection(float aspect) const noexcept;
    // Same lens without the Y flip, as the transform gizmo expects.
    Mat4 GizmoProjection(float aspect) const noexcept;

    float MinDistance() const noexcept;
    float MaxDistance() const noexcept;
    float NearZ() const noexcept;
    float FarZ() const noexcept;

    // Looks at `center` from a distance that fits a sphere of `meshRadius`. A bad radius becomes 1.
    void Frame(const Vec3& center, float meshRadius) noexcept;
    // Pixel deltas; non-finite input is ignored.
    void Rotate(float dx, float dy) noexcept;
    void Pan(float dx, float dy) noexcept;
    // Positive wheel moves closer.
    void Dolly(float wheel) noexcept;
};

} // namespace gte
