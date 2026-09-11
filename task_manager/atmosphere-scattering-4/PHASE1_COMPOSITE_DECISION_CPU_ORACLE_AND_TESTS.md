# PHASE1 — Aerial Perspective Composite Decision: CPU Oracle + Unit Tests

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first)
**Depends on:** nothing (first phase)
**Touches shaders?** No. This phase is pure C++ + tests only.

---

## Step 1 — The Goal

Before touching a single line of GLSL, encode the **correct** decision logic
for the Aerial Perspective composite pass as a small, pure, Tier-1-testable
C++ function — following this codebase's own established "permanent CPU
oracle, written and reviewed before the shader mirrors it" discipline (see
`AGENTS.md`, "Atmosphere Scattering": *"every `.comp`/`.frag` shader ... is a
faithful GLSL transcription of this file's own math ... if a shader and this
CPU oracle ever disagree, the CPU oracle is right by definition"*).

Concretely, ship:

1. A new header/source pair,
   `src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h/.cpp`,
   with two pure functions:
   - `ShouldBypassAerialPerspectiveComposite(float rawDepth) -> bool`
   - `ComputeAerialPerspectiveCompositeColor(float rawDepth, const Vec3&
     sceneColorRgb, const Vec3& sampledAerialRgb, float sampledAerialA, float
     strength) -> Vec3`
2. A new test file,
   `tests/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp`,
   with a full set of Tier-1 unit tests covering both the bypass branch and
   the existing (unchanged) blend-math branch.
3. Both new files wired into `CMakeLists.txt` (`gte_core` sources) and
   `tests/CMakeLists.txt` (`GTE_TEST_SOURCES`) so they actually compile and
   run.

This phase's own deliverable has **zero visible runtime effect** (nothing
calls the new function from the shader/engine yet) — it exists purely so
Phase 2's shader edit has an already-reviewed, already-tested "ground truth"
to mirror mechanically, rather than being a freehand GLSL edit nobody can
regression-test.

---

## Step 2 — The Situation

### 2.1 The exact GLSL this phase must mirror

`src/Shaders/AtmosphereAerialPerspectiveComposite.comp`, current `main()`
(confirmed via direct file read), relevant excerpt:

```glsl
float rawDepth = texture(sourceDepth, uv).r;

float viewDistanceKm;
if (rawDepth >= 0.999999) {
    viewDistanceKm = maxDistanceKm;              // <- BUG: fake "infinity" stand-in
} else {
    ... reconstruct real world position/distance ...
}

... // volume Z-slice lookup, half-texel bias, first-slice fade-in blend —
    // ALL of this is GPU-texture-sampling machinery with no CPU equivalent,
    // and is completely unaffected by this bug (see PHASE0's Locked Design
    // Decision 5) — NOT mirrored by this phase's oracle.

vec4 sampledAerial = texture(aerialPerspectiveVolume, vec3(uv, sliceUv));
float firstSliceBlend = clamp(boundaryU * sliceCount, 0.0, 1.0);
vec4 aerial = mix(vec4(0.0, 0.0, 0.0, 1.0), sampledAerial, firstSliceBlend);

float strength = pc.aerialPerspectiveStrengthAndPad.x;
vec3 inScattering = aerial.rgb * strength;
float transmittance = mix(1.0, aerial.a, strength);

vec3 sceneColor = texture(sourceColor, uv).rgb;
vec3 finalColor = sceneColor * transmittance + inScattering;   // <- runs UNCONDITIONALLY today

imageStore(destinationImage, texel, vec4(finalColor, 1.0));
```

The oracle this phase writes must capture:
- **The missing branch**: when `rawDepth >= 0.999999`, the correct
  `finalColor` is `sceneColor`, unchanged — full stop, no `aerial`/`strength`
  math involved at all.
- **The existing (already-correct) blend math** for the `rawDepth <
  0.999999` case, so the SAME oracle function can be used by Phase 2's
  shader mirror for both branches, and so a future edit to the strength/
  transmittance formula gets a regression test automatically.

