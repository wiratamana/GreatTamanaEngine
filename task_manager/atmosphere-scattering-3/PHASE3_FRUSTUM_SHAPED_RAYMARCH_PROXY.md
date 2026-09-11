# PHASE3 — Frustum-Shaped Raymarch Proxy (MANDATORY — matches the reference image's widening shape)

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on Phase 2 having landed (this
phase's own camera angle/positioning REUSES Phase 2's already-tuned
side-on viewing angle constants — it only replaces the SHAPE being
intersected/sampled, from an axis-aligned box to a literal tapering
frustum).

**This phase is a MANDATORY deliverable of this campaign (confirmed with the
project owner — see `PHASE0_MASTER_STRATEGY.md`'s own Locked Design
Decisions).** The project owner explicitly wants the literal widening-funnel
shape implemented, matching the reference image
(`aerial-persepective-lut-3d-texture.png`) as closely as reasonably
possible — this is NOT an optional/skippable stretch goal, and Phase 4's own
visual tuning pass assumes this phase's frustum shape is the FINAL shape in
production, not a fallback-to-box scenario.

## Step 1 — The Goal (Where are we going?)

Replace the axis-aligned (even if now depth-elongated, per Phase 2) box
proxy used ONLY for the Aerial Perspective interpretation with a literal
TAPERING FRUSTUM proxy — narrow near the camera-facing end, wide at the far
end — so the rendered preview genuinely WIDENS as it recedes, mirroring both
(a) the reference diagram's own visual language (a fan of increasingly large
quads) and (b) the real, physical shape a camera's view frustum actually has
in the real world (the froxel grid's X/Y extent, in real WORLD units, does
grow proportionally with distance for a fixed field of view — even though
the TEXTURE's own X/Y texel COUNT stays a constant 128x128 at every depth
slice).

**Still zero changes to the Aerial Perspective LUT's own generation/composite
shaders or `AtmosphereMath.h`** — this phase only changes the PREVIEW
renderer's own proxy geometry and sampling-coordinate math.

## Step 2 — The Situation (Where are we now?)

- After Phase 2, `VolumeTexturePreview.comp`'s `IntersectRayBox()` and its
  `uvw = (localPos / pc.boxHalfExtents) * 0.5 + 0.5` sampling formula are
  still being used for the Aerial Perspective case too — just with
  different `boxHalfExtents`/camera numbers than the generic case. A true
  axis-aligned box, no matter how elongated, has a CONSTANT cross-section
  along its long axis — it cannot visually "widen," only "extend."
- `VolumeTexturePreviewMath.h/.cpp`'s existing `IntersectRayBox()` uses the
  classic axis-aligned slab method (component-wise min/max of the two
  per-axis t-ranges). A LINEARLY-tapering frustum (cross-section growing
  proportionally with distance along one axis) has perfectly FLAT side
  walls too (a linear function of one coordinate is still a plane equation)
  — so the exact same "compute a `[tEnter, tExit]` per bounding plane, then
  intersect all of them" strategy generalizes cleanly; it just needs six
  arbitrary (not axis-aligned) half-space planes instead of six axis-aligned
  ones.
- There is no existing `Plane`/`Frustum` math type anywhere in
  `src/Math/` — this phase adds a small, self-contained, purpose-built
  structure directly in `VolumeTexturePreviewMath.h` rather than growing
  `src/Math/`'s own public API for a single, narrow use case.

## Step 3 — The Plan (Detailed Steps)

### 3.1 — `VolumeTexturePreviewMath.h`: the frustum proxy type + intersection function

