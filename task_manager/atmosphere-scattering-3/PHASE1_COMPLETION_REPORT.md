# PHASE1_COMPLETION_REPORT — Root-Cause Instrumentation and Regression Tests

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE1_ROOT_CAUSE_INSTRUMENTATION_AND_REGRESSION_TESTS.md` in full. No
previous phase completion report existed to read (Phase 0 is the orchestrator
document only, per the task instructions).

## Summary

This phase made **zero rendering/behavior changes** — it is pure
instrumentation and test-writing, exactly as scoped. Two additive changes
landed:

1. A permanent **characterization test** in
   `tests/Renderer/VolumeTexturePreviewMathTests.cpp` that codifies the exact
   geometry defect described in `PHASE0_MASTER_STRATEGY.md` (Step 2.2): feeding
   the real Aerial Perspective volume's dimensions (`128x128x32`) into the
   existing, unmodified `ComputeVolumeCameraSetup()` produces a depth
   (`boxHalfExtents.z`) squashed to exactly 1/4 of the X/Y half-extents.
2. A new **per-band (Near/Mid/Far third) numeric summary** layered on top of
   the existing whole-volume `AtmosphereAerialPerspectiveLutInspection` tool
   (`src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp`), plus
   Tier-1 tests proving the new reduction math is correct in isolation.

No file outside this phase's own scoped file map
(`tests/Renderer/VolumeTexturePreviewMathTests.cpp`,
`src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp`,
`tests/Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp`) was touched.
`VolumeTexturePreviewMath.h/.cpp`'s `ComputeVolumeCameraSetup()` itself was
**not modified** — per Locked Design Decision 2, this phase only adds a new
test that exercises its existing, unchanged behavior.

## Step 3.1 — Characterization test (implemented exactly as specified)

Added `TEST(VolumeTexturePreviewMathTest,
AerialPerspectiveVolumeDimensionsProduceSeverelyFlattenedDepthAxisUnderGenericFunction)`
to `tests/Renderer/VolumeTexturePreviewMathTests.cpp`, verbatim per the phase
document's own code block. Real numbers observed from the actual test run
(`ComputeVolumeCameraSetup(128, 128, 32)`):

- `boxHalfExtents.x == 0.5`
- `boxHalfExtents.y == 0.5`
- `boxHalfExtents.z == 0.125` (exactly `32/128 * 0.5`)
- `depthToWidthRatio == boxHalfExtents.z / boxHalfExtents.x == 0.25`, which is
  `< 0.3` as asserted.

This is a characterization test, not a "fails before/passes after" regression
test — it will keep passing after Phase 2/3 add a *second*, dedicated
function; it exists purely to make the generic function's own (still correct,
for a genuine spatial volume) behavior a permanent, checked fact.

## Step 3.2 — Per-band numeric inspection (implemented exactly as specified)

`src/Editor/AtmosphereAerialPerspectiveLutInspection.h`:

- Added `#include <array>`.
- Added `AerialPerspectiveBandSummary` (`sliceBeginInclusive`/
  `sliceEndExclusive`/`meanTransmittance`/`meanInScatteringMagnitude`).
- Declared `FinalizeAerialPerspectiveBandSummary(const
  AerialPerspectiveSliceStats&, int sliceBeginInclusive, int
  sliceEndExclusive)` as a named, header-declared, Tier-1-testable function.
- Added `std::array<AerialPerspectiveBandSummary, 3> bandSummaries;` to
  `AtmosphereAerialPerspectiveLutInspectionResult`, appended after the
  existing `likelyVisibleAtDefaultExposure` field — every pre-existing field
  is untouched.

`src/Editor/AtmosphereAerialPerspectiveLutInspection.cpp`:

- Implemented `FinalizeAerialPerspectiveBandSummary()` — a pure
  `sum / texelCount` reduction, mirroring
  `FinalizeAerialPerspectiveLutInspection()`'s own mean computation exactly,
  with the same "empty band -> defaults, never divide by zero" contract.
- `InspectAerialPerspectiveVolume()` now accumulates THREE parallel
  `AerialPerspectiveSliceStats` accumulators (`bandAccumulators[3]`) alongside
  the pre-existing `totalStats`, fed from the exact same per-slice
  `raw.pixels.data()` inside the same loop (no second GPU capture pass). Each
  slice routes to `bandIndex = std::min(2, sliceIndex * 3 / depth)`, and each
  band's own `sliceBeginInclusive`/`sliceEndExclusive` are recorded from the
  first/last slice index that actually routed to it (so an uneven split, or a
  future smaller/differently-shaped volume, is handled correctly rather than
  assuming an exact `depth/3` boundary). After the loop,
  `FinalizeAerialPerspectiveBandSummary()` is called once per band and the
  results are written into `result.bandSummaries[0..2]`.
- Extended `ToDiagnosticString()` to append one line per band after the
  existing whole-volume text, exactly as specified (`"\n  slices [%d,%d):
  mean transmittance=... mean in-scattering=..."`).
- Every pre-existing field/behavior (`minTransmittance`/`maxTransmittance`/
  `meanTransmittance`/etc., the existing whole-volume diagnostic text) is
  unchanged — confirmed by all 7 pre-existing tests in
  `AtmosphereAerialPerspectiveLutInspectionTests.cpp` still passing unmodified.

## Step 3.3 — Tier-1 tests for the new per-band aggregation math

Added three new tests to
`tests/Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp`, calling
`FinalizeAerialPerspectiveBandSummary()` directly (no GPU/live volume
involved):

