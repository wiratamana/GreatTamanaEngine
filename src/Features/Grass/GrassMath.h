#pragma once

#include "GrassTypes.h"
#include "../../Math/Mat4.h"

#include <cstdint>

namespace gte {

// Clamps every value, orders the LOD distances, makes the densities non-increasing and sizes the grid.
GrassSettings SanitizeGrassSettings(const GrassSettings& in) noexcept;

// Fewest chunks per side whose grid reaches `lod2Distance` from the camera in every direction.
std::uint32_t MinGrassGridCells(float lod2Distance) noexcept;

// Fewest texels per side of a height tile centred on the camera that reaches the LOD2 distance plus two chunks.
std::uint32_t MinGrassTileTexels(float lod2Distance, float texelSize) noexcept;

// The grid of chunks around the camera: cell (0, 0) is the chunk with this world chunk index.
struct GrassGrid {
    std::int32_t originChunkX = 0;
    std::int32_t originChunkZ = 0;
    std::uint32_t cells = 0;
};

// Centres a `cells` x `cells` grid on the camera; the origin only changes when the camera crosses a chunk border.
GrassGrid MakeGrassGrid(double eyeX, double eyeZ, std::uint32_t cells) noexcept;

// Six normalised planes of a view-projection (zero-to-one depth); inside when dot(n, p) + w >= 0.
// Give it an eye-relative matrix and the planes are eye-relative.
void ExtractGrassFrustumPlanes(const Mat4& viewProjection, float (&planes)[6][4]) noexcept;

// Everything the params block needs besides the settings. Matrices are eye-relative.
struct GrassFrameInputs {
    Mat4 viewProjection = Mat4::Identity();     // Unjittered: culling and Hi-Z.
    Mat4 drawViewProjection = Mat4::Identity(); // Jittered: drawing.
    double eyeX = 0.0;
    double eyeY = 0.0;
    double eyeZ = 0.0;
    const GrassHeightTile* tile = nullptr;      // Null: no data, every chunk is dropped.
    float timeSeconds = 0.0f;
    float windDirX = 0.7071068f;
    float windDirZ = 0.7071068f;
    float windSpeed = 4.0f;
    double windOffsetX = 0.0;
    double windOffsetZ = 0.0;
    float sunDirection[3] = { 0.0f, 1.0f, 0.0f };
    float sunRadiance[3] = { 3.0f, 2.85f, 2.55f };
    float skyAmbient[3] = { 0.44f, 0.46f, 0.49f };
    float groundAmbient[3] = { 0.23f, 0.20f, 0.16f };
    bool hizUsable = false;
    float hizAtlas[2] = {};
    float hizBase[2] = {};
    float hizDepthK = 0.0f;
    float hizDepthB = 0.0f;
    float hizLevels = 0.0f;
};

// Fills the params block; `settings` must be sanitized. Instance regions follow each other: LOD0, LOD1, LOD2.
GrassParamsGpu MakeGrassParams(const GrassSettings& settings, const GrassFrameInputs& in) noexcept;

// Bytes of the instance storage for the three regions.
std::uint64_t GrassInstanceBytes(const GrassSettings& settings) noexcept;

// A made-up landscape for tests without terrain.
enum class GrassSyntheticKind : std::uint8_t { Flat, Hills, Cliff };

struct GrassSyntheticSpec {
    GrassSyntheticKind kind = GrassSyntheticKind::Hills;
    float baseHeight = 12.0f;    // World Y of the flat ground.
    float amplitude = 5.0f;      // Hill height (Hills, Cliff).
    float wavelength = 90.0f;    // Hill size in metres.
    float seaLevel = 0.0f;
    bool lake = true;
    float lakeX = 40.0f;         // World centre of the lake.
    float lakeZ = 30.0f;
    float lakeRadius = 35.0f;
    float lakeDepth = 4.0f;      // Below the water line at the centre.
    float lakeLevel = 6.0f;      // World Y of the lake water.
    float cliffX = 90.0f;        // World x where the wall starts (Cliff).
};

// Ground height of the made-up landscape at a world point.
float GrassSyntheticHeight(const GrassSyntheticSpec& spec, double x, double z) noexcept;

// A `size` x `size` tile of `texelSize` metre texels centred on (centerX, centerZ), aligned to the texel grid.
GrassHeightTile MakeSyntheticGrassTile(const GrassSyntheticSpec& spec, double centerX, double centerZ, std::uint32_t size,
    float texelSize, std::uint64_t revision);

// Bilinear height and submerged flag of a tile at a world point, as the cull shader reads it. False outside the tile.
bool SampleGrassTile(const GrassHeightTile& tile, double x, double z, float& height, float& submerged) noexcept;

} // namespace gte
