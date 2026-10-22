#pragma once

#include <cstddef>

namespace gte {

// Per-vertex GPU layout a Pipeline expects from its bound Mesh; the Pipeline/Mesh builder must agree on it.
// No Vulkan dependency; shared by Pipeline.h and PipelineDesc.h.
enum class VertexLayout {
    // Position + color (Vertex.h): built-in primitives and the triangle demo.
    PositionColor,

    // Position + normal (MeshVertex.h): indexed mesh without per-vertex color.
    PositionNormal,

    // Position + normal + UV (MeshVertex.h): textured indexed submesh; the pipeline also gets one
    // combined-image-sampler set layout (set 0, binding 0).
    PositionNormalUv,

    // Position + normal + UV + tangent (MeshVertex.h): textured submesh with a tangent-space frame.
    // Same material set as PositionNormalUv.
    PositionNormalUvTangent,

    // No vertex input; the vertex shader builds a fullscreen triangle from gl_VertexIndex.
    None,
};

// Number of VertexLayout enumerators. Keep in sync with the enum above.
inline constexpr std::size_t kVertexLayoutCount = 5;

static_assert(kVertexLayoutCount == static_cast<std::size_t>(VertexLayout::None) + 1,
    "kVertexLayoutCount must equal the enum's last entry + 1 - add a new case and bump this constant together.");

} // namespace gte
