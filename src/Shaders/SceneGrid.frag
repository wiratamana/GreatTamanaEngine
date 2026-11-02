#version 450

// Infinite ground grid: a ray-plane hit at Y == 0 per pixel. Lines are about one pixel wide and fade as their spacing
// on screen shrinks, so the grid thins out toward the horizon instead of being cut at a distance.
// No Y flip here: the CPU built the rays from the same matrix the scene geometry used.

// The ray of a pixel at NDC (x, y) is affine in x and y. The CPU computes the terms in double precision, so the hit
// stays exact when the ground is thousands of units beyond the far plane.
layout(push_constant) uniform PushConstants {
    vec4 originBase; // Ray origin (a point on the near plane) at NDC (0, 0).
    vec4 originDx;   // Change of the origin per unit of NDC x.
    vec4 originDy;   // Change of the origin per unit of NDC y.
    vec4 dirBase;    // Ray direction (near point to far point) at NDC (0, 0).
    vec4 dirDx;      // Change of the direction per unit of NDC x.
    vec4 dirDy;      // Change of the direction per unit of NDC y.
    vec4 depthRowZ;  // Row 2 of view * projection, to re-derive gl_FragDepth.
    vec4 depthRowW;  // Row 3 of view * projection.
} pc;

layout(location = 0) in vec2 inNdcXY;

layout(location = 0) out vec4 outColor;

const int kLevelCount = 5;               // Cell sizes 1, 10, 100, 1000, 10000 world units.
const float kBaseCellSize = 1.0;
const float kCellSizeStep = 10.0;
const float kLineHalfWidthPx = 0.35;     // Half width of a grid line on screen.
const float kLineFadeStartPx = 3.0;      // Lines closer than this on screen are invisible.
const float kLineFadeFullPx = 12.0;      // Lines farther apart than this on screen are fully visible.
const float kAxisHalfWidthPx = 0.9;      // Half width of the X and Z axis lines on screen.
const float kAxisHorizonFadeSin = 0.03;  // Axis lines fade out below this sine of the view angle to the ground.
const float kMaxGridDepth = 0.99999;     // Keeps ground beyond the far plane inside the depth range.
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

// Draws `color` with alpha `a` over the accumulated premultiplied color.
void Accumulate(inout vec3 premul, inout float alpha, vec3 color, float a)
{
    premul = color * a + premul * (1.0 - a);
    alpha = a + alpha * (1.0 - a);
}

// Coverage of the lines of one cell size. `footprint` is world units per pixel along x and z.
// Lines are one family per axis, so each family fades by its own spacing in pixels.
float GridLineCoverage(vec2 worldXZ, float cellSize, vec2 footprint)
{
    vec2 cellPx = cellSize / footprint;
    vec2 distPx = abs(fract(worldXZ / cellSize - 0.5) - 0.5) * cellPx;
    vec2 coverage = clamp(kLineHalfWidthPx + 0.5 - distPx, 0.0, 1.0)
        * smoothstep(kLineFadeStartPx, kLineFadeFullPx, cellPx);
    return max(coverage.x, coverage.y);
}

// Coverage of an axis line, a fixed number of pixels wide.
float AxisLineCoverage(float distanceFromAxis, float footprint)
{
    return clamp(kAxisHalfWidthPx + 0.5 - distanceFromAxis / footprint, 0.0, 1.0);
}

void main()
{
    // Ray through this pixel, from the near plane (t = 0) to the far plane (t = 1). Ground beyond t = 1 is still drawn.
    // Every derivative is taken before any discard, so all invocations of a quad run them.
    vec3 origin = pc.originBase.xyz + pc.originDx.xyz * inNdcXY.x + pc.originDy.xyz * inNdcXY.y;
    vec3 rayDir = pc.dirBase.xyz + pc.dirDx.xyz * inNdcXY.x + pc.dirDy.xyz * inNdcXY.y;

    bool valid = abs(rayDir.y) >= 1e-6;
    float t = -origin.y / (valid ? rayDir.y : 1.0);
    valid = valid && t > 0.0;

    vec3 worldPos = origin + rayDir * t;
    vec4 hit = vec4(worldPos, 1.0);
    float ndcDepth = dot(pc.depthRowZ, hit) / max(dot(pc.depthRowW, hit), 1e-6);
    vec2 footprint = max(fwidth(worldPos.xz), vec2(1e-6));

    vec3 premul = vec3(0.0);
    float alpha = 0.0;
    float cellSize = kBaseCellSize;
    for (int level = 0; level < kLevelCount; ++level) {
        bool finest = (level == 0);
        float a = GridLineCoverage(worldPos.xz, cellSize, footprint)
            * (finest ? kMinorLineMaxAlpha : kMajorLineMaxAlpha);
        Accumulate(premul, alpha, finest ? kMinorLineColor : kMajorLineColor, a);
        cellSize *= kCellSizeStep;
    }

    // The X axis is the line Z == 0, the Z axis is the line X == 0. The X axis is drawn last, so it wins at the origin.
    float horizonFade = smoothstep(0.0, kAxisHorizonFadeSin, abs(rayDir.y) / max(length(rayDir), 1e-6));
    float axisAlpha = kAxisLineMaxAlpha * horizonFade;
    Accumulate(premul, alpha, kZAxisColor, AxisLineCoverage(abs(worldPos.x), footprint.x) * axisAlpha);
    Accumulate(premul, alpha, kXAxisColor, AxisLineCoverage(abs(worldPos.z), footprint.y) * axisAlpha);

    if (!valid || alpha <= 0.003) {
        discard;
    }

    gl_FragDepth = min(ndcDepth, kMaxGridDepth);
    outColor = vec4(SrgbToLinear(premul / alpha), alpha);
}