The oracle's `aerial`/`rgb`/`a` inputs are **already-sampled, already-
first-slice-blended** values (i.e. what the shader's local variable `aerial`
holds right before its own final two lines) — the oracle does not attempt to
re-derive a 3D-texture trilinear sample or the Z-slice math on the CPU (see
`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 5 for why not).

### 2.2 Where this new file belongs, and why not `AtmosphereMath.h`

`AtmosphereMath.h` is explicitly the density/optical-depth/phase-function
oracle, and `atmosphere-scattering-2`'s Locked Design Decision 5 established
that it (and `AtmosphereCommon.glsl`'s shared functions) must never gain new,
feature-local logic. This campaign's fix is unrelated to density/optical-
depth math, so putting it there would be both architecturally wrong and would
risk destabilizing a file three prior campaigns have carefully kept stable.
Instead, this new file follows the exact same "small, dedicated, narrowly-
scoped pure-math header, separate from the big shared oracle" pattern already
used by `src/Renderer/VolumeTexturePreviewMath.h` (a math-only header,
paired with its own `.cpp`, consumed by a GPU-touching renderer class) and by
`src/Editor/AtmosphereAerialPerspectiveLutInspection.h`'s own
`AccumulateAerialPerspectiveSliceStats()`/`FinalizeAerialPerspectiveLutInspection()`
(new, narrowly-scoped pure aggregation functions living in their own file,
not bolted onto an existing oracle).

This new file lives under `src/Renderer/Atmosphere/` (not `src/Editor/`)
because, unlike the Editor-only inspection tools, this logic is a genuine
**rendering-correctness oracle** for a `gte_core` shader — it must be
available and tested regardless of `GTE_ENABLE_EDITOR`, exactly like
`AtmosphereMath.h`/`DirectionalLightResolver.h` already are.

### 2.3 Existing conventions to copy exactly

- File header comment style: see `AtmosphereMath.h`'s own top-of-file
  comment block for the exact tone/structure to mirror (what this file is,
  which bug/phase it belongs to, the "if GLSL and CPU disagree, CPU is right"
  rule, and a note that it is Tier-1-testable with zero Vulkan/Renderer
  dependency).
- `Vec3` is `src/Math/Vec3.h`'s tightly-packed 3-float POD — already used
  throughout `AtmosphereMath.h`'s own signatures; use it here identically
  (component-wise multiply is `Vec3::operator*` — confirm the exact operator
  set available in `src/Math/Vec3.h` before use; if a needed component-wise
  multiply/scale operator does not already exist, add the missing one to
  `Vec3.h` itself rather than hand-rolling arithmetic inline — check
  `src/Math/Vec3.h` for what already exists, e.g. `operator*(float)` and
  `operator+`, before assuming a gap).
- Namespace: everything lives inside `namespace gte { ... }` per `AGENTS.md`,
  "Coding Guidelines".
- `noexcept` on every pure math function, matching every existing
  `AtmosphereMath.h` declaration's own signature convention.

---

## Step 3 — The Plan

### 3.1 Create `src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h`

```cpp
#pragma once

#include "../../Math/Vec3.h"

namespace gte {

// ============================================================================
// AtmosphereAerialPerspectiveCompositeMath.h - the PERMANENT CPU ORACLE for
// the Aerial Perspective Composite pass's own per-pixel BRANCH + FINAL BLEND
// decision (src/Shaders/AtmosphereAerialPerspectiveComposite.comp's main()).
// ============================================================================
// atmosphere-scattering-4 campaign - see
// task_manager/atmosphere-scattering-4/PHASE0_MASTER_STRATEGY.md and
// task_manager/atmosphere-scattering-4/AERIAL_PERSPECTIVE_NO_GEOMETRY_BUG_REPORT_20260911.md
// for the full root-cause writeup this file fixes.
//
// Confirmed bug: the composite shader used to run its full
// `sceneColor * transmittance + inScattering` blend UNCONDITIONALLY, even for
// pixels with no opaque geometry drawn into them (rawDepth still at the
// frame's own clear value, 1.0) - double-fogging a sky pixel the Sky
// Background pass had already finished, correctly, earlier in the same
// frame. This file is the reviewed, tested ground truth
// AtmosphereAerialPerspectiveComposite.comp's own GLSL must mirror exactly -
// the same "CPU oracle first, GLSL mirrors it, and if they ever disagree the
// CPU oracle is right by definition" discipline AGENTS.md's "Atmosphere
// Scattering" section already establishes for AtmosphereMath.h, applied here
// to a brand new, narrowly-scoped file (NOT added to AtmosphereMath.h itself
// - see PHASE0's own Locked Design Decision 2/5 for why not: this has nothing
// to do with density/optical-depth math, and AtmosphereMath.h/
// AtmosphereCommon.glsl's shared oracle functions are locked against
// feature-local additions).
//
// DELIBERATE SCOPE LIMIT: this oracle does NOT reproduce the volume's own
// trilinear 3D-texture sample, Z-slice-from-distance mapping, half-texel
// bias, or first-slice fade-in mix - that is GPU-texture-sampling machinery
// with no meaningful CPU equivalent, and is completely unrelated to this
// bug. `sampledAerialRgb`/`sampledAerialA` below are the ALREADY-sampled,
// ALREADY-first-slice-blended texel values (exactly what the shader's own
// local `aerial` variable holds immediately before its final two lines) -
// this oracle only covers the branch and the blend arithmetic that follows.
//
// Pure functions only - zero Vulkan/Renderer/ECS dependency, so this whole
// file is trivially Tier-1-testable (see
// tests/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp)
// with no live VkDevice/SDL window at all.

// The exact depth-comparison threshold the shader uses to detect "nothing
// was drawn at this pixel this frame" (still at the frame's own clear depth
// value). MUST match AtmosphereAerialPerspectiveComposite.comp's own literal
// `0.999999` exactly - GLSL cannot #include a C++ header, so this constant is
// deliberately duplicated in both places; if this value is ever changed here,
// the SAME literal must be changed in the shader in the same commit (see this
// file's own unit tests for the exact boundary this guards).
constexpr float kAerialPerspectiveFarPlaneDepthThreshold = 0.999999f;

// Returns true when a pixel has no opaque geometry drawn into it this frame
// (rawDepth is still at, or beyond, the clear-depth threshold above) - the
// Sky Background pass has ALREADY produced this pixel's final, correct color
// earlier in the same frame (see this file's own header comment), so the
// Aerial Perspective composite must treat it as a pure pass-through rather
// than sampling/blending the aerial-perspective volume at all.
bool ShouldBypassAerialPerspectiveComposite(float rawDepth) noexcept;

// Mirrors AtmosphereAerialPerspectiveComposite.comp's own final per-pixel
// color decision exactly:
//   - if ShouldBypassAerialPerspectiveComposite(rawDepth): returns
//     sceneColorRgb UNCHANGED (no aerial/strength math involved at all -
//     this is the fix for the no-geometry double-fogging bug).
//   - otherwise: returns sceneColorRgb * transmittance + inScattering, where
//     inScattering = sampledAerialRgb * strength and
//     transmittance  = mix(1.0, sampledAerialA, strength)
//     (unchanged from the shader's pre-existing, already-correct blend
//     formula for real opaque geometry).
//
// `sampledAerialRgb`/`sampledAerialA` must already be the POST-first-slice-
// fade-in-blend texel (see this file's own "DELIBERATE SCOPE LIMIT" note
// above) - this function does not know about, or need, the volume's raw
// texel/slice machinery at all.
Vec3 ComputeAerialPerspectiveCompositeColor(float rawDepth, const Vec3& sceneColorRgb,
    const Vec3& sampledAerialRgb, float sampledAerialA, float strength) noexcept;

} // namespace gte
```

### 3.2 Create `src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.cpp`

```cpp
#include "AtmosphereAerialPerspectiveCompositeMath.h"

namespace gte {

bool ShouldBypassAerialPerspectiveComposite(float rawDepth) noexcept
{
    return rawDepth >= kAerialPerspectiveFarPlaneDepthThreshold;
}

Vec3 ComputeAerialPerspectiveCompositeColor(float rawDepth, const Vec3& sceneColorRgb,
    const Vec3& sampledAerialRgb, float sampledAerialA, float strength) noexcept
{
    if (ShouldBypassAerialPerspectiveComposite(rawDepth)) {
        return sceneColorRgb;
    }

    const Vec3 inScattering = sampledAerialRgb * strength;
    const float transmittance = 1.0f + (sampledAerialA - 1.0f) * strength; // mix(1.0, a, strength)
    return sceneColorRgb * transmittance + inScattering;
}

} // namespace gte
```

**Before writing this `.cpp`, open `src/Math/Vec3.h` and confirm:**
- `Vec3 operator*(const Vec3&, float)` (or equivalent scalar-scale) exists —
  used for `sampledAerialRgb * strength`.
- `Vec3 operator*(const Vec3&, float)` for `sceneColorRgb * transmittance`
  (a `Vec3 * float`, scalar broadcast — NOT a component-wise `Vec3 * Vec3`,
  since `transmittance` is a single scalar mean value, matching the GLSL's
  own `vec3 * float` usage exactly).
- `Vec3 operator+(const Vec3&, const Vec3&)` exists for the final sum.

If any of these operators do not already exist on `Vec3`, add the missing
one(s) directly to `src/Math/Vec3.h`/`.cpp` first (with its own
`tests/Math/Vec3Tests.cpp` addition if it is a genuinely new operator), then
proceed — do not hand-roll `Vec3(a.x*b, a.y*b, a.z*b)` inline as a workaround
if the proper operator is simply missing.

### 3.3 Register the new files in `CMakeLists.txt`

Add immediately after the existing Atmosphere block (see the file around
line 421-422, right after `src/Renderer/Atmosphere/AtmosphereMath.cpp`):

```cmake
    src/Renderer/Atmosphere/AtmosphereMath.h
    src/Renderer/Atmosphere/AtmosphereMath.cpp
    src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h
    src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.cpp
    src/Renderer/Atmosphere/DirectionalLightResolver.h
    ...
```

(Insert the two new lines directly below the existing `AtmosphereMath.cpp`
line and above `DirectionalLightResolver.h` — re-read the file first to
confirm the exact current line numbers before editing, since other phases in
this same file may have shifted them slightly by the time this phase runs.)

Also add a corresponding one-line mention to `tests/CMakeLists.txt`'s own
top-of-file "Tier 1 test files" comment block (the block that already lists
`Renderer/Atmosphere/AtmosphereMathTests.cpp` around line 142-146 — note this
comment block lives in `tests/CMakeLists.txt`, NOT the root `CMakeLists.txt`,
despite the similar name — double-check which file you have open), following
that comment's existing format exactly, so the comment stays an accurate,
human-readable index. As with every other line-number reference in this
document, re-read `tests/CMakeLists.txt` fresh immediately before editing to
confirm the exact current line number rather than trusting this stale
snapshot.

### 3.4 Create `tests/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp`

Mirror the header/include/namespace style of
`tests/Renderer/Atmosphere/AtmosphereMathTests.cpp` exactly (GoogleTest,
`#include "Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h"`,
`using namespace gte;` or fully-qualified `gte::` calls matching that file's
own convention — check which one `AtmosphereMathTests.cpp` actually uses and
copy it verbatim).

