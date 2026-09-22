#version 450

// Vertex shader for the GPU-Driven Frustum Culling + Indirect Draw campaign
// (render-pass-5), PHASE2 (task_manager/render-pass-5/
// PHASE2_INSTANCED_DRAW_PRIMITIVE_AND_INDIRECT_SUBMIT.md). Reuses
// Shaders/Mesh.frag UNCHANGED as its fragment shader - see Pipeline.h's
// VertexLayout::PositionNormalInstanced. Same vertex INPUT attributes as
// Mesh.vert (position, normal - src/Renderer/MeshVertex.h's MeshVertex
// layout, unchanged).
//
// UNLIKE every other vertex shader in this engine, the per-draw "model"
// matrix does NOT come from the push constant here - it comes from THIS
// SHADER'S OWN per-instance storage buffer (set = 0, binding = 0),
// indexed by gl_InstanceIndex. This is what lets ONE indirect draw call
// (Renderer::SubmitIndirect() -> vkCmdDrawIndexedIndirect(Count)) render N
// differently-positioned/oriented objects: every emitted
// IndirectDrawCommand (src/Renderer/IndirectDrawTypes.h, a mirror of
// VkDrawIndexedIndirectCommand) has instanceCount == 1 and
// firstInstance == that instance's own row index in this (uncompacted)
// per-instance array, so gl_InstanceIndex always resolves to exactly the
// right row for that draw.
//
// The push constant block below is still declared with the SAME 128-byte
// shape (model then viewProj) every other Pipeline in this engine uses -
// Pipeline.h's fixed push-constant convention/range is shared
// unconditionally by every VertexLayout - but `pc.model` is deliberately
// UNUSED by this shader: only `pc.viewProj` is read (the active camera
// doesn't change per-instance, so it is still exactly one push-constant
// value per draw call). Do not be confused by the apparently-unused first
// half of this block - see Renderer::SubmitIndirect(), which pushes
// whatever convenient (never read) value into the model half.
layout(push_constant) uniform PushConstants {
    mat4 model; // UNUSED by this shader - see the header comment above.
    mat4 viewProj;
} pc;

// Must match src/Renderer/Culling/CullingTypes.h's GpuCullingInstanceInput
// exactly (112 bytes, std430) - the SAME per-instance array the future
// Shaders/FrustumCull.comp compute shader (PHASE3) reads and PHASE4/5's
// per-frame packing step (PackCullingInstanceInput()) writes. Only
// worldMatrix is actually READ by this shader; aabbMin/aabbMax/meta exist
// purely so this struct's own std430 stride/array-indexing lines up
// byte-for-byte with the real C++ struct - see that header's own
// field-by-field layout and its own "Math/Mat4.h storage convention" note
// (PHASE1_COMPLETION_REPORT.md): Mat4::Data() is already a contiguous
// column-major float[16], bit-identical to this `mat4`, so no transpose
// happens anywhere in this pipeline.
struct InstanceData {
    mat4 worldMatrix;
    vec4 aabbMin; // xyz + pad - UNUSED by this shader.
    vec4 aabbMax; // xyz + pad - UNUSED by this shader.
    uvec4 meta; // x=firstIndex, y=indexCount, z=vertexOffset (bit-cast), w=pad - UNUSED by this shader.
};

layout(std430, set = 0, binding = 0) readonly buffer InstanceBuffer {
    InstanceData instances[];
} instanceBuffer;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

// Matches Shaders/Mesh.frag's own input interface block exactly (world-space
// normal only) - confirmed by reading that file before writing this one, so
// the two genuinely link/match with zero fragment-shader changes.
layout(location = 0) out vec3 outWorldNormal;

void main()
{
    mat4 model = instanceBuffer.instances[gl_InstanceIndex].worldMatrix;
    gl_Position = pc.viewProj * model * vec4(inPosition, 1.0);
    // mat3(model) (no inverse-transpose) is only exactly correct for a
    // uniform scale - see Shaders/Mesh.vert's own identical, already-accepted
    // caveat and Math/Mat4.h's TransformNormal() for the general case.
    outWorldNormal = mat3(model) * inNormal;
}
