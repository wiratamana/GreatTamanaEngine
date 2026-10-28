#pragma once

// Survivors of the primary view's cluster chain, kept for one frame so another view can draw them again.
// Flat data and pure decisions; Vulkan-free.

#include "ClusterDrawTypes.h"
#include "../../Renderer/RenderGraph/RenderGraphHandles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte::fx {

// Buffers of one lane (one occlusion phase) of a published part.
struct ClusterSurvivorLane {
    rg::BufferHandle visible;
    rg::BufferHandle commands;
    rg::BufferHandle count;
};

// One mesh of the primary chain. `setKey` identifies its instance set and order.
struct ClusterSurvivorPart {
    MeshToken mesh;
    std::uint64_t setKey = 0;
    std::uint64_t workItems = 0;
    rg::BufferHandle instances;
    std::array<ClusterSurvivorLane, 2> lanes;
};

// Valid only for the frame and graph it was begun for.
struct ClusterSurvivorTable {
    std::uint64_t frameIndex = 0;
    const void* graph = nullptr;
    std::uint32_t laneCount = 0; // 0 = empty, 1 = single-phase chain, 2 = two-phase chain.
    std::uint32_t cullFlags = 0;
    Mat4 cullViewProjection = Mat4::Identity();
    Vec3 cullEye;
    std::vector<ClusterSurvivorPart> parts;

    void Begin(std::uint64_t frame, const void* graphId, std::uint32_t lanes, std::uint32_t flags,
        const Mat4& viewProjection, const Vec3& eye)
    {
        frameIndex = frame;
        graph = graphId;
        laneCount = lanes;
        cullFlags = flags;
        cullViewProjection = viewProjection;
        cullEye = eye;
        parts.clear();
    }

    void Clear() noexcept
    {
        laneCount = 0;
        parts.clear();
    }

    bool IsFor(std::uint64_t frame, const void* graphId) const noexcept
    {
        return laneCount != 0 && frameIndex == frame && graph == graphId;
    }

    const ClusterSurvivorPart* Find(MeshToken mesh, std::uint64_t setKey) const noexcept
    {
        for (const ClusterSurvivorPart& part : parts) {
            if (part.mesh == mesh && part.setKey == setKey) {
                return &part;
            }
        }
        return nullptr;
    }
};

enum class ClusterSurvivorSource : std::uint8_t { Own, Shared };

// A chain draws the published survivors only when it asked for them, the table is of this frame and graph, the
// cull setup is the same, and every planned part was published. Otherwise it culls on its own.
constexpr ClusterSurvivorSource ChooseSurvivorSource(
    bool requested, bool tableIsCurrent, bool sameCullSetup, std::size_t plannedParts, std::size_t coveredParts) noexcept
{
    return requested && tableIsCurrent && sameCullSetup && plannedParts > 0 && coveredParts == plannedParts
        ? ClusterSurvivorSource::Shared
        : ClusterSurvivorSource::Own;
}

} // namespace gte::fx