Required test cases (write every one of these — this is the regression
contract Phase 2's shader edit is checked against):

1. `ShouldBypassAerialPerspectiveCompositeTest.TrueExactlyAtClearDepthValue`
   — `ShouldBypassAerialPerspectiveComposite(1.0f)` is `true`.
2. `ShouldBypassAerialPerspectiveCompositeTest.TrueAtTheDocumentedThresholdItself`
   — `ShouldBypassAerialPerspectiveComposite(0.999999f)` is `true` (the `>=`
   boundary, matching the shader's own `>=` comparison exactly — this is the
   single most important test in this whole file, since an `>` vs. `>=`
   mistake here is exactly the kind of off-by-one a shader port could
   introduce).
3. `ShouldBypassAerialPerspectiveCompositeTest.FalseJustBelowTheThreshold`
   — `ShouldBypassAerialPerspectiveComposite(0.9999989f)` is `false`.
4. `ShouldBypassAerialPerspectiveCompositeTest.FalseForOrdinaryOpaqueGeometryDepth`
   — `ShouldBypassAerialPerspectiveComposite(0.5f)` is `false`.
5. `ComputeAerialPerspectiveCompositeColorTest.BypassReturnsSceneColorUnchangedRegardlessOfAerialInputs`
   — with `rawDepth = 1.0f`, feed a deliberately extreme/nonsense
   `sampledAerialRgb`/`sampledAerialA`/`strength` (e.g.
   `Vec3(9.0f, 9.0f, 9.0f)`, `0.0f`, `2.0f`) and assert the returned color is
   *exactly* the input `sceneColorRgb`, componentwise — this is the literal
   regression test for the reported bug: it fails against the OLD shader
   behavior's equivalent logic (which would have blended those extreme
   values in) and passes against the fix.
