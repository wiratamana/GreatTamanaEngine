#pragma once

#include <cstddef>

namespace gte {

// Which per-vertex GPU layout a Pipeline expects its bound Mesh's vertex
// buffer to already be in - see Vertex.h (PositionColor) and MeshVertex.h
// (PositionNormal/PositionNormalUv). A Mesh carries no notion of "its own"
// vertex layout; it is up to whoever builds a Pipeline/Mesh pair to agree on
// this. Zero Vulkan dependency - shared by Pipeline.h (Renderer-internal)
// and PipelineDesc.h (public, cross-layer contract).
enum class VertexLayout {
    // Position+color (Vertex.h) - every built-in primitive shape and the
    // original triangle demo use this.
    PositionColor,

    // Position+normal (MeshVertex.h) - for an imported, indexed mesh asset
    // with no per-vertex color.
    PositionNormal,

    // Position+normal+UV (MeshVertex.h's MeshVertexUv) - for a textured,
    // indexed mesh submesh. A Pipeline built with this layout also carries a
    // descriptor-set layout for one combined-image-sampler (set 0, binding 0).
    PositionNormalUv,

    // Same vertex INPUT attributes as PositionNormal, but the per-draw model
    // matrix comes from a per-instance storage buffer (set 0, binding 0,
    // vertex stage) indexed by gl_InstanceIndex, for GPU-driven indirect
    // instanced draws. Indexed meshes only.
    PositionNormalInstanced,

    // No vertex input at all - zero bindings, zero attributes. For a
    // fullscreen-triangle pipeline whose vertex shader synthesizes its own
    // 3 vertices from gl_VertexIndex and never reads a vertex buffer.
    None,
};

// Number of VertexLayout enumerators. Keep in sync with the enum above.
inline constexpr std::size_t kVertexLayoutCount = 5;

static_assert(kVertexLayoutCount == static_cast<std::size_t>(VertexLayout::None) + 1,
    "kVertexLayoutCount must equal the enum's last entry + 1 - add a new case and bump this constant together.");

} // namespace gte