```cpp
// atmosphere-scattering-3 campaign, Phase 3
// (task_manager/atmosphere-scattering-3/PHASE3_FRUSTUM_SHAPED_RAYMARCH_PROXY.md)
// - a raymarch proxy shaped like a real camera view frustum: local +Z spans
// [-halfDepth, +halfDepth], and the cross-section half-width/half-height
// grows LINEARLY from (0, 0) at z=-halfDepth (the near "apex" end) to
// (farHalfWidth, farHalfHeight) at z=+halfDepth (the far, wide end). Because
// the taper is LINEAR, every side wall is a flat PLANE (never a curved
// surface) - this is what keeps IntersectRayFrustum() below a
// straightforward generalization of IntersectRayBox()'s own slab method,
// rather than needing genuinely curved-surface intersection math.
struct FrustumProxy {
    float halfDepth = 0.0f;
    float farHalfWidth = 0.0f;
    float farHalfHeight = 0.0f;
};

// A direct C++ implementation of clipping a ray against FrustumProxy's six
// bounding half-spaces (2 for the near/far Z caps, 4 for the four linearly-
// tapering side walls) via the standard Cyrus-Beck-style "compute one
// [tEnter, tExit] t-range per half-space, intersect them all" technique -
// the same overall shape as IntersectRayBox()'s own min/max slab reduction,
// generalized from axis-aligned planes to arbitrary ones. Returns false
// (tEnter/tExit untouched) if the ray never enters the frustum, or only
// enters it entirely behind the ray's own origin - same contract as
// IntersectRayBox().
bool IntersectRayFrustum(const Vec3& rayOrigin, const Vec3& rayDirection, const FrustumProxy& frustum, float& outTEnter,
    float& outTExit);

// Maps a point already known to be INSIDE (or on the boundary of) a
// FrustumProxy - in the frustum's own local space, i.e. the same space
// IntersectRayFrustum() above operates in - to normalized [0,1]^3 texture
// space, mirroring IntersectRayBox()'s callers' own
// `(localPos / boxHalfExtents) * 0.5 + 0.5` formula, generalized for a
// shape whose X/Y cross-section size depends on Z. `localPos.z == -halfDepth`
// (the apex) maps to `uvw.x == uvw.y == 0.5` (dividing by a
// cross-section size of exactly 0 is avoided via a small epsilon floor -
// see the .cpp implementation).
Vec3 MapFrustumLocalPositionToUvw(const Vec3& localPos, const FrustumProxy& frustum);
```

### 3.2 — `VolumeTexturePreviewMath.cpp`: implement both functions

