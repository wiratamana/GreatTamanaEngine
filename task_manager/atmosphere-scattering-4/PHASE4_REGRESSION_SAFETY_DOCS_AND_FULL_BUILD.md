# PHASE4 — Regression Safety Net, Documentation, Full Build & Campaign Closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first)
**Depends on:** Phases 1, 2, and 3 all completed and committed.
**Touches shaders?** No.

---

## Step 1 — The Goal

Close out the campaign:

1. Add explicit regression tests locking in that the **unchanged** opaque-
   geometry composite path still behaves exactly as it did before this
   campaign (nobody accidentally narrowed/broke the real-geometry blend while
   fixing the sky-pixel bypass).
2. Update this codebase's three standing documentation files —
   `AGENTS.md`, `README.md`, `TODO.md` — to describe what this campaign
   changed, mirroring exactly how campaigns 1-3 each closed out their own
   entries.
3. Run a **full** clean build and the **full** `ctest` regression suite (the
   only phase in this campaign allowed to do so, per `PHASE0_MASTER_STRATEGY.md`'s
   workflow rule 7) and confirm zero regressions anywhere in the existing
   suite.
4. Write a final `CAMPAIGN_COMPLETION_REPORT.md` summarizing all four phases,
   mirroring `atmosphere-scattering-3/CAMPAIGN_COMPLETION_REPORT.md`'s own
   shape.

---

## Step 2 — The Situation

By the start of this phase:
- The actual bug is fixed (Phase 2) and backed by a small, focused CPU oracle
  + unit tests (Phase 1).
- A permanent, code-based diagnostic exists to catch a future regression of
  this exact bug class (Phase 3).
- What is **not yet explicitly regression-tested** is the *other* side of the
  branch: that real opaque geometry (a mesh, the reference grid) still fogs
  with distance exactly as it did before this campaign touched the shader.
  Phase 1's own test suite already has partial coverage of this
  (`ZeroStrengthIsAPureSceneColorPassThroughEvenWithGeometry`,
  `FullStrengthMatchesHandComputedBlend`, `PartialStrengthInterpolatesLinearly`
  — see `PHASE1_COMPOSITE_DECISION_CPU_ORACLE_AND_TESTS.md`, Step 3.4, tests
  6-8) but this phase should explicitly widen that coverage rather than
  assume it is already exhaustive.
- Nothing has been written yet to `AGENTS.md`/`README.md`/`TODO.md` — every
  prior phase deliberately deferred that to this final phase, per this
  codebase's own established convention (see e.g. `atmosphere-scattering-2`'s
  own Phase 6, `atmosphere-scattering-3`'s own Phase 5).

---

## Step 3 — The Plan

### 3.1 Widen `tests/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp`

Add these additional cases to the file Phase 1 created (do not create a
second test file — extend the existing one):

1. `ComputeAerialPerspectiveCompositeColorTest.NeverBypassesForAnyDepthStrictlyBelowThreshold`
   — a small table-driven/parameterized loop (or several discrete
   `EXPECT_FALSE`/non-bypass assertions) over a spread of plausible real
   geometry depths (e.g. `0.0f`, `0.001f`, `0.5f`, `0.9f`, `0.99f`,
   `0.9999988f` — one ULP-ish below the threshold) confirming
   `ShouldBypassAerialPerspectiveComposite()` is `false` for every one of
   them, and that `ComputeAerialPerspectiveCompositeColor()` for each
   produces the SAME hand-computed blend formula as tests 7/8 already
   established (i.e. the result is depth-INDEPENDENT once past the bypass
   check — the bypass predicate is a pure function of `rawDepth` alone, never
   of the blend inputs) — this directly guards against a future edit
   accidentally widening or narrowing the bypass condition's threshold.