6. `ComputeAerialPerspectiveCompositeColorTest.ZeroStrengthIsAPureSceneColorPassThroughEvenWithGeometry`
   — `rawDepth = 0.3f` (real geometry), `strength = 0.0f`: result must equal
   `sceneColorRgb` exactly (mirrors the shader's pre-existing
   `aerialPerspectiveStrength = 0.0` "fully disabled" contract — a regression
   guard that this campaign's fix does not accidentally touch that existing,
   unrelated feature).
7. `ComputeAerialPerspectiveCompositeColorTest.FullStrengthMatchesHandComputedBlend`
   — `rawDepth = 0.3f`, `sceneColorRgb = Vec3(1.0f, 1.0f, 1.0f)`,
   `sampledAerialRgb = Vec3(0.2f, 0.1f, 0.05f)`, `sampledAerialA = 0.5f`,
   `strength = 1.0f` → expect `Vec3(0.7f, 0.6f, 0.55f)` (hand-computed:
   `transmittance = 0.5`, `inScattering = (0.2,0.1,0.05)`,
   `sceneColor*0.5 + inScattering`), asserted with `EXPECT_NEAR` at a tight
   epsilon (e.g. `1e-5f`).
8. `ComputeAerialPerspectiveCompositeColorTest.PartialStrengthInterpolatesLinearly`
   — same inputs as test 7 but `strength = 0.5f` → hand-compute the expected
   `transmittance = mix(1.0, 0.5, 0.5) = 0.75` and
   `inScattering = (0.1, 0.05, 0.025)`, expect
   `Vec3(0.85f, 0.8f, 0.775f)`.

