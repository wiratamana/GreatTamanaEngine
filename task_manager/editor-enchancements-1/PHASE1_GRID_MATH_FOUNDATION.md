# PHASE1_GRID_MATH_FOUNDATION — Pure, Tier-1-Testable Ray-Plane Grid Math

> Parent: `PHASE0_MASTER_STRATEGY.md` — read it first for the full goal,
> the rejected-design rationale, and the locked design decisions this phase
> depends on.

## Step 1: The Goal (Where are we going?)

Produce a small, pure, `Vulkan`/`ImGui`-free C++ module,
`src/Editor/SceneGridMath.h` / `.cpp`, implementing:

1. The exact ray-plane-intersection algorithm the GPU fragment shader
   (Phase 2) will mirror — "given this pixel's NDC (x, y) and the Scene
   camera's inverse-view-projection/view-projection matrices, where does
   the camera ray through this pixel hit the world `Y = 0` plane, and what
   depth value should be written for it?"
2. The exact anti-aliased grid-line-coverage math the same shader will
   mirror — "given a world-space (x, z) position and a screen-space
   derivative, how strongly does this pixel sit on a grid line of a given
   cell size?"
3. The exact anti-aliased AXIS-line-coverage math the same shader's colored
   X/Z axis highlight lines will mirror — "given a world-space distance from
   an axis and a screen-space derivative, how strongly does this pixel sit
   on a fixed-half-width axis line?" (this third function was added during
   this document's own second-iteration review specifically so the axis
   lines have the same CPU-side, hand-checkable oracle as the plain grid
   lines do — see PHASE5_POLISH_AND_VERIFICATION.md's own note on why this
   used to be deferred and is no longer).

All three functions must be genuinely Tier-1-testable (see `AGENTS.md`,
"Testability & Regression Safety") — no live `VkDevice`, no ImGui, no
`Renderer`, following the exact same precedent `src/Editor/EditorCamera.h`
already established (a pure-math class living under `src/Editor/` despite
that whole folder being `GTE_ENABLE_EDITOR`-gated at compile time — see
`tests/Editor/EditorCameraTests.cpp`).

