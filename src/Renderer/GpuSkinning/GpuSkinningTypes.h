#pragma once

#include "../../Assets/MeshData.h"
#include "../../Math/Vec2.h"
#include "../../Math/Vec3.h"

#include <cstdint>
#include <vector>

namespace gte {

// ============================================================================
// GPU Vertex Skinning - Phase 1: Data & Buffer Layout Foundations
// ============================================================================
// See task_manager/gpu_skinning/GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md for
// the full campaign map and
// task_manager/gpu_skinning/GPU_SKINNING_PHASE1_DATA_BUFFER_FOUNDATIONS_STRATEGY_v1.md
// for the full design reasoning behind every struct/function below. This
// file defines the byte-for-byte GPU buffer layouts a future compute kernel
// (Phase 2) will read/write to reproduce Animation/VertexSkinning.cpp's
// SkinVertexRange() entirely on the GPU - it creates NO Vulkan buffers/
// descriptor sets itself (that's Phase 4's job) and dispatches NOTHING (that
// is Phase 2/3's job). This phase's own deliverable is exactly this one
// header (+ its .cpp) and its Tier-1 test file.
//
// Deliberately Vulkan-header-free (no <volk.h>/<vulkan/vulkan.h> anywhere in
// this file or its .cpp) - mirrors src/Renderer/GpuTiming.h's own precedent
// exactly, so every struct/function here stays trivially Tier-1-testable
// with no live VkDevice at all (see
// tests/Renderer/GpuSkinning/GpuSkinningTypesTests.cpp).
//
// THE CORE PITFALL THIS FILE EXISTS TO HEAD OFF: Vec3 (src/Math/Vec3.h) is a
// plain, TIGHTLY-PACKED 3-float struct - correct for ordinary CPU-side
// arrays, but a std430 GLSL layout aligns an array element CONTAINING a vec3
// to 16 bytes (the same alignment as vec4). Uploading a raw
// std::vector<Vec3> byte-for-byte and reading it back as `vec3 positions[]`
// in GLSL would silently read the WRONG bytes for every element after the
// first. Every struct below that a compute shader reads via a std430
// `readonly buffer` block is therefore explicitly padded to a vec4-multiple
// size - the four Pack*() functions at the bottom of this file are the ONE
// place that padding/repacking ever happens; never upload a raw
// std::vector<Vec3>/std::vector<VertexSkinWeights> directly and hope for
// the best.
//
// ----------------------------------------------------------------------------
// BINDING-NUMBER CONVENTION (single source of truth - Phase 2's .comp files
// and Phase 4's descriptor-set-building code must both cite this table
// rather than re-deriving binding numbers independently):
//
//   | Binding | Resource                  | GLSL qualifier                |
//   |---------|---------------------------|--------------------------------|
//   | 0       | Bind-pose vertex buffer   | readonly buffer (std430)       |
//   | 1       | Skin-weight buffer        | readonly buffer (std430)       |
//   | 2       | Bone matrix buffer        | readonly buffer (std430)       |
//   | 3       | Skinned output buffer     | buffer (tightly packed, NOT    |
//   |         |                           | std430-vec-padded - see        |
//   |         |                           | GpuSkinnedVertexPositionNormal/|
//   |         |                           | Uv below)                      |
//
// PHASE 2 ADDENDUM (see GPU_SKINNING_PHASE2_COMPUTE_KERNEL_STRATEGY_v1.md):
// the PositionNormalUv compute-kernel variant (SkinVerticesPositionNormalUv.comp)
// needed one MORE read-only input this table didn't originally have a slot
// for - the bind-pose UV buffer (GpuUv, packed via PackUvs() below) - since
// Phase 1's fixed 4-binding table has no UV entry at all. This is an
// ADDITIVE extension, not a redefinition of bindings 0-3 above: a new
// binding 4 (readonly buffer, plain vec2[] matching GpuUv exactly), used
// ONLY by the PositionNormalUv variant's own descriptor-set layout - the
// untextured PositionNormal variant's layout is unaffected and still has
// exactly the 4 bindings above. See SkinVerticesPositionNormalUv.comp's own
// header comment for the full reasoning.
//   | 4       | Bind-pose UV buffer (Uv   | readonly buffer (std430) -     |
//   |         | variant only)             | UV-VARIANT-ONLY ADDITION        |
//
// Lifetime convention (see Phase 1's own Step 3.3/3.4): the bind-pose vertex
// buffer, skin-weight buffer, and skinned output buffer are all uploaded
// ONCE, at model-load time, and never change again for that model's whole
// lifetime. The bone matrix buffer is the ONE genuinely dynamic input -
// re-uploaded every frame, for every currently-playing animator, from a
// freshly-computed AnimationPoseEvaluator::EvaluateAnimatedSkinningPose()
// result.
// ----------------------------------------------------------------------------

// GPU-side bind-pose vertex: position + normal, each padded out to a vec4
// (16 bytes) so a std430 `vec4 positionAndPad[]`/`vec4 normalAndPad[]`-style
// GLSL declaration (or an equivalent array-of-structs declaration reading
// this exact struct) never misaligns past the first element. Uploaded ONCE
// per model, at load time - see PackBindPoseVertices() below, the one place
// this padding actually happens.
struct GpuBindPoseVertex {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
    float _padPosition = 0.0f;

