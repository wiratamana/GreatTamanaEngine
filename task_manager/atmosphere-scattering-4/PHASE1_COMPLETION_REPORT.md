# PHASE1 COMPLETION REPORT — Composite Decision: CPU Oracle + Unit Tests

**Parent:** `PHASE0_MASTER_STRATEGY.md`
**Phase document:** `PHASE1_COMPOSITE_DECISION_CPU_ORACLE_AND_TESTS.md`
**Status:** Complete. Zero shader changes. Zero visible runtime effect (as
designed — nothing calls the new function yet).

---

## What was done

1. **`src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h`**
   (new) — declares:
   - `constexpr float kAerialPerspectiveFarPlaneDepthThreshold = 0.999999f;`
   - `bool ShouldBypassAerialPerspectiveComposite(float rawDepth) noexcept;`
   - `Vec3 ComputeAerialPerspectiveCompositeColor(float rawDepth, const Vec3& sceneColorRgb, const Vec3& sampledAerialRgb, float sampledAerialA, float strength) noexcept;`

   Implemented and worded exactly as specified in the phase document's own
   Step 3.1 (file header comment, doc comments, scope-limit note all
   transcribed verbatim). Lives under `src/Renderer/Atmosphere/`, NOT added to
   `AtmosphereMath.h` (per PHASE0 Locked Design Decisions 2/5).

2. **`src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.cpp`**
   (new) — implements both functions exactly as specified in Step 3.2:
   `ShouldBypassAerialPerspectiveComposite()` is a single `>=` comparison;
   `ComputeAerialPerspectiveCompositeColor()` early-returns `sceneColorRgb`
   unchanged when bypassed, otherwise computes
   `inScattering = sampledAerialRgb * strength`,
   `transmittance = mix(1.0, sampledAerialA, strength)` (written as
   `1.0f + (sampledAerialA - 1.0f) * strength`, algebraically identical to
   `mix`), and returns `sceneColorRgb * transmittance + inScattering`.

3. **`src/Math/Vec3.h` — confirmed, no changes needed.** Re-read the live file
   before writing the `.cpp`: `operator*(const Vec3&, float)` (line 26) and
   `operator+(const Vec3&, const Vec3&)` (line 21) already exist exactly as
   the phase document anticipated. No new operator was added.

