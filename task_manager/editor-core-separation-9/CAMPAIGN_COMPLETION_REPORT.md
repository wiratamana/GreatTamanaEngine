# editor-core-separation-9 — CAMPAIGN COMPLETION REPORT

**Branch:** `feature/editor-core-separation`
**Campaign:** 5 phases, `PHASE0_MASTER_STRATEGY.md` through
`PHASE5_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`.
**Status: DONE.** `IPluginRenderPassBuilder_v3` — a real, second-generation,
feature-agnostic plugin render-feature ABI surface, a strict superset of
capability over `IPluginRenderPassBuilder_v2` while never touching, removing,
or deprecating `_v1`/`_v2` — is shipped, tested, and documented.

## The goal, restated plainly

`_v2` (the `editor-core-separation-6` campaign) fixed multi-plugin
compositing but left plugin authors with a CLOSED enumeration: exactly 3 fixed
C++ methods (`AddSolidFillPass`/`AddRadialVignettePass`/`AddColorGradePass`),
each hardcoded to one `opCode` inside one host-owned uber compute shader. A
third-party plugin author could never add a genuinely new visual effect
without an engine rebuild, and "a compute pass writes a texture/buffer, a
later pass reads it" — ordinary render-graph plumbing every internal engine
feature already takes for granted — was completely impossible for any
plugin. This campaign closes both gaps with `_v3`: a real, generic,
two-phase setup/execute resource-graph builder plus a growable, host-owned
operation registry, so a brand-new operation becomes a host-side content
addition instead of an ABI change.

## Phase-by-phase summary

