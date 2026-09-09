# PHASE2_GRID_SHADERS — The Procedural GLSL Grid Shader Pair

> Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: `PHASE1_GRID_MATH_FOUNDATION.md`
> (this phase's GLSL must implement the EXACT same algorithm Phase 1's C++
> already implements and tests — treat that file as the spec).

## Step 1: The Goal (Where are we going?)

Write the two GLSL shader files that are the actual, real-time GPU
implementation of the grid — `src/Shaders/SceneGrid.vert` and
`src/Shaders/SceneGrid.frag` — compiled to SPIR-V by the existing
`cmake/CompileShaders.cmake`/`gte_add_shader()` machinery, exactly like
every other shader pair in this engine. This phase produces **shader source
files and their CMake registration only** — no C++ calls into them yet
(that is Phase 3/4's job); a successful `glslc` compile (verified via a
normal `cmake --build` of the `GreatTamanaEngine` target, which triggers
every registered `gte_add_shader()` custom command) is this phase's own
compile check.

## Step 2: The Situation / The Problem (Where are we now?)

- Every existing shader in this engine lives flat under `src/Shaders/`
  (`Triangle.vert/frag`, `Mesh.vert/frag`, `MeshPreview.vert/frag`,
  `TexturedMesh.vert/frag`, `BoxBlur.comp`, ...) and is registered via
  `gte_add_shader(GreatTamanaEngine src/Shaders/<Name>.<stage>)` in the root
  `CMakeLists.txt` — `glslc` infers the shader stage from the file
  extension automatically (already relied upon by the compute-shader
  campaign — see `AGENTS.md` cross-references). `MeshPreview.vert/frag`
  (compiled/staged **only** when `GTE_ENABLE_EDITOR`... actually
  `GTE_ENABLE_PROJECT_PANEL`, see `CMakeLists.txt` lines ~727-730) is the
  closest existing precedent for "an Editor-only shader pair gated behind a
  CMake switch" — this feature's own gate is `GTE_ENABLE_EDITOR` directly
  (since `SceneGridRenderer`, Phase 3, is compiled unconditionally whenever
  the Editor is, unlike `AssetPreviewMesh`, which additionally needs
  `GTE_ENABLE_PROJECT_PANEL`).
