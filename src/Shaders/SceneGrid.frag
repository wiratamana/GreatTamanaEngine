#version 450

// Fragment shader for the Editor's "Scene" panel infinite ground grid -
// see SceneGrid.vert's own header comment, and
// task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md /
// PHASE1_GRID_MATH_FOUNDATION.md for the full design. This is the GPU
// mirror of src/Editor/SceneGridMath.cpp's ComputeGridPlaneHit()/
// ComputeGridLineCoverage() - keep the two in lockstep; if this shader's
// math ever needs to change, update SceneGridMath.cpp FIRST, prove it with
// a test, then port the change here (see that module's own file comment
// for the full "CPU math is the spec" rationale, mirroring how this
// engine already treats Animation/VertexSkinning.cpp as the permanent
// oracle for its own GPU compute-skinning mirror).
//
// SELF-CONSISTENCY NOTE (do not "fix" this by adding a Y-flip): this
// shader's own vertex stage (SceneGrid.vert) synthesizes gl_Position
// directly from a raw NDC full-screen triangle, with NO camera/projection
// matrix involved. Whatever physical screen pixel Vulkan's rasterizer maps
// a given NDC (x, y) to, THIS shader's own interpolated inNdcXY varying is
// GUARANTEED to carry that exact same (x, y) value at that exact pixel -
// entirely by construction, independent of Vulkan's Y-down-vs-OpenGL's
// Y-up clip-space convention. Since invViewProj/viewProj below are the
// exact inverse/forward pair of the REAL scene camera's own projection
// (which DOES bake in a flipY - see EditorCamera::Projection()), unprojecting
// THIS shader's inNdcXY through invViewProj already reconstructs the
// correct world-space ray through the SAME physical pixel the real scene
// geometry was drawn to. Adding any extra flip here would double-correct
// and mirror the grid vertically relative to the real geometry it must
// line up with.

layout(push_constant) uniform PushConstants {
    mat4 invViewProj; // Inverse of the Scene camera's own combined view * projection matrix.
    mat4 viewProj;    // The exact same matrix, non-inverted - used to re-derive gl_FragDepth.
} pc;

layout(location = 0) in vec2 inNdcXY;

layout(location = 0) out vec4 outColor;

// --- Tunable constants (kept in one place, at the top, for PHASE5's own
// tuning pass - see PHASE5_POLISH_AND_VERIFICATION.md) ---------------------
const float kMinorCellSize = 1.0;   // World units - Unity's own default minor grid spacing.
const float kMajorCellSize = 10.0;  // World units - Unity's own default major grid spacing.
const float kFadeDistance = 100.0;  // World units - distance at which the whole grid reaches zero alpha.
const float kAxisLineHalfWidthWorld = 0.03; // World-unit half-width of the colored X/Z axis lines' core.
const vec3 kMinorLineColor = vec3(0.5, 0.5, 0.5);
const vec3 kMajorLineColor = vec3(0.8, 0.8, 0.8);
const vec3 kXAxisColor = vec3(0.85, 0.16, 0.16); // Red - world X axis (Z == 0 line).
const vec3 kZAxisColor = vec3(0.20, 0.35, 0.90); // Blue - world Z axis (X == 0 line).
const float kMinorLineMaxAlpha = 0.55;
const float kMajorLineMaxAlpha = 0.85;
const float kAxisLineMaxAlpha = 0.95;

// Anti-aliased grid-line coverage (0 = no line, 1 = fully on a line) - the
// GLSL mirror of SceneGridMath.cpp's ComputeGridLineCoverage(). fwidth()
// is GLSL's real screen-space-derivative primitive - there is no literal
// CPU equivalent, which is exactly why PHASE1's C++ port takes the
// derivative as a plain, already-computed parameter instead.
float GridLineCoverage(vec2 worldXZ, float cellSize)
{
    vec2 coord = worldXZ / cellSize;
    vec2 deriv = max(abs(fwidth(coord)), 1e-6);
    vec2 g = abs(fract(coord - 0.5) - 0.5) / deriv;
    float lineFactor = min(g.x, g.y);
    return 1.0 - clamp(lineFactor, 0.0, 1.0);
}

