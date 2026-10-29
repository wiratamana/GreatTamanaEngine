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

// Opaque id of a material. Core resolves it to the descriptor set that a draw binds; features never see that set.
struct MaterialToken {
    std::uint32_t index = 0;
    std::uint32_t generation = 0; // 0 means no material.

    constexpr bool IsValid() const noexcept { return generation != 0; }

    friend bool operator==(const MaterialToken& a, const MaterialToken& b) noexcept
    {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(const MaterialToken& a, const MaterialToken& b) noexcept { return !(a == b); }
    friend bool operator<(const MaterialToken& a, const MaterialToken& b) noexcept
    {
        return a.index != b.index ? a.index < b.index : a.generation < b.generation;
    }
};

struct MaterialTokenHash {
    std::size_t operator()(const MaterialToken& token) const noexcept
    {
        return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(token.index) << 32) | token.generation);
    }
};

// One drawable instance of a cluster-ready mesh. `flags` holds kClusterFlag* bits; `maxScale` is the largest axis scale.
// `kind` selects the vertex layout and shaders; `material` is valid exactly when `kind` is not None.
struct ClusterInstance {
    Entity entity;
    MeshToken mesh;
    MaterialToken material;
    ClusterMaterialKind kind = ClusterMaterialKind::None;
    Mat4 world = Mat4::Identity();
    std::uint32_t flags = 0;
    float maxScale = 0.0f;
};

// Renderables that candidate selection kept on the CPU draw, by reason. A renderable counts once.
struct ClusterRejectCounts {
    std::uint32_t skinned = 0;           // Skinned vertices change every frame.
    std::uint32_t noIndexBuffer = 0;
    std::uint32_t unsupportedLayout = 0; // Vertex layout outside the cluster set, or a textured mesh without a material.
    std::uint32_t noClusterTable = 0;    // No table: alpha-tested or refused textured slice, or an untextured mesh that cannot get one.
    std::uint32_t tooFewInstances = 0;   // A synthesized table is only worth a chain for several instances.
    std::uint32_t nonFinite = 0;

    constexpr std::uint32_t Total() const noexcept
    {
        return skinned + noIndexBuffer + unsupportedLayout + noClusterTable + tooFewInstances + nonFinite;
    }
};

// World box that holds a set of instances. `bounded` is false for an empty set or when any instance box is unusable.
struct ClusterPartBox {
    AABB box;
    bool bounded = false;
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
    bool ready = false; // Record() declares the chain and claims its entities; no pass when every part is outside the view.
    EntityExclusion exclusion;
    std::uint32_t parts = 0;
    ClusterInertReason inertReason = ClusterInertReason::None;
};

struct ClusterStats {
    bool declared = false;
    bool occlusionApplied = false; // The two-phase occlusion cull ran in this chain.
    bool sharedSurvivors = false;  // The chain drew another view's published survivors; nothing was culled here.
    bool sharedOcclusion = false;  // Those survivors came from a two-phase occlusion chain.
    HizReason hizReason = HizReason::None; // Why the view's HiZ phase was skipped (incl. nothing in view); None otherwise.
    std::uint32_t instances = 0;
    std::uint32_t parts = 0;
    std::uint64_t workItems = 0; // Sum over declared parts of instances * clusters: what the cull tested.
    std::uint32_t noSlotInstances = 0; // Instances of parts that found no part slot and stay on the CPU draw.
    std::uint32_t skippedParts = 0;     // Parts outside the cull frustum: no pass, entities still claimed.
    std::uint32_t skippedInstances = 0; // Instances of those parts.
    ClusterInertReason inertReason = ClusterInertReason::None;
};

// Survivor count read back from the GPU. `frame` is the oldest frame among the cells that were summed.
struct ClusterVisibleCount {
    std::uint32_t value = 0;
    std::uint64_t frame = 0;
    bool valid = false;
};

// One readback cell of a part slot: the survivor count and the outcome counters (kClusterReason* slots).
struct ClusterStatsWords {
    std::uint32_t visible = 0;
    std::uint32_t reasons[kClusterReasonWords] = {};
};
static_assert(sizeof(ClusterStatsWords) == kClusterCountBytes, "a readback cell mirrors the count buffer layout");

// Items by outcome, summed over the parts of a chain. Zero unless the cull passes collected reasons.
struct ClusterReasonCounts {
    std::uint32_t frustum = 0; // Rejected by the frustum test.
    std::uint32_t cone = 0;    // Rejected by the back-face cone test.
    std::uint32_t pyramid = 0; // Rejected by the occlusion test.
    std::uint32_t history = 0; // Drawn by the first phase from last frame's history.
    std::uint32_t kept = 0;    // Passed every enabled test.
    std::uint64_t frame = 0;   // Oldest frame among the cells that were summed.
    bool valid = false;
};

} // namespace gte::fx