| Phase | What it shipped | Status |
|---|---|---|
| PHASE1 — Resource Vocabulary & ABI Foundation | `plugins/gte_plugin_abi/PluginRenderResource.h` (`PluginTextureHandle`/`PluginBufferHandle`, curated `PluginResourceAccess`, `PluginTextureDesc`/`PluginBufferDesc`); `IPluginRenderPassBuilder_v3.h`'s full interface declarations (`IPluginPassSetupContext`, `IPluginCommandRecorder`, `IPluginBlackboard`, the top-level builder); `IRenderFeatureModule_v3` appended to `IRenderFeatureModule.h`; the pure, Tier-1-tested `PluginRenderResourceTranslation.h/.cpp` mapping functions (9 new tests). Zero behavior change — nothing calls or implements any of it yet. | DONE, zero deviations |
| PHASE2 — Operation Registry & Adapter v3 | `PluginRenderOperationRegistry` (owns every op's pipeline/descriptor-set-layout/slot-table, migrated `RenderFeatureOps.comp`/`RenderFeatureBlend.comp` pipeline ownership out of `RenderFeatureCompositor` with zero `_v2` behavior change); `PluginRenderPassBuilderAdapter_v3` (full resource creation, pass declaration, `Dispatch`/`DrawFullscreenTriangle`, handle-translation table, `GetPrivateOutputTarget()`/`TryGetNamedTexture("SceneColor")`); `gte.builtin.box_blur` registered as a genuinely new operation; `RenderFeatureCompositor`'s `moduleV3` wiring; 3 permanent `_v3` demo plugins reimplementing `_v2`'s exact 3 effects, pixel-parity A/B proven byte-identical; 25 new Tier-1 validation tests. Two real bugs found and fixed during this phase's own mandatory live verification: a `RenderFeatureCompositor::m_device` regression that crashed the editor (0xC0000005), and a real use-after-free hazard in the adapter's `execute`-closure capture scheme (fixed via a `shared_ptr`-held `TranslationState`). | DONE, 2 real bugs found+fixed, both documented |
| PHASE3 — Generic Resource Plumbing & Blur Demo Proof | The real, permanent 2-pass GPU downsample-blur inside `plugins/demo_render_feature_v3/`: a compute pass reads `"SceneColor"`, writes a new half-res texture via `gte.builtin.box_blur`; a graphics pass reads it and draws into `GetPrivateOutputTarget()` via a brand-new `gte.builtin.blit_fullscreen` operation (the first `DrawFullscreenTriangle`-kind registry entry). Verified with a real, live, HTTP-driven, mathematically-checked pixel proof (sharp-vs-blurred captures matching the shader's own hand-computed kernel radius). A pre-existing, unrelated environmental fact was investigated and correctly diagnosed (the `_v1` legacy demo plugins' always-on magenta clear, not a bug). | DONE, zero code deviations beyond 2 documented, load-bearing implementation corrections (a dummy vertex buffer for the fullscreen-triangle draw; a transparent scratch depth-attachment write) |
| PHASE4 — Blackboard & Diagnostics Integration | The real `IPluginBlackboard` implementation (`RenderFeatureCompositor::BlackboardAdapter`, replacing PHASE2's `NoOpPluginBlackboard` stand-in), proven with a real, minimal, 2-plugin publish/fetch demo that VISIBLY changes a second plugin's own rendering (a widened vignette radius). The diagnostics live-check confirmed individual `_v3` passes are ALREADY generically visible via `GET /render_graph` (zero new code needed, exactly as PHASE0's Step 2.7 predicted) — but found one real, confirmed gap in the SEPARATE "Plugin Render Features" section (no way to tell a `_v3` row from a `_v2` row structurally), fixed with one small, additive `bool isV3` field. | DONE, one real, confirmed gap found and fixed (pre-authorized latitude, not scope creep) |
| PHASE5 — Docs, Full Regression & Campaign Closeout | Updated `AGENTS.md`'s "Plugin Architecture" section, `docs/conventions/plugin-architecture.md` (a full new "`_v3` Generic Render-Feature System" section), and `plugins/gte_plugin_abi/PublicSurface.md`'s "Added by later phases" list. Ran the ONE full clean build (`rmdir /s /q build` + fresh reconfigure + `cmake --build build`, 557/557 steps, zero errors) + full `ctest` regression pass (1882 tests, 100% of executed passing, 2 legitimate pre-existing skips, 0 failures) + one final live, HTTP-driven smoke test with BOTH `_v2` and `_v3` demo plugins loaded and rendering simultaneously, exercising resource creation, the operation registry, and the blackboard all at once, with zero unexpected warnings/errors. | DONE, this report |

## What actually shipped (the real, final ABI surface)

- **`plugins/gte_plugin_abi/PluginRenderResource.h`** — `PluginTextureHandle`/
  `PluginBufferHandle` (POD index+generation), `PluginResourceAccess`
  (`ColorAttachmentWrite`/`ShaderRead`/`ComputeShaderRead`/`ComputeShaderWrite`
  — a curated subset, never a raw mirror of every internal
  `rg::ResourceAccess` value), `PluginTextureDesc` (width/height/a 3-value
  curated `Format` enum), `PluginBufferDesc` (sizeBytes only).
- **`plugins/gte_plugin_abi/IPluginRenderPassBuilder_v3.h`** —
  `IPluginPassSetupContext` (declare-time reads/writes), the two named caps
  (`kPluginComputeDispatchMaxGroupsPerDimension = 64`,
  `kPluginMaxOperationParamBytes = 128`), `IPluginCommandRecorder`
  (execute-time `BindTexture`/`BindBuffer`/`Dispatch`/`DrawFullscreenTriangle`),
  `PluginBlackboardValueKind`/`PluginBlackboardValue`/`IPluginBlackboard`, and
  the top-level `IPluginRenderPassBuilder_v3` (`CreateTexture`/`CreateBuffer`/
  `TryGetNamedTexture`/`GetPrivateOutputTarget`/`AddGraphicsPass`/
  `AddComputePass`/`Blackboard`).
- **`IRenderFeatureModule_v3`** (appended to the existing
  `IRenderFeatureModule.h`) — `GetRenderFeatureDescriptor()` (the SAME
  `GtePluginRenderFeatureDescriptor`, unchanged) + `AddRenderGraphPasses(IPluginRenderPassBuilder_v3&)`.
- **`gte_core`-internal** (never crossing the ABI boundary):
  `PluginRenderOperationRegistry.h/.cpp`, `PluginRenderPassBuilderAdapter_v3.h/.cpp`,
  `PluginRenderPassBuilderAdapterV3Validation.h`, `PluginRenderResourceTranslation.h/.cpp` —
  confirmed by direct re-read before writing `PublicSurface.md`'s own bullet.
- **4 built-in registry operations**: `gte.builtin.solid_fill`/
  `_radial_vignette`/`_color_grade` (reproducing `_v2`'s exact 3 effects,
  proven byte-identical) and 2 genuinely NEW operations,
  `gte.builtin.box_blur` and `gte.builtin.blit_fullscreen` — proving R13's
  central claim (a new operation is a host-side content addition, never an
  ABI change) twice, once for a compute op and once for a graphics op.
- **3 permanent `_v3` demo plugins** committed to the repo:
  `plugins/demo_render_feature_v3/` (the real, permanent 2-pass GPU blur, plus
  the blackboard's publisher half), `plugins/demo_render_feature_v3_second/`
  (the blackboard's fetcher half, its own vignette widened by the fetched
  value), `plugins/demo_render_feature_v3_third/` (the fill+color-grade pair,
  proving the third shared uber-op).

## Locked Product/Architecture Decisions — final status

All 13 Locked Decisions from `PHASE0_MASTER_STRATEGY.md` were followed
exactly as written; none were challenged or reversed during real
implementation. In particular:

1. `_v3` IS now the recommended path for new plugin authors (`AGENTS.md`/
   `docs/conventions/plugin-architecture.md` both point future authors at it
   first) — `_v2` remains fully supported, forever, unchanged.
2. **The `Dispatch()` group-count cap (64x64x1 groups per call) is
   host-enforced, loud-warning-and-skip, never a crash — but has NO
   device-lost recovery path of any kind**, restated here as this campaign's
   own final, honest caveat (see below).
3. `TryGetNamedTexture()` exposes exactly ONE name, `"SceneColor"` — no
   `"SceneDepth"` or anything else was added.
4. `PluginRenderOperationRegistry` remains 100% host-authored/curated this
   whole campaign — no mechanism for a plugin to register its own custom
   operation exists or was scaffolded.
5. `GetPrivateOutputTarget()` was added exactly as specified — no implicit
   "last write wins" auto-detection anywhere.
6. `_v3` reuses the EXACT SAME per-plugin private-target + 5-mode blend
   pipeline `_v2` already has — confirmed unchanged, byte-for-byte, by the
   pixel-parity A/B proof in PHASE2 and the full regression pass in PHASE5.
7. The real, permanent 2-pass GPU blur demo plugin shipped, committed,
   verified with a real, live, mathematically-checked pixel proof (PHASE3).

## Real deviations found during implementation (all honestly disclosed at the time, restated here)

1. **PHASE2 — `RenderFeatureCompositor::m_device` regression** (a real crash,
   0xC0000005, found and fixed during PHASE2's own mandatory pre-check before
   any `_v3` code was even exercised).
2. **PHASE2 — a real use-after-free hazard** in the adapter's `execute`
   closure capture scheme, not present in either master doc's own pseudocode
   sketch — fixed by extracting the handle-translation table into a
   `std::shared_ptr`-held `TranslationState` captured by value.
3. **PHASE3 — `PluginRenderOpInfo` gained a `dummyVertexBuffer` field** not
   in PHASE2's own pseudocode, because the shared `Pipeline` class always
   requires a real (if unread) vertex-input binding — mirrors
   `GBufferValidation.cpp`'s own already-documented identical finding.
4. **PHASE3 — the adapter transparently attaches a scratch depth-stencil
   write** to every `_v3` graphics pass, since `Pipeline`'s constructor
   always enables a real depth test and `_v3`'s own curated
   `PluginResourceAccess` vocabulary has no depth-declaration method.
5. **PHASE4 — host-side blackboard logging** (not plugin-side, since a
   plugin `.dll` has zero logging capability of its own) — a more generic
   solution than the phase's own literal text describes, benefiting every
   future `_v3` plugin's blackboard traffic, not just this campaign's own
   demo.
6. **PHASE4 — the `isV3` diagnostics gap was a real, confirmed finding**, not
   a "looks fine, nothing to add" outcome — fixed with the one small,
   pre-authorized, additive field the phase's own text anticipated almost
   verbatim.

None of these deviations required deviating from any Locked Product/
Architecture Decision itself — every one was a genuine implementation-level
correction found by this campaign's own required live verification
discipline ("prove it for real, not just compiles-and-doesn't-crash"), not a
change of plan.

## Final honest caveats (restated plainly, not smoothed over)

- **The `Dispatch()` group-count cap (64x64x1 groups per call) has NO
  device-lost recovery path of any kind** — this is a restated, pre-existing
  fact about this engine as a whole (no workload of any kind, internal or
  plugin-driven, has a documented device-lost recovery story today), not a
  `_v3`-specific regression or omission. A plugin that somehow manages to
  drive the GPU into a bad state despite the cap has no better outcome than
  any other GPU-heavy internal engine feature would in the same situation.
- **No plugin-supplied shader bytecode/SPIR-V of any kind** — every operation
  in `PluginRenderOperationRegistry` remains 100% host-authored/curated. A
  plugin registering its OWN custom operation backed by its OWN shader is a
  separate, future, security-reviewed campaign (Design Doc R17/P4), not
  started, not scaffolded, not stubbed here.
- **This repository's own default toolchain still cannot produce a
  shared-CRT-linked binary** (a pre-existing, already-documented condition
  from `editor-core-separation-3`, restated by every plugin-architecture
  campaign since — this campaign changed nothing about it).
- **`_v1`/`_v2` render-feature plugins are still not unified into one
  deterministic composited order with each other** when both are loaded
  simultaneously (a pre-existing, already-documented `editor-core-separation-6`
  acceptance, unchanged by this campaign).

## Final verification evidence

- **Full clean build**: `rmdir /s /q build` then a fresh
  `cmake -S . -B build -G Ninja` reconfigure (no network access — every
  third-party dependency this project needs lives in a persistent,
  gitignored `include/`/`third_party/` tree outside `build/`, confirmed via
  `BUILDING.md`'s own documented guarantee before deleting anything) plus
  `cmake --build build` — **557/557 build steps, zero errors.**
- **Full `ctest -C Debug --output-on-failure`**: **1882 tests total, 1880
  PASSED (100% of executed tests), 2 legitimate, pre-existing,
  environment-gated skips, 0 FAILED** — unchanged from this campaign's own
  PHASE2/PHASE4 fast-run baseline (1857 before this campaign's own PHASE1
  work + 25 new PHASE2 validation tests = 1882), confirming the full clean
  rebuild introduced zero regressions of any kind.
- **Live, HTTP-driven smoke test**: `GreatTamanaEditor.exe` run with all 9
  real, permanent plugin `.dll`s loaded simultaneously (`_v1` x2, `_v2` x2,
  `_v3` x3, plus the editor-panel and hello-world demos) — BOTH `_v2` and
  `_v3` render-feature demo plugins confirmed loaded and contributing real
  passes this frame (`GET /get_logs?keyword=DemoRenderFeatureV3` shows every
  one of `_v3`'s own passes actually executing); `GET /get_game_view` returns
  a real, correctly-composited 186756-byte PNG; `GET /render_graph`
  structurally confirms every real internal pass with correct
  reads/writes/`render_pass_event`; the blackboard's publish/fetch round-trip
  confirmed live (`GET /get_logs?category=RenderFeatureCompositor.Blackboard`);
  zero adapter rejections (`GET /get_logs?category=PluginRenderPassBuilderAdapter_v3`
  returns `"count":0`); zero errors of any kind
  (`GET /get_logs?min_level=Error` returns `"count":0`) across the entire
  session. Cleanly shut down via `stop_app_background`.

See each `PHASEn_COMPLETION_REPORT.md` in this same folder for the full,
phase-by-phase implementation detail and verification evidence this summary
condenses.