2. `ComputeAerialPerspectiveCompositeColorTest.BlackAerialContributionWithFullTransmittanceIsANoOpEvenWithGeometry`
   — `rawDepth = 0.5f`, `sampledAerialRgb = Vec3::Zero()`,
   `sampledAerialA = 1.0f`, `strength = 1.0f` → expect the result equals
   `sceneColorRgb` exactly (the physically-meaningful "no atmosphere between
   camera and this point" case for real geometry — e.g. very close objects —
   must still be a true no-op, not just the explicit bypass branch).
3. `ShouldBypassAerialPerspectiveCompositeTest.MonotonicBoundaryNeverFlickers` —
   confirms `ShouldBypassAerialPerspectiveComposite(x)` is `true` for every
   `x` in `{0.999999f, 0.9999995f, 1.0f, 1.5f}` and `false` for every `x` in
   `{0.0f, 0.5f, 0.9f, 0.999998f}` — a slightly broader boundary sweep than
   Phase 1's own original 4 tests, added here specifically as this campaign's
   own closing regression net.

Re-run this test file's own `ctest` filter (per Phase 1's own verification
step) and confirm every case, old and new, passes.

### 3.2 Update `AGENTS.md`

Add a new bullet to the existing "Atmosphere Scattering" section (append
after its current last bullet, "No scene (de)serialization...") — do not
rewrite any existing bullet in that section, only add a new one, mirroring
how `atmosphere-scattering-2`/`atmosphere-scattering-3`'s own campaigns each
appended their own new bullets to this same section without touching the
others:

```markdown
- **The Aerial Perspective Composite pass (`Shaders/AtmosphereAerialPerspectiveComposite.comp`)
  is a PURE PASS-THROUGH for any pixel with no opaque geometry drawn into it
  this frame (`rawDepth >= 0.999999`, the frame's own clear-depth value) -
  see `task_manager/atmosphere-scattering-4/PHASE0_MASTER_STRATEGY.md`.**
  Before this campaign, the composite shader ran its full blend
  UNCONDITIONALLY, double-fogging a sky pixel the Sky Background pass had
  ALREADY finished, correctly, earlier in the same frame - confirmed and
  fixed by the `atmosphere-scattering-4` campaign (see
  `AERIAL_PERSPECTIVE_NO_GEOMETRY_BUG_REPORT_20260911.md` for the original
  root-cause investigation). The bypass DECISION (not the volume's own
  trilinear sample/Z-slice math, which has no CPU equivalent - see that
  campaign's own Locked Design Decision 5) is codified as a small, dedicated,
  Tier-1-tested CPU oracle,
  `src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h/.cpp`'s
  `ShouldBypassAerialPerspectiveComposite()`/
  `ComputeAerialPerspectiveCompositeColor()` - deliberately its OWN new file,
  never added to `AtmosphereMath.h` (see that campaign's own Locked Design
  Decision 2 for why not). A permanent, automated regression guard,
  `src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h/.cpp`'s
  `ValidateAerialPerspectiveSkyPurity()` (a "Validate Aerial Perspective Sky
  Purity" button in the Editor's "Atmosphere" panel, mirroring
  `AtmosphereTransmittanceLutValidation`'s own proven shape), numerically
  confirms every sky pixel's post-composite color still exactly matches its
  pre-composite color, every session, on demand - a future edit that
  reintroduces double-compositing onto background pixels will show up here as
  a non-zero `mismatchingSkyPixelCount` rather than only being caught by a
  human eyeballing a screenshot. `AtmosphereSettings`'s own aerial-perspective
  tunables (`aerialPerspectiveMaxDistanceKm`/`aerialPerspectiveScatteringExaggeration`/
  etc.) were NOT changed by this campaign - they remain exactly as
  `atmosphere-scattering-2` shipped them, since they are correctly, and
  separately, tuned for real opaque geometry.
```

(Write the actual final wording once the real file paths/line numbers are
confirmed at implementation time — this is a close draft, not necessarily
the literal final text; keep the same substance and citations.)

### 3.3 Update `README.md`