```cpp
namespace {

struct HalfSpacePlane {
    Vec3 normal;
    float constant; // Half-space is "inside" where Dot(normal, pos) + constant <= 0.
};

// Clips the running [tEnter, tExit] range against ONE half-space - returns
// false the instant the range becomes empty (tEnter > tExit), exactly like
// IntersectRayBox()'s own final range check, just applied incrementally
// per-plane instead of once at the end (necessary here since, unlike the
// axis-aligned box case, there is no single component-wise min/max
// shortcut across all six planes at once).
bool ClipRayAgainstHalfSpace(const Vec3& rayOrigin, const Vec3& rayDirection, const HalfSpacePlane& plane, float& tEnter, float& tExit)
{
    const float denom = Dot(plane.normal, rayDirection);
    const float originValue = Dot(plane.normal, rayOrigin) + plane.constant;
    constexpr float kEpsilon = 1e-8f;

    if (std::fabs(denom) < kEpsilon) {
        // Ray direction is parallel to this plane - either the ray origin
        // is already on the "inside" side of it for its entire length, or
        // it never is.
        return originValue <= 0.0f;
    }

    const float t = -originValue / denom;
    if (denom > 0.0f) {
        // Moving along +rayDirection moves FROM inside TOWARD outside -
        // this plane bounds the EXIT side.
        tExit = std::min(tExit, t);
    } else {
        tEnter = std::max(tEnter, t);
    }
    return tEnter <= tExit;
}

} // namespace

bool IntersectRayFrustum(const Vec3& rayOrigin, const Vec3& rayDirection, const FrustumProxy& frustum, float& outTEnter, float& outTExit)
{
    float tEnter = -std::numeric_limits<float>::infinity();
    float tExit = std::numeric_limits<float>::infinity();

    // Near/far caps - axis-aligned in Z only.
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(0.0f, 0.0f, -1.0f), -frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(0.0f, 0.0f, 1.0f), -frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }

    // Four tapering side walls: half-width(z) = kx * (z + halfDepth),
    // half-height(z) = ky * (z + halfDepth) - zero at z=-halfDepth (the
    // apex), farHalfWidth/farHalfHeight at z=+halfDepth (the far cap).
    const float kx = frustum.farHalfWidth / (2.0f * frustum.halfDepth);
    const float ky = frustum.farHalfHeight / (2.0f * frustum.halfDepth);

    // x <= kx*(z+halfDepth)  ->  x - kx*z - kx*halfDepth <= 0
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(1.0f, 0.0f, -kx), -kx * frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }
    // x >= -kx*(z+halfDepth)  ->  -x - kx*z - kx*halfDepth <= 0
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(-1.0f, 0.0f, -kx), -kx * frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(0.0f, 1.0f, -ky), -ky * frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }
    if (!ClipRayAgainstHalfSpace(rayOrigin, rayDirection, { Vec3(0.0f, -1.0f, -ky), -ky * frustum.halfDepth }, tEnter, tExit)) {
        return false;
    }

    if (tExit < std::max(tEnter, 0.0f)) {
        return false;
    }
    outTEnter = tEnter;
    outTExit = tExit;
    return true;
}

Vec3 MapFrustumLocalPositionToUvw(const Vec3& localPos, const FrustumProxy& frustum)
{
    const float w = (localPos.z + frustum.halfDepth) / (2.0f * frustum.halfDepth); // [0,1] along depth.
    const float kx = frustum.farHalfWidth / (2.0f * frustum.halfDepth);
    const float ky = frustum.farHalfHeight / (2.0f * frustum.halfDepth);
    // Cross-section half-size AT this z - floored to a small epsilon so a
    // point exactly at (or numerically near) the apex never divides by ~0.
    constexpr float kMinCrossSection = 1e-5f;
    const float halfWidthAtZ = std::max(kx * (localPos.z + frustum.halfDepth), kMinCrossSection);
    const float halfHeightAtZ = std::max(ky * (localPos.z + frustum.halfDepth), kMinCrossSection);

    const float u = (localPos.x / halfWidthAtZ) * 0.5f + 0.5f;
    const float v = (localPos.y / halfHeightAtZ) * 0.5f + 0.5f;
    return Vec3(u, v, w);
}
```

Add `#include <limits>` to `VolumeTexturePreviewMath.cpp` if not already
present (for `std::numeric_limits<float>::infinity()`).

### 3.3 — Tier-1 tests for the new math (write BEFORE wiring into the shader/renderer)

New tests in `tests/Renderer/VolumeTexturePreviewMathTests.cpp` (reuse this
file's own existing `kEpsilon = 1e-4f` constant for every `EXPECT_NEAR`
below, rather than inventing a new tolerance):

