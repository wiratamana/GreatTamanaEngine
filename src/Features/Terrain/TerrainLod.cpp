#include "TerrainLod.h"

#include <algorithm>
#include <cmath>

namespace gte::terrain {

namespace {

constexpr double kSplitHysteresis = 0.12;      // A split node stays split until this much farther away.
constexpr double kPrefetchMargin = 0.25;       // Of node size: nodes this far outside the view still load.
constexpr float kHeightRangePad = 0.1f;        // Of the measured range, for finer nodes below a coarse range.

// Distance from `p` to the interval [lo, hi].
inline double AxisDistance(double p, double lo, double hi) noexcept
{
    return p < lo ? lo - p : (p > hi ? p - hi : 0.0);
}

} // namespace

FrustumPlanes ExtractFrustumPlanes(const float* m) noexcept
{
    FrustumPlanes out;
    // Row i of the matrix: m[i], m[4 + i], m[8 + i], m[12 + i] (column-major).
    const auto row = [m](int i) { return std::array<float, 4>{ m[i], m[4 + i], m[8 + i], m[12 + i] }; };
    const auto r0 = row(0);
    const auto r1 = row(1);
    const auto r2 = row(2);
    const auto r3 = row(3);
    const std::array<std::array<float, 4>, 6> raw = { {
        { r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3] },
        { r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3] },
        { r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3] },
        { r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3] },
        { r2[0], r2[1], r2[2], r2[3] },
        { r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3] },
    } };
    out.valid = true;
    for (std::size_t i = 0; i < 6; ++i) {
        const float len = std::sqrt(raw[i][0] * raw[i][0] + raw[i][1] * raw[i][1] + raw[i][2] * raw[i][2]);
        if (!(len > 1.0e-12f) || !std::isfinite(len)) {
            out.valid = false;
            return out;
        }
        out.planes[i] = { raw[i][0] / len, raw[i][1] / len, raw[i][2] / len, raw[i][3] / len };
    }
    return out;
}

bool IsBoxOutside(const FrustumPlanes& f, const float* bmin, const float* bmax) noexcept
{
    for (const FrustumPlanes::Plane& p : f.planes) {
        const float x = p.nx >= 0.0f ? bmax[0] : bmin[0];
        const float y = p.ny >= 0.0f ? bmax[1] : bmin[1];
        const float z = p.nz >= 0.0f ? bmax[2] : bmin[2];
        if (p.nx * x + p.ny * y + p.nz * z + p.d < 0.0f) {
            return true;
        }
    }
    return false;
}

LodSelector::LodSelector(const TerrainSettings& settings)
    : m_settings(settings)
    , m_layout(MakeLayout(settings))
{
}

void LodSelector::SetSettings(const TerrainSettings& settings)
{
    m_settings = settings;
    m_layout = MakeLayout(settings);
    m_split.clear();
    m_splitNext.clear();
    m_heightRanges.clear();
}

void LodSelector::SetNodeHeightRange(const NodeKey& key, float minY, float maxY)
{
    m_heightRanges[key] = { minY, maxY };
}

void LodSelector::Select(const LodView& view, std::vector<WantedNode>& out, LodSelectionStats* stats)
{
    constexpr double kMinFactor = 1.5;      // Below this the 2:1 level balance guarantee is gone.
    constexpr double kShrink = 0.8;         // Detail drop per extra pass.
    constexpr double kRecover = 1.02;       // Per frame, while well under the cap.
    constexpr int kMaxPasses = 6;

    const double baseFactor = static_cast<double>(m_settings.lodDistanceFactor);
    const double minScale = std::min(1.0, kMinFactor / std::max(baseFactor, kMinFactor));
    double scale = std::clamp(m_distanceScale, minScale, 1.0);

    const auto overBudget = [this](const LodSelectionStats& s) {
        return (m_budget.maxInView > 0 && s.inView > m_budget.maxInView)
            || (m_budget.maxTotal > 0 && s.leaves > m_budget.maxTotal);
    };

    LodSelectionStats local;
    int passes = 0;
    while (true) {
        local = LodSelectionStats{};
        SelectPass(view, baseFactor * scale, out, local);
        ++passes;
        if (!overBudget(local) || passes >= kMaxPasses || scale <= minScale) {
            break;
        }
        scale = std::max(minScale, scale * kShrink);
    }
    local.passes = passes;
    local.distanceScale = scale;
    local.overBudget = overBudget(local);

    // Win the detail back slowly once the view is well under the cap.
    const bool roomLeft = (m_budget.maxInView <= 0 || local.inView < m_budget.maxInView * 3 / 4)
        && (m_budget.maxTotal <= 0 || local.leaves < m_budget.maxTotal * 3 / 4);
    m_distanceScale = (passes == 1 && roomLeft) ? std::min(1.0, scale * kRecover) : scale;

    m_split.swap(m_splitNext);
    if (stats != nullptr) {
        *stats = local;
    }
}