    float normalX = 0.0f;
    float normalY = 0.0f;
    float normalZ = 0.0f;
    float _padNormal = 0.0f;
};
static_assert(sizeof(GpuBindPoseVertex) == 32,
    "GpuBindPoseVertex must match a std430 vec4+vec4 layout exactly (16-byte-aligned position, 16-byte-aligned normal)");

// GPU-side UV: a plain vec2 - std430's own alignment rule for vec2 is 8
// bytes (two tightly-packed floats), so - unlike GpuBindPoseVertex above -
// NO padding is needed here at all. Uploaded once per model, at load time,
// only for a TEXTURED model (see Pipeline.h's VertexLayout::PositionNormalUv
// and GpuSkinnedVertexPositionNormalUv below) - an untextured model has no
// UV buffer/binding at all.
struct GpuUv {
    float u = 0.0f;
    float v = 0.0f;
};
static_assert(sizeof(GpuUv) == 8, "GpuUv must match a std430 vec2 element exactly (no padding needed)");

// GPU-side per-vertex skin weights - mirrors VertexSkinWeights
// (src/Assets/MeshData.h) exactly, minus its SDEF-only correction terms
// (sdefC/sdefR0/sdefR1 - deliberately NOT carried onto the GPU path, exactly
// like Animation/VertexSkinning.cpp's own CPU SkinVertices()/SkinVertexRange()
// already treats SDEF identically to BDEF2 and ignores those terms - see
// this campaign's own "no shader permutation, no new skinning math" refusal).
// boneIndices are std::uint32_t (not int32_t) to match GLSL's uvec4/uint[]
// convention directly - see PackSkinWeights() below for how
// VertexSkinWeights::boneIndices' -1-means-unused convention is translated
// into this unsigned representation (bone 0, weight 0.0 - never a raw
// bit-reinterpreted -1, which would be a huge, out-of-bounds index).
// Uploaded ONCE per model, at load time - this data never changes for a
// given model/vertex, only the bone matrices themselves do, per frame.
struct GpuSkinWeights {
    std::uint32_t boneIndices[4] = { 0, 0, 0, 0 };
    float weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};
static_assert(sizeof(GpuSkinWeights) == 32,
    "GpuSkinWeights must match a std430 uvec4+vec4 layout exactly (16 bytes each, tightly packed, no extra padding needed)");

// GPU-side SKINNED OUTPUT vertex - matches VertexLayout::PositionNormal
// (Mesh.vert/.frag consumers) - see src/Renderer/MeshVertex.h's MeshVertex.
// Deliberately NOT std430-vec-padded like GpuBindPoseVertex above: this
// buffer is read by the graphics pipeline's fixed-function vertex-input
// assembler (a real VkVertexInputAttributeDescription, exactly like
// MeshVertex/MeshVertexUv are read today), not by a shader's std430 block -
// so it must stay TIGHTLY PACKED, byte-identical to MeshVertex. The
// static_assert below is the load-bearing guard against a future edit to
// MeshVertex.h silently breaking this agreement - this file deliberately
// does NOT #include MeshVertex.h (which pulls in <volk.h> transitively, see
// this file's own "Vulkan-header-free" rule above), so the literal byte
// count (24 = 3 position floats + 3 normal floats, all 4-byte) is asserted
// directly and cross-referenced here BY NAME instead of by #include - the
// exact same "kept as two separate named things on purpose, cross-referenced
// by comment, not a shared #include" precedent GpuTiming.h's own
// kGpuTimingFramesInFlight already establishes for FramePresenter::
// kFramesInFlight. Add/update a matching static_assert here any time either
// struct changes.
struct GpuSkinnedVertexPositionNormal {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;