1. `BandSummaryReductionMatchesHandComputedMean` — a 2-texel hand-built slice
   (transmittance 1.0 and 0.5, in-scattering magnitude 0 and 5) reduces to
   `meanTransmittance == 0.75`, `meanInScatteringMagnitude == 2.5`, with
   `sliceBeginInclusive == 5`/`sliceEndExclusive == 9` passed straight through.
2. `NearBandStaysClearerThanFarBand` — a NEAR band (transmittance 1.0,
   in-scattering 0.0) and a FAR band (transmittance 0.5, in-scattering
   magnitude 5.0) are finalized independently and asserted
   `nearSummary.meanTransmittance > farSummary.meanTransmittance` and
   `nearSummary.meanInScatteringMagnitude < farSummary.meanInScatteringMagnitude`
   — a real, checked proof that "near stays clearer than far" is something
   this tool can actually detect.
3. `EmptyBandReportsDefaultsRatherThanDividingByZero` — a default-constructed
   (zero `texelCount`) `AerialPerspectiveSliceStats` reduces to
   `meanTransmittance == 1.0f`/`meanInScatteringMagnitude == 0.0f` (the
   struct's own defaults), never a divide-by-zero, for a depth not evenly
   divisible by 3.

## Deviations from the strategy document

None of substance. One small, deliberate implementation choice not spelled
out verbatim in the phase document: `InspectAerialPerspectiveVolume()`
records each band's `sliceBeginInclusive`/`sliceEndExclusive` by tracking the
first/last `sliceIndex` that actually routed to that `bandIndex` during the
loop (via two small `std::array<int, 3>` trackers initialized to `-1`),
rather than precomputing fixed boundaries up front — this was necessary
because the phase document itself warns the `sliceIndex * 3 / depth` split
does not always land on exact `depth/3` boundaries, so deriving the reported
range directly from what actually happened is more honest than assuming an
even split. This does not change any asserted behavior; it is the mechanism
used to satisfy the phase document's own requirement.

## Verification performed

### Targeted compile

- `cmake --build build --target gte_core` — succeeded (rebuilt
  `AtmosphereAerialPerspectiveLutInspection.cpp` plus two Editor files that
  transitively include the changed header — `AtmospherePanel.cpp`,
  `ImGuiEditorLayer.cpp` — all compiled cleanly, zero warnings/errors).
- `cmake --build build --target GreatTamanaEngineTests` — succeeded (rebuilt
  `AtmosphereAerialPerspectiveLutInspectionTests.cpp` and
  `VolumeTexturePreviewMathTests.cpp`, relinked the test binary).
- `cmake --build build --target GreatTamanaEngine` — succeeded (full engine
  executable relinked, for the runtime smoke check below).

### Targeted test runs

`ctest -C Debug -R VolumeTexturePreviewMathTest --output-on-failure`:

```
100% tests passed out of 9
```

(8 pre-existing tests + the 1 new characterization test, all passing.)

`ctest -C Debug -R AtmosphereAerialPerspectiveLutInspectionTest --output-on-failure`:

```
100% tests passed out of 10
```

(7 pre-existing tests + the 3 new band-summary tests, all passing.)

### Optional runtime smoke check (performed)

Ran the real engine (`run_app_background`, PID 3088) and queried its embedded
HTTP server directly, confirming the engine still builds/runs/serves exactly
as before this phase's changes:

- `GET /list_textures` — confirmed `AtmosphereAerialPerspectiveVolume_GameView`/
  `..._SceneView` are still live `"texture3d"` entries at `128x128x32`,
  unchanged.
- `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView` —
  still returns the same thin, flattened, nearly-featureless dark-blue
  parallelogram `PHASE0_MASTER_STRATEGY.md`'s own investigation described —
  visually confirming this phase introduced **no visible change at all**,
  exactly as intended (the actual fix is Phase 2's job). Screenshot captured
  and inspected directly during this session.
- The Editor's own "Inspect Aerial Perspective LUT" button (which would
  exercise the new per-band diagnostic text end-to-end against live GPU data)
  is an ImGui UI control with no HTTP endpoint of its own — driving it
  requires interactive UI automation this session's toolset doesn't have, so
  per the phase document's own "optional, not a blocker" wording, this one
  specific manual check was not performed; the per-band aggregation math
  itself is instead proven directly via the three new Tier-1 tests above
  (Step 3.3), which do not require a live volume/GPU device at all.
- `stop_app_background(pid: 3088)` — engine closed cleanly afterward.

## Tool anomalies

None. Every tool call behaved as expected this session (two `edit_line`
calls triggered the tool's own documented auto-dedup safety net for a
boundary-duplicate closing brace/line, which is expected, helpful behavior
per the tool's own description, not a malfunction).

## Files changed

- `tests/Renderer/VolumeTexturePreviewMathTests.cpp` (new test added)
- `src/Editor/AtmosphereAerialPerspectiveLutInspection.h` (new struct/function
  declaration/field added)
- `src/Editor/AtmosphereAerialPerspectiveLutInspection.cpp` (new function
  implemented, `InspectAerialPerspectiveVolume()` extended, `ToDiagnosticString()`
  extended)
- `tests/Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp` (3 new
  tests added)
- `task_manager/atmosphere-scattering-3/PHASE1_COMPLETION_REPORT.md` (this
  file)

## Next step

Phase 2 (`PHASE2_ATMOSPHERE_AWARE_PREVIEW_CAMERA_FRAMING.md`) is the primary
fix — a second, dedicated camera + proxy-box setup function for the Aerial
Perspective volume specifically, selected by `texture_name` prefix. This
phase's characterization test and per-band tooling are now available for
Phase 2 (and later phases) to lean on when confirming the fix actually
changes something.