4. **`tests/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp`**
   (new) — all 8 required test cases from Step 3.4, using the same
   GoogleTest/namespace style as `tests/Renderer/Atmosphere/AtmosphereMathTests.cpp`
   (`namespace gte { namespace { TEST(...) { ... } } }`, no `using namespace`
   directive, matching that file's own convention exactly):
   - `ShouldBypassAerialPerspectiveCompositeTest.TrueExactlyAtClearDepthValue`
   - `ShouldBypassAerialPerspectiveCompositeTest.TrueAtTheDocumentedThresholdItself`
   - `ShouldBypassAerialPerspectiveCompositeTest.FalseJustBelowTheThreshold`
   - `ShouldBypassAerialPerspectiveCompositeTest.FalseForOrdinaryOpaqueGeometryDepth`
   - `ComputeAerialPerspectiveCompositeColorTest.BypassReturnsSceneColorUnchangedRegardlessOfAerialInputs`
   - `ComputeAerialPerspectiveCompositeColorTest.ZeroStrengthIsAPureSceneColorPassThroughEvenWithGeometry`
   - `ComputeAerialPerspectiveCompositeColorTest.FullStrengthMatchesHandComputedBlend`
   - `ComputeAerialPerspectiveCompositeColorTest.PartialStrengthInterpolatesLinearly`

   Test 7/8's expected values match the phase document's own hand-computed
   numbers exactly (`(0.7, 0.6, 0.55)` at full strength, `(0.85, 0.8, 0.775)`
   at partial strength).

5. **`CMakeLists.txt`** — re-read fresh immediately before editing. The
   `AtmosphereMath.cpp` line was at index 422 (0-based) at edit time (this
   drifted slightly from the phase document's stale "421-422" reference, as
   expected/warned about by the document itself). Inserted the two new lines
   directly after it:
   ```
   src/Renderer/Atmosphere/AtmosphereMath.h        (unchanged, line 421)
   src/Renderer/Atmosphere/AtmosphereMath.cpp      (unchanged, line 422)
   src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h   (new, line 423)
   src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.cpp (new, line 424)
   src/Renderer/Atmosphere/DirectionalLightResolver.h  (unchanged, now line 425)
   ```

6. **`tests/CMakeLists.txt`** — re-read fresh immediately before editing.
   - `GTE_TEST_SOURCES` list: `Renderer/Atmosphere/AtmosphereMathTests.cpp`
     was at index 1753 (0-based) at edit time (also slightly drifted from the
     document's stale "~1753" reference — confirmed the same line number by
     coincidence, but re-verified live rather than assumed). Inserted
     `Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp`
     immediately after it, immediately before
     `Renderer/Atmosphere/DirectionalLightResolverTests.cpp`.
   - Top-of-file "Tier 1 test files" comment block: added a new entry
     (mirroring the existing `AtmosphereMathTests.cpp`/
     `DirectionalLightResolverTests.cpp` entries' own format/tone) right
     between those two files' own comment entries, describing what this new
     test file covers.

## Deviations from the phase document

None of substance — only the expected, explicitly-anticipated line-number
drift (both files' insertion points were re-confirmed by a fresh read
immediately before editing, per the phase document's own instruction, and
turned out to match the document's own stale references almost exactly). No
`Vec3` operator needed to be added. No shader file was touched (as scoped).

## Verification

1. **Targeted compile check** — `cmake --build build --target GreatTamanaEngineTests`
   from the repository root. Result: **success**. `gte_core` rebuilt
   (`AtmosphereAerialPerspectiveCompositeMath.cpp.obj` compiled cleanly, no
   warnings related to the new code), `GreatTamanaEngineTests.exe` relinked
   successfully. (The KTX-Software "cannot describe anything"/git-version
   warning in the build log is pre-existing, unrelated third-party-fetch
   noise, not caused by this change.)

2. **Targeted test run** — from the `build` directory:
   ```
   ctest -C Debug -R AerialPerspectiveComposite --output-on-failure
   ```
   Result: **8/8 tests passed** (100%), test IDs 739-746:
   - `ShouldBypassAerialPerspectiveCompositeTest.TrueExactlyAtClearDepthValue` — Passed
   - `ShouldBypassAerialPerspectiveCompositeTest.TrueAtTheDocumentedThresholdItself` — Passed
   - `ShouldBypassAerialPerspectiveCompositeTest.FalseJustBelowTheThreshold` — Passed
   - `ShouldBypassAerialPerspectiveCompositeTest.FalseForOrdinaryOpaqueGeometryDepth` — Passed
   - `ComputeAerialPerspectiveCompositeColorTest.BypassReturnsSceneColorUnchangedRegardlessOfAerialInputs` — Passed
   - `ComputeAerialPerspectiveCompositeColorTest.ZeroStrengthIsAPureSceneColorPassThroughEvenWithGeometry` — Passed
   - `ComputeAerialPerspectiveCompositeColorTest.FullStrengthMatchesHandComputedBlend` — Passed
   - `ComputeAerialPerspectiveCompositeColorTest.PartialStrengthInterpolatesLinearly` — Passed

No full clean build / full `ctest` regression run was performed, per this
phase's own scope (that is Phase 4's job).

## Next step

Phase 2 (`PHASE2_SHADER_PASSTHROUGH_FIX.md`, context only — not implemented
in this session) will add the missing early pass-through branch to
`src/Shaders/AtmosphereAerialPerspectiveComposite.comp`, mirroring this
phase's oracle mechanically.