- This engine's Vulkan clip-space convention: `Mat4::PerspectiveFovLH_ZO`
  produces a **zero-to-one depth range** (matching Vulkan, not OpenGL's
  `[-1, 1]`), and `flipY = true` (used by every real in-game camera
  projection, including the Editor's own `EditorCamera::Projection()`)
  bakes in a Y-axis flip to compensate for Vulkan's own NDC-to-screen
  convention (`gl_Position.y` more negative = toward the TOP of the
  framebuffer — the opposite of OpenGL).
- **Critical, easy-to-get-wrong point for this shader specifically:** the
  grid's own vertex shader does **not** go through any camera/projection
  matrix at all — it synthesizes `gl_Position` directly as a raw NDC
  full-screen triangle. Because of this, the grid's fragment shader must
  **not** apply any additional Y-flip logic when reconstructing the
  world-space ray from that same NDC value — see this document's own
  "Self-consistency, no extra flip needed" note in Step 3.2 below. Getting
  this wrong (e.g. "helpfully" negating `ndc.y` before unprojecting, by
  analogy with `Camera::ProjectionMatrix()`'s own `flipY`) would make the
  grid appear vertically mirrored relative to the real scene geometry it's
  supposed to align with.
- `EditorCamera::ViewProjection(aspect)` (`src/Editor/EditorCamera.h`) is
  exactly the `viewProj` matrix this feature's `invViewProj`/`viewProj` push
  constants must be built from (see Phase 4 for exactly where this value
  comes from at the real call site) — it already has `flipY = true` baked
  in (via `Projection()`), so `invViewProj = Inverse(ViewProjection(aspect))`
  is the exact, correct inverse of what the real scene was rendered with.

## Step 3: The Plan

### 3.1 — `src/Shaders/SceneGrid.vert`

```glsl
#version 450

// Vertex shader for the Editor's "Scene" panel infinite ground grid (see
// task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md). Unlike
// every other vertex shader in this engine, this one has NO vertex input
// at all - src/Editor/SceneGridRenderer.cpp (PHASE3) issues a bare
// vkCmdDraw(3, 1, 0, 0) with no bound vertex/index buffer. This is the
// classic "full-screen triangle from gl_VertexIndex alone" trick: 3
// vertices whose NDC positions cover the entire [-1, 1] clip-space square
// with a single triangle (the far corner sits safely outside the visible
// area, which is fine - it's clipped/rasterized normally). All of the
// actual grid math happens in SceneGrid.frag; this file exists purely to
// hand it a per-pixel NDC (x, y) to work from. Compiled to SPIR-V at build
// time by cmake/CompileShaders.cmake (glslc) - never committed as a
// binary, see .gitignore.

layout(location = 0) out vec2 outNdcXY;

void main()
{
    // gl_VertexIndex in {0, 1, 2} -> ndc in {(-1,-1), (3,-1), (-1,3)} - the
    // standard derivation: ((i << 1) & 2, i & 2) yields (0,0), (2,0),
    // (0,2) for i = 0, 1, 2; scaling by 2 and subtracting 1 maps that to
    // (-1,-1), (3,-1), (-1,3).
    vec2 ndc = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    outNdcXY = ndc;
}
```

### 3.2 — `src/Shaders/SceneGrid.frag`

```glsl
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
# line up with.

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
```

Note (corrected during this document's own second-iteration review — the
original note below only mentioned ONE of these and an implementer copying
this snippet verbatim would have shipped a shader that fails to compile):
there are TWO stray `#` typos in the `SceneGrid.frag` snippet above, both of
which MUST become a clean `//` continuation in the real file — (1) `# line
up with.` in the file's own header comment, and (2) `# being derived from a
cell size like the grey grid lines above).` in `AxisLineCoverage()`'s own
doc comment immediately above its definition. GLSL line comments are `//`/
`/* */` only; a stray `#` at the start of a line is parsed as a (here,
invalid/unrecognized) preprocessor directive and will fail to compile, not
silently get treated as a comment. Proofread the WHOLE file for this exact
mistake when creating it — do not assume only one instance exists just
because only one used to be called out here.

### 3.3 — Register both shaders in the root `CMakeLists.txt`

Add a new block immediately after the existing `if(GTE_ENABLE_PROJECT_PANEL)`
block that compiles `MeshPreview.vert`/`.frag` (see the surrounding
`gte_add_shader(...)` calls around line 727-730):

```cmake
# The Editor's "Scene" panel infinite ground grid (Unity-style procedural
# shader grid - see task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md).
# Only compiled/staged when GTE_ENABLE_EDITOR is ON, since
# src/Editor/SceneGridRenderer.cpp (PHASE3) - the only thing that ever
# loads these - isn't compiled into gte_core otherwise (unlike
# MeshPreview.vert/frag above, this does NOT additionally need
# GTE_ENABLE_PROJECT_PANEL - SceneGridRenderer has no dependency on the
# Project panel at all).
if(GTE_ENABLE_EDITOR)
    gte_add_shader(GreatTamanaEngine src/Shaders/SceneGrid.vert)
    gte_add_shader(GreatTamanaEngine src/Shaders/SceneGrid.frag)
endif()
```

### 3.4 — `Definition of Done` for this phase

- Both shader files exist under `src/Shaders/`.
- A normal `cmake --build build` (Ninja/MinGW, per this project's own
  `BUILDING.md`) with `GTE_ENABLE_EDITOR=ON` (the default) successfully
  invokes `glslc` on both new files and produces
  `build/shaders/SceneGrid.vert.spv` / `SceneGrid.frag.spv` with **zero**
  `glslc` errors/warnings — this is the fast compile check for this phase;
  no C++ changes exist yet to build/test beyond this.
- Nothing in the engine loads these `.spv` files yet — that's expected;
  Phase 3 is their first real consumer (`SceneGridRenderer::EnsurePipeline()`
  will `ReadShaderFile("shaders/SceneGrid.vert.spv")`, mirroring
  `AssetPreviewMesh::EnsurePipeline()`'s exact existing pattern).