This C++ code is **not** what actually draws the grid at runtime (a real
GPU fragment shader has no way to call into engine C++) — it exists purely
as the reviewable, hand-checkable, unit-tested "spec" Phase 2's GLSL must
follow byte-for-byte, exactly the same relationship
`Animation/VertexSkinning.cpp`'s CPU skinning code has with its GPU compute
mirror (`Shaders/SkinVerticesPositionNormal.comp` — see `AGENTS.md`, "GPU
Vertex Skinning": *"the CPU path is the permanent ORACLE... If the two paths
ever disagree, the CPU path is right by definition and the GPU kernel is the
one that needs fixing"*). The same rule applies here: if `SceneGrid.frag`
(Phase 2) ever needs to change its math, update `SceneGridMath.cpp` FIRST,
prove it with a test, then port the change into the shader.

## Step 2: The Situation / The Problem (Where are we now?)

- `src/Math/Mat4.h` already provides every primitive this needs:
  - `Mat4::TryInverse(Mat4& outInverse) const noexcept` — non-asserting,
    returns `false` on a singular matrix (use this, never the asserting
    `Inverse()`, for anything derived from live camera state that could
    theoretically become singular).
  - `Vec4 operator*(const Mat4& m, const Vec4& v) noexcept` — the
    **projective** multiply (no perspective divide performed). **This is
    the one to use.** `Mat4::TransformPoint()` is explicitly documented as
    assuming `w == 1` going in and NOT performing/returning a perspective
    divide — using it here would silently produce wrong results for a
    general inverse-view-projection matrix (whose output `w` is never `1`
    in general). Do not use `TransformPoint()`/`TransformVector()` anywhere
    in this module.
  - `Mat4::Data()` — contiguous column-major `float[16]`, matching a GLSL
    `mat4` exactly (see `Renderer::Submit()`'s own push-constant memcpy
    convention in `FrameRecorder.cpp` for the precedent this module's own
    future caller, `SceneGridRenderer` in Phase 3, will follow).
- `src/Math/Vec4.h`/`Vec3.h`/`Vec2.h` provide plain `x`/`y`/`z`/`w` fields
  and the usual `operator+`/`operator-`/`operator*(float)` — check the
  exact constructor signature in `Vec4.h` before use (`Vec4(x, y, z, w)`).
- `src/Math/MathTypes.h` provides `kEpsilon` (this engine's shared
  floating-point tolerance constant, already used throughout — e.g.
  `RigidBodyWireframe.cpp`) — use this, never a locally-invented epsilon
  literal.
- No existing file in this engine does an inverse-view-projection
  "unproject a screen pixel back into world space" computation anywhere
  yet — this is new math for the engine, not a refactor of anything
  existing.

## Step 3: The Plan

### 3.1 — Create `src/Editor/SceneGridMath.h`

```cpp
#pragma once

#include "../Math/Mat4.h"
#include "../Math/Vec2.h"
#include "../Math/Vec3.h"

namespace gte {

// The result of casting the Scene camera's ray through one screen pixel
// (in NDC space) against the world's Y = 0 ground plane - the pure CPU
// mirror of the per-pixel computation SceneGrid.frag (see
// PHASE2_GRID_SHADERS.md) performs on the GPU. See this header's own file
// comment in SceneGridMath.cpp for why the two must be kept in lockstep,
// and PHASE0_MASTER_STRATEGY.md's "Locked Design Decisions" for why this
// is computed via a raw ray-plane intersection rather than any real 3D
// geometry.
struct GridPlaneHit {
    // False whenever this pixel's camera ray never meaningfully hits the
    // ground plane in front of the camera and within the view frustum's
    // own depth range - a ray parallel to the plane (looking exactly
    // level with the horizon), a plane behind the camera, or a plane
    // intersection beyond the far clip plane. A caller (the shader)
    // discards the pixel entirely whenever this is false - see
    // SceneGrid.frag.
    bool valid = false;

    // World-space position of the intersection - only meaningful when
    // valid == true.
    Vec3 worldPosition{};

    // The exact NDC depth ([0, 1], matching this engine's Vulkan
    // zero-to-one depth range - see Mat4::PerspectiveFovLH_ZO) this
    // intersection point re-projects to through the SAME viewProj matrix
    // that was used to unproject it - what the shader writes to
    // gl_FragDepth so the grid is correctly depth-TESTED (never
    // depth-WRITTEN - see PHASE0_MASTER_STRATEGY.md's own "Depth
    // handling" design decision) against real scene geometry. Only
    // meaningful when valid == true.
    float ndcDepth = 1.0f;
};

// Computes GridPlaneHit for the camera ray through NDC screen position
// `ndcXY` (each component in [-1, 1], Vulkan's own raw clip-space
// convention - NOT flipped/adjusted here in any way; see
// PHASE2_GRID_SHADERS.md's own header comment for why no Y-flip is ever
// needed anywhere in this feature). `invViewProj` MUST be the caller's own
// Mat4::TryInverse() result of `viewProj` (the exact SAME matrix,
// inverted) - passing a mismatched pair produces meaningless output with
// no way for this function to detect it.
GridPlaneHit ComputeGridPlaneHit(const Mat4& invViewProj, const Mat4& viewProj, Vec2 ndcXY) noexcept;

// Anti-aliased grid-line coverage in [0, 1] (0 = fully off a line, 1 =
// fully on one) for a square grid of `cellSize` world units, at world-space
// position (worldX, worldZ), given the screen-space derivative of that
// same world position along each axis (derivativeX/derivativeZ - the
// fwidth()-equivalent inputs; a real GPU call site supplies these via
// fwidth(), which has no literal CPU equivalent - see this function's own
// definition comment in SceneGridMath.cpp for why a plain float parameter
// is still exactly the right, fully-testable shape here). Returns 0.0f for
// a non-positive cellSize - never NaN/divide-by-zero garbage.
float ComputeGridLineCoverage(
    float worldX, float worldZ, float cellSize, float derivativeX, float derivativeZ) noexcept;

// Anti-aliased single-axis-line coverage in [0, 1] (0 = fully off the axis
// line, 1 = fully on it) for a Unity-style colored X/Z axis highlight line -
// the pure CPU mirror of SceneGrid.frag's AxisLineCoverage() (Phase 2).
// Unlike ComputeGridLineCoverage() above (whose line spacing comes from a
// cell size), a single axis line has a fixed WORLD-SPACE half-width
// (`halfWidthWorld`) instead: `distanceFromAxis` is the absolute world-space
// distance from the axis itself (e.g. abs(worldPos.z) for the X axis, which
// runs along Z == 0), and `derivative` is the screen-space derivative of
// that same distance (the fwidth()-equivalent input, same shape as
// ComputeGridLineCoverage()'s own derivativeX/derivativeZ parameters).
// Returns 0.0f for a non-positive halfWidthWorld - never NaN/Inf.
float ComputeAxisLineCoverage(float distanceFromAxis, float derivative, float halfWidthWorld) noexcept;

} // namespace gte
```

### 3.2 — Create `src/Editor/SceneGridMath.cpp`

```cpp
#include "SceneGridMath.h"

#include "../Math/MathTypes.h" // kEpsilon
#include "../Math/Vec4.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

// Unprojects NDC (ndcX, ndcY, ndcZ) through invViewProj INCLUDING the
// perspective divide - deliberately using the raw Vec4 projective multiply
// (Mat4's own `operator*(const Mat4&, const Vec4&)`), NEVER
// Mat4::TransformPoint()/TransformVector() (both of those assume affine
// input/output and never divide by the resulting w - see this module's
// own header comment for why that would silently be wrong here).
Vec3 UnprojectNdc(const Mat4& invViewProj, float ndcX, float ndcY, float ndcZ) noexcept
{
    const Vec4 clip(ndcX, ndcY, ndcZ, 1.0f);
    const Vec4 world = invViewProj * clip;
    if (std::fabs(world.w) < kEpsilon) {
        // Degenerate (a genuinely singular/degenerate invViewProj, or a
        // clip position exactly at the camera's own eye) - the caller
        // (ComputeGridPlaneHit) is expected to still produce a sane
        // "invalid" result from whatever this returns; returning the
        // origin here is an arbitrary-but-safe (never NaN/Inf) fallback.
        return Vec3::Zero();
    }
    const float invW = 1.0f / world.w;
    return Vec3(world.x * invW, world.y * invW, world.z * invW);
}

// x - floor(x): GLSL's fract() semantics exactly (always in [0, 1), even
// for negative x) - deliberately NOT std::fmod(), whose sign convention
// differs for negative inputs and would silently break the grid pattern's
// symmetry across the world origin.
float Frac(float x) noexcept
{
    return x - std::floor(x);
}

} // namespace

GridPlaneHit ComputeGridPlaneHit(const Mat4& invViewProj, const Mat4& viewProj, Vec2 ndcXY) noexcept
{
    // Two points along this pixel's camera ray: one on the near plane
    // (NDC z = 0), one on the far plane (NDC z = 1) - Vulkan's own
    // zero-to-one clip-space depth convention (see Mat4::PerspectiveFovLH_ZO).
    const Vec3 nearPoint = UnprojectNdc(invViewProj, ndcXY.x, ndcXY.y, 0.0f);
    const Vec3 farPoint = UnprojectNdc(invViewProj, ndcXY.x, ndcXY.y, 1.0f);
    const Vec3 rayDir = farPoint - nearPoint;

    // The ray is (numerically) parallel to the Y = 0 plane - e.g. looking
    // exactly level with the horizon. No meaningful single intersection
    // point exists.
    if (std::fabs(rayDir.y) < kEpsilon) {
        return GridPlaneHit{};
    }

    // Solve nearPoint.y + t * rayDir.y == 0 for t. t == 0 is the near
    // plane, t == 1 is the far plane - t outside (0, 1] means the
    // intersection is either behind the camera (t <= 0) or beyond the far
    // clip plane (t > 1), neither of which this pixel should ever draw a
    // grid fragment for.
    const float t = -nearPoint.y / rayDir.y;
    if (t <= 0.0f || t > 1.0f) {
        return GridPlaneHit{};
    }

    const Vec3 worldPos = nearPoint + rayDir * t;

    // Re-project worldPos through the ORIGINAL viewProj (not invViewProj)
    // to get the exact NDC depth a real rasterized quad at this exact
    // world position would have produced - this is what
    // SceneGridRenderer/SceneGrid.frag writes to gl_FragDepth (see
    // PHASE0_MASTER_STRATEGY.md's "Depth handling" decision).
    const Vec4 clipHit = viewProj * Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    if (clipHit.w <= kEpsilon) {
        return GridPlaneHit{}; // Behind the camera in projective terms - reject.
    }
    const float ndcDepth = clipHit.z / clipHit.w;
    if (ndcDepth < 0.0f || ndcDepth > 1.0f) {
        // Should be structurally impossible given the t-range check above
        // (both derivations describe the same physical constraint), but
        // guarded explicitly anyway - never write an out-of-range
        // gl_FragDepth (undefined per the Vulkan spec without
        // VK_EXT_depth_range_unrestricted, which this engine does not use).
        return GridPlaneHit{};
    }

    GridPlaneHit result;
    result.valid = true;
    result.worldPosition = worldPos;
    result.ndcDepth = ndcDepth;
    return result;
}

float ComputeGridLineCoverage(float worldX, float worldZ, float cellSize, float derivativeX, float derivativeZ) noexcept
{
    if (cellSize <= kEpsilon) {
        return 0.0f;
    }

    const float coordX = worldX / cellSize;
    const float coordZ = worldZ / cellSize;

    // Never let a zero/near-zero derivative (e.g. a synthetic test value)
    // produce a divide-by-near-zero explosion - clamps the effective line
    // width to at most one full cell, which is a harmless, sane fallback
    // rather than Inf/NaN.
    const float dX = std::max(std::fabs(derivativeX / cellSize), kEpsilon);
    const float dZ = std::max(std::fabs(derivativeZ / cellSize), kEpsilon);

    // Classic "pristine grid" line-coverage formula: distance (in
    // fractional-cell units) from the NEAREST cell boundary, divided by
    // the pixel's own on-screen footprint in that same unit - <= 1 means
    // this pixel's footprint already spans the line, i.e. full coverage.
    const float gx = std::fabs(Frac(coordX - 0.5f) - 0.5f) / dX;
    const float gz = std::fabs(Frac(coordZ - 0.5f) - 0.5f) / dZ;
    const float lineFactor = std::min(gx, gz);

    return 1.0f - std::clamp(lineFactor, 0.0f, 1.0f);
}

float ComputeAxisLineCoverage(float distanceFromAxis, float derivative, float halfWidthWorld) noexcept
{
    if (halfWidthWorld <= 0.0f) {
        return 0.0f;
    }

    // Same "never let a near-zero derivative explode the divide" clamp
    // ComputeGridLineCoverage() above already applies - see its own
    // comment. std::fabs() here (rather than a bare max(derivative,
    // kEpsilon) the way SceneGrid.frag's own GLSL mirror can get away with)
    // is what keeps this CPU function well-defined even for a synthetic
    // test value that happens to pass a negative derivative - a real
    // fwidth() call site (see PHASE2_GRID_SHADERS.md) only ever produces a
    // non-negative value, so this never changes real, on-GPU behavior.
    const float d = std::max(std::fabs(derivative), kEpsilon);
    const float factor = (distanceFromAxis - halfWidthWorld) / d;
    return 1.0f - std::clamp(factor, 0.0f, 1.0f);
}

} // namespace gte
```

**Verify `Vec4.h`'s exact constructor/member names before pasting this
code** (`x`/`y`/`z`/`w`, and a 4-argument constructor) — adapt trivially if
the real header spells anything differently; the algorithm itself must stay
identical.

### 3.3 — Register the new files in `CMakeLists.txt`

Inside the existing `if(GTE_ENABLE_EDITOR)` `target_sources(gte_core
PRIVATE ...)` block, immediately after the existing
`src/Editor/EditorCamera.cpp` line (see that block's own comment header for
why this whole block is where every Editor-only-but-otherwise-pure file
belongs):

```cmake
        src/Editor/EditorCamera.h
        src/Editor/EditorCamera.cpp
        src/Editor/SceneGridMath.h
        src/Editor/SceneGridMath.cpp
```

### 3.4 — Create `tests/Editor/SceneGridMathTests.cpp`

Follow `tests/Editor/EditorCameraTests.cpp`'s own structure/conventions
(plain GoogleTest, no fixture needed for pure functions like these). Cover
at minimum:

- `ComputeGridPlaneHit`:
  - A camera looking straight down at the origin from `(0, 10, 0)` with an
    identity-ish orientation: the NDC-center ray (`ndcXY = (0, 0)`) must hit
    very close to world `(0, 0, 0)` with `valid == true`.
  - A camera looking exactly level with the horizon (view direction with
    `y == 0`) must produce `valid == false` for every NDC position (the
    "ray parallel to the plane" case).
  - A ray that would only hit the plane BEHIND the camera (e.g. camera
    below the plane looking further down/away) must produce
    `valid == false`.
  - A round-trip check: for a hit with `valid == true`, re-projecting
    `worldPosition` by hand through the same `viewProj` used in the call
    must reproduce `ndcXY` (within `kEpsilon`) — this is the single most
    important regression this test file guards, since it is exactly the
    computation the shader depends on being self-consistent.
- `ComputeGridLineCoverage`:
  - At a cell boundary (`worldX` an exact multiple of `cellSize`), coverage
    must be near `1.0` (on a line).
  - At a cell center (`worldX` a multiple of `cellSize` plus half a cell),
    coverage must be near `0.0` (not on a line).
  - `cellSize <= 0` must return exactly `0.0f`, never NaN/Inf.
  - A very large derivative (simulating "zoomed far out, this cell is
    sub-pixel") must still return a finite value in `[0, 1]`.
- `ComputeAxisLineCoverage`:
  - `distanceFromAxis` well inside `halfWidthWorld` (e.g. `0.0`) must return
    exactly `1.0` (fully on the axis line's solid core).
  - `distanceFromAxis` several `derivative`-widths beyond `halfWidthWorld`
    must return near `0.0` (well clear of the line).
  - `halfWidthWorld <= 0` must return exactly `0.0f`, never NaN/Inf.
  - A negative `derivative` (a synthetic-test-only input — real `fwidth()`
    values are always non-negative, see this function's own header comment)
    must not change the result compared to the same value's absolute
    magnitude — this is the specific regression the `std::fabs()` in this
    function's own implementation guards.

### 3.5 — `Definition of Done` for this phase

- `SceneGridMath.h`/`.cpp` compile cleanly as part of `gte_core` (an
  `GTE_ENABLE_EDITOR=ON` configure/build).
- `tests/Editor/SceneGridMathTests.cpp` is added to
  `GTE_TEST_SOURCES` and every new test passes.
- Run the fast compile check (build `gte_core` and
  `GreatTamanaEngineTests`, then run the new test file's cases) — no full
  regression suite run needed yet at this phase (nothing else in the engine
  references this new module yet).
- Nothing else in the engine calls `SceneGridMath.h` yet — that's expected;
  Phase 3 is its first real consumer.
