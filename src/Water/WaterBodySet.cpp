#include "WaterBodySet.h"

#include "WaterHash.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace gte::water {

namespace {

bool Finite(double v) noexcept { return std::isfinite(v) && std::abs(v) <= kMaxWorldCoord; }

float ClampF(float v, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

void SanitizeColor(float (&c)[3], const float (&fallback)[3]) noexcept
{
    for (int i = 0; i < 3; ++i) {
        c[i] = ClampF(c[i], 0.0f, 1.0f, fallback[i]);
    }
}

void AddCarveFields(Fnv& f, const WaterBody& b)
{
    f.Add(static_cast<std::uint8_t>(b.kind));
    f.Add(b.level); f.Add(b.depth); f.Add(b.shoreWidth); f.Add(b.bankWidth); f.Add(b.rimHeight); f.Add(b.sealRim);
    for (const Point2& p : b.polygon) {
        f.Add(p.x); f.Add(p.z);
    }
    for (const RiverPoint& p : b.river) {
        f.Add(p.x); f.Add(p.z); f.Add(p.surfaceY); f.Add(p.width);
    }
}

std::uint64_t BodyHash(const WaterBody& b)
{
    Fnv f;
    f.Add(b.id); f.Add(b.name); f.Add(b.carveTerrain);
    AddCarveFields(f, b);
    f.Add(b.look.overrideColors); f.Add(b.look.waveScale);
    for (int i = 0; i < 3; ++i) {
        f.Add(b.look.shallowColor[i]); f.Add(b.look.deepColor[i]); f.Add(b.look.foamColor[i]);
    }
    return f.h;
}

// Returns an empty string when the body is fine (after repair), else the reason it must be dropped.
std::string SanitizeBody(WaterBody& b)
{
    const WaterBody d{};
    b.level = ClampF(b.level, -2000.0f, 5000.0f, 0.0f);
    b.depth = ClampF(b.depth, 0.5f, 200.0f, d.depth);
    b.shoreWidth = ClampF(b.shoreWidth, 0.5f, 500.0f, d.shoreWidth);
    b.bankWidth = ClampF(b.bankWidth, 0.5f, 1000.0f, d.bankWidth);
    b.rimHeight = ClampF(b.rimHeight, 0.0f, 20.0f, d.rimHeight);
    b.look.waveScale = ClampF(b.look.waveScale, 0.0f, 4.0f, 1.0f);
    SanitizeColor(b.look.shallowColor, d.look.shallowColor);
    SanitizeColor(b.look.deepColor, d.look.deepColor);
    SanitizeColor(b.look.foamColor, d.look.foamColor);

    switch (b.kind) {
    case WaterBodyKind::Ocean:
        b.polygon.clear();
        b.river.clear();
        return {};
    case WaterBodyKind::Lake:
        b.river.clear();
        if (static_cast<int>(b.polygon.size()) > kMaxLakeVertices) {
            b.polygon.resize(kMaxLakeVertices);
        }
        if (static_cast<int>(b.polygon.size()) < kMinLakeVertices) {
            return "lake needs at least 3 corners";
        }
        for (const Point2& p : b.polygon) {
            if (!Finite(p.x) || !Finite(p.z)) {
                return "lake corner is not finite";
            }
        }
        if (std::abs(PolygonArea(b.polygon)) < 1.0) {
            return "lake area is below 1 square metre";
        }
        if (PolygonSelfIntersects(b.polygon)) {
            return "lake outline crosses itself";
        }
        return {};
    default:
        b.polygon.clear();
        if (static_cast<int>(b.river.size()) > kMaxRiverPoints) {
            b.river.resize(kMaxRiverPoints);
        }
        if (static_cast<int>(b.river.size()) < kMinRiverPoints) {
            return "river needs at least 2 points";
        }
        for (RiverPoint& p : b.river) {
            if (!Finite(p.x) || !Finite(p.z)) {
                return "river point is not finite";
            }
            p.surfaceY = ClampF(p.surfaceY, -2000.0f, 5000.0f, 0.0f);
            p.width = ClampF(p.width, 1.0f, 500.0f, 12.0f);
        }
        EnforceDescent(b.river);
        return {};
    }
}

} // namespace

std::shared_ptr<const WaterBodySet> WaterBodySet::Build(std::vector<WaterBody> bodies, const WaterSettings& settings)
{
    auto set = std::make_shared<WaterBodySet>();
    set->m_settings = Sanitize(settings);

    std::unordered_set<std::uint32_t> used;
    int oceans = 0;
    std::vector<WaterBody> kept;
    for (WaterBody& b : bodies) {
        if (static_cast<int>(kept.size()) >= kMaxBodies) {
            set->m_issues.push_back("more than 16 bodies: extra bodies ignored");
            break;
        }
        if (b.kind == WaterBodyKind::Ocean && oceans >= kMaxOceans) {
            set->m_issues.push_back("only one ocean is allowed: extra ocean ignored");
            continue;
        }
        const std::string why = SanitizeBody(b);
        if (!why.empty()) {
            set->m_issues.push_back((b.name.empty() ? std::string("body") : b.name) + ": " + why);
            continue;
        }
        if (b.kind == WaterBodyKind::Ocean) {
            ++oceans;
            b.level = set->m_settings.seaLevel;
            b.carveTerrain = false;
        }
        kept.push_back(std::move(b));
    }
    for (const WaterBody& b : kept) {
        if (b.id != 0) {
            used.insert(b.id);
        }
    }
    std::uint32_t next = 1;
    std::unordered_set<std::uint32_t> seen;
    for (WaterBody& b : kept) {
        if (b.id == 0 || !seen.insert(b.id).second) {
            while (used.contains(next)) {
                ++next;
            }
            b.id = next;
            used.insert(next);
            seen.insert(next);
        }
    }

    set->m_bodies = std::move(kept);
    Fnv all;
    Fnv carve;
    all.Bytes(water::Hash(set->m_settings));
    all.Bytes(set->m_bodies.size());
    set->m_prepared.resize(set->m_bodies.size());
    for (std::size_t i = 0; i < set->m_bodies.size(); ++i) {
        const WaterBody& b = set->m_bodies[i];
        Prepared& p = set->m_prepared[i];
        if (b.kind == WaterBodyKind::Lake) {
            p.bounds = PolygonBounds(b.polygon);
        } else if (b.kind == WaterBodyKind::River) {
            p.segments = FlattenRiver(b.river, kRiverFlattenStep);
            p.bounds = RiverBounds(p.segments);
        }
        const bool carves = b.kind != WaterBodyKind::Ocean && b.carveTerrain && set->m_settings.carveTerrain;
        if (carves) {
            Fnv f;
            AddCarveFields(f, b);
            p.carveHash = f.h == 0 ? 1 : f.h;
            carve.Bytes(b.id);
            carve.Bytes(p.carveHash);
        }
        all.Bytes(BodyHash(b));
    }
    set->m_hash = all.h;
    set->m_carveHash = std::any_of(set->m_prepared.begin(), set->m_prepared.end(), [](const Prepared& p) { return p.carveHash != 0; })
        ? carve.h : 0;
    return set;
}

int WaterBodySet::IndexOf(std::uint32_t id) const noexcept
{
    for (std::size_t i = 0; i < m_bodies.size(); ++i) {
        if (m_bodies[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const WaterBody* WaterBodySet::Find(std::uint32_t id) const noexcept
{
    const int i = IndexOf(id);
    return i < 0 ? nullptr : &m_bodies[static_cast<std::size_t>(i)];
}

std::span<const RiverSegment> WaterBodySet::Segments(std::size_t i) const noexcept
{
    return i < m_prepared.size() ? std::span<const RiverSegment>(m_prepared[i].segments) : std::span<const RiverSegment>();
}

bool WaterBodySet::BodyCarves(std::size_t i) const noexcept
{
    return i < m_prepared.size() && m_prepared[i].carveHash != 0;
}

std::uint64_t WaterBodySet::BodyCarveHash(std::size_t i) const noexcept
{
    return i < m_prepared.size() ? m_prepared[i].carveHash : 0;
}

bool WaterBodySet::CarveFootprint(std::size_t i, Rect2& out) const noexcept
{
    if (!BodyCarves(i)) {
        return false;
    }
    const WaterBody& b = m_bodies[i];
    const double pad = static_cast<double>(b.bankWidth) + static_cast<double>(b.shoreWidth) + kCarveCoarseMargin;
    out = m_prepared[i].bounds.Expanded(pad);
    return true;
}

WaterSample WaterBodySet::SampleAt(double x, double z) const noexcept
{
    WaterSample best;
    for (std::size_t i = 0; i < m_bodies.size(); ++i) {
        const WaterBody& b = m_bodies[i];
        WaterSample s;
        if (b.kind == WaterBodyKind::Lake) {
            if (!m_prepared[i].bounds.Expanded(1.0).Overlaps({ x, z, x, z })) {
                continue;
            }
            const double d = PolygonSignedDistance(b.polygon, x, z);
            if (d >= 0.0) {
                continue;
            }
            s = { true, b.id, b.kind, b.level, d, 0.0f };
        } else if (b.kind == WaterBodyKind::River) {
            const RiverHit hit = NearestOnRiver(m_prepared[i].segments, x, z);
            const double edge = hit.distance - 0.5 * static_cast<double>(hit.width);
            if (hit.segment < 0 || edge >= 0.0) {
                continue;
            }
            s = { true, b.id, b.kind, hit.surfaceY, edge, hit.along };
        } else {
            continue;
        }
        if (!best.hasWater || s.surfaceY > best.surfaceY) {
            best = s;
        }
    }
    if (!best.hasWater) {
        for (const WaterBody& b : m_bodies) {
            if (b.kind == WaterBodyKind::Ocean) {
                best = { true, b.id, b.kind, b.level, 0.0, 0.0f };
            }
        }
    }
    return best;
}

void WaterBodySet::DirtyRects(const WaterBodySet* previous, const WaterBodySet& next, std::vector<Rect2>& out)
{
    auto add = [&](const WaterBodySet& set, std::size_t i, std::uint64_t otherHash) {
        Rect2 r;
        if (set.BodyCarveHash(i) != otherHash && set.CarveFootprint(i, r)) {
            out.push_back(r);
        }
    };
    if (previous != nullptr) {
        for (std::size_t i = 0; i < previous->m_bodies.size(); ++i) {
            const int j = next.IndexOf(previous->m_bodies[i].id);
            add(*previous, i, j < 0 ? 0 : next.BodyCarveHash(static_cast<std::size_t>(j)));
        }
    }
    for (std::size_t j = 0; j < next.m_bodies.size(); ++j) {
        const int i = previous != nullptr ? previous->IndexOf(next.m_bodies[j].id) : -1;
        add(next, j, i < 0 ? 0 : previous->BodyCarveHash(static_cast<std::size_t>(i)));
    }
}

} // namespace gte::water
