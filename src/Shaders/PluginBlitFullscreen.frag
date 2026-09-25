#version 450

// editor-core-separation-9 campaign, PHASE3
// (PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md, Step 3.1) -
// fragment shader for `gte.builtin.blit_fullscreen`: a literal passthrough,
// making an already-computed texture visible as a color-attachment write -
// the graphics equivalent of Shaders/GBufferCopy.comp's own compute-side
// "just copy it" role. No color grading, no blending - this op's entire job
// is "make an already-computed texture visible as a color-attachment
// write".
//
// Binding convention (matches
// PluginRenderOperationRegistry::RegisterBlitFullscreen() exactly): binding
// 0 = sourceTexture, a read-only combined image sampler, FRAGMENT stage
// only (DescriptorSetLayoutBuilder::AddCombinedImageSampler()'s own
// `stageFlags` parameter defaults to VK_SHADER_STAGE_COMPUTE_BIT - this is
// this whole campaign's first GRAPHICS-kind registry entry, so that default
// must be explicitly overridden - flagged during PHASE2's own planning,
// see PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md Step 2.2).

layout(binding = 0) uniform sampler2D sourceTexture;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(sourceTexture, inUv);
}