- `IntersectRayFrustumAlongCentralAxisSpansFullDepth` — a ray from far
  outside, aimed straight down local `+Z` through the frustum's own center
  line (`rayOrigin = Vec3(0,0,-10)`, `rayDirection = Vec3(0,0,1)`, a unit
  direction), for a `FrustumProxy` with a chosen `halfDepth`/`farHalfWidth`/
  `farHalfHeight` (e.g. `halfDepth = 2`). **Wording correction (a real
  ambiguity caught during this document's own double-check pass): `tEnter`/
  `tExit` are RAY-PARAMETER offsets measured from `rayOrigin`, NOT raw `z`
  coordinates** — do NOT assert `EXPECT_NEAR(tEnter, -halfDepth, kEpsilon)`
  directly; that is dimensionally wrong for this `rayOrigin` and will fail
  (hand-verified: with `halfDepth = 2` and `rayOrigin.z = -10`, the near cap
  at `z = -2` is reached at `t = 8`, the far cap at `z = +2` is reached at
  `t = 12` — neither equals `±halfDepth`). Assert instead on
  `rayOrigin.z + tEnter ≈ -halfDepth`, `rayOrigin.z + tExit ≈ +halfDepth`,
  and/or `tExit - tEnter ≈ 2 * halfDepth` (the full depth span). Note this
  test, by itself, only ever grazes all four side walls at their degenerate
  zero-cross-section apex point (`x == y == 0` for the entire ray) — it does
  NOT exercise `kx`/`ky` (the side-wall taper rate) at all, which is exactly
  why the next test exists.
- `IntersectRayFrustumSideWallEntryAndExitMatchHandComputedT` — **new test,
  added by this review to close a real gap**: none of this phase's originally
  planned tests ever intersect a side wall away from the fully-degenerate
  apex case above, so a sign error or swapped taper-rate (`kx`/`ky`) bug in
  `IntersectRayFrustum()`'s four side-wall half-spaces could slip through
  undetected. Use `FrustumProxy{ halfDepth = 1.0f, farHalfWidth = 2.0f,
  farHalfHeight = 2.0f }` (so `kx == ky == 1.0`), `rayOrigin =
  Vec3(3.0f, 0.0f, 0.0f)`, `rayDirection = Vec3(-1.0f, 0.0f, 0.0f)` (a unit
  ray held at `z == 0`/`y == 0`, where the frustum's true half-width is
  `kx * (0 + halfDepth) == 1.0`). Hand-derived expected result (independently
  re-derived during this review directly from `ClipRayAgainstHalfSpace()`'s
  own plane equations, not just asserted on faith): `IntersectRayFrustum()`
  returns `true` with `tEnter ≈ 2.0` (entry point `(+1, 0, 0)`, the `+X` wall)
  and `tExit ≈ 4.0` (exit point `(-1, 0, 0)`, the `-X` wall) — i.e.
  `rayOrigin + tEnter * rayDirection ≈ Vec3(1, 0, 0)` and
  `rayOrigin + tExit * rayDirection ≈ Vec3(-1, 0, 0)`. Both Z-cap planes and
  both Y-wall planes are parallel to this ray and stay non-binding for its
  whole length (it never leaves `z == 0`/`y == 0`, well inside every other
  half-space) — the X walls are what's actually being exercised here.
- `IntersectRayFrustumMissingRayReturnsFalse` — a ray that passes well
  outside the frustum's maximum extent entirely (e.g. parallel to Z, offset
  in X by MORE than `farHalfWidth` — the frustum's single WIDEST point at
  any `z` — so the ray is guaranteed to miss regardless of where along Z it
  passes, not just near the narrow apex end) returns `false`.
- `IntersectRayFrustumRayFromInsideHasNonPositiveTEnter` — mirrors
  `IntersectRayBox`'s own existing equivalent test.
- `MapFrustumLocalPositionToUvwApexMapsToCenterUAndV` — a `localPos` at
  `z == -halfDepth`, `x == 0`, `y == 0` maps to `u ≈ 0.5`, `v ≈ 0.5`.
  **Correction (a real error in this document's own original wording, caught
  during this review): "regardless of x/y, since the cross-section is ~0
  there" is WRONG and must NOT be implemented literally as the test.** The
  cross-section at `z == -halfDepth` is exactly zero, so `x == 0`/`y == 0` is
  the ONLY valid (genuinely inside-the-frustum) point at that `z` — passing a
  NONZERO `x`/`y` there (a point that is actually OUTSIDE the frustum) divides
  by the `kMinCrossSection = 1e-5f` epsilon floor and produces a `u`/`v` far
  from `0.5` (e.g. `x == 0.001` alone already yields `u ≈ 50.5`, nowhere near
  `0.5`) — expected behavior for an out-of-range input (the real raymarch
  loop in `VolumeTexturePreview.comp` only ever calls this function with a
  `localPos` already known to be between `tEnter`/`tExit`, i.e. genuinely
  inside the frustum), not a bug in `MapFrustumLocalPositionToUvw()` — but the
  TEST itself must only assert `x == y == 0` at the apex.
