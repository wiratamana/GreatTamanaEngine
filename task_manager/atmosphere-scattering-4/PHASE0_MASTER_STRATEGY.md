# Atmosphere Scattering — Campaign 4: PHASE0 MASTER STRATEGY
## "Aerial Perspective Applied To Empty Sky" Bug-Fix Campaign

**Status:** Strategy only (no implementation yet). This file is the
orchestrator every child phase document reports back to.

**Source documents (READ THESE FIRST, in this order):**
1. `task_manager/atmosphere-scattering-4/PDF_Extraction_SkyAndAtmosphere_20260911_140048.md`
   (Hillaire, *A Scalable and Production Ready Sky and Atmosphere Rendering
   Technique*, EGSR 2020) — §5.4 "Aerial Perspective LUT" is the load-bearing
   section: aerial perspective is a post-process applied to **already-rendered
   opaque geometry**; it is never meant to touch a pixel that has no geometry
   at all.
2. `task_manager/atmosphere-scattering-4/AERIAL_PERSPECTIVE_NO_GEOMETRY_BUG_REPORT_20260911.md`
   — the confirmed, reproducible root-cause investigation this whole campaign
   fixes.
3. `AGENTS.md`'s "Atmosphere Scattering" section — the accumulated,
   permanent conventions from campaigns 1-3 that this campaign must keep
   following (CPU-oracle discipline, `AtmosphereSettings` tunability,
   post-process-only compositing, no scene serialization, etc.).

---

## Step 1 — The Goal (Where are we going?)

Ship a **code fix**, with a permanent regression test and a permanent
diagnostic tool, such that:

