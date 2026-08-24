#version 450

// Fragment shader half of the engine's shared "imported mesh" draw path (see
// Mesh.vert). Deliberately simple - a fixed-direction lambert term plus a
// flat ambient floor, no textures/materials involved at all: a *.gta
// AssetType::Mesh payload carries no material/texture data yet (see
// TODO.md, "PMX material/texture import") - the same flat, neutral-grey
// "clay" look src/Editor/AssetPreviewMesh.cpp's own (Editor-only)
// MeshPreview.frag already uses for the Inspector's mesh preview, just
// promoted here so real gameplay rendering of a spawned mesh entity (see
// Game::CreateMeshEntityFromGtaFile()) looks the same way instead of an
// unshaded flat silhouette.

layout(location = 0) in vec3 inWorldNormal;

layout(location = 0) out vec4 outColor;

void main()
{
    // Fixed world-space light direction (pointing FROM the surface TOWARD
    // the light - a classic upper-front-right key light) - there is no real
    // light/material system yet, so this never actually moves with a scene
    // light.
    const vec3 lightDir = normalize(vec3(0.45, 0.8, -0.4));
    const vec3 baseColor = vec3(0.72, 0.73, 0.76); // Neutral light grey - matches MeshPreview.frag's own "clay" look.

    vec3 normal = normalize(inWorldNormal);
    float diffuse = max(dot(normal, lightDir), 0.0);

    const float ambient = 0.35;
    vec3 shaded = baseColor * (ambient + diffuse * (1.0 - ambient));
    outColor = vec4(shaded, 1.0);
}