- `MapFrustumLocalPositionToUvwFarCapCornersMapToUnitSquareCorners` — a
  `localPos` at `z == +halfDepth`, `x == +farHalfWidth`, `y == +farHalfHeight`
  maps to `u ≈ 1.0`, `v ≈ 1.0` (and the three other sign combinations map to
  the other three unit-square corners).
- `MapFrustumLocalPositionToUvwWMatchesDepthFraction` — several `z` values
  spaced through `[-halfDepth, +halfDepth]` map to the expected linear `w`.

### 3.4 — `VolumeTexturePreviewMath.cpp`: Phase 2's camera setup now also emits frustum parameters

`ComputeAtmosphereAerialPerspectivePreviewCameraSetup()` (Phase 2) currently
only fills `VolumeCameraSetup::boxHalfExtents`. This phase does NOT change
that function's own signature or its camera positioning math (eye/forward/
right/up/tanHalfFovY all stay exactly as Phase 2 tuned them) — it adds a
SEPARATE, small helper used only by the renderer's frustum path:

```cpp
// atmosphere-scattering-3, Phase 3 - derives this preview's own FrustumProxy
// directly from the SAME two constants
// (kAerialPreviewXYHalfExtent/kAerialPreviewDepthHalfExtent) Phase 2's
// ComputeAtmosphereAerialPerspectivePreviewCameraSetup() already uses for
// its (superseded, for this shape) boxHalfExtents - kept in the same
// anonymous namespace, single source of truth for both.
FrustumProxy ComputeAtmosphereAerialPerspectivePreviewFrustum()
{
    FrustumProxy frustum;
    frustum.halfDepth = kAerialPreviewDepthHalfExtent;
    frustum.farHalfWidth = kAerialPreviewXYHalfExtent * 2.0f; // The far (wide) end deliberately exceeds the old box's own flat half-extent, so the WIDENING itself is visually obvious, not subtle.
    frustum.farHalfHeight = kAerialPreviewXYHalfExtent * 2.0f;
    return frustum;
}
```
Declare this in `VolumeTexturePreviewMath.h` too (non-static, so
`VolumeTexturePreviewRenderer.cpp` can call it).

### 3.5 — `VolumeTexturePreviewRenderer.h`/`.cpp`: thread the frustum through

`PushConstants` (in `VolumeTexturePreviewRenderer.cpp`) gains ONE new field
at its tail (same "append, never reorder existing fields" discipline the
`atmosphere-scattering-2` Phase 4 addition already established for this
exact struct):

