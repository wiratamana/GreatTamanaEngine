# PHASE1 — Root-Cause Instrumentation and Regression Tests

Parent: `PHASE0_MASTER_STRATEGY.md`. First phase — every later phase depends
on this one having landed (Phase 2's own fix is explicitly designed to be
provably different from what this phase codifies as "the current, buggy
behavior").

## Step 1 — The Goal (Where are we going?)

Turn this campaign's own investigation (see `PHASE0_MASTER_STRATEGY.md`,
Step 2) from "prose in a strategy document" into **permanent, automated,
Tier-1-tested facts checked into the codebase** — so:

1. The exact geometric defect (the Aerial Perspective volume's depth axis
   being squashed to 1/4 the size of its other two axes by
   `ComputeVolumeCameraSetup()`) is captured as a real, running test, not
   just a claim a future contributor has to take on faith or re-derive by
   reading code themselves.
2. The claim "the underlying LUT DATA has a real, meaningful near/far
   gradient — this is a preview bug, not a data bug" gets a permanent,
   reusable, PER-BAND (not just whole-volume) numeric tool behind it, so any
   future visual-tuning phase (Phase 4) or future regression can re-confirm
   this in seconds without re-deriving the reasoning from scratch.

No visual/rendering behavior changes in this phase at all — this is pure
instrumentation and test-writing, landing safely on its own before Phase 2's
actual fix.

## Step 2 — The Situation (Where are we now?)

- `src/Renderer/VolumeTexturePreviewMath.h/.cpp` already has a real,
  Tier-1-tested `ComputeVolumeCameraSetup(int width, int height, int depth)`
  (see `tests/Renderer/VolumeTexturePreviewMathTests.cpp`, which already
  covers the generic cube/non-cube cases, camera-basis orthonormality, and
  ray/box intersection edge cases) — but nothing in that test file exercises
  the REAL Aerial Perspective volume's actual dimensions
  (`128, 128, 32` — `AtmosphereLutRenderer.cpp`'s
  `kAerialPerspectiveVolumeWidth/Height/Depth`), so the specific, severe
  4:1 flattening this campaign is built around is not written down anywhere
  as a checked fact yet.
- `src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp`
  (`atmosphere-scattering-2` campaign, Phase 5) already loops over every Z
  slice of a live Aerial Perspective volume and accumulates ONE combined,
  whole-volume `AtmosphereAerialPerspectiveLutInspectionResult`
  (min/max/mean transmittance and in-scattering magnitude across ALL 32
  slices at once). This already proves the volume as a WHOLE has meaningful
  non-1.0/non-0.0 numbers, but collapses away the one thing this campaign
  actually cares about: whether those numbers are systematically DIFFERENT
  between the near end (small Z index) and the far end (large Z index) of
  the volume — the literal definition of "there is a near/far gradient to
  visualize."
- `tests/Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp` already
  Tier-1-tests `AccumulateAerialPerspectiveSliceStats()`/
  `FinalizeAerialPerspectiveLutInspection()` (pure aggregation math, no GPU)
  with several small, hand-computed cases — this phase extends that same
  file with new cases for the new per-band aggregation this phase adds.

## Step 3 — The Plan (Detailed Steps)

### 3.1 — Characterization test: codify the current geometry defect

Add to `tests/Renderer/VolumeTexturePreviewMathTests.cpp` (this file already
exists — add these as NEW `TEST(...)` blocks, do not touch any existing
test):

```cpp
// atmosphere-scattering-3 campaign, Phase 1 - codifies, as a permanent
// checked fact, the exact geometry defect PHASE0_MASTER_STRATEGY.md's own
// investigation found: ComputeVolumeCameraSetup() scales boxHalfExtents
// directly proportional to raw texel counts, which is CORRECT for a
// genuine spatial volume but WRONG for the Aerial Perspective froxel
// volume (128x128x32 - see AtmosphereLutRenderer.cpp's
// kAerialPerspectiveVolumeWidth/Height/Depth), whose Z axis is a camera-
// relative DISTANCE SLICE index, not a comparable physical length to its
// X/Y screen-column/row indices. This test is NOT a "fails before fix,
// passes after" regression test - ComputeVolumeCameraSetup() itself is
// NEVER changed by this campaign (see PHASE0's own Locked Design Decision
// 2); it is a CHARACTERIZATION test, permanently documenting the generic
// function's own (still correct, for an ACTUAL spatial volume) behavior,
// so a future reader can see exactly why Phase 2 needed a SECOND, separate
// function instead of just tweaking constants inside this one.
TEST(VolumeTexturePreviewMathTest, AerialPerspectiveVolumeDimensionsProduceSeverelyFlattenedDepthAxisUnderGenericFunction)
{
    const VolumeCameraSetup setup = ComputeVolumeCameraSetup(128, 128, 32);

    // X/Y both hit the generic function's own maximum half-extent (0.5),
    // since width == height == the max dimension here.
    EXPECT_NEAR(setup.boxHalfExtents.x, 0.5f, 1e-5f);
    EXPECT_NEAR(setup.boxHalfExtents.y, 0.5f, 1e-5f);
    // Z (the depth/distance axis - the ONLY axis carrying this LUT's
    // near/far story) is squashed to exactly 32/128 = 0.25 of that.
    EXPECT_NEAR(setup.boxHalfExtents.z, 0.125f, 1e-5f);

    // Written as an explicit ratio assertion too, so the "severely
    // flattened" claim is a checked number, not just an eyeballed one.
    const float depthToWidthRatio = setup.boxHalfExtents.z / setup.boxHalfExtents.x;
    EXPECT_LT(depthToWidthRatio, 0.3f); // 0.25 in practice - comfortably confirms the flattening.
}
```

