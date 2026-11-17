#pragma once

// Carves lake basins and river channels into the terrain heights. Implements the terrain carve hook.

#include "../../Terrain/TerrainCarve.h"
#include "WaterBodySet.h"

#include <memory>
#include <vector>

namespace gte::water {

class WaterCarve final : public terrain::ITerrainCarve {
public:
    // Null when no body carves the terrain.
    static std::shared_ptr<const WaterCarve> Build(const WaterBodySet& set);

    std::uint64_t Hash() const noexcept override { return m_hash; }
    bool Affects(const Rect2& area, double spacing) const noexcept override;
    float Apply(double x, double z, float h, double spacing) const noexcept override;
    void ApplyTile(float* tile, int side, double originX, double originZ, double spacing) const override;

private:
    struct Lake {
        std::vector<Point2> poly;
        Rect2 bounds;
        float level = 0.0f;
        float depth = 0.0f;
        float shore = 0.0f;
        float bank = 0.0f;
        float rim = 0.0f;
        bool seal = false;
    };
    struct River {
        std::vector<RiverSegment> segments;
        std::vector<Rect2> segmentBounds; // Per segment, widened by half its width.
        Rect2 bounds;
        float depth = 0.0f;
        float bank = 0.0f;
        float maxWidth = 0.0f;
    };
    // Primitives and river pieces that can touch one area at one spacing.
    struct Candidates;

    float Eval(double x, double z, float h, double spacing, const Candidates& c) const noexcept;
    void Collect(const Rect2& area, double spacing, Candidates& out) const;

    std::vector<Lake> m_lakes;
    std::vector<River> m_rivers;
    std::uint64_t m_hash = 0;
};

} // namespace gte::water