```cpp
std::int32_t shapeMode = 0; // 0 = axis-aligned box (existing/default), 1 = tapering frustum (Phase 3).
```
(`boxHalfExtents` is REUSED to carry `(farHalfWidth, farHalfHeight, halfDepth)`
when `shapeMode == 1` — no new Vec3 field needed; document this dual-use
explicitly, right on the existing `boxHalfExtents` field's own comment.)

In `RenderPreview()`, when `interpretation ==
VolumeTexturePreviewInterpretation::AtmosphereAerialPerspective`, ALSO set:
```cpp
pushConstants.shapeMode = 1;
const FrustumProxy frustum = ComputeAtmosphereAerialPerspectivePreviewFrustum();
pushConstants.boxHalfExtents = Vec3(frustum.farHalfWidth, frustum.farHalfHeight, frustum.halfDepth);
```
(overwriting the `boxHalfExtents` value Phase 2's camera-setup call already
assigned a few lines earlier in this same function — the camera's own
`eyePosition`/`forward`/`right`/`up`/`tanHalfFovY` from
`ComputeAtmosphereAerialPerspectivePreviewCameraSetup()` are still used
unchanged; only the SHAPE-related field is overridden here). For the
generic `GenericDensityInAlpha` path, `shapeMode` stays at its default `0`
and `boxHalfExtents` is left exactly as `ComputeVolumeCameraSetup()`
produced it — zero regression.

**Ordering pitfall to avoid (caught during this document's own double-check
pass, before any implementation existed):** `FrustumProxy`'s own C++ struct
fields are declared in the order `halfDepth, farHalfWidth, farHalfHeight`
(Section 3.1), but the `Vec3` actually packed into `boxHalfExtents` above is
in a DIFFERENT order — `boxHalfExtents.x == farHalfWidth`,
`boxHalfExtents.y == farHalfHeight`, `boxHalfExtents.z == halfDepth`. This is
unambiguous in C++ (the named `Vec3(frustum.farHalfWidth, ...)` constructor
call above makes the order explicit), but there is no equivalent named
struct on the GLSL side — Section 3.6's push-constant block only ever sees a
bare `vec3`. See Section 3.6's own "Reminder" callout for the exact
unpacking order the hand-written GLSL mirror must use.

**A real geometric interaction with Phase 2's own camera framing, worth
flagging explicitly (found during this same review, not hypothetical):**
`ComputeAtmosphereAerialPerspectivePreviewCameraSetup()` (Phase 2) sizes the
camera's distance from `Length(setup.boxHalfExtents)` using the OLD, NARROWER
box half-extents (today: `(kAerialPreviewXYHalfExtent,
kAerialPreviewXYHalfExtent, kAerialPreviewDepthHalfExtent)` =
`(0.35, 0.35, 0.9)`, bounding radius ≈ `1.03`) — this happens BEFORE the two
lines above overwrite `pushConstants.boxHalfExtents` with the actual, WIDER
far-cap half-extents `(farHalfWidth, farHalfHeight, halfDepth)` =
`(0.7, 0.7, 0.9)` today, bounding radius ≈ `1.34`. Because the camera's
distance/field-of-view was computed for the SMALLER sphere, the frustum's
real (wider) far cap may extend beyond the camera's field of view and get
visibly clipped at the output frame's edges once this phase actually renders
it — see this phase's own Verification section below, which now has an
explicit checklist item for this. This phase is not required to change
Phase 2's camera call to fix this (that would be a camera-framing change,
arguably Phase 4's job) — but it must not go unnoticed or unmentioned if it
happens.

### 3.6 — `VolumeTexturePreview.comp`: the shader-side branch

Mirror `PushConstants`' new tail field exactly:
```glsl
layout(push_constant) uniform PushConstants {
    // ... all existing fields, UNCHANGED ...
    int interpretationMode;
    float aerialPreviewExposure;
    int shapeMode; // 0 = box (IntersectRayBox), 1 = frustum (IntersectRayFrustum) - atmosphere-scattering-3, Phase 3.
} pc;
```

**Reminder when writing the GLSL mirror below (see Section 3.5's own
"Ordering pitfall" callout for the full explanation): unpack the reused
`pc.boxHalfExtents` vec3 as `farHalfWidth = pc.boxHalfExtents.x`,
`farHalfHeight = pc.boxHalfExtents.y`, `halfDepth = pc.boxHalfExtents.z` —
this is NOT the same order `FrustumProxy`'s own C++ struct fields are
declared in (Section 3.1: `halfDepth, farHalfWidth, farHalfHeight`), and
there is no named struct on the GLSL side to catch a mix-up by construction.**

Add a GLSL transcription of `IntersectRayFrustum()`/
`MapFrustumLocalPositionToUvw()` (by hand, exactly mirroring
`IntersectRayBox()`'s own existing "direct GLSL transcription of the CPU
oracle" precedent, same file, same doc-comment convention citing
`VolumeTexturePreviewMath.cpp` as the oracle). In `main()`, branch on
`pc.shapeMode` right where `tEnter`/`tExit` are computed today:

```glsl
float tEnter;
float tExit;
bool hit;
if (pc.shapeMode == 1) {
    hit = IntersectRayFrustum(pc.eyePosition, rayDir, pc.boxHalfExtents /* (farHalfWidth, farHalfHeight, halfDepth) when shapeMode==1 */, tEnter, tExit);
} else {
    hit = IntersectRayBox(pc.eyePosition, rayDir, pc.boxHalfExtents, tEnter, tExit);
}
if (!hit) {
    imageStore(outputImage, pixel, vec4(kBackgroundColor, 1.0));
    return;
}
```

Inside the per-step loop, replace the existing
`vec3 uvw = (localPos / pc.boxHalfExtents) * 0.5 + 0.5;` line with the same
branch, calling the new `MapFrustumLocalPositionToUvw()` GLSL function when
`pc.shapeMode == 1`.

### 3.7 — Zero regression to the generic path only (the Aerial Perspective path itself IS expected to change, on purpose)

Confirm: `shapeMode == 0` (every OTHER, generic, non-Aerial-Perspective
volume texture) produces IDENTICAL output to before this phase, since
`IntersectRayBox()`/the box `uvw` formula are completely untouched code
paths, only reached via the `else` branch now. The Aerial Perspective path
itself (`shapeMode == 1`) is EXPECTED and INTENDED to look visibly different
from Phase 2's own elongated-box result — that difference (a genuine
widening funnel) is this phase's entire purpose, not a regression to guard
against.

