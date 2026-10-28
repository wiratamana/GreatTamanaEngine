#pragma once

#include "View.h"
#include "../../ECS/Entity.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"
#include "../../Renderer/Culling/ClusterCullMath.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace gte::fx {

// Opaque id of a mesh. Core resolves it; features never see the renderer's own handle.
struct MeshToken {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    friend bool operator==(const MeshToken& a, const MeshToken& b) noexcept
    {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(const MeshToken& a, const MeshToken& b) noexcept { return !(a == b); }
    friend bool operator<(const MeshToken& a, const MeshToken& b) noexcept
    {
        return a.index != b.index ? a.index < b.index : a.generation < b.generation;
    }
};

struct MeshTokenHash {
    std::size_t operator()(const MeshToken& token) const noexcept
    {
        return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(token.index) << 32) | token.generation);
    }
};

// One drawable instance of a cluster-ready mesh. `flags` holds kClusterFlag* bits; `maxScale` is the largest axis scale.
struct ClusterInstance {
    Entity entity;
    MeshToken mesh;
    Mat4 world = Mat4::Identity();
    std::uint32_t flags = 0;
    float maxScale = 0.0f;
};

// Camera of one chain. `eyeOrDirection` is the eye point, or the view direction when `orthographic`.
struct ClusterCamera {
    Mat4 viewProjection = Mat4::Identity();
    Vec3 eyeOrDirection;
    bool orthographic = false;
    bool valid = true; // False for a non-finite matrix or eye; the verb refuses it.
};

// Camera data of `view`. Orthographic when the last matrix row is (0, 0, 0, 1).
ClusterCamera ClusterCameraOf(const View& view);

// Camera from a bare view-projection. `eye` is used only for a perspective matrix.
ClusterCamera MakeClusterCamera(const Mat4& viewProjection, const Vec3& eye);

// Opaque handle to a per-frame set of entities that the CPU scene draw skips. Valid until the next frame.
struct EntityExclusion {
    const void* handle = nullptr;
};

// Why a chain declared nothing on purpose (not a refusal). None when it ran or was refused.
enum class ClusterInertReason : std::uint8_t { None, DeviceCaps, SharedScope, CaptureArmed, ToggledOff, InvalidState };

constexpr std::string_view ClusterInertReasonName(ClusterInertReason reason) noexcept
{
    switch (reason) {
    case ClusterInertReason::None: return "none";
    case ClusterInertReason::DeviceCaps: return "device caps";
    case ClusterInertReason::SharedScope: return "shared scope";
    case ClusterInertReason::CaptureArmed: return "capture armed";
    case ClusterInertReason::ToggledOff: return "toggled off";
    case ClusterInertReason::InvalidState: return "invalid state";
    }
    return "none";
}

struct ClusterPlan {
    bool ready = false; // Record() is expected to declare the whole chain.
    EntityExclusion exclusion;
    std::uint32_t parts = 0;
    ClusterInertReason inertReason = ClusterInertReason::None;
};

struct ClusterStats {
    bool declared = false;
    bool occlusionApplied = false; // The two-phase occlusion cull ran in this chain.
    HizReason hizReason = HizReason::None; // Why a requested two-phase chain ran single-phase; None otherwise.
    std::uint32_t instances = 0;
    std::uint32_t parts = 0;
    std::uint64_t workItems = 0; // Sum over declared parts of instances * clusters: what the cull tested.
    ClusterInertReason inertReason = ClusterInertReason::None;
};

// Survivor count read back from the GPU. `frame` is the oldest frame among the cells that were summed.
struct ClusterVisibleCount {
    std::uint32_t value = 0;
    std::uint64_t frame = 0;
    bool valid = false;
};

} // namespace gte::fx