Find this project's existing "Status" section (the same place every prior
`atmosphere-scattering-N` campaign added its own one-line/short-paragraph
summary — search for the most recent atmosphere-related entry there first,
to match its exact tone/format) and append one short new entry describing:
the aerial-perspective-on-empty-sky bug, that it is now fixed, and that a
permanent Editor diagnostic exists to catch a regression of it. Keep it to
the same length/style as neighboring entries — this is a user-facing status
log, not a design document.

### 3.4 Update `TODO.md`

Check `TODO.md`'s existing "Atmosphere Scattering" section (referenced by
`AGENTS.md`'s own "No scene (de)serialization..." bullet, which points readers
there for tracked follow-ups). If this bug was ever previously listed there
as a known issue, remove/check it off. If not, no entry is needed purely for
the bug fix itself — but consider adding one new, clearly-scoped forward-
looking note if Phase 3's work surfaced a genuine, real follow-up opportunity
worth tracking (e.g. "extend `ValidateAerialPerspectiveSkyPurity()` to also
cover the Scene View pair, not just Game View" — only add this if it is
judged to be genuinely worth tracking, not merely because a TODO entry feels
obligatory; an empty, padding-only TODO addition is explicitly discouraged by
this whole campaign's own "no vacuous phases/steps" spirit).

### 3.5 Full clean build

Run the project's real, full build (per `BUILDING.md`'s documented
Ninja/MinGW `build` folder workflow — the same command referenced in this
campaign's own task description):

```
cmake --build build
```

(Working directory: the repository root,
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`.) Confirm it completes
with zero errors and inspect warnings for anything newly introduced by this
campaign's own files specifically (pre-existing warnings elsewhere in the
codebase are out of scope to fix here).

### 3.6 Full regression test run

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Confirm:
- Every test that passed before this campaign still passes (no regression).
- Every new test this campaign added (Phase 1's original 8 cases + Phase 4's
  new cases from Step 3.1 above) is present in the run and passes.

If anything fails, treat it as a real regression to diagnose and fix per
`AGENTS.md`'s "Testability & Regression Safety" section rule ("Treat any
newly-failing test as a real regression to fix, not something to work around
by loosening the test's expectation without understanding why it failed") —
do not close out this campaign with a known-red test suite.

### 3.7 Live runtime smoke test (final human/LLM-visual confirmation)

1. `run_app_background` the built engine.
2. Reproduce the original bug-report scenario (empty scene, sky visible) and
   also a scene with a real mesh at varying distances.
3. Via `gte_send_request`, capture both `GET /get_game_view` and, if useful,
   `GET /get_texture?texture_name=GameView` (pre-composite) side by side, to
   visually confirm the sky region is identical between the two and that the
   mesh still fogs correctly with distance.
4. Click "Validate Aerial Perspective Sky Purity" in the Editor and confirm
   `PASS`.
5. `stop_app_background`.

### 3.8 Write `CAMPAIGN_COMPLETION_REPORT.md`

In `task_manager/atmosphere-scattering-4/`, summarizing:
- The original bug and its root cause (one paragraph, can lift directly from
  the bug report's own TL;DR).
- What each of the four phases actually shipped (file-by-file, mirroring
  `atmosphere-scattering-3/CAMPAIGN_COMPLETION_REPORT.md`'s own level of
  detail).
- The full build + full `ctest` result (pass count, zero failures).
- The live smoke-test result (what was visually confirmed).
- Any deviation from this strategy's own original plan, and why (per
  `PHASE0_MASTER_STRATEGY.md`'s own "the real source always wins, but flag
  it" workflow rule).

### 3.9 Final commit

`git_add` (code + all four completion reports + the campaign completion
report + updated `AGENTS.md`/`README.md`/`TODO.md`) and `git_commit` with a
message summarizing the whole campaign, e.g. "atmosphere-scattering-4: fix
aerial perspective double-compositing onto empty sky pixels + permanent
regression diagnostic".