## Verification

- Fast, targeted compile: build `GreatTamanaEngine` (forces `glslc` to
  recompile `VolumeTexturePreview.comp` — this is the one phase in this
  whole campaign that touches a shader) plus `GreatTamanaEngineTests`.
- Run the new Tier-1 tests: `ctest -C Debug -R VolumeTexturePreviewMathTest --output-on-failure`.
- `run_app_background` + `gte_send_request`
  `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
  — confirm the preview now visibly WIDENS from one end to the other (not
  just elongates), and re-confirm a known 2D texture (`Swapchain`) is
  unaffected. `stop_app_background` when done.
- **New verification item, added by this document's own double-check pass:**
  also confirm the frustum's WIDE (far) end is not visually clipped by the
  256x256 output frame's edges. This is a real, concrete risk, not a
  hypothetical — see Section 3.5's own callout: Phase 2's camera distance was
  computed against the narrower, pre-widening box half-extents, not the
  actual, wider far-cap half-extents this phase introduces. If the far end
  IS clipped, do not silently accept it — note it explicitly in
  `PHASE3_COMPLETION_REPORT.md` as a known follow-up for Phase 4's tuning
  pass (which already owns both the camera-framing constants and the
  frustum's own far-plane multiplier, so it is equipped to fix this either
  way — it must simply not be a surprise Phase 4 discovers on its own).
- Write `PHASE3_COMPLETION_REPORT.md` (this phase is MANDATORY — the report
  must confirm the frustum shape was actually implemented and wired in, with
  a description of the resulting visual compared against both Phase 2's own
  "before" result and the reference PNG), then commit.

## Double-Check Notes (pre-full-review pass)

This section records a focused, math-heavy double-check pass performed on
THIS file only, ahead of the broader six-file review the whole
`atmosphere-scattering-3` campaign will eventually receive. Scope: rigorous,
by-hand verification of `IntersectRayFrustum()`/`ClipRayAgainstHalfSpace()`/
`MapFrustumLocalPositionToUvw()`'s math, the `PushConstants`/GLSL packing
plan, the `boxHalfExtents` reuse description, and the Tier-1 test list (3.3).

**What was checked:**
- Worked the half-space plane equations by hand against three concrete
  numeric examples: (1) a ray straight down the local `+Z` axis through the
  frustum's center (on-axis, touches the near/far Z caps and grazes all four
  side walls exactly at the degenerate zero-width apex), (2) a ray aimed
  through a known far-cap corner along `-Z` (confirmed a legitimate,
  zero-length "graze the corner" degenerate hit, not a bug), and (3) a ray
  that intersects two SIDE walls away from the apex, at `z == 0` for a
  `halfDepth = 1, farHalfWidth = farHalfHeight = 2` frustum
  (`rayOrigin = (3,0,0)`, `rayDirection = (-1,0,0)`) — hand-derived
  `tEnter == 2.0`, `tExit == 4.0` exactly, confirmed against the plane
  equations directly. Also re-derived the "missing ray" case (offset beyond
  `farHalfWidth`, the frustum's single widest point). **Conclusion: the
  half-space normal/constant signs and the entry-vs-exit assignment
  (`denom > 0` -> exit, `denom < 0` -> enter) in the document's own code
  snippets are CORRECT** — no sign flip or swapped-plane bug was found in
  `IntersectRayFrustum()`/`ClipRayAgainstHalfSpace()` as written.
- Confirmed `MapFrustumLocalPositionToUvw()`'s apex/far-cap-corner/depth-
  fraction math is correct, but found and fixed a real WORDING bug in the
  apex test's own description (it incorrectly claimed the mapping holds
  "regardless of x/y" at the apex, which is false and would produce a wildly
  wrong `u`/`v` for any nonzero `x`/`y` there — corrected in 3.3 above).
- Confirmed the `PushConstants`/GLSL push-constant packing plan (appending
  `shapeMode` as a lone `int32` at the true tail, offset 88, size 92 total)
  is byte-for-byte consistent with the REAL, current `PushConstants` struct
  in `VolumeTexturePreviewRenderer.cpp` and the REAL current
  `layout(push_constant)` block in `VolumeTexturePreview.comp` (both read
  directly, not assumed) — no padding/alignment error found, and Phase 2
  (also read directly) adds no competing tail fields of its own.
- Found and documented two real, previously-unstated gaps, now fixed in this
  file: (1) the `boxHalfExtents` reuse packs `(farHalfWidth, farHalfHeight,
  halfDepth)` in a DIFFERENT order than `FrustumProxy`'s own C++ struct
  field declaration order, which is an easy mix-up risk for the hand-written
  GLSL mirror specifically (no named struct to catch it there) — now called
  out explicitly in both Section 3.5 and 3.6; (2) Phase 2's camera distance
  is computed against the narrower, pre-widening box half-extents, not the
  actual wider far-cap half-extents this phase introduces, which risks the
  widened far end being visually clipped at the output frame's edges — now
  called out explicitly in Section 3.5 and added as a new Verification
  checklist item.
- Strengthened the Tier-1 test list (3.3): added a new, fully-worked-numeric
  test (`IntersectRayFrustumSideWallEntryAndExitMatchHandComputedT`) because
  none of the originally-planned tests ever actually intersected a side wall
  away from the fully-degenerate on-axis/apex case — a sign or scale error
  specific to the side-wall taper rate (`kx`/`ky`) could otherwise have
  passed every originally-listed test undetected. Also clarified the
  on-axis test's own `tEnter`/`tExit` semantics (ray-parameter offsets from
  `rayOrigin`, not raw `z` coordinates) and tightened the "missing ray"
  test's offset requirement.
- Cross-referenced `AtmosphereLutRenderer.cpp`'s real
  `kAerialPerspectiveVolumeWidth/Height/Depth` (128/128/32) — matches every
  assumption this document and Phase 1/2 already make.

**What was NOT changed:** the core intersection/mapping algorithms
themselves (`IntersectRayFrustum()`, `ClipRayAgainstHalfSpace()`,
`MapFrustumLocalPositionToUvw()`), the `PushConstants`/GLSL field layout
plan, and the overall phase structure/locked design decisions were all found
to be correct and sufficient as originally written — only documentation
wording, missing test coverage, and two under-stated cross-cutting risks
were added/clarified. No C++/GLSL implementation code was written as part of
this pass (this was a documentation/strategy review only, per this task's
own rules).
