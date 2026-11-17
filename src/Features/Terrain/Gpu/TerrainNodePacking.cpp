#include "TerrainNodePacking.h"

#include <cmath>
#include <limits>

namespace gte::terrain::gpu {

namespace {

float RoundDown(double v) noexcept
{
    float f = static_cast<float>(v);
    if (static_cast<double>(f) > v) {
        f = std::nextafter(f, -std::numeric_limits<float>::infinity());
    }
    return f;
}

float RoundUp(double v) noexcept
{
    float f = static_cast<float>(v);
    if (static_cast<double>(f) < v) {
        f = std::nextafter(f, std::numeric_limits<float>::infinity());
    }
    return f;
}

} // namespace

BoxF NodeWorldBox(const NodeMesh& node) noexcept
{
    BoxF box;
    box.min[0] = RoundDown(node.originX + static_cast<double>(node.boundsMin.x));
    box.min[1] = RoundDown(static_cast<double>(node.boundsMin.y));
    box.min[2] = RoundDown(node.originZ + static_cast<double>(node.boundsMin.z));
    box.max[0] = RoundUp(node.originX + static_cast<double>(node.boundsMax.x));
    box.max[1] = RoundUp(static_cast<double>(node.boundsMax.y));
    box.max[2] = RoundUp(node.originZ + static_cast<double>(node.boundsMax.z));
    return box;
}

BoxF PackNodeVertices(const NodeMesh& node, const RampUvMap& uvMap, std::span<TerrainVertex> out) noexcept
{
    const std::size_t count = node.vertices.size() < out.size() ? node.vertices.size() : out.size();
    for (std::size_t i = 0; i < count; ++i) {
        const TerrainVertex& in = node.vertices[i];
        TerrainVertex& v = out[i];
        v.position[0] = static_cast<float>(node.originX + static_cast<double>(in.position[0]));
        v.position[1] = in.position[1];
        v.position[2] = static_cast<float>(node.originZ + static_cast<double>(in.position[2]));
        v.normal[0] = in.normal[0];
        v.normal[1] = in.normal[1];
        v.normal[2] = in.normal[2];
        v.uv[0] = uvMap.Apply(in.uv[0]);
        v.uv[1] = uvMap.Apply(in.uv[1]);
    }
    return NodeWorldBox(node);
}

void PackPreviewVertices(const NodeMesh& node, std::span<PreviewVertex> out) noexcept
{
    const std::size_t count = node.vertices.size() < out.size() ? node.vertices.size() : out.size();
    constexpr float k = 1.0f / 255.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const TerrainVertex& in = node.vertices[i];
        PreviewVertex& v = out[i];
        v.position[0] = static_cast<float>(node.originX + static_cast<double>(in.position[0]));
        v.position[1] = in.position[1];
        v.position[2] = static_cast<float>(node.originZ + static_cast<double>(in.position[2]));
        v.normal[0] = in.normal[0];
        v.normal[1] = in.normal[1];
        v.normal[2] = in.normal[2];
        const std::uint32_t packed = i < node.colors.size() ? node.colors[i] : 0xFF808080u;
        v.color[0] = static_cast<float>(packed & 0xFFu) * k;
        v.color[1] = static_cast<float>((packed >> 8) & 0xFFu) * k;
        v.color[2] = static_cast<float>((packed >> 16) & 0xFFu) * k;
    }
}

} // namespace gte::terrain::gpu