### 3.2 — Per-band numeric inspection (proves the data-side gradient is real)

Extend `src/Editor/AtmosphereAerialPerspectiveLutInspection.h`:

```cpp
// atmosphere-scattering-3 campaign, Phase 1 - a lightweight per-BAND
// summary (Near/Mid/Far thirds of the volume's own Z/depth range), added
// alongside the existing whole-volume AtmosphereAerialPerspectiveLutInspectionResult
// specifically to answer a question the whole-volume min/max/mean cannot:
// "is there a real, systematic DIFFERENCE between the near end and the far
// end of this volume, or is it just noisy/uniform?" - the literal
// definition of "does this LUT have a near/far gradient worth visualizing
// at all", which is this whole campaign's own root-cause question (see
// PHASE0_MASTER_STRATEGY.md, Step 2.2, point 3).
struct AerialPerspectiveBandSummary {
    int sliceBeginInclusive = 0;
    int sliceEndExclusive = 0;
    float meanTransmittance = 1.0f;
    float meanInScatteringMagnitude = 0.0f;
};

// Reduces ONE band's own accumulated AerialPerspectiveSliceStats (e.g. one
// entry of InspectAerialPerspectiveVolume()'s own bandAccumulators[3] -
// see that function's own updated body below) into a single
// AerialPerspectiveBandSummary. Deliberately a NAMED, header-declared
// function - never a .cpp-local/anonymous-namespace helper - specifically
// so this per-band reduction is directly Tier-1-testable in isolation
// (see Section 3.3 below), mirroring FinalizeAerialPerspectiveLutInspection()'s
// own existing testable shape exactly (mean = sum / texelCount, reusing
// AerialPerspectiveSliceStats's own existing sumTransmittance/
// sumInScatteringMagnitude/texelCount fields directly - no floating-point
// re-derivation from raw pixels needed here, same contract as
// FinalizeAerialPerspectiveLutInspection() itself). Always succeeds - an
// empty/zero-texelCount band reports meanTransmittance=1.0f/
// meanInScatteringMagnitude=0.0f (the struct's own defaults), never a
// divide-by-zero.
AerialPerspectiveBandSummary FinalizeAerialPerspectiveBandSummary(
    const AerialPerspectiveSliceStats& bandStats, int sliceBeginInclusive, int sliceEndExclusive);
```

Add a `std::array<AerialPerspectiveBandSummary, 3> bandSummaries;` field
(Near/Mid/Far, in that fixed order) to
`AtmosphereAerialPerspectiveLutInspectionResult` (`#include <array>`).

In `AtmosphereAerialPerspectiveLutInspection.cpp`'s `InspectAerialPerspectiveVolume()`:
replace the single, flat per-slice loop's accumulation with THREE parallel
`AerialPerspectiveSliceStats` accumulators (`bandAccumulators[3]`), each
slice routed to `bandIndex = std::min(2, static_cast<int>(sliceIndex) * 3 /
depth)` (integer division — for the REAL volume's depth, 32, this actually
produces slices `[0,11)`, `[11,22)`, `[22,32)` (11/11/10 slices) — NOT a
perfectly even `32/3 ≈ 10.67`-per-band split, since `bandIndex` is computed
per-slice from `sliceIndex * 3 / depth` rather than by comparing `sliceIndex`
against precomputed boundaries `depth/3`/`2*depth/3` (those two ways of
expressing "one third of depth" do not coincide exactly for a `depth` not
evenly divisible by 3 — verify this by hand for whatever `depth` is live at
the time, rather than assuming a perfectly even three-way split); "close
enough to equal thirds" is the actual guarantee here, not exact equality —
no need for a perfectly even split), IN ADDITION TO the existing single
`totalStats` accumulator (both keep accumulating from the exact same
per-slice `raw.pixels.data()` — never a second, separate GPU capture pass).
After the loop, finalize each band by calling the new
`FinalizeAerialPerspectiveBandSummary()` (declared above) once per band —
passing `bandAccumulators[i]` plus that band's own recorded
`sliceBeginInclusive`/`sliceEndExclusive` (captured from the same
band-index math above as the loop runs, e.g. the first/last `sliceIndex`
routed to each `bandIndex`) — and populate `result.bandSummaries[0..2]`
with each call's return value directly.

Extend `ToDiagnosticString()` (same file) to append, after the existing
whole-volume lines, one line per band:

