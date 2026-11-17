#include "WaterCarve.h"

#include <algorithm>
#include <cmath>

namespace gte::water {

namespace {

constexpr double kBedBelowSurface = 0.3; // The shore line sits this far under the water surface.
constexpr double kMinSpacingsPerBand = 2.0;
constexpr double kMinSpacingsPerHalfWidth = 1.5;

}

struct WaterCarve::Candidates {
    struct Piece {
        const River* river = nullptr;
        std::vector<RiverSegment> segments;
    };
    std::vector<const Lake*> lakes;
    std::vector<Piece> rivers;
    std::size_t riverCount = 0;
};

std::shared_ptr<const WaterCarve> WaterCarve::Build(const WaterBodySet& set)
{
    auto carve = std::make_shared<WaterCarve>();
    for (std::size_t i = 0; i < set.Bodies().size(); ++i) {
        if (!set.BodyCarves(i)) {
            continue;
        }
        const WaterBody& b = set.Bodies()[i];
        if (b.kind == WaterBodyKind::Lake) {
            Lake l;
            l.poly = b.polygon;
            l.bounds = PolygonBounds(l.poly);
            l.level = b.level;
            l.depth = b.depth;
            l.shore = b.shoreWidth;
            l.bank = b.bankWidth;
            l.rim = b.rimHeight;
            l.seal = b.sealRim;
            carve->m_lakes.push_back(std::move(l));
        } else if (b.kind == WaterBodyKind::River) {
            River r;
            const auto segs = set.Segments(i);
            r.segments.assign(segs.begin(), segs.end());
            r.segmentBounds.reserve(r.segments.size());
            for (const RiverSegment& s : r.segments) {
                const double half = 0.5 * std::max(s.wa, s.wb);
                r.segmentBounds.push_back({ std::min(s.ax, s.bx) - half, std::min(s.az, s.bz) - half,
                    std::max(s.ax, s.bx) + half, std::max(s.az, s.bz) + half });
                r.maxWidth = std::max({ r.maxWidth, s.wa, s.wb });
            }
            r.bounds = RiverBounds(r.segments);
            r.depth = b.depth;
            r.bank = b.bankWidth;
            carve->m_rivers.push_back(std::move(r));
        }
    }
    if (carve->m_lakes.empty() && carve->m_rivers.empty()) {
        return nullptr;
    }
    carve->m_hash = set.CarveHash();
    return carve;
}

bool WaterCarve::Affects(const Rect2& area, double spacing) const noexcept
{
    for (const Lake& l : m_lakes) {
        const double reach = std::max(static_cast<double>(l.bank), kMinSpacingsPerBand * spacing);
        if (l.bounds.Expanded(reach).Overlaps(area)) {
            return true;
        }
    }
    for (const River& r : m_rivers) {
        const double half = std::max(0.5 * static_cast<double>(r.maxWidth), kMinSpacingsPerHalfWidth * spacing);
        const double reach = half + std::max(static_cast<double>(r.bank), kMinSpacingsPerBand * spacing);
        if (r.bounds.Expanded(reach).Overlaps(area)) {
            return true;
        }
    }
    return false;
}

void WaterCarve::Collect(const Rect2& area, double spacing, Candidates& out) const
{
    out.lakes.clear();
    out.riverCount = 0;
    for (const Lake& l : m_lakes) {
        const double reach = std::max(static_cast<double>(l.bank), kMinSpacingsPerBand * spacing);
        if (l.bounds.Expanded(reach).Overlaps(area)) {
            out.lakes.push_back(&l);
        }
    }
    for (const River& r : m_rivers) {
        const double half = std::max(0.5 * static_cast<double>(r.maxWidth), kMinSpacingsPerHalfWidth * spacing);
        const double reach = half + std::max(static_cast<double>(r.bank), kMinSpacingsPerBand * spacing);
        if (!r.bounds.Expanded(reach).Overlaps(area)) {
            continue;
        }
        if (out.rivers.size() <= out.riverCount) {
            out.rivers.emplace_back();
        }
        Candidates::Piece& piece = out.rivers[out.riverCount++];
        piece.river = &r;
        piece.segments.clear();
        const Rect2 wide = area.Expanded(reach);
        for (std::size_t i = 0; i < r.segments.size(); ++i) {
            if (r.segmentBounds[i].Overlaps(wide)) {
                piece.segments.push_back(r.segments[i]);
            }
        }
    }
}

float WaterCarve::Eval(double x, double z, float hIn, double spacing, const Candidates& c) const noexcept
{
    const double h = hIn;
    double low = h;
    double dam = -1.0e300;
    bool touched = false;

    for (const Lake* lake : c.lakes) {
        const double bank = std::max(static_cast<double>(lake->bank), kMinSpacingsPerBand * spacing);
        const Rect2 reachBox = lake->bounds.Expanded(bank);
        if (x < reachBox.minX || x > reachBox.maxX || z < reachBox.minZ || z > reachBox.maxZ) {
            continue;
        }
        const double shore = std::max(static_cast<double>(lake->shore), kMinSpacingsPerBand * spacing);
        const double d = PolygonSignedDistance(lake->poly, x, z);
        const double level = lake->level;
        if (d < 0.0) {
            const double bed = level - kBedBelowSurface - (static_cast<double>(lake->depth) - kBedBelowSurface) * SmoothStep(0.0, shore, -d);
            low = std::min(low, std::min(h, bed));
            touched = true;
        } else if (d < bank) {
            const double w = 1.0 - SmoothStep(0.0, bank, d);
            const double target = std::min(h, level - kBedBelowSurface);
            low = std::min(low, h + (target - h) * w);
            touched = true;
        }
        if (lake->seal && d >= 0.0 && d < bank) {
            const double t = d / bank;
            dam = std::max(dam, level - kBedBelowSurface + (static_cast<double>(lake->rim) + kBedBelowSurface) * 4.0 * t * (1.0 - t));
            touched = true;
        }
    }

    for (std::size_t i = 0; i < c.riverCount; ++i) {
        const Candidates::Piece& piece = c.rivers[i];
        const RiverHit hit = NearestOnRiver(piece.segments, x, z);
        if (hit.segment < 0) {
            continue;
        }
        const double width = hit.width;
        const double scale = SmoothStep(1.0, 3.0, width / spacing);
        if (scale <= 0.0) {
            continue;
        }
        const double half = std::max(0.5 * width, kMinSpacingsPerHalfWidth * spacing);
        const double bank = std::max(static_cast<double>(piece.river->bank), kMinSpacingsPerBand * spacing);
        const double surface = hit.surfaceY;
        const double s = hit.distance;
        if (s < half) {
            const double u = s / half;
            const double bed = surface - kBedBelowSurface - (static_cast<double>(piece.river->depth) - kBedBelowSurface) * (1.0 - u * u);
            const double target = std::min(h, bed);
            low = std::min(low, h + (target - h) * scale);
            touched = true;
        } else if (s < half + bank) {
            const double w = (1.0 - SmoothStep(half, half + bank, s)) * scale;
            const double target = std::min(h, surface - kBedBelowSurface);
            low = std::min(low, h + (target - h) * w);
            touched = true;
        }
    }

    if (!touched) {
        return hIn;
    }
    return static_cast<float>(std::max(low, dam));
}

float WaterCarve::Apply(double x, double z, float h, double spacing) const noexcept
{
    Candidates c;
    Collect({ x, z, x, z }, spacing, c);
    return Eval(x, z, h, spacing, c);
}

void WaterCarve::ApplyTile(float* tile, int side, double originX, double originZ, double spacing) const
{
    const double extent = static_cast<double>(side - 1) * spacing;
    thread_local Candidates c;
    Collect({ originX, originZ, originX + extent, originZ + extent }, spacing, c);
    if (c.lakes.empty() && c.riverCount == 0) {
        return;
    }
    for (int j = 0; j < side; ++j) {
        const double z = originZ + static_cast<double>(j) * spacing;
        float* row = tile + static_cast<std::size_t>(j) * static_cast<std::size_t>(side);
        for (int i = 0; i < side; ++i) {
            row[i] = Eval(originX + static_cast<double>(i) * spacing, z, row[i], spacing, c);
        }
    }
}

} // namespace gte::water
