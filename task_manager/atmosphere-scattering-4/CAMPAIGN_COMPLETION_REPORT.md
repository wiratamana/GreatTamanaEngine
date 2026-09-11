# Atmosphere Scattering — Campaign 4: CAMPAIGN COMPLETION REPORT
## "Aerial Perspective Applied To Empty Sky" Bug-Fix Campaign

**Parent:** `PHASE0_MASTER_STRATEGY.md`
**Phases:** 1 (CPU oracle + tests), 2 (shader fix), 3 (regression diagnostic tooling), 4 (this phase — regression safety net, docs, full build/ctest, live smoke test, closeout)
**Status:** Complete. Full clean build succeeded, full `ctest` regression suite passed 100% (1296/1297, 1 pre-existing machine-gated smoke test skipped, zero regressions), live runtime smoke test confirmed the fix end-to-end.

---

## 1. The original bug and its root cause

`src/Shaders/AtmosphereAerialPerspectiveComposite.comp`'s `main()` ran its full
`sceneColor * transmittance + inScattering` Aerial Perspective blend
**unconditionally, for every pixel on screen** — including pixels where
nothing was ever drawn that frame (the sky, still sitting at the frame's own
clear depth of `1.0`). For that "no geometry" case, the shader fabricated a
`viewDistanceKm = maxDistanceKm` (the volume's farthest froxel slice) purely
to have *something* to sample, and re-blended that sample onto a sky pixel
the Sky Background pass had **already** finished, correctly, earlier in the
very same frame (confirmed via that pass's own `VK_COMPARE_OP_EQUAL`/
depth-write-disabled pipeline state). This produced a second, independent,
artistically-exaggerated haze layer stacked on top of an already-complete
sky — directly confirmed by the fact that toggling `aerialPerspectiveStrength`
visibly changed the sky itself, which should never happen once the fix is in
place. See `AERIAL_PERSPECTIVE_NO_GEOMETRY_BUG_REPORT_20260911.md` for the
full original investigation trail. Critically, this was a **branch/logic
bug, not a tuning problem** — `AtmosphereSettings`'s aerial-perspective
tunables (`aerialPerspectiveMaxDistanceKm`, `aerialPerspectiveScattering
Exaggeration`, etc.) were correctly, deliberately tuned by
`atmosphere-scattering-2` for real opaque geometry and were never touched by
this campaign.

---

## 2. What each phase shipped

### Phase 1 — Composite Decision: CPU Oracle + Tests
(`PHASE1_COMPOSITE_DECISION_CPU_ORACLE_AND_TESTS.md`,
`PHASE1_COMPLETION_REPORT.md`)

- **New:** `src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h/.cpp`
  — a small, pure, Tier-1-tested CPU oracle, deliberately its OWN new file
  (never added to `AtmosphereMath.h`, per this campaign's own Locked Design
  Decisions 2/5): `kAerialPerspectiveFarPlaneDepthThreshold = 0.999999f`,
  `ShouldBypassAerialPerspectiveComposite(float rawDepth)`, and
  `ComputeAerialPerspectiveCompositeColor(rawDepth, sceneColorRgb,
  sampledAerialRgb, sampledAerialA, strength)` — mirrors exactly the branch +
  final blend arithmetic the shader needed to implement, deliberately NOT
  reproducing the volume's own trilinear-sample/Z-slice machinery (no
  meaningful CPU equivalent).
- **New:** `tests/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp`
  — 8 original test cases covering the bypass threshold boundary and the
  blend formula at zero/full/partial strength.
- **Modified:** `CMakeLists.txt`, `tests/CMakeLists.txt` (registered the new
  files).
- Zero shader changes; zero visible runtime effect (as designed).

### Phase 2 — The Actual Fix: Pass-Through Branch in the Composite Shader
(`PHASE2_SHADER_PASSTHROUGH_FIX.md`, `PHASE2_COMPLETION_REPORT.md`)

- **Modified:** `src/Shaders/AtmosphereAerialPerspectiveComposite.comp` —
  added the missing early pass-through branch (`if (rawDepth >= 0.999999) {
  imageStore(destinationImage, texel, vec4(sceneColor, 1.0)); return; }`),
  mirroring Phase 1's oracle mechanically, placed BEFORE any volume-lookup/
  Z-slice machinery so a sky pixel never even samples the aerial-perspective
  volume. No C++/render-graph/CMakeLists.txt change was needed anywhere —
  confirmed by re-reading every candidate call site
  (`AtmosphereLutRenderer.cpp`, `AtmospherePassSequence.cpp`,
  `Application.cpp`, `RenderPasses.cpp`/`.h`).
- Verified with a fast targeted shader recompile plus visual confirmation:
  toggling `aerialPerspectiveStrength` between `0.0`/`1.0`/`2.0` produced
  **byte-for-byte identical** `GET /get_swapchain` PNG captures of the sky
  (135339 bytes each); a spawned near/far cube pair confirmed the real-geometry
  fogging path was completely untouched.

