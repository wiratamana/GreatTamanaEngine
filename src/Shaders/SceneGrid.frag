#version 450

// Infinite ground grid: ray-plane hit at Y == 0 per pixel, anti-aliased lines, colored X/Z axes, distance fade.
// No Y flip here: invViewProj already maps this pixel's NDC xy to the ray the scene geometry used.

layout(push_constant) uniform PushConstants {
    mat4 invViewProj; // Inverse of the scene camera's view * projection.
    mat4 viewProj;    // The same matrix, used to re-derive gl_FragDepth.
} pc;

layout(location = 0) in vec2 inNdcXY;

layout(location = 0) out vec4 outColor;

const float kMinorCellSize = 1.0;   // World units between minor lines.
const float kMajorCellSize = 10.0;  // World units between major lines.
const float kFadeDistance = 100.0;  // World units at which the grid reaches zero alpha.
const float kAxisLineHalfWidthWorld = 0.03; // World-unit half-width of the X/Z axis lines.
const vec3 kMinorLineColor = vec3(0.5, 0.5, 0.5);
const vec3 kMajorLineColor = vec3(0.8, 0.8, 0.8);
const vec3 kXAxisColor = vec3(0.85, 0.16, 0.16); // Red: world X axis (Z == 0 line).
const vec3 kZAxisColor = vec3(0.20, 0.35, 0.90); // Blue: world Z axis (X == 0 line).
const float kMinorLineMaxAlpha = 0.55;
const float kMajorLineMaxAlpha = 0.85;
const float kAxisLineMaxAlpha = 0.95;

// Grid colors are authored as display values; the scene target is linear.
vec3 SrgbToLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

// Grid-line coverage, 0 = no line, 1 = on a line; the line width follows the screen-space derivative.
float GridLineCoverage(vec2 worldXZ, float cellSize)
{
    vec2 coord = worldXZ / cellSize;
    vec2 deriv = max(abs(fwidth(coord)), 1e-6);
    vec2 g = abs(fract(coord - 0.5) - 0.5) / deriv;
    float lineFactor = min(g.x, g.y);
    return 1.0 - clamp(lineFactor, 0.0, 1.0);
}

// Coverage of an axis line with a fixed world-space half-width.
float AxisLineCoverage(float distanceFromAxis, float derivative)
{
    float d = max(abs(derivative), 1e-6);
    return 1.0 - clamp((distanceFromAxis - kAxisLineHalfWidthWorld) / d, 0.0, 1.0);
}

void main()
{
    // Ray through this pixel, from the near plane to the far plane.
    vec4 nearH = pc.invViewProj * vec4(inNdcXY, 0.0, 1.0);
    vec4 farH = pc.invViewProj * vec4(inNdcXY, 1.0, 1.0);
    if (abs(nearH.w) < 1e-6 || abs(farH.w) < 1e-6) {
        discard;
    }
    vec3 nearPoint = nearH.xyz / nearH.w;
    vec3 farPoint = farH.xyz / farH.w;
    vec3 rayDir = farPoint - nearPoint;

    if (abs(rayDir.y) < 1e-6) {
        discard; // Ray parallel to the ground plane.
    }

    float t = -nearPoint.y / rayDir.y;
    if (t <= 0.0 || t > 1.0) {
        discard; // Behind the camera, or beyond the far clip plane.
    }

    vec3 worldPos = nearPoint + rayDir * t;

    vec4 clipHit = pc.viewProj * vec4(worldPos, 1.0);
    if (clipHit.w <= 1e-6) {
        discard;
    }
    float ndcDepth = clipHit.z / clipHit.w;
    if (ndcDepth < 0.0 || ndcDepth > 1.0) {
        discard;
    }

    // The near-plane point stands in for the eye position, so no camera push constant is needed.
    float distanceFromCamera = length(worldPos - nearPoint);
    float distanceFade = 1.0 - smoothstep(kFadeDistance * 0.5, kFadeDistance, distanceFromCamera);
    if (distanceFade <= 0.001) {
        discard;
    }

    float minorCoverage = GridLineCoverage(worldPos.xz, kMinorCellSize);
    float majorCoverage = GridLineCoverage(worldPos.xz, kMajorCellSize);

    // Hide minor lines as their cells shrink below about half a pixel (avoids moire).
    vec2 minorDeriv = fwidth(worldPos.xz / kMinorCellSize);
    float minorLod = max(minorDeriv.x, minorDeriv.y);
    float minorFade = clamp(2.0 - minorLod, 0.0, 1.0);
    minorCoverage *= minorFade;

    // Every major line is also a minor line; draw it once.
    minorCoverage *= (1.0 - majorCoverage);

    vec3 gridColor = kMinorLineColor * minorCoverage + kMajorLineColor * majorCoverage;
    float gridAlpha = max(minorCoverage * kMinorLineMaxAlpha, majorCoverage * kMajorLineMaxAlpha);

    vec2 worldDeriv = max(abs(fwidth(worldPos.xz)), 1e-6);
    // The X axis is the line Z == 0, the Z axis is the line X == 0.
    float xAxisCoverage = AxisLineCoverage(abs(worldPos.z), worldDeriv.y);
    float zAxisCoverage = AxisLineCoverage(abs(worldPos.x), worldDeriv.x);

    vec3 finalColor = gridColor;
    float finalAlpha = gridAlpha;

    // Axis lines win over the plain grid where they overlap.
    if (zAxisCoverage > 0.0) {
        finalColor = mix(finalColor, kZAxisColor, zAxisCoverage);
        finalAlpha = max(finalAlpha, zAxisCoverage * kAxisLineMaxAlpha);
    }
    if (xAxisCoverage > 0.0) {
        finalColor = mix(finalColor, kXAxisColor, xAxisCoverage);
        finalAlpha = max(finalAlpha, xAxisCoverage * kAxisLineMaxAlpha);
    }

    finalAlpha *= distanceFade;
    if (finalAlpha <= 0.003) {
        discard;
    }

    gl_FragDepth = ndcDepth;
    outColor = vec4(SrgbToLinear(finalColor), finalAlpha);
}
