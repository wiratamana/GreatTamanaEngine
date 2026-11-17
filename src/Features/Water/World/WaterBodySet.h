#pragma once

// Immutable, validated snapshot of every water body plus the global settings. Safe to share between threads.

#include "WaterMath.h"
#include "WaterSettings.h"
#include "WaterTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gte::water {

// What the water is like at one world position.
struct WaterSample {
    bool hasWater = false;
    std::uint32_t bodyId = 0;
    WaterBodyKind kind = WaterBodyKind::Ocean;
    float surfaceY = 0.0f;
    double edgeDistance = 0.0; // Lake: signed distance to the outline (negative inside). River: centre distance minus half width.
    float riverAlong = 0.0f;
};

class WaterBodySet {
public:
    // Sanitizes: caps, finite values, unique ids, river descent. Problems are listed in Issues().
    static std::shared_ptr<const WaterBodySet> Build(std::vector<WaterBody> bodies, const WaterSettings& settings);

    const std::vector<WaterBody>& Bodies() const noexcept { return m_bodies; }
    const WaterSettings& Settings() const noexcept { return m_settings; }
    const std::vector<std::string>& Issues() const noexcept { return m_issues; }

    // Hash of everything (look included); changes whenever anything visible changes.
    std::uint64_t Hash() const noexcept { return m_hash; }
    // Hash of only what shapes the terrain; 0 when nothing carves.
    std::uint64_t CarveHash() const noexcept { return m_carveHash; }

    const WaterBody* Find(std::uint32_t id) const noexcept;
    int IndexOf(std::uint32_t id) const noexcept;
    std::span<const RiverSegment> Segments(std::size_t bodyIndex) const noexcept;
    // Area a body can change in the terrain (outline plus bank plus a coarse-level margin); empty when it does not carve.
    bool CarveFootprint(std::size_t bodyIndex, Rect2& out) const noexcept;
    bool BodyCarves(std::size_t bodyIndex) const noexcept;
    std::uint64_t BodyCarveHash(std::size_t bodyIndex) const noexcept;

    // Water at (x, z): the highest lake or river covering it, else the ocean.
    WaterSample SampleAt(double x, double z) const noexcept;

    // Rectangles where the terrain of `next` differs from `previous` (null = nothing carved before).
    static void DirtyRects(const WaterBodySet* previous, const WaterBodySet& next, std::vector<Rect2>& out);

private:
    struct Prepared {
        std::vector<RiverSegment> segments;
        Rect2 bounds{};
        std::uint64_t carveHash = 0;
    };

    std::vector<WaterBody> m_bodies;
    std::vector<Prepared> m_prepared;
    WaterSettings m_settings;
    std::vector<std::string> m_issues;
    std::uint64_t m_hash = 0;
    std::uint64_t m_carveHash = 0;
};

// River polylines are flattened with this step, metres.
inline constexpr double kRiverFlattenStep = 12.0;
// Extra margin around a carve footprint so coarse LOD nodes are rebuilt too.
inline constexpr double kCarveCoarseMargin = 300.0;

} // namespace gte::water
