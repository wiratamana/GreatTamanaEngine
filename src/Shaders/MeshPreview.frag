#version 450

// Fragment shader half of the Inspector's Mesh Asset preview viewer (see
// MeshPreview.vert). Deliberately simple - a fixed-direction lambert term
// plus a flat ambient floor, no textures/materials involved at all (this
// engine's *.gta AssetType::Mesh payload carries no material data yet - see
// MeshFile.h) - the same "flat, neutral-grey clay model" look Unity's own
// model-import preview uses, just enough to actually SEE the imported
// geometry's shape/silhouette rather than a flat silhouette with no shading
// cues at all.

layout(location = 0) in vec3 inWorldNormal;

layout(location = 0) out vec4 outColor;

void main()
{
    // Fixed world-space light direction (pointing FROM the surface TOWARD
    // the light - a classic upper-front-right key light) - never rotates
    // with the model, matching AssetPreviewMesh's own fixed-camera/
    // spinning-model convention (see its class comment).
    const vec3 lightDir = normalize(vec3(0.45, 0.8, -0.4));
    const vec3 baseColor = vec3(0.72, 0.73, 0.76); // Neutral light grey - Unity's own model-preview "clay" look.

    vec3 normal = normalize(inWorldNormal);
    float diffuse = max(dot(normal, lightDir), 0.0);

    const float ambient = 0.35;
    vec3 shaded = baseColor * (ambient + diffuse * (1.0 - ambient));
    outColor = vec4(shaded, 1.0);
}
