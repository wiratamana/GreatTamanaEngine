#pragma once

#include "../../Renderer/GpuTiming.h"
#include "../../Renderer/MeshHandle.h"
#include "../../Renderer/OffscreenFrameTicket.h"
#include "../../Renderer/OffscreenRingGuard.h"
#include "../../Renderer/RenderGraph/GpuBuffer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

class Renderer;
class RenderSystem;
struct SkinnedMeshData;
struct MeshAssetPart;

// The bone matrices of one model: one host-written copy per frame slot, so the CPU never overwrites a copy that a
// frame in flight still reads.
class BoneMatrixRing {
public:
    BoneMatrixRing() = default;
    BoneMatrixRing(rg::DeviceRef device, std::size_t byteSize)
    {
        for (rg::GpuBuffer& copy : m_copies) {
            copy = rg::GpuBuffer(device, byteSize, "GpuSkinningBoneMatrices",
                rg::GpuBufferUse::Storage | rg::GpuBufferUse::HostWrite);
        }
    }

    // Writes the copy of `ticket`'s slot. False, nothing written, when a frame in flight still reads that copy.
    bool Upload(const OffscreenFrameTicket& ticket, const void* data, std::size_t size)
    {
        const std::uint32_t copy = RingCopyOf(ticket, kGpuFramesInFlight);
        return m_use[copy].Claim(ticket) && m_copies[copy].Upload(data, size);
    }

    // The copy a frame of `ticket`'s slot reads.
    const rg::GpuBuffer& CopyFor(const OffscreenFrameTicket& ticket) const noexcept
    {
        return m_copies[RingCopyOf(ticket, kGpuFramesInFlight)];
    }

private:
    std::array<rg::GpuBuffer, kGpuFramesInFlight> m_copies;
    std::array<RingCopyUse, kGpuFramesInFlight> m_use;
};

// GPU resources of every GPU-skinned model: registered once per model, looked up every frame, never evicted.
class GpuSkinningRigCache {
public:
    // One skinning output shared by a group of mesh parts that share one CPU-side vertex buffer.
    struct OutputGroup {
        // Written by the skinning compute pass, read as the vertex buffer of the group's meshes below.
        rg::GpuBuffer outputVertexBuffer;

        // Unique per group: the skinning pass uses it to give each group its own descriptor set.
        std::uint64_t instanceKey = 0;

        // True: output uses the position-normal-uv layout; false: position-normal.
        bool isTextured = false;

        // The model's full vertex count; every group's output covers every vertex.
        std::uint32_t vertexCount = 0;

        // Label "<file>#SkinGroup<N>" used as the imported buffer's name; lives as long as the entry.
        std::string debugName;

        // Each CPU-mode part mesh and its GPU-skinned counterpart; the runtime switch swaps between them.
        struct PartMeshBinding {
            MeshHandle cpuMeshHandle;
            MeshHandle gpuMeshHandle;
        };
        std::vector<PartMeshBinding> partMeshBindings;
    };

    // All GPU resources of one registered model.
    struct GpuModelEntry {
        // Uploaded once at registration.
        rg::GpuBuffer bindPoseBuffer;
        rg::GpuBuffer skinWeightsBuffer;
        // Present only when at least one output group is textured.
        std::optional<rg::GpuBuffer> uvBuffer;

        // Rewritten every frame the model animates on the GPU, one copy per frame slot. Mutable because TryGet()
        // returns a const entry.
        mutable BoneMatrixRing boneMatrices;

        std::vector<OutputGroup> outputGroups;

        // The GPU-skinned counterpart of a CPU-mode mesh, or kInvalidMeshHandle.
        MeshHandle TryGetGpuMeshHandle(MeshHandle cpuMeshHandle) const;

        // The CPU-mode counterpart of a GPU-skinned mesh, or kInvalidMeshHandle.
        MeshHandle TryGetCpuMeshHandle(MeshHandle gpuMeshHandle) const;
    };

    // Builds the model's GPU resources. No-op for a boneless or empty model, or one with no resolvable parts.
    void Register(Renderer& renderer, RenderSystem& renderSystem, const std::string& absoluteGtaPath,
        const SkinnedMeshData& data, const std::vector<MeshAssetPart>& parts);

    const GpuModelEntry* TryGet(const std::string& absoluteGtaPath) const;

    // Appends the CPU and GPU mesh handle of every part binding of every model.
    void AppendBoundMeshHandles(std::vector<MeshHandle>& out) const;

private:
    std::unordered_map<std::string, GpuModelEntry> m_models;
    std::uint64_t m_nextInstanceKey = 1;
};

} // namespace gte
