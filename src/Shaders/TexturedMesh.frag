#version 450

// Fragment shader half of the engine's TEXTURED "imported mesh submesh"
// draw path (see TexturedMesh.vert). Samples ONE combined-image-sampler
// (set = 0, binding = 0 - see GpuResourceFactory::MaterialDescriptorSetLayout()/
// CreateMaterialTexture2D() and Renderer/MaterialTexture.h) - a PMX
// material's diffuse texture - and combines it with the real scene
// directional light + shadow map via the shared
// ApplyDirectionalLightAndReceiverMask() function (see Mesh.frag), so a
// textured and untextured part of the SAME model still shade consistently
// with each other.

#include "DirectionalLightingAndReceiverMask.glsl"

layout(set = 0, binding = 0) uniform sampler2D uDiffuseTexture;
layout(set = 1, binding = 0) uniform sampler2D sceneOcclusionMap;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texColor = texture(uDiffuseTexture, inUv);
    vec3 shaded = ApplyDirectionalLightAndReceiverMask(texColor.rgb, inWorldNormal, inWorldPos, sceneOcclusionMap);
    outColor = vec4(shaded, texColor.a);
}
