#pragma once

// Hook that lets another system (lakes, rivers) reshape the terrain heights. Const and thread safe.

#include <cstdint>

namespace gte::terrain {

// Axis-aligned rectangle in world X/Z, metres.
struct Rect2 {
    double minX = 0.0;
    double minZ = 0.0;
    double maxX = 0.0;
    double maxZ = 0.0;

    bool Overlaps(const Rect2& o) const noexcept { return minX <= o.maxX && o.minX <= maxX && minZ <= o.maxZ && o.minZ <= maxZ; }
    Rect2 Expanded(double m) const noexcept { return { minX - m, minZ - m, maxX + m, maxZ + m }; }
    Rect2 Union(const Rect2& o) const noexcept;
};

inline Rect2 Rect2::Union(const Rect2& o) const noexcept
{
    return { minX < o.minX ? minX : o.minX, minZ < o.minZ ? minZ : o.minZ,
        maxX > o.maxX ? maxX : o.maxX, maxZ > o.maxZ ? maxZ : o.maxZ };
}

// The result must depend only on (x, z, h, spacing), so neighbouring nodes stay seamless.
class ITerrainCarve {
public:
    virtual ~ITerrainCarve() = default;

    // Stable content hash; equal hash means equal output.
    virtual std::uint64_t Hash() const noexcept = 0;

    // False when this carve can change nothing inside `area` at that vertex spacing.
    virtual bool Affects(const Rect2& area, double spacing) const noexcept = 0;

    // Carved height at one world position.
    virtual float Apply(double x, double z, float h, double spacing) const noexcept = 0;

    // Carves `side` x `side` heights in place; sample (i, j) sits at (originX + i * spacing, originZ + j * spacing).
    virtual void ApplyTile(float* tile, int side, double originX, double originZ, double spacing) const = 0;
};

} // namespace gte::terrain