### Phase 3 — Permanent Regression Diagnostic: "Validate Aerial Perspective Sky Purity"
(`PHASE3_REGRESSION_DIAGNOSTIC_TOOLING.md`, `PHASE3_COMPLETION_REPORT.md`)

- **New:** `src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h/.cpp`
  — `ValidateAerialPerspectiveSkyPurity(Renderer&, const rg::RenderGraph&,
  const char* preCompositeColorTextureName, const char*
  compositedColorTextureName, double toleranceUnorm8 = 1/255)`: captures
  BOTH the pre-composite ("GameView") and post-composite
  ("GameViewComposited") color textures, and for every pixel whose PRE-
  composite depth is at/beyond the bypass threshold (a genuine sky pixel),
  numerically confirms its post-composite color is unchanged within
  tolerance — reporting pixel count checked, mismatch count, max/mean
  channel delta.
- **Genuine bug found and fixed during this phase's own verification**: the
  original sketch compared raw bytes channel-index-for-channel-index without
  accounting for `"GameView"` being BGRA (`VK_FORMAT_B8G8R8A8_UNORM`, the
  swapchain-negotiated format) while `"GameViewComposited"` is RGBA
  (`VK_FORMAT_R8G8B8A8_UNORM`, required for compute-shader storage-image
  support) — a real, reproducible false-FAIL was caught (415413/828990
  mismatching) and fixed via two small helpers,
  `IsBgraColorFormat()`/`ReadLogicalRgb()`, that resolve each texture's own
  logical RGB using its own real captured format before comparing.
- **Modified:** `CMakeLists.txt`, `src/Editor/Panels/AtmospherePanel.h/.cpp`
  (new "Validate Aerial Perspective Sky Purity" button + PASS/FAIL/ERROR
  readout), `src/Editor/ImGuiEditorLayer.cpp` (new
  `m_lastAerialPerspectiveSkyPurityResult` member + updated
  `BuildAtmospherePanel(...)` call site).
- Verified live: **PASS** (0/828990 mismatching) with the Phase 2 fix in
  place; **FAIL** (414495/828990 mismatching) when the fix was temporarily
  reverted to `if (false)`; **PASS** again after restoring the real fix —
  confirming the tool genuinely detects this exact regression class.

### Phase 4 — Regression Safety Net, Documentation, Full Build & Campaign Closeout (this phase)

- **Widened `tests/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp`**
  (extended the existing file — no second test file created) with 3 new
  cases, per Step 3.1:
  - `ComputeAerialPerspectiveCompositeColorTest.NeverBypassesForAnyDepthStrictlyBelowThreshold`
    — a depth sweep (`0.0f, 0.001f, 0.5f, 0.9f, 0.99f, 0.9999988f`) confirming
    the bypass predicate never fires below the threshold AND the blend
    formula's result stays depth-independent past it.
  - `ComputeAerialPerspectiveCompositeColorTest.BlackAerialContributionWithFullTransmittanceIsANoOpEvenWithGeometry`
    — real geometry (`rawDepth = 0.5`) with zero in-scattering + full
    transmittance is an exact no-op, not just the explicit bypass branch.
  - `ShouldBypassAerialPerspectiveCompositeTest.MonotonicBoundaryNeverFlickers`
    — a broader boundary sweep (`{0.999999, 0.9999995, 1.0, 1.5}` all bypass;
    `{0.0, 0.5, 0.9, 0.999998}` all do not).
  - All 11 tests in this file (8 original + 3 new) pass — verified via a
    targeted `ctest -R AerialPerspectiveComposite` run before the full suite.
- **`AGENTS.md`** — appended ONE new bullet to the end of the existing
  "Atmosphere Scattering" section (after "No scene (de)serialization..."),
  describing the pass-through branch, the CPU oracle, and the new permanent
  Editor diagnostic — no existing bullet in that section was rewritten.
- **`README.md`** — appended one new short "Status" entry (after the
  `atmosphere-scattering-3` entry) describing the bug, the fix, and the new
  permanent diagnostic, matching the style/length of neighboring entries.
- **`TODO.md`** — the bug was never listed in the "Atmosphere Scattering"
  section (confirmed, nothing to remove/check off). Added ONE new,
  genuinely-scoped forward-looking entry: extending
  `ValidateAerialPerspectiveSkyPurity()` to also cover the "Scene" view's
  own `"SceneView"`/`"SceneViewComposited"` pair (today it only checks
  "Game") — a real, clearly-scoped gap this phase's own review surfaced, not
  a padding-only entry.
- **Full clean build**: `cmake --build build` — succeeded with zero errors
  (reported "ninja: no work to do" since every target, including
  `GreatTamanaEngineTests`, had already been rebuilt earlier in this same
  session by this phase's own targeted compile checks — confirming
  everything, including the widened test file, built cleanly).
