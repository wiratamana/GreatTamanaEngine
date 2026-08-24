#version 450

// Fragment shader half of the engine's TEXTURED "imported mesh submesh"
// draw path (see TexturedMesh.vert). Samples ONE combined-image-sampler
// (set = 0, binding = 0 - see GpuResourceFactory::MaterialDescriptorSetLayout()/
// CreateMaterialTexture2D() and Renderer/MaterialTexture.h) - a PMX
// material's diffuse texture - and combines it with the same simple,
// fixed-direction lambert term Mesh.frag already uses for an untextured
// submesh, so a textured and untextured part of the SAME model still shade
// consistently with each other rather than looking like two unrelated
// rendering styles bolted together.

layout(set = 0, binding = 0) uniform sampler2D uDiffuseTexture;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    // Same fixed world-space light direction/ambient floor as Mesh.frag -
    // see that file's own comment for why there is no real light/material
    // system yet.
    const vec3 lightDir = normalize(vec3(0.45, 0.8, -0.4));
    const float ambient = 0.35;

    vec3 normal = normalize(inWorldNormal);
    float diffuseTerm = max(dot(normal, lightDir), 0.0);

    vec4 texColor = texture(uDiffuseTexture, inUv);
    vec3 shaded = texColor.rgb * (ambient + diffuseTerm * (1.0 - ambient));
    outColor = vec4(shaded, texColor.a);
}