void LodSelector::SelectPass(const LodView& view, double factor, std::vector<WantedNode>& out, LodSelectionStats& local)
{
    out.clear();
    m_splitNext.clear();

    const double rootSize = NodeSize(m_layout, m_layout.rootLevel);
    const int rootSpan = static_cast<int>(std::llround(m_layout.worldHalfExtent / rootSize));
    const bool useFrustum = m_settings.frustumCulling && view.frustum.valid;
    const double maxDist = static_cast<double>(m_settings.maxViewDistance);
    const HeightRange globalRange{ MinHeight(m_settings), MaxHeight(m_settings) };

    m_stack.clear();
    for (int rz = rootSpan - 1; rz >= -rootSpan; --rz) {
        for (int rx = rootSpan - 1; rx >= -rootSpan; --rx) {
            m_stack.push_back({ NodeKey{ rx, rz, static_cast<std::uint8_t>(m_layout.rootLevel) }, globalRange, false });
        }
    }

    while (!m_stack.empty()) {
        const StackEntry entry = m_stack.back();
        const NodeKey key = entry.key;
        m_stack.pop_back();
        ++local.visited;

        const double size = NodeSize(m_layout, key.level);
        const double minX = static_cast<double>(key.x) * size;
        const double minZ = static_cast<double>(key.z) * size;

        HeightRange range = entry.range;
        bool measured = entry.measured;
        HeightRange effective = range;
        if (const auto it = m_heightRanges.find(key); it != m_heightRanges.end()) {
            range = effective = it->second;
            measured = true;
        } else if (measured) {
            const float pad = (range.maxY - range.minY) * kHeightRangePad;
            effective = { range.minY - pad, range.maxY + pad };
        }

        const double dx = AxisDistance(view.cameraX, minX, minX + size);
        const double dz = AxisDistance(view.cameraZ, minZ, minZ + size);
        const double dy = AxisDistance(view.cameraY, static_cast<double>(effective.minY), static_cast<double>(effective.maxY));
        const double dist = std::sqrt(dx * dx + dz * dz + dy * dy);
        if (dist > maxDist) {
            ++local.rejectedDistance;
            continue;
        }

        bool inView = true;
        if (useFrustum) {
            const float rel[3] = { static_cast<float>(minX - view.cameraX), effective.minY - static_cast<float>(view.cameraY),
                static_cast<float>(minZ - view.cameraZ) };
            const float relMax[3] = { rel[0] + static_cast<float>(size), effective.maxY - static_cast<float>(view.cameraY),
                rel[2] + static_cast<float>(size) };
            inView = !IsBoxOutside(view.frustum, rel, relMax);
            if (!inView) {
                const float m = static_cast<float>(size * kPrefetchMargin);
                const float eMin[3] = { rel[0] - m, rel[1] - m, rel[2] - m };
                const float eMax[3] = { relMax[0] + m, relMax[1] + m, relMax[2] + m };
                if (IsBoxOutside(view.frustum, eMin, eMax)) {
                    ++local.rejectedFrustum;
                    continue;
                }
            }
        }

        bool split = false;
        if (key.level > 0) {
            const double limit = factor * size;
            if (dist < limit * (1.0 + kSplitHysteresis)) {
                split = dist < limit || m_split.contains(key);
            }
        }
        if (split) {
            m_splitNext.insert(key);
            for (int c = 3; c >= 0; --c) {
                m_stack.push_back({ key.Child(c), range, measured });
            }
        } else {
            ++local.leaves;
            local.inView += inView ? 1 : 0;
            out.push_back({ key, dist, inView });
        }
    }
}

} // namespace gte::terrain