1. A pixel with **no opaque geometry drawn into it** (the sky, still at the
   frame's clear depth of `1.0`) is composited by the Aerial Perspective pass
   as a **pure pass-through** — its final color is *exactly* whatever the Sky
   Background pass already wrote there. Toggling `aerialPerspectiveStrength`
   anywhere in `[0, 2]` must have **zero visible effect on sky pixels ever
   again**.
2. A pixel with **real opaque geometry** (the ground/reference grid, a mesh,
   etc.) keeps being correctly fogged/tinted by distance exactly as before —
   this campaign changes **nothing** about that code path.
3. The specific arithmetic that decides "pass through vs. blend" exists as a
   small, pure, Tier-1-testable C++ function (this codebase's established
   "permanent CPU oracle" pattern — see `AGENTS.md`, "Atmosphere Scattering"
   and "GPU Vertex Skinning") **before** the GLSL is touched, so the shader
   fix is a mechanical mirror of an already-reviewed, already-tested piece of
   logic, not a freehand shader edit nobody can regression-test.
4. A **permanent, code-based Editor diagnostic** exists afterward that can
   catch this exact class of bug again in the future (double-compositing atmosphere
   effects onto empty/background pixels) without a human having to eyeball a
   screenshot — extending this campaign's own established "Validate .../
   Inspect ..." button pattern in the Editor's "Atmosphere" panel.
5. The fix, its tests, and the new diagnostic are committed with a clean
   build and a green `ctest` run, and the campaign's own documentation
   (`AGENTS.md`, `README.md`, `TODO.md`) is updated to describe what changed,
   mirroring how campaigns 1-3 closed out.

---

## Step 2 — The Situation (Where are we now?)

### 2.1 Confirmed root cause (see the bug report for the full trail)

`src/Shaders/AtmosphereAerialPerspectiveComposite.comp`'s `main()`
unconditionally runs:

```glsl
vec3 finalColor = sceneColor * transmittance + inScattering;
imageStore(destinationImage, texel, vec4(finalColor, 1.0));
```

for **every** pixel, including ones where `rawDepth >= 0.999999` (i.e.
nothing was ever drawn there — the sky, still at the frame's own clear
depth). For that case, the shader currently fabricates a `viewDistanceKm =
maxDistanceKm` (the farthest froxel slice) purely so it has *something* to
sample from the Aerial Perspective volume — but that volume was never
designed to represent "infinity"; it is a `pl-sky`/Hillaire-style froxel LUT
that only makes sense relative to real opaque-geometry distances. Since the
Sky Background pass (`AtmosphereSkyBackgroundRenderer`) already wrote the
**final, correct, physically-integrated-to-the-top-of-the-atmosphere** sky
color into that exact same pixel earlier in the same frame (confirmed via its
`VK_COMPARE_OP_EQUAL` / depth-write-disabled pipeline state — see the bug
report §3.2), the composite pass re-fogs an already-complete sky pixel with a
second, shorter-range, artistically-exaggerated haze layer. This is why
toggling `aerialPerspectiveStrength` visibly changes the sky, which is the
report's own "second, easy visual confirmation" of the bug.

### 2.2 Why this is not "just tune the numbers"

`AtmosphereTypes.h`'s `AtmosphereSettings::aerialPerspectiveMaxDistanceKm =
0.5f` / `aerialPerspectiveScatteringExaggeration = 30.0f` defaults were
deliberately, empirically tuned during the `atmosphere-scattering-2` campaign
(see `AGENTS.md`, "Atmosphere Scattering", and
`task_manager/atmosphere-scattering-2/PHASE3_COMPLETION_REPORT.md`) so that
real opaque geometry (the reference grid, test meshes) visibly fogs at the
engine's actual few-hundred-meter test-content scale. Those numbers are
*correct for their intended purpose* (opaque geometry) and must **not** be
casually changed by this campaign — doing so would silently undo prior,
carefully-validated work for an unrelated reason. The bug is **exclusively**
that the composite shader applies this same, intentionally-aggressive volume
to pixels it was never meant to touch at all. Fix the *branch*, not the
*tuning*.

### 2.3 What already exists that this campaign must respect (do not re-invent)

- `AtmosphereMath.h`/`.cpp` is the campaign's **permanent CPU oracle** for
  density/optical-depth/phase-function math. `atmosphere-scattering-2`'s own
  Locked Design Decision 5 explicitly forbids modifying it, or
  `AtmosphereCommon.glsl`'s shared oracle functions, for a feature-local
  tunable — this campaign's fix has nothing to do with density/optical-depth
  math at all, so this rule is not in tension with anything here, but it does
  mean the new CPU-oracle code this campaign adds (Phase 1) must live in its
  **own new, narrowly-scoped file**, never bolted onto `AtmosphereMath.h`.
- `src/Editor/AtmosphereTransmittanceLutValidation.h/.cpp` and
  `src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp` are the two
  existing precedents for "a button in the Editor's Atmosphere panel that
  runs a GPU readback and reports a pass/fail or descriptive-statistics
  result" — Phase 3 of this campaign follows that exact same shape for a
  brand new, narrowly-scoped tool.
- This whole feature has **no** `GTE_ENABLE_ATMOSPHERE` switch and is always
  compiled — nothing in this campaign introduces one.
- No GPU/live-`VkDevice` automated test infrastructure exists yet ("Tier 2" —
  see `TESTING.md`/`AGENTS.md`, "Testability & Regression Safety"). Every
  piece of genuinely new logic this campaign adds must therefore be designed,
  from the start, to be extractable into a pure, Tier-1-testable function —
  never left as untested inline shader/GPU-only logic if it can be avoided.

---

## Step 3 — The Plan (super-detailed, phase by phase)

This campaign is deliberately small in raw code-diff terms (the actual root
cause is a ~15-line shader edit), but is broken into focused, independently
compilable/committable phases so each piece is reviewable and individually
regression-tested, mirroring the discipline every prior `atmosphere-scattering-N`
campaign already established.

| Phase | One-line goal | Primary files touched |
|---|---|---|
| **1** | Add a new, small, pure, Tier-1-testable C++ function that mirrors the EXACT decision + blend arithmetic the composite shader must implement (the CPU oracle for THIS bug fix) — plus its unit tests. Zero shader changes yet. | `src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h` (new), `.cpp` (new), `tests/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp` (new), `CMakeLists.txt`, `tests/CMakeLists.txt` |
| **2** | The actual root-cause fix: add the missing early pass-through branch to `AtmosphereAerialPerspectiveComposite.comp`, mirroring Phase 1's oracle exactly, byte-for-byte in intent. Recompile shaders. | `src/Shaders/AtmosphereAerialPerspectiveComposite.comp` |
| **3** | Regression-proofing: a new, permanent, code-based Editor diagnostic ("Validate Aerial Perspective Sky Purity") that programmatically re-detects this exact bug class in the future — no more relying on a human eyeballing a screenshot. | `src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h` (new), `.cpp` (new), `src/Editor/Panels/AtmospherePanel.h/.cpp`, `src/Editor/ImGuiEditorLayer.cpp`, `CMakeLists.txt` |
| **4** | Regression safety net for the UNCHANGED opaque-geometry path, documentation updates (`AGENTS.md`/`README.md`/`TODO.md`), full clean build + full `ctest` regression run, campaign completion report. | `tests/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp` (extended), `AGENTS.md`, `README.md`, `TODO.md`, a completion report |

### Locked Design Decisions (do not re-litigate)

1. **This is a branch/logic fix, not a re-tune.** `AtmosphereSettings`'s
   existing aerial-perspective defaults (`aerialPerspectiveMaxDistanceKm`,
   `aerialPerspectiveScatteringExaggeration`, `aerialPerspectiveStrength`,
   `aerialPerspectiveDepthExponent`, `aerialPerspectiveSamplesPerSlice`) are
   **not modified** by this campaign. They remain exactly as
   `atmosphere-scattering-2` shipped them.
2. **No change to `AtmosphereMath.h`/`AtmosphereCommon.glsl`'s shared density/
   optical-depth/phase-function oracle functions.** This bug has nothing to
   do with that math. The new CPU oracle this campaign adds lives in its own
   new, dedicated file (Phase 1), never inside `AtmosphereMath.h`.
3. **No change to `AtmosphereFrameUniforms`/`AtmosphereParametersGpu`'s GPU
   buffer layout.** No new push-constant/storage-buffer field is needed —
   the bug report's own proposed fix (§4) confirms the existing
   `sourceDepth`/`sourceColor` bindings are already everything the shader
   needs; it is purely a missing branch.
4. **No change to `AtmosphereAerialPerspectiveVolume.comp`** (the LUT
   *generation* pass). It is fine as-is — the bug is entirely in how the
   composite pass *consumes* that LUT for the no-geometry case.
5. **The new Phase 1 CPU oracle intentionally does NOT reproduce the
   volume's own trilinear 3D-texture sampling/Z-slice math** (`textureSize`,
   `ViewDepthToFroxelSlice`, the half-texel bias, the first-slice fade-in
   mix). That machinery has no meaningful CPU equivalent (a GPU texture fetch
   is not something to hand-port) and is completely unaffected by this bug —
   consistent with `atmosphere-scattering-2`'s own precedent that this
   specific machinery has no CPU oracle. Phase 1's oracle instead starts from
   an **already-sampled, already-first-slice-blended** aerial texel
   (`rgb`=in-scattering, `a`=transmittance) as its input — exactly the two
   values the shader has in hand right before its own final two lines — and
   mirrors only the branch + the final blend arithmetic, which is exactly the
   part this bug lives in.
6. **Every phase writes its own `PHASEn_COMPLETION_REPORT.md`** in this same
   folder once its own compile check passes, then commits (code + report) to
   git — never bundle two phases into one commit, mirroring every prior
   campaign's own workflow rule.
7. **No full build/full regression test (`ctest`) until Phase 4.** Phases 1-3
   only need a fast, targeted compile check (see each phase's own
   "Verification" section).

### Non-goals (explicitly out of scope)

- No change to test-scene content, camera placement, or any `.gta`/asset
  file.
- No new HTTP endpoint/query parameter.
- No volumetric clouds, god-rays, or any other new atmosphere visual
  feature.
- No change to the Aerial Perspective volume's fixed `128x128x32` resolution.
- No scene (de)serialization work for `AtmosphereSettings` (unchanged,
  pre-existing limitation).
- No GPU-headless/Tier-2 automated test infrastructure work (out of scope for
  this bug fix, tracked separately per `TESTING.md`).

### Cross-phase file map (every file this campaign touches)

**New files:**
- `src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h` (Phase 1)
- `src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.cpp` (Phase 1)
- `tests/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp` (Phase 1, extended Phase 4)
- `src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h` (Phase 3)
- `src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.cpp` (Phase 3)

**Modified files:**
- `CMakeLists.txt` (Phases 1, 3)
- `tests/CMakeLists.txt` (Phase 1)
- `src/Shaders/AtmosphereAerialPerspectiveComposite.comp` (Phase 2)
- `src/Editor/Panels/AtmospherePanel.h` (Phase 3)
- `src/Editor/Panels/AtmospherePanel.cpp` (Phase 3)
- `src/Editor/ImGuiEditorLayer.cpp` (Phase 3 — adds the new
  `m_lastAerialPerspectiveSkyPurityResult` member and updates the
  `BuildAtmospherePanel(...)` call site to pass the render graph + that new
  result-storage reference; confirmed present via a real source read on
  2026-09-11, see `PHASE3_REGRESSION_DIAGNOSTIC_TOOLING.md`'s own Step 3.4 —
  this file was missing from this map in an earlier revision of this
  document, added here so the map stays a complete, accurate index of every
  file this campaign touches)
- `AGENTS.md`, `README.md`, `TODO.md` (Phase 4)

### Workflow rules every child phase must follow

1. Read this file (`PHASE0_MASTER_STRATEGY.md`) and the two source documents
   listed at the top before starting.
2. Read the previous phase's own completion report (once it exists) for any
   deviation/clue before starting the next phase — mirrors every prior
   campaign's own convention.
3. Do the phase's code work exactly as scoped in its own document. If reality
   disagrees with this document (a file has moved, a signature differs, a
   line number is stale), **the real source code always wins** — fix it and
   note the deviation in that phase's own completion report, the same
   "flag it, don't silently improvise" discipline `AtmosphereTypes.h`'s own
   header comments already demonstrate.
4. Fast compile check only (see each phase). Full build/ctest is Phase 4
   only.
5. Write `PHASEn_COMPLETION_REPORT.md`, then `git_add` + `git_commit`
   (code + report together, one commit per phase).