// Anti-aliased single-axis-line coverage - the GLSL mirror of
// SceneGridMath.cpp's ComputeAxisLineCoverage() (PHASE1_GRID_MATH_FOUNDATION.md
// - added to that file during this document's own second-iteration review,
// see that file's own history). For the colored X/Z axis highlight lines,
// which have a fixed WORLD-SPACE half-width rather than being derived from
// a cell size like the grey grid lines above.
float AxisLineCoverage(float distanceFromAxis, float derivative)
{
    // abs(derivative) mirrors ComputeAxisLineCoverage()'s own std::fabs()
    // exactly (a real fwidth() result is always non-negative already, so
    // this is a no-op for actual on-GPU values - see that CPU function's
    // own comment for why it still guards against a negative input).
    float d = max(abs(derivative), 1e-6);
    return 1.0 - clamp((distanceFromAxis - kAxisLineHalfWidthWorld) / d, 0.0, 1.0);
}

void main()
{
    // --- Ray-plane intersection (mirrors SceneGridMath.cpp::ComputeGridPlaneHit()) ---
    vec4 nearH = pc.invViewProj * vec4(inNdcXY, 0.0, 1.0);
    vec4 farH = pc.invViewProj * vec4(inNdcXY, 1.0, 1.0);
    if (abs(nearH.w) < 1e-6 || abs(farH.w) < 1e-6) {
        discard;
    }
    vec3 nearPoint = nearH.xyz / nearH.w;
    vec3 farPoint = farH.xyz / farH.w;
    vec3 rayDir = farPoint - nearPoint;

    if (abs(rayDir.y) < 1e-6) {
        discard; // Ray parallel to the ground plane - looking level with the horizon.
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

    // --- Distance fade -------------------------------------------------------
    // nearPoint is a point on the near clip plane along this pixel's own
    // ray, which is an extremely close (near-plane-distance-off, entirely
    // negligible for a visual fade) stand-in for the real camera eye
    // position - see PHASE0_MASTER_STRATEGY.md for why this avoids ever
    // needing to pass an explicit camera-position push constant at all.
    float distanceFromCamera = length(worldPos - nearPoint);
    float distanceFade = 1.0 - smoothstep(kFadeDistance * 0.5, kFadeDistance, distanceFromCamera);
    if (distanceFade <= 0.001) {
        discard;
    }

    // --- Grid line pattern (two LOD levels) -----------------------------------
    float minorCoverage = GridLineCoverage(worldPos.xz, kMinorCellSize);
    float majorCoverage = GridLineCoverage(worldPos.xz, kMajorCellSize);

    // Fade the MINOR grid out as its own cells shrink toward sub-pixel size
    // on screen (avoids moire/aliasing noise when zoomed far out) - a
    // standard heuristic: once one minor cell's on-screen footprint
    // (fwidth of its own coordinate space) exceeds ~2 cells per pixel, fully
    // hide it.
    vec2 minorDeriv = fwidth(worldPos.xz / kMinorCellSize);
    float minorLod = max(minorDeriv.x, minorDeriv.y);
    float minorFade = clamp(2.0 - minorLod, 0.0, 1.0);
    minorCoverage *= minorFade;

    // Never double-draw a minor line exactly where a major line already
    // covers the same pixel (every major gridline is also a minor
    // gridline, since 10 is a multiple of 1).
    minorCoverage *= (1.0 - majorCoverage);

    vec3 gridColor = kMinorLineColor * minorCoverage + kMajorLineColor * majorCoverage;
    float gridAlpha = max(minorCoverage * kMinorLineMaxAlpha, majorCoverage * kMajorLineMaxAlpha);

    // --- Colored X/Z axis highlight lines --------------------------------------
    vec2 worldDeriv = max(abs(fwidth(worldPos.xz)), 1e-6);
    // World X axis is the line where Z == 0 (runs along X).
    float xAxisCoverage = AxisLineCoverage(abs(worldPos.z), worldDeriv.y);
    // World Z axis is the line where X == 0 (runs along Z).
    float zAxisCoverage = AxisLineCoverage(abs(worldPos.x), worldDeriv.x);

    vec3 finalColor = gridColor;
    float finalAlpha = gridAlpha;

    // Axis lines take priority over the plain grid wherever they overlap.
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
    outColor = vec4(finalColor, finalAlpha);
}
