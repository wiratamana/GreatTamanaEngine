# ATMOSPHERE_CAMPAIGN_COMPLETION_REPORT.md

Campaign-level summary tying together all nine phases of the Atmosphere
Scattering + Aerial Perspective campaign described in
`ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md`, now that Phase 9
(`ATMOSPHERE_PHASE9_VALIDATION_DEBUG_TOOLING_AND_DOCS_v1.md`) has landed —
mirroring `task_manager/render_graphs/RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`'s
own role for the Render Graph campaign. Every phase below has its own
detailed, standalone `ATMOSPHERE_PHASEn_COMPLETION_REPORT.md` — this document
is a map of the whole journey, not a replacement for any of them.

## Why this campaign existed

The engine had zero atmosphere/sky/fog of any kind before this campaign — no
`Light`/`DirectionalLight` ECS component, no sky rendering, and every mesh was
shaded only by a small, fixed-direction ambient+lambert term. See
`ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md`'s own "Step 2: The Situation" for
the full, source-grounded audit that motivated this campaign. This campaign
gave the engine a physically-based, real-time atmosphere scattering system
with aerial perspective — the same class of technique documented in
Sébastien Hillaire's *"A Scalable and Production Ready Sky and Atmosphere
Rendering Technique"* (Eurographics 2020) and implemented as a reference,
engine-agnostic sample in [hoffstadt/pl-sky](https://github.com/hoffstadt/pl-sky)
— hand-ported (never vendored, never compiled, never committed) into this
engine's own `src/Renderer/Atmosphere/`/`src/Shaders/` from a clone kept in a
gitignored `_reference/` scratch folder.

## The nine phases, in one line each

| # | Phase | One-line outcome |
|---|-------|-------------------|
| 1 | Reference Analysis + Physical Model Foundations | Cloned/read `pl-sky`; stood up `src/Renderer/Atmosphere/AtmosphereMath.h/.cpp` — the permanent, Tier-1-tested CPU oracle for every density-profile/optical-depth/transmittance/phase-function formula this campaign uses; verified/extended shared-GLSL-`#include` support in `cmake/CompileShaders.cmake`. |
| 2 | Volume Texture + Render Graph 3rd Resource Kind | `src/Renderer/VolumeTexture.h/.cpp` (a real 3D Vulkan image) plus a genuine THIRD `gte::rg::ResourceKind` — `VolumeTextureHandle`/`ImportVolumeTexture()` — taught to the compiler/barrier-planner/executor. The single highest-risk phase in the campaign; proven end-to-end with a disposable validation pass before being deleted. |
| 3 | Transmittance LUT | First real compute pass: `Shaders/AtmosphereTransmittanceLut.comp` (256x256) + `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp` — the single home for every atmosphere GPU pass this campaign adds. Established the "storage buffer, never a true UBO" binding convention every later phase followed. |
| 4 | Multi-Scattering LUT | Second compute pass, `AtmosphereMultiScatteringLut.comp` (64x64), reading Phase 3's output. Found and fixed a real engine gap along the way: `Renderer::CaptureImagePixels()` could not yet capture an 8-bytes/pixel HDR texture — fixed with a new `Encoding/HdrColorVisualization.h/.cpp`. |
| 5 | Sky-View LUT | Third compute pass, `AtmosphereSkyViewLut.comp` (200x100, one per Game/Scene View) — the campaign's first genuinely PER-FRAME pass, introducing `AtmosphereFrameUniforms` (camera height, sun direction) and the per-view `std::unordered_map<std::string, ViewState>` state-storage pattern every later per-view pass reused. |
| 6 | Aerial Perspective Froxel Volume | Fourth compute pass, `AtmosphereAerialPerspectiveVolume.comp` (128x128x32) — the campaign's first real `VolumeTexture` consumer. Found and fixed a genuine Phase 2 infrastructure gap: a volume-texture-only write could never survive culling at all until `RenderGraphBuilder::KeepVolumeTextureOutput()` was added. |
| 7 | Sky Background + Composite Passes | Two new full-screen passes — a Sky Background draw (`AtmosphereSkyBackgroundRenderer.h/.cpp`) and an Aerial Perspective Composite compute pass (`AtmosphereAerialPerspectiveComposite.comp`) — wired into the REAL, permanent per-frame Game/Scene View pass sequence for the first time (`src/Application/AtmospherePassSequence.h/.cpp`). **This is the phase that made the feature visible at all** — every subsequent screenshot/capture of the Game/Scene View includes it. Also added `DepthBuffer::allowSampledAccess` and a new `ResourceUsage::isDepthResource` render-graph flag. |
| 8 | Sun ECS + Editor Controls | A new `DirectionalLight` ECS component + `src/Renderer/Atmosphere/DirectionalLightResolver.h/.cpp` (the real, first-active-wins sun resolution, replacing Phase 5's hardcoded placeholder) + a new `AtmosphereSettings` struct + the Editor's new "Atmosphere" panel. |
| 9 | Validation, Debug Tooling, and Docs | The numeric CPU-vs-GPU parity tool (`AtmosphereTransmittanceLutValidation.h/.cpp`) — the LUT passed with zero mismatches on its first real run; the aerial-perspective volume's permanent debug-slice mirror (Phase 2's own deferred gap, finally closed); confirmed Render Graph panel visibility; a full clean build + full `ctest` regression pass (1205 tests, zero regressions); this `README.md`/`AGENTS.md`/`TODO.md` documentation pass. |

## Data flow (what each phase produced, and who consumed it)

```
Phase 1: AtmosphereParametersGpu (physical constants) + AtmosphereMath.h (CPU oracle)
             |
Phase 2: VolumeTexture primitive + RenderGraph 3rd resource kind (infrastructure only)
             |
Phase 3: TransmittanceLut (2D texture)  -------------------------\
             |                                                    |
Phase 4: MultiScatteringLut (2D texture, reads TransmittanceLut)  |
             |                                                    |
Phase 5: SkyViewLut (2D texture, reads both above) + AtmosphereFrameUniforms
             |                                                    |
Phase 6: AerialPerspectiveVolume (3D VolumeTexture, reads TransmittanceLut + MultiScatteringLut + AtmosphereFrameUniforms)
             |
Phase 7: SkyBackground pass (reads SkyViewLut) + AerialPerspectiveComposite pass (reads AerialPerspectiveVolume + SceneColor + SceneDepth)
             |
Phase 8: DirectionalLight ECS component + AtmosphereSettings feed AtmosphereFrameUniforms every frame
             |
Phase 9: Validates the Transmittance LUT against the CPU oracle, adds the volume's debug-slice mirror, confirms Render Graph visibility, full build+test pass, docs
```

## Cumulative test coverage

This campaign added new Tier-1 tests at four of its nine phases (the rest were
Tier-2 shader/GPU-pass work with no automated-test infrastructure available,
per `AGENTS.md`'s own "Testability & Regression Safety" — verified instead by
live capture/visual inspection, as each phase's own completion report
documents in detail):

| Phase | New tests | What they cover |
|---|---|---|
| 1 | 25 | `AtmosphereMath.h`/`AtmosphereParameters.h` — density profiles, optical depth/transmittance, phase functions, default Earth parameters, world-unit conversion. |
| 2 | 10 | `RenderGraphTypesTests.cpp` — `VolumeTextureHandle`/`VolumeTextureDesc`/`ResourceUsage::ForVolumeTexture()`. |
| 6 | 2 | `RenderGraphCompilerTests.cpp` — the `KeepVolumeTextureOutput()` culling-survival fix (both the positive and negative case). |
| 9 | 4 | `TransmittanceLutUvToHeightZenith()` — the new C++ port of the shader's own UV parameterization. |

**41 new Tier-1 tests total.** Phase 9's own full, clean-build `ctest` run
confirms the whole engine test suite now stands at **1205 tests, 100% passing**
(1 pre-existing, machine-gated smoke test skipped — unrelated to this
campaign), with **zero regressions** introduced at any point across all nine
phases — every individual phase's own completion report already confirmed a
green targeted build before moving on; Phase 9 is what finally re-confirmed
this holds true for the ENTIRE suite, from a genuinely clean rebuild, not just
the atmosphere-specific subset each earlier phase checked incrementally.

## What is genuinely proven vs. what remains open

**Proven, in shipped production code, verified this session:**

- A physically-plausible sky renders behind all scene geometry in both "Game"
  and "Scene" views, reacting live to the sun's (`DirectionalLight`) rotation
  (Phase 8's own live-rotation capture proof) and to `AtmosphereSettings`
  panel edits (ground albedo tint, aerial perspective strength, sky exposure).
- Aerial perspective genuinely composites onto already-rendered opaque
  geometry via a real post-process pass reading the froxel volume — confirmed
  functioning (if subtle at close range, exactly as physically expected) via
  Phase 7's own spawned-primitive sanity check.
- **The Transmittance LUT's GPU output is now NUMERICALLY proven correct
  against the permanent CPU oracle** — not merely "looks plausible" — with
  zero texels exceeding a documented, principled tolerance on its first real
  run. This is the one genuine, repeatable correctness proof this campaign
  owed itself per Phase 9's own stated goal, and it passed cleanly.
- The Render Graph's new third resource kind (`VolumeTexture`) is exercised
  by a real, shipped, always-running production pass (the aerial-perspective
  volume) — not just Phase 2's own disposable validation code.
- Every atmosphere pass is correctly visible in the Editor's "Render Graph"
  panel (name, resource read/write chips, ordering/culling) with zero
  atmosphere-specific code needed in that panel's own reshape logic — proof
  the Render Graph campaign's own generality claims hold for a real, novel,
  much-later-arriving feature.
- A full clean build (all three targets) and a full `ctest` regression pass
  (1205 tests) both succeed with zero regressions, and a live runtime smoke
  pass confirms every expected named texture (four LUTs + the new debug-slice
  texture + composited Game/Scene views) reports sane, live-updating data.

**Still open — recorded explicitly in `TODO.md`'s new "Atmosphere Scattering"
section, not afterthoughts:**

1. **No scene (de)serialization for `DirectionalLight`/`AtmosphereSettings`.**
   A Sun entity or tuned atmosphere settings do not survive a Save/Load cycle
   today, exactly like a `Camera` entity doesn't either yet.
2. **No volumetric clouds, no god-rays/light-shafts, no general lighting
   system, no day-night animation.** All explicitly out of scope from Phase
   0's own Locked Design Decisions/"What We Will NOT Do" — `DirectionalLight`
   exists solely to drive the atmosphere, not general mesh lighting.
3. **Per-render-graph-pass GPU timing remains generally unimplemented** — a
   pre-existing, campaign-external gap (see
   `RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`'s own "Still open" list),
   deliberately NOT attempted by this campaign's own Phase 9, per its explicit
   scope refusal.
4. **The release-build/both-Editor-panels-hidden direct-to-swapchain path
   does not get the atmosphere effect** — flagged by Phases 7, 8, and 9 in
   turn, never resolved; `AddPresentPass()`'s own direct-render branch needs
   its own explicit design discussion before this is closed.

## Recommendation for whoever picks up the next session

None of the four open items above block anything else in the engine today —
prioritize based on actual need, not this list's own ordering. If a future
session DOES pick one up: (1) scene serialization is the most
self-contained/lowest-risk (a new `SceneObjectKind`, following the exact
pattern `AGENTS.md`'s "Scene Serialization" section already documents); (2)
per-render-graph-pass GPU timing is a real, engine-wide `GpuTimingService`
generalization, not something to attempt piecemeal per-feature; (3) a general
lighting system and day-night animation are genuinely new, substantial
features that deserve their own from-scratch design discussion rather than
being bolted onto this campaign's own `DirectionalLight` component
opportunistically. This nine-phase campaign is now considered **COMPLETE**.
