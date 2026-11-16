#include "TerrainNode.h"

#include "TerrainErosion.h"
#include "TerrainMaterial.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace gte::terrain {

namespace {

constexpr int kErosionMargin = 10;       // Extra samples around the node that droplets may roam on.
constexpr float kSkirtDropSpacings = 1.5f; // Skirt depth in vertex spacings.

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point from) noexcept
{
    return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
}

struct Scratch {
    std::vector<float> tile;
    std::vector<float> original;
};

Scratch& ThreadScratch()
{
    thread_local Scratch scratch;
    return scratch;
}

inline float SmoothStepRange(float e0, float e1, float v) noexcept
{
    const float t = std::clamp((v - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

NodeGeometry NodeGeometryOf(const TerrainLayout& layout, const NodeKey& key) noexcept
{
    NodeGeometry g;
    g.size = NodeSize(layout, key.level);
    g.spacing = NodeSpacing(layout, key.level);
    g.originX = static_cast<double>(key.x) * g.size;
    g.originZ = static_cast<double>(key.z) * g.size;
    return g;
}

bool NodeUsesErosion(const TerrainSettings& s, int level) noexcept
{
    return s.erosionEnabled && s.erosionDropletCount > 0 && level <= s.erosionMaxLevel;
}

void FinishNodeMesh(NodeMesh& mesh, int quads)
{
    const int verts = quads + 1;
    const float sp = mesh.spacing;
    const float drop = kSkirtDropSpacings * sp;
    float minY = 1.0e30f;
    float maxY = -1.0e30f;
    const std::size_t gridCount = static_cast<std::size_t>(verts) * static_cast<std::size_t>(verts);
    for (std::size_t i = 0; i < gridCount; ++i) {
        const float h = mesh.vertices[i].position[1];
        minY = std::min(minY, h);
        maxY = std::max(maxY, h);
    }

    // Skirt ring: copies of the border vertices pushed down; same normal and colour so lighting has no seam.
    for (int edge = 0; edge < 4; ++edge) {
        for (int k = 0; k < verts; ++k) {
            int ix = 0;
            int iz = 0;
            switch (edge) {
            case 0: ix = k; iz = 0; break;
            case 1: ix = quads; iz = k; break;
            case 2: ix = k; iz = quads; break;
            default: ix = 0; iz = k; break;
            }
            const std::size_t src = static_cast<std::size_t>(iz) * static_cast<std::size_t>(verts) + static_cast<std::size_t>(ix);
            const std::size_t dst = gridCount + static_cast<std::size_t>(edge) * static_cast<std::size_t>(verts) + static_cast<std::size_t>(k);
            mesh.vertices[dst] = mesh.vertices[src];
            mesh.vertices[dst].position[1] -= drop;
            mesh.colors[dst] = mesh.colors[src];
            minY = std::min(minY, mesh.vertices[dst].position[1]);
        }
    }

    mesh.boundsMin = Vec3{ 0.0f, minY, 0.0f };
    mesh.boundsMax = Vec3{ static_cast<float>(quads) * sp, maxY, static_cast<float>(quads) * sp };
}

std::vector<std::uint32_t> BuildSharedIndices(int quads)
{
    const std::uint32_t q = static_cast<std::uint32_t>(quads);
    const std::uint32_t stride = q + 1;
    std::vector<std::uint32_t> idx;
    idx.reserve(NodeIndexCount(quads));
    for (std::uint32_t z = 0; z < q; ++z) {
        for (std::uint32_t x = 0; x < q; ++x) {
            const std::uint32_t v00 = z * stride + x;
            const std::uint32_t v10 = v00 + 1;
            const std::uint32_t v01 = v00 + stride;
            const std::uint32_t v11 = v01 + 1;
            const std::uint32_t quad[6] = { v00, v10, v11, v11, v01, v00 };
            idx.insert(idx.end(), std::begin(quad), std::end(quad));
        }
    }
    // Skirt strips: south, east, north, west. South and east are wound the other way so they face outward.
    const std::uint32_t skirtBase = stride * stride;
    for (std::uint32_t edge = 0; edge < 4; ++edge) {
        const bool reversed = edge < 2;
        for (std::uint32_t k = 0; k < q; ++k) {
            std::uint32_t t0 = 0;
            std::uint32_t t1 = 0;
            switch (edge) {
            case 0: t0 = k; t1 = k + 1; break;                                   // z = 0
            case 1: t0 = k * stride + q; t1 = (k + 1) * stride + q; break;       // x = q
            case 2: t0 = q * stride + k; t1 = q * stride + k + 1; break;         // z = q
            default: t0 = k * stride; t1 = (k + 1) * stride; break;              // x = 0
            }
            const std::uint32_t s0 = skirtBase + edge * stride + k;
            const std::uint32_t s1 = s0 + 1;
            if (reversed) {
                const std::uint32_t r[6] = { t0, s1, t1, t0, s0, s1 };
                idx.insert(idx.end(), std::begin(r), std::end(r));
            } else {
                const std::uint32_t f[6] = { t0, t1, s1, t0, s1, s0 };
                idx.insert(idx.end(), std::begin(f), std::end(f));
            }
        }
    }
    return idx;
}

NodeMesh BuildNode(const TerrainHeightField& field, const NodeKey& key, NodeBuildTiming* timing)
{
    const TerrainSettings& s = field.Settings();
    const NodeGeometry g = NodeGeometryOf(field.Layout(), key);
    GridBuildSpec spec;
    spec.key = key;
    spec.originX = g.originX;
    spec.originZ = g.originZ;
    spec.spacing = g.spacing;
    spec.quads = field.Layout().quads;
    spec.erode = NodeUsesErosion(s, key.level);
    spec.erosionSeed = ErosionSeed(s.seed, key.x, key.z, key.level);
    return BuildGridMesh(field, spec, timing);
}

NodeMesh BuildGridMesh(const TerrainHeightField& field, const GridBuildSpec& spec, NodeBuildTiming* timing)
{
    const TerrainSettings& s = field.Settings();
    NodeGeometry g;
    g.originX = spec.originX;
    g.originZ = spec.originZ;
    g.spacing = spec.spacing;
    g.size = spec.spacing * static_cast<double>(spec.quads);
    const int q = spec.quads;
    const int verts = q + 1;
    const bool erode = spec.erode && s.erosionEnabled && s.erosionDropletCount > 0;
    const int margin = erode ? kErosionMargin : 1;
    const int side = verts + 2 * margin;
    const std::size_t tileCount = static_cast<std::size_t>(side) * static_cast<std::size_t>(side);

    Scratch& scratch = ThreadScratch();
    scratch.tile.resize(tileCount);
    float* tile = scratch.tile.data();

    auto t0 = Clock::now();
    field.FillGrid(g.originX - margin * g.spacing, g.originZ - margin * g.spacing, g.spacing, side, side, tile);
    if (timing != nullptr) {
        timing->heightMs = MsSince(t0);
        t0 = Clock::now();
    }

    if (erode) {
        scratch.original.assign(tile, tile + tileCount);
        const float unitScale = static_cast<float>(g.spacing) * kErosionSlopeUnits;
        const float inv = 1.0f / unitScale;
        for (std::size_t i = 0; i < tileCount; ++i) {
            tile[i] *= inv;
        }
        const double areaScale = static_cast<double>(tileCount) / (96.0 * 96.0);
        const int droplets = static_cast<int>(std::lround(static_cast<double>(s.erosionDropletCount) * areaScale));
        ErodeTile(tile, side, MakeErosionParams(s), droplets, spec.erosionSeed, 0.0f);

        // Fade the change to zero at the node border so neighbours agree on shared heights and normals.
        const float falloff = static_cast<float>(s.erosionEdgeFalloff);
        const float strength = s.erosionStrength;
        const float lo = MinHeight(s);
        const float hi = MaxHeight(s);
        for (int tz = 0; tz < side; ++tz) {
            for (int tx = 0; tx < side; ++tx) {
                const int ix = tx - margin;
                const int iz = tz - margin;
                const int inside = std::min(std::min(ix, iz), std::min(q - ix, q - iz));
                const std::size_t i = static_cast<std::size_t>(tz) * static_cast<std::size_t>(side) + static_cast<std::size_t>(tx);
                if (inside <= 1) {
                    tile[i] = scratch.original[i];
                    continue;
                }
                const float w = SmoothStepRange(1.0f, falloff, static_cast<float>(inside));
                const float delta = (tile[i] - scratch.original[i] * inv) * unitScale * strength;
                tile[i] = std::clamp(scratch.original[i] + delta * w, lo, hi);
            }
        }
        if (timing != nullptr) {
            timing->erosionMs = MsSince(t0);
            t0 = Clock::now();
        }
    }

    NodeMesh mesh;
    mesh.key = spec.key;
    mesh.originX = g.originX;
    mesh.originZ = g.originZ;
    mesh.spacing = static_cast<float>(g.spacing);
    mesh.size = static_cast<float>(g.size);
    mesh.vertices.resize(NodeVertexCount(q));
    mesh.colors.resize(mesh.vertices.size());

    const float sp = mesh.spacing;
    for (int iz = 0; iz < verts; ++iz) {
        for (int ix = 0; ix < verts; ++ix) {
            const float* c = tile + static_cast<std::size_t>(iz + margin) * static_cast<std::size_t>(side) + static_cast<std::size_t>(ix + margin);
            const float h = c[0];
            const float nx = c[-1] - c[1];
            const float nz = c[-side] - c[side];
            const float ny = 2.0f * sp;
            const float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            const float ux = nx * invLen;
            const float uy = ny * invLen;
            const float uz = nz * invLen;
            const float slope = std::acos(std::min(1.0f, uy));

            const std::size_t vi = static_cast<std::size_t>(iz) * static_cast<std::size_t>(verts) + static_cast<std::size_t>(ix);
            TerrainVertex& v = mesh.vertices[vi];
            v.position[0] = static_cast<float>(ix) * sp;
            v.position[1] = h;
            v.position[2] = static_cast<float>(iz) * sp;
            v.normal[0] = ux;
            v.normal[1] = uy;
            v.normal[2] = uz;
            v.uv[0] = Height01(h, s);
            v.uv[1] = Slope01(slope);
            mesh.colors[vi] = PackRgba8(SurfaceColor(h, slope, s));
        }
    }

    FinishNodeMesh(mesh, q);
    if (timing != nullptr) {
        timing->meshMs = MsSince(t0);
    }
    return mesh;
}

void RecolorNode(NodeMesh& mesh, const TerrainSettings& settings)
{
    mesh.colors.resize(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const TerrainVertex& v = mesh.vertices[i];
        mesh.colors[i] = PackRgba8(SurfaceColor(HeightFromHeight01(v.uv[0], settings), SlopeFromSlope01(v.uv[1]), settings));
    }
}

} // namespace gte::terrain
