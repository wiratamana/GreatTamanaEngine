#include "TerrainSurface.h"

#include "TerrainMaterial.h"
#include "TerrainStreamer.h"

#include <algorithm>
#include <cmath>

namespace gte::terrain {

SurfaceSnapshot CollectSurface(const TerrainStreamer& streamer, const TerrainLayout& layout, double minX, double minZ,
    double maxX, double maxZ)
{
    SurfaceSnapshot out;
    out.open = true;
    out.field = streamer.FieldShared();
    out.nodeSizeLevel0 = layout.nodeSizeLevel0;
    const StreamerStats stats = streamer.Stats();
    out.streamRevision = stats.builtTotal + stats.evictedTotal;
    out.rebuilding = stats.rebuilding;
    out.carveHash = streamer.CarveHash();
    for (int level = 0; level <= layout.rootLevel; ++level) {
        const double size = NodeSize(layout, level);
        const auto x0 = static_cast<std::int32_t>(std::floor(minX / size));
        const auto x1 = static_cast<std::int32_t>(std::floor(maxX / size));
        const auto z0 = static_cast<std::int32_t>(std::floor(minZ / size));
        const auto z1 = static_cast<std::int32_t>(std::floor(maxZ / size));
        for (std::int32_t z = z0; z <= z1; ++z) {
            for (std::int32_t x = x0; x <= x1; ++x) {
                if (std::shared_ptr<const NodeMesh> mesh = streamer.Find(NodeKey{ x, z, static_cast<std::uint8_t>(level) })) {
                    out.nodes.push_back(std::move(mesh));
                }
            }
        }
    }
    return out;
}

bool NodeHeightAt(const NodeMesh& node, double x, double z, float& out) noexcept
{
    if (node.spacing <= 0.0f || node.size <= 0.0f) {
        return false;
    }
    const double lx = x - node.originX;
    const double lz = z - node.originZ;
    const double size = static_cast<double>(node.size);
    if (lx < 0.0 || lz < 0.0 || lx > size || lz > size) {
        return false;
    }
    const int quads = static_cast<int>(std::lround(size / static_cast<double>(node.spacing)));
    const std::size_t stride = static_cast<std::size_t>(quads) + 1;
    if (quads < 1 || node.vertices.size() < stride * stride) {
        return false;
    }
    const double u = lx / static_cast<double>(node.spacing);
    const double v = lz / static_cast<double>(node.spacing);
    const int ix = std::min(static_cast<int>(u), quads - 1);
    const int iz = std::min(static_cast<int>(v), quads - 1);
    const auto fu = static_cast<float>(u - ix);
    const auto fv = static_cast<float>(v - iz);
    const std::size_t i00 = static_cast<std::size_t>(iz) * stride + static_cast<std::size_t>(ix);
    const float h00 = node.vertices[i00].position[1];
    const float h10 = node.vertices[i00 + 1].position[1];
    const float h01 = node.vertices[i00 + stride].position[1];
    const float h11 = node.vertices[i00 + stride + 1].position[1];
    // The drawn quad is split along the (0,0)-(1,1) diagonal, see BuildSharedIndices.
    out = fu >= fv ? h00 + fu * (h10 - h00) + fv * (h11 - h10) : h00 + fv * (h01 - h00) + fu * (h11 - h01);
    return true;
}

bool NodeColorAt(const NodeMesh& node, double x, double z, std::uint32_t& rgba) noexcept
{
    if (node.spacing <= 0.0f || node.size <= 0.0f) {
        return false;
    }
    const double lx = x - node.originX;
    const double lz = z - node.originZ;
    const double size = static_cast<double>(node.size);
    if (lx < 0.0 || lz < 0.0 || lx > size || lz > size) {
        return false;
    }
    const int quads = static_cast<int>(std::lround(size / static_cast<double>(node.spacing)));
    const std::size_t stride = static_cast<std::size_t>(quads) + 1;
    if (quads < 1 || node.colors.size() < stride * stride) {
        return false;
    }
    const double u = lx / static_cast<double>(node.spacing);
    const double v = lz / static_cast<double>(node.spacing);
    const int ix = std::min(static_cast<int>(u), quads - 1);
    const int iz = std::min(static_cast<int>(v), quads - 1);
    const auto fu = static_cast<float>(u - ix);
    const auto fv = static_cast<float>(v - iz);
    const std::size_t i00 = static_cast<std::size_t>(iz) * stride + static_cast<std::size_t>(ix);
    const std::uint32_t corners[4] = { node.colors[i00], node.colors[i00 + 1], node.colors[i00 + stride], node.colors[i00 + stride + 1] };
    rgba = 0xFF000000u;
    for (int channel = 0; channel < 3; ++channel) {
        const auto at = [&](int corner) { return static_cast<float>((corners[corner] >> (8 * channel)) & 0xFFu); };
        const float top = at(0) + fu * (at(1) - at(0));
        const float bottom = at(2) + fu * (at(3) - at(2));
        const float value = top + fv * (bottom - top);
        rgba |= static_cast<std::uint32_t>(std::clamp(value + 0.5f, 0.0f, 255.0f)) << (8 * channel);
    }
    return true;
}

SurfaceSampler::SurfaceSampler(SurfaceSnapshot snapshot)
    : m_snapshot(std::move(snapshot))
{
    m_byKey.reserve(m_snapshot.nodes.size());
    for (const std::shared_ptr<const NodeMesh>& node : m_snapshot.nodes) {
        m_byKey.emplace(node->key, node.get());
        if (node->key.level < m_hasLevel.size()) {
            m_hasLevel[node->key.level] = true;
        }
    }
}

SurfaceHit SurfaceSampler::Sample(double x, double z, const NodeMesh*& hint) const noexcept
{
    SurfaceHit hit;
    if (hint != nullptr && NodeHeightAt(*hint, x, z, hit.height)) {
        hit.level = hint->key.level;
        NodeColorAt(*hint, x, z, hit.color);
        return hit;
    }
    for (std::size_t level = 0; level < m_hasLevel.size(); ++level) {
        if (!m_hasLevel[level]) {
            continue;
        }
        const double size = m_snapshot.nodeSizeLevel0 * static_cast<double>(1u << level);
        const NodeKey key{ static_cast<std::int32_t>(std::floor(x / size)), static_cast<std::int32_t>(std::floor(z / size)),
            static_cast<std::uint8_t>(level) };
        const auto it = m_byKey.find(key);
        if (it != m_byKey.end() && NodeHeightAt(*it->second, x, z, hit.height)) {
            hint = it->second;
            hit.level = static_cast<int>(level);
            NodeColorAt(*it->second, x, z, hit.color);
            return hit;
        }
    }
    if (m_snapshot.field != nullptr) {
        hit.height = m_snapshot.field->Height(x, z, 2.0);
        hit.level = -1;
        const TerrainSettings& settings = m_snapshot.field->Settings();
        hit.color = PackRgba8(SurfaceColor(hit.height, m_snapshot.field->Slope(x, z, 2.0), settings));
        return hit;
    }
    hit.level = -2;
    return hit;
}

} // namespace gte::terrain
