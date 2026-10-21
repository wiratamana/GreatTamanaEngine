#pragma once

#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <cstdint>

namespace gte {

// GPU buffer layouts read by the atmosphere compute shaders, Vulkan-free. Each struct is a sequence of 16-byte
// groups (a Vec3 plus a scalar), identical under std140 and std430.
static_assert(sizeof(Vec3) == 12, "Vec3 must be 3 packed floats so Vec3 + float fills one 16-byte group");

// Physical constants of one atmosphere. Coefficients are per km (km^-1); heights, radii and thickness are in km.
// The ozone layer is one symmetric tent profile; thickness is measured above the ground.
struct AtmosphereParametersGpu {
    // 16 bytes: Rayleigh scattering at sea level (it also equals Rayleigh extinction) plus the reciprocal of its
    // exponential scale height.
    Vec3 rayleighScattering = Vec3(0.0058f, 0.0135f, 0.0331f);
    float rayleighDensityExpScale = 1.0f / 8.0f; // 1 / (8km scale height)

    // 16 bytes: grey Mie scattering plus the Cornette-Shanks phase asymmetry g.
    Vec3 mieScattering = Vec3(0.006f, 0.006f, 0.006f);
    float miePhaseG = 0.76f;

    // 16 bytes: grey Mie absorption plus the reciprocal of the Mie exponential scale height.
    Vec3 mieAbsorption = Vec3(0.00066f, 0.00066f, 0.00066f);
    float mieDensityExpScale = 1.0f / 1.2f; // 1 / (1.2km scale height)

    // 16 bytes: ozone absorption plus the tent profile's center altitude.
    Vec3 ozoneAbsorption = Vec3(0.00065f, 0.00188f, 0.00008f);
    float ozoneTentCenterKm = 25.0f;

    // 16 bytes: ground albedo (used by the multi-scattering ground bounce) plus the tent's half-width.
    Vec3 groundAlbedo = Vec3(0.3f, 0.3f, 0.3f);
    float ozoneTentHalfWidthKm = 15.0f;

    // 16 bytes: planet radius, atmosphere thickness, multi-scattering strength, one padding float.
    float planetRadiusKm = 6371.0f;
    float atmosphereThicknessKm = 100.0f;
    float multiScatteringStrength = 1.0f;
    float _pad1 = 0.0f;
};
static_assert(sizeof(AtmosphereParametersGpu) == 96, "AtmosphereParametersGpu must be 96 bytes (6 vec4 groups)");

// Per-view, per-frame values the atmosphere passes read next to AtmosphereParametersGpu.
struct AtmosphereFrameUniforms {
    // 16 bytes: camera position in atmosphere space (km, measured from the planet center) plus padding.
    Vec3 cameraPositionAtmosphere = Vec3::Zero();
    float _pad0 = 0.0f;

    // 16 bytes: normalized direction from the scene toward the sun plus padding.
    Vec3 sunDirection = Vec3::Up();
    float _pad1 = 0.0f;

    // 16 bytes: sun color/illuminance plus padding.
    Vec3 sunIlluminance = Vec3::One();
    float _pad2 = 0.0f;

    // 64 bytes: the view's inverse view-projection, inverted on the CPU. Shaders unproject two NDC points with it
    // to get a world-space view ray. Defaults to identity so an unset value stays invertible.
    Mat4 invViewProjection = Mat4::Identity();

    // 16 bytes: aerial perspective ray-march tunables, copied from AtmosphereSettings every frame. These defaults
    // are the unexaggerated real-scale values and only apply to a struct built without going through settings.
    float aerialPerspectiveMaxDistanceKm = 10.0f;
    float aerialPerspectiveDepthExponent = 2.0f;
    float aerialPerspectiveSamplesPerSliceAsFloat = 8.0f; // Stored as float; the shader rounds and clamps to [1, 8].
    float aerialPerspectiveScatteringExaggeration = 1.0f; // Multiplies the extinction while marching the volume.
};
static_assert(sizeof(AtmosphereFrameUniforms) == 128, "AtmosphereFrameUniforms must be 128 bytes (3 vec4 + mat4 + vec4)");

// Tunable atmosphere knobs, edited live in the Editor's "Atmosphere" panel. The physical fields mirror
// AtmosphereParametersGpu and are re-read every frame by AtmosphereFeature. One instance per session.
struct AtmosphereSettings {
    Vec3 rayleighScattering = Vec3(0.0058f, 0.0135f, 0.0331f);
    float rayleighScaleHeightKm = 8.0f; // Scale height (km), not the GPU struct's reciprocal.
    Vec3 mieScattering = Vec3(0.006f, 0.006f, 0.006f);
    float miePhaseG = 0.76f;
    Vec3 mieAbsorption = Vec3(0.00066f, 0.00066f, 0.00066f);
    float mieScaleHeightKm = 1.2f; // Scale height (km).
    Vec3 ozoneAbsorption = Vec3(0.00065f, 0.00188f, 0.00008f);
    float ozoneTentCenterKm = 25.0f;
    Vec3 groundAlbedo = Vec3(0.3f, 0.3f, 0.3f); // Fraction of light the ground reflects back.
    float ozoneTentHalfWidthKm = 15.0f;
    float planetRadiusKm = 6371.0f;
    float atmosphereThicknessKm = 100.0f;
    float multiScatteringStrength = 1.0f;

    // Strength of the aerial perspective composite: 1.0 is the physical result, 0.0 passes the scene color through.
    float aerialPerspectiveStrength = 1.0f;

    // Linear multiplier on the sky background radiance.
    float skyIntensity = 6.0f;

    // Aerial perspective volume ray-march tunables, tuned for content tens to a few hundred meters from the camera.
    float aerialPerspectiveMaxDistanceKm = 0.5f;
    float aerialPerspectiveDepthExponent = 2.0f;
    int aerialPerspectiveSamplesPerSlice = 8;
    float aerialPerspectiveScatteringExaggeration = 8.0f;

    // Sun and sky light of materials and light shafts follow the atmosphere.
    bool sceneLightFollowsAtmosphere = true;
};

} // namespace gte