- **Full regression suite**: `ctest -C Debug --output-on-failure` from
  `build/` — **100% tests passed, 1297 total (1296 passed + 1 pre-existing
  machine-gated smoke test skipped, `PmxLoaderRealModelSmokeTest.
  LoadsAnMmdModelIfPresentOnThisMachine`), zero failures, zero regressions**.
  All 11 `AerialPerspectiveComposite`-prefixed tests (Phase 1's original 8 +
  this phase's new 3) are present and passing (test IDs 739-749).
- **Live runtime smoke test**: see §4 below.

---

## 3. Full build + full `ctest` result

- `cmake --build build` — **zero errors**.
- `ctest -C Debug --output-on-failure` — **1297 tests, 100% passed** (1296
  passed, 1 skipped — the pre-existing, machine-gated
  `PmxLoaderRealModelSmokeTest`, unrelated to this campaign and skipped the
  same way every prior campaign's full run has reported it). **Zero
  regressions** anywhere in the pre-existing suite, and every new test this
  campaign added across all four phases (Phase 1's 8 + Phase 4's 3 = 11
  `AerialPerspectiveComposite`-prefixed tests) is present and passing.

---

## 4. Live smoke-test result

1. `run_app_background` launched `GreatTamanaEngine.exe` from `build/`.
2. **Empty-sky scene** (`GET /get_game_view`, after the user helped click the
   "Game" tab so it counted as visible — see §5 deviation note below):
   confirmed a clean sky gradient with no double-fogging artifact.
3. Spawned `POST /instantiate_light` ("Sun") and two
   `POST /instantiate_primitive` cubes at different distances/scales via
   HTTP (`NearCube`, 3x3x3 scale at ~15 world units away; `FarCube`, 30x30x30
   scale at 400 world units away, near the froxel volume's 500m far edge),
   repositioned with `POST /set_entity_trs` to land inside the camera's
   frustum.
4. `GET /get_game_view` after spawning: `NearCube` renders crisp/dark-grey
   with clearly visible faces/edges (effectively unfogged at this short
   range); `FarCube` renders visibly smaller and lighter/lower-contrast,
   blending toward the horizon color — the expected, still-fully-working
   aerial-perspective haze increasing with distance, confirming this
   campaign's fix did not touch, weaken, or bypass the real-geometry path.
5. `GET /get_texture?texture_name=GameView` (the pre-composite target)
   captured alongside, for visual cross-reference — the sky region reads
   identically to the post-composite `GET /get_game_view` capture.
6. **"Validate Aerial Perspective Sky Purity"** clicked in the Editor's
   "Atmosphere" panel: reported **PASS**.
7. `stop_app_background` cleanly terminated the engine.

---

## 5. Deviations from the original plan, and why

- **GUI mouse-click automation.** This session's tool set has no reliable
  built-in GUI-input-automation capability for this engine's native
  SDL/Vulkan window (unlike a browser, which `playwright` could drive) —
  the same limitation Phase 2/3 already ran into and worked around
  differently in their own sessions. Rather than risk a fragile,
  hand-rolled `mouse_event`/`SetCursorPos` PowerShell script (which Phase 3's
  own session used successfully but which is inherently brittle across
  window-position/DPI differences), this session asked the human user
  directly (via `ask_questions`) to click the "Game" tab and the "Validate
  Aerial Perspective Sky Purity" button and report the result — a more
  reliable substitute for the exact same manual verification step
  `PHASE4_REGRESSION_SAFETY_DOCS_AND_FULL_BUILD.md`'s own Step 3.7 calls
  for. The user confirmed the button reports **PASS**. No production code
  or shipped default was altered by this deviation.
- **`cmake --build build` reported "ninja: no work to do"** rather than
  visibly recompiling anything, because this same phase's own earlier
  targeted `cmake --build build --target GreatTamanaEngineTests` step (done
  immediately after widening the test file, to get a fast confirmation
  before running the full suite) had already rebuilt every target this
  build graph depends on, including `gte_core`/`GreatTamanaEngine` — the
  full build command is still the one actually run and confirmed as this
  phase's own mandatory step, it simply had nothing left to do by that
  point, which is itself confirmation of a fully up-to-date, zero-error
  build.
- No other deviation of substance — every phase's own file/line-number
  citations matched the real, live source at the point each was
  implemented (see each phase's own completion report for the ordinary,
  expected, small line-number drifts already called out there).

---

## 6. Final state

- The Aerial Perspective Composite pass is a pure pass-through for any pixel
  with no opaque geometry drawn into it this frame — permanently backed by a
  Tier-1-tested CPU oracle, a mechanically-mirrored shader fix, and a
  permanent, automated Editor diagnostic that will catch a future regression
  of this exact bug class without relying on a human eyeballing a
  screenshot.
- Real opaque geometry's aerial-perspective fogging is completely unchanged
  and untuned by this campaign.
- Full test suite: 1297 tests, 100% passing (1 pre-existing, unrelated,
  machine-gated skip), zero regressions.
- All code, tests, documentation updates, and this report are committed
  together in one commit, per this campaign's own workflow rule.