    float normalX = 0.0f;
    float normalY = 0.0f;
    float normalZ = 0.0f;
};
static_assert(sizeof(GpuSkinnedVertexPositionNormal) == 24,
    "must stay byte-identical to MeshVertex (src/Renderer/MeshVertex.h) - the graphics pipeline reads this buffer via the SAME vertex input state");

// GPU-side SKINNED OUTPUT vertex - matches VertexLayout::PositionNormalUv
// (TexturedMesh.vert/.frag consumers) - see src/Renderer/MeshVertex.h's
// MeshVertexUv. Same "tightly packed, no std430 padding" rule as
// GpuSkinnedVertexPositionNormal above, and the same "byte count asserted
// directly, cross-referenced by name rather than by #include" rule.
struct GpuSkinnedVertexPositionNormalUv {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;

    float normalX = 0.0f;
    float normalY = 0.0f;
    float normalZ = 0.0f;

    float u = 0.0f;
    float v = 0.0f;
};
static_assert(sizeof(GpuSkinnedVertexPositionNormalUv) == 32,
    "must stay byte-identical to MeshVertexUv (src/Renderer/MeshVertex.h)");

// Converts plain CPU-side bind-pose positions/normals into the padded GPU
// layout above. Mirrors Animation/VertexSkinning.cpp's own
// `hasNormals ? bindNormals[i] : Vec3::Up()` fallback exactly (see
// SkinVertexRange()) - a mismatched/missing normals array degrades to a
// fixed Vec3::Up() per vertex rather than reading out of bounds or leaving
// garbage. Pure function, no GPU/Renderer dependency of any kind.
std::vector<GpuBindPoseVertex> PackBindPoseVertices(const std::vector<Vec3>& positions, const std::vector<Vec3>& normals);

// Converts plain CPU-side UVs into the GPU layout above - a straight,
// unmodified copy (UVs never change under skinning, see Phase 2's own "UVs
// are copied from the bind-pose UV buffer straight to the output buffer
// unmodified" note), just reshaped from Vec2 into GpuUv. Pure function.
std::vector<GpuUv> PackUvs(const std::vector<Vec2>& uvs);

// Converts plain CPU-side skin weights (VertexSkinWeights, whose "unused
// slot" convention is boneIndex == -1 / weight == 0.0 - see MeshData.h's own
// doc comment) into the GPU layout above. An unused slot (boneIndex < 0) is
// packed as bone 0 with weight 0.0 - NEVER a raw bit-reinterpreted -1 (which
// would alias a huge, out-of-bounds std::uint32_t index) - the accompanying
// zero weight is what the GPU kernel (Phase 2) actually relies on to skip
// that slot's contribution entirely, exactly mirroring the CPU path's own
// `weight == 0.0f` skip test in SkinVertexRange(). Pure function, no GPU/
// Renderer dependency of any kind.
std::vector<GpuSkinWeights> PackSkinWeights(const std::vector<VertexSkinWeights>& skinWeights);

} // namespace gte