```cpp
// Appended inside ToDiagnosticString(), after the existing snprintf() call
// (build a second small buffer per band, or extend the existing one - a
// std::string += is fine here, this is Editor diagnostic text, not a hot
// path):
for (const AerialPerspectiveBandSummary& band : result.bandSummaries) {
    char bandBuffer[192];
    std::snprintf(bandBuffer, sizeof(bandBuffer),
        "\n  slices [%d,%d): mean transmittance=%.6f  mean in-scattering=%.6f",
        band.sliceBeginInclusive, band.sliceEndExclusive, band.meanTransmittance, band.meanInScatteringMagnitude);
    diagnostic += bandBuffer;
}
```
(Adjust variable names to whatever this function's real existing local
variable is called — read the CURRENT file before editing, since Phase 1
of this campaign is the first to touch it since `atmosphere-scattering-2`
shipped it.)

### 3.3 — Tier-1 test for the new per-band aggregation math

Extend `tests/Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp` with
new, PURE (no GPU) tests proving the per-band math itself is correct — this
phase's new `FinalizeAerialPerspectiveBandSummary()` is a plain, public,
header-declared function (see 3.2 above), so these tests call it DIRECTLY,
exactly the same way this file's existing tests already call
`FinalizeAerialPerspectiveLutInspection()` directly (no need to drive the
whole, GPU-touching `InspectAerialPerspectiveVolume()` to exercise this
math):

1. A basic reduction test: build a hand-computed `AerialPerspectiveSliceStats`
   (e.g. via `AccumulateAerialPerspectiveSliceStats()` over a couple of
   hand-encoded texels, mirroring this file's own existing style) and assert
   `FinalizeAerialPerspectiveBandSummary(stats, 5, 9)`'s returned
   `sliceBeginInclusive == 5`, `sliceEndExclusive == 9`, and
   `meanTransmittance`/`meanInScatteringMagnitude` match the hand-computed
   `sum / texelCount` exactly (same tolerance/style as
   `AtmosphereAerialPerspectiveLutInspectionTest`'s own existing mean
   assertions).
2. A "near stays clearer than far" proof: construct two separate
   `AerialPerspectiveSliceStats` accumulators (via
   `AccumulateAerialPerspectiveSliceStats()`, mirroring this file's own
   existing style) — one representing a NEAR band with transmittance
   uniformly `1.0`/in-scattering `0.0`, the other representing a FAR band
   with transmittance `0.5`/in-scattering non-zero — finalize each via
   `FinalizeAerialPerspectiveBandSummary()`, and assert
   `nearSummary.meanTransmittance > farSummary.meanTransmittance` and
   `nearSummary.meanInScatteringMagnitude < farSummary.meanInScatteringMagnitude`
   — i.e. a real, checked proof that "near stays clearer than far" is
   something this tool can actually detect, not just something a diagnostic
   string prints and hopes is right.
3. An empty-band edge case: `FinalizeAerialPerspectiveBandSummary()` given a
   default-constructed (zero-`texelCount`) `AerialPerspectiveSliceStats`
   returns `meanTransmittance == 1.0f`/`meanInScatteringMagnitude == 0.0f`
   (the struct's own defaults) rather than dividing by zero — a real,
   plausible input for a future, smaller test volume whose depth isn't a
   multiple of 3, where one band could legitimately end up empty.

### 3.4 — Do not change any existing behavior

Both changes in this phase are strictly ADDITIVE:
`AtmosphereAerialPerspectiveLutInspectionResult`'s existing fields
(`minTransmittance`/`maxTransmittance`/`meanTransmittance`/etc.,
`likelyVisibleAtDefaultExposure`) and `ToDiagnosticString()`'s existing
whole-volume text are completely unchanged — this phase only APPENDS the
new `bandSummaries` field and its new diagnostic lines. Confirm every
pre-existing test in `AtmosphereAerialPerspectiveLutInspectionTests.cpp`
still passes unmodified.

## Verification

- Fast, targeted compile: build `gte_core` (both modified files compile into
  it) and `GreatTamanaEngineTests`.
- Run the two specific test suites this phase touches (never the full
  suite yet — see `PHASE0_MASTER_STRATEGY.md`'s own workflow rule 6):
  `ctest -C Debug -R VolumeTexturePreviewMathTest --output-on-failure` and
  `ctest -C Debug -R AtmosphereAerialPerspectiveLutInspectionTest --output-on-failure`
  (run from the `build` directory).
- Optional, not a blocker: if convenient, `run_app_background` the Editor
  build and manually confirm (via the Editor's "Atmosphere" panel's existing
  "Inspect Aerial Perspective LUT" button, if a live interactive session is
  available) that the new per-band lines print and show a real, visible
  difference between the Near and Far bands — this is a genuine Tier-2,
  no-automated-coverage manual check (mirrors this whole file's own
  documented Tier-2 status), not a gate on this phase's own completion;
  `stop_app_background` when done if performed.
- Write `PHASE1_COMPLETION_REPORT.md` (include the exact numbers the new
  characterization test asserts, and, if the optional manual check above was
  performed, the real Near/Mid/Far band numbers observed), then commit.
