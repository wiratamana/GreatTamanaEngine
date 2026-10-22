#pragma once

#include "View.h"
#include "../../ECS/Entity.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <cstddef>
#include <cstdint>
#include <functional>

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

// Opaque handle to a per-frame set of entities that the CPU scene draw skips. Valid until the next frame.
struct EntityExclusion {
    const void* handle = nullptr;
};

struct ClusterPlan {
    bool ready = false; // Record() is expected to declare the whole chain.
    EntityExclusion exclusion;
    std::uint32_t parts = 0;
};

struct ClusterStats {
    bool declared = false;
    std::uint32_t instances = 0;
    std::uint32_t parts = 0;
};

} // namespace gte::fx