### 3.5 Register the new test file in `tests/CMakeLists.txt`

Add immediately after the existing
`Renderer/Atmosphere/AtmosphereMathTests.cpp` line (around line 1753):

```cmake
    Renderer/Atmosphere/AtmosphereMathTests.cpp
    Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp
    Renderer/Atmosphere/DirectionalLightResolverTests.cpp
```

(Re-read the file first to confirm the exact current line numbers before
editing.)

### 3.6 Verification (fast compile check only — no full build yet)

1. Configure/build only the affected targets — do **not** run a full clean
   rebuild of the whole engine yet (that is Phase 4's job). A targeted
   `cmake --build build --target GreatTamanaEngineTests` (or the project's
   equivalent fast test-target build) is sufficient to confirm the new files
   compile and link.
2. Run the new test binary/`ctest` filtered to just this new test suite. NOTE:
   `gtest_discover_tests(GreatTamanaEngineTests)` (`tests/CMakeLists.txt`)
   registers each ctest test case by its real GoogleTest name,
   `<TestSuiteName>.<TestName>` — this phase's own test suite names are
   `ShouldBypassAerialPerspectiveCompositeTest` and
   `ComputeAerialPerspectiveCompositeColorTest`, NEITHER of which contains the
   literal substring "Math", so a filter regex of literally
   `AerialPerspectiveCompositeMath` (the file name) matches ZERO registered
   ctest tests — use `-R AerialPerspectiveComposite` instead (matches both
   suite names above; does not accidentally also match unrelated existing
   suites like `InspectAerialPerspectiveVolumeTest`, which lacks the
   "Composite" substring), e.g.
   `ctest -C Debug -R AerialPerspectiveComposite --output-on-failure` from the
   `build` directory — confirm every one of the 8 cases above passes.
3. Write `PHASE1_COMPLETION_REPORT.md` in this same folder describing what
   was added, the exact final file contents/line numbers if they drifted from
   this plan, and the test run output, then `git_add` + `git_commit` (code +
   tests + report together, one commit).
