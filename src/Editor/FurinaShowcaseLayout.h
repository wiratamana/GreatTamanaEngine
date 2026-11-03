#pragma once

#include "../Math/Vec3.h"

// Pure layout of the Furina showcase scene: a portrait camera and a key light fitted to the model height.
// The model stands at the origin facing -Z, so the camera sits on -Z and looks along +Z. No registry, no GPU.

namespace gte {

inline constexpr float kFurinaShowcaseFovYDegrees = 28.0f;
inline constexpr float kFurinaShowcaseCameraPitchUpDegrees = 6.5f;

// Screen position, measured from the top edge, of the hat top and of the hips.
inline constexpr float kFurinaShowcaseHatTopFromTop = 0.03f;
inline constexpr float kFurinaShowcaseHipsFromTop = 0.85f;

// Hip height as a share of the model height, used when the skeleton gives no usable hip bone.
inline constexpr float kFurinaShowcaseFallbackHipFraction = 0.52f;

inline constexpr float kFurinaShowcaseSunElevationDegrees = 40.0f;
inline constexpr float kFurinaShowcaseSunLeftOfCameraDegrees = 35.0f;
inline constexpr Vec3 kFurinaShowcaseSunColor{ 1.0f, 0.96f, 0.90f };

// Model-space heights of the bind pose: lowest vertex, highest vertex (hat top) and the hip bone.
struct FurinaShowcaseModel {
    float minY = 0.0f;
    float maxY = 0.0f;
    float hipY = 0.0f;
};

struct FurinaShowcaseLayout {
    bool valid = false;
    float cameraDistance = 0.0f; // Horizontal distance from the model axis.
    Vec3 cameraPosition;
    float cameraPitchEulerDegrees = 0.0f; // Editor Euler pitch; negative looks up.
    float fovYDegrees = kFurinaShowcaseFovYDegrees;
    float nearZ = 0.1f;
    float farZ = 1000.0f;
    float sunPitchEulerDegrees = 0.0f;
    float sunYawEulerDegrees = 0.0f;
    Vec3 sunColor = kFurinaShowcaseSunColor;
};

// Hip height inside the model bounds: `hipY` when it lies strictly between them, else a fixed share of the height.
float ResolveFurinaHipHeight(float minY, float maxY, float hipY) noexcept;

// Puts the hat top and the hips at their screen positions. `valid` is false for empty or non-finite bounds.
FurinaShowcaseLayout ComputeFurinaShowcaseLayout(const FurinaShowcaseModel& model) noexcept;

} // namespace gte
