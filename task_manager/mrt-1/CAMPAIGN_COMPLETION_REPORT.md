# `mrt-1` — Campaign Completion Report: Multi-Render-Target (MRT) / G-Buffer Support

Parent: `PHASE0_MASTER_STRATEGY.md`. This is the final phase (`PHASE5_DOCS_TESTS_AND_FULL_VALIDATION.md`)
of the five-phase `mrt-1` campaign — the whole campaign's final integration
checkpoint: documentation, a full Tier-1 test sweep, the campaign's one and
only full build + full `ctest` regression pass, and a final live,
running-engine visual proof tying every earlier phase's claims together.

## Status: DONE

`PHASE1_COMPLETION_REPORT.md`, `PHASE2_COMPLETION_REPORT.md`,
`PHASE3_COMPLETION_REPORT.md`, and `PHASE4_COMPLETION_REPORT.md` were all read
in full before starting, per this phase's own prerequisite (and the top-level
task instruction). Every phase's own recorded decision/deviation was carried
forward correctly into this final phase — see "Per-phase outcome summary"
below.

## Administrative note (branch), re-confirmed one final time

This phase's own top-level task instructions again said "Stay on the current
branch: `feature/logger-impl`" — the same stale/mistaken instruction
PHASE2/3/4's own reports already flagged and corrected. Re-verified via
`git status` before starting: the repository's actual current branch is
`feature/render-pass-impl`, with a clean working tree (PHASE1-4's commits
already present). Stayed on `feature/render-pass-impl`; `feature/logger-impl`
was never touched. No fresh `ask_questions` round-trip was needed — this is
now a well-established, repeatedly-confirmed correction across all five
phases of this campaign.

## Per-phase outcome summary

- **PHASE1 (Setup Layer, Ordered Color Attachments)** — added
  `kMaxColorAttachments = 8`, `ColorAttachmentDesc`, and
  `PassRecord::colorAttachments` (`RenderGraphTypes.h`);
  `WriteColorAttachment()` now appends to an ordered list instead of
  overwriting, with a debug-only cap assert. Proved, via new Tier-1 tests,
  that `RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp` need ZERO changes
  for a real 3-color-write pass. **Deviation**: `PassRecord::colorClearValue`
  was deliberately KEPT (not removed per the phase document's own
  illustrative code sample) so `RenderGraph::ExecuteCompiledGraph()` — out of
  scope for PHASE1 — kept reading a valid value until PHASE2 landed.
- **PHASE2 (Execute Layer, MRT Recording)** — `RenderGraph::ExecuteCompiledGraph()`
  rewritten to build one `VkRenderingAttachmentInfo` per entry in
  `pass.colorAttachments`, with a real `colorAttachmentCount`; added
  `FindMismatchedColorAttachmentExtent()` (a pure, Tier-1-tested function) and
  a real `std::runtime_error` throw for a genuinely mismatched multi-attachment
  extent. Confirmed the branch-name correction (`feature/render-pass-impl`,
  not `feature/logger-impl`) via `ask_questions` — this is the origin of the
  "always re-verify, never assume" instruction that propagated through every
  later phase (including this one). `colorClearValue` confirmed fully dead in
  production but still deliberately kept (same reasoning as PHASE1).
- **PHASE3 (Pipeline Layer, N-Target PSO)** — `Pipeline` gained a real
  `std::span<const VkFormat>`-taking constructor (one
  `VkPipelineColorBlendAttachmentState` per target); the original
  single-`VkFormat` constructor now delegates into it, unchanged in
  behavior. Parallel `std::span` overloads added to
  `GpuResourceFactory::CreatePipeline()`/`Renderer::CreatePipeline()`. Every
  pre-existing single-format call site re-confirmed compiling/behaving
  identically. Re-confirmed, via a fresh `search_in_dir`, the corrected
  `Renderer::CreatePipeline()` call-site enumeration (see "Corrections made
  during this campaign's pre-implementation double-check pass" below) — only
  `MeshAssetGpuCatalog.cpp`/`PrimitiveGpuCatalog.cpp` are real external call
  sites; sky background/scene grid/mesh preview/GPU skinning/Atmosphere are
  NOT `Pipeline`/`CreatePipeline()` consumers at all. The live smoke
  instantiation of the new N-format constructor was explicitly, correctly
  deferred to PHASE4 (no real multi-output shader existed yet to exercise it
  with).
- **PHASE4 (First Real Consumer, GBuffer Validation Pass)** — built
  `Shaders/GBufferValidation.vert/.frag` (2-output full-screen-triangle
  fragment shader: `outAlbedo` checkerboard, `outNormal` radial-gradient fake
  normal) and `Shaders/GBufferCopy.comp` (trivial compute copy), plus
  `src/Editor/GBufferValidation.h/.cpp` mirroring `ComputeBlurValidation`'s
  proven shape exactly: lazy init, three persistent `RenderTexture`s, a
  `Pipeline` built via PHASE3's new N-format overload, two declared passes
  (`"GBufferValidation"` graphics + `"GBufferValidationCopy"` compute).
  Wired through `IEditorLayer`'s two new pure-virtual methods
  (`AddGBufferValidationPass()`/`FinalizeGBufferValidationForSampling()`),
  `NullEditorLayer.cpp`'s matching no-op stubs (confirmed compiling under
  `GTE_ENABLE_EDITOR=OFF`), `EditorContext::showGBufferValidationOutput`/
  `gbufferValidationOutputDescriptor` (a separate pair of fields from the
  existing Compute Blur toggle, so the two debug tools stay independently
  toggleable), and a second "Show GBuffer Validation (debug)" checkbox in the
  Scene panel (which DOES swap the panel's own displayed image, mirroring
  Compute Blur's own precedent). Verified with a full clean build in BOTH
  `GTE_ENABLE_EDITOR` configurations and a live, HTTP-driven,
  screenshot-verified smoke test (checkbox off = zero effect; checkbox on =
  `GBufferAlbedo`/`GBufferNormal`/`GBufferVisualized` independently
  inspectable, visually distinct, Game View unaffected) using the
  temporary-default-flip technique documented below (no HTTP mechanism exists
  to click an arbitrary ImGui checkbox).
- **PHASE5 (this phase)** — documentation, final Tier-1 test sweep (one new
  regression-guard test added), the campaign's one full build + full `ctest`
  pass, and the final live visual proof against the fully-integrated build
  (see below).

## Corrections made during this campaign's own pre-implementation double-check pass

Per this task's own explicit instruction to summarize these two corrections
here:

1. **`Renderer::CreatePipeline()` call-site enumeration fix.** An earlier
   draft of `PHASE0_MASTER_STRATEGY.md` implied sky background/scene
   grid/mesh preview/GPU skinning/Atmosphere passes might be
   `Pipeline`/`CreatePipeline()` call sites this campaign needed to keep
   compiling. A `search_in_dir` for `.CreatePipeline(` across `src/`,
   performed before PHASE1 started, found this to be false: those features
   each hand-build their own raw `VkPipeline` directly (see
   `AtmosphereSkyBackgroundRenderer.h`'s own header comment) or go through
   the entirely separate `ComputePipeline`/`CreateComputePipeline()` path
   (GPU Skinning, Atmosphere LUTs). The ONLY two real external
   `Renderer::CreatePipeline()` call sites in the whole engine, both before
   and after this campaign, are `MeshAssetGpuCatalog.cpp`'s `Mesh`/
   `TexturedMesh` pipelines and `PrimitiveGpuCatalog.cpp`'s default/Triangle
   primitive pipeline. `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 7
   and every later phase's own Definition of Done were written against this
   corrected, verified enumeration — PHASE3 independently re-confirmed it via
   its own fresh `search_in_dir` before considering itself done, finding
   nothing had changed in the meantime.
2. **`NullEditorLayer.cpp`/CMake `GTE_ENABLE_EDITOR` gating additions.** The
   two new `IEditorLayer` pure-virtual methods PHASE4 needed
   (`AddGBufferValidationPass()`/`FinalizeGBufferValidationForSampling()`)
   required a matching no-op override in `src/Editor/NullEditorLayer.cpp` in
   the SAME change that added them to the interface — `IEditorLayer` itself
   is compiled in BOTH `GTE_ENABLE_EDITOR` configurations, and an abstract
   class missing an override cannot be instantiated by
   `CreateEditorLayer()`, which would break the `GTE_ENABLE_EDITOR=OFF`
   release build entirely. PHASE4 added these stubs proactively (not after
   discovering a build failure) and confirmed the fix by actually building a
   fresh, separate `GTE_ENABLE_EDITOR=OFF` configuration before considering
   itself done. Separately, the three new shaders
   (`GBufferValidation.vert`/`.frag`/`GBufferCopy.comp`) were registered in
   the root `CMakeLists.txt` wrapped in their own `if(GTE_ENABLE_EDITOR) ...
   endif()` block (mirroring `Shaders/BoxBlur.comp`'s own correct precedent
   for an Editor-only debug shader), confirmed NOT unconditionally
   registered like the core-gameplay `Mesh.frag`/`TexturedMesh.frag` shaders
   would be — the wrong precedent this campaign was explicitly warned away
   from copying.

No other deviations from the original five-phase plan occurred anywhere in
this campaign — every other design decision recorded in each phase's own
completion report was already pre-authorized by that phase's own strategy
document (its own stated recommended default, an explicitly offered
alternative, or an explicitly allowed deferral), not an unplanned departure.

## What PHASE5 itself did

### 3.1 — Documentation

- **`AGENTS.md`** gained a new "Multi-Render-Target (MRT) / G-Buffer Support"
  section (placed directly after "Render Pass System", before "Job System"),
  describing: `WriteColorAttachment()`'s new append-not-overwrite behavior
  and attachment-index-equals-shader-`location` convention, the 8-attachment
  cap, `RenderGraph::ExecuteCompiledGraph()`'s real N-attachment
  `vkCmdBeginRendering`, `FindMismatchedColorAttachmentExtent()`'s real
  `std::runtime_error` throw path, `Pipeline`/`GpuResourceFactory`/
  `Renderer`'s new `std::span<const VkFormat>` overloads (with the original
  single-format constructor preserved as a genuine, unmodified-behavior
  overload), the confirmed "zero changes needed" fact for
  `RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp`, and the
  `GBufferValidation` first-consumer pass's own shape/toggle/wiring.
- **`README.md`**'s "Status" section gained one new bullet (inserted at the
  top of the list, matching the existing bullets' tone/format/level of
  detail), summarizing the whole campaign and pointing at
  `task_manager/mrt-1/CAMPAIGN_COMPLETION_REPORT.md`.
- **`docs/conventions/`**: confirmed via `browse_dir` that NO existing
  rendering/render-graph-specific conventions file exists there today (the
  closest neighbor, `render-target-format-matching.md`, covers a narrower,
  unrelated concern — matching a render target's exact `VkFormat`, not MRT
  attachment ordering/PSO construction). Per this phase's own strategy
  document's explicit instruction ("do not create a new conventions file
  speculatively unless this project's own existing pattern clearly calls for
  one for every campaign of this size"), and consistent with the precedent
  that not every campaign creates its own dedicated `docs/conventions/` file
  (e.g. `render-pass-1` through `render-pass-4`'s combined coverage lives
  entirely inside `AGENTS.md`'s "Render Pass System" section, with no
  separate `docs/conventions/render-pass-system.md` file), **no new
  `docs/conventions/` file was created** — this campaign's own `AGENTS.md`
  addition is the sole detailed documentation surface, exactly mirroring the
  Render Pass System campaigns' own precedent. This did not require
  `ask_questions` — the strategy document's own text already supplied the
  exact precedent-check instruction and the campaign most directly comparable
  in shape/size (`render-pass-*`) already established the answer.
- **`task_manager/RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`**:
  row 10 of Section A's table ("Full multi-color-attachment (MRT) support")
  updated in place — the item name is now struck through, and the "What
  unlocks it" column now reads "✅ DONE", with a full description of what
  shipped and a pointer to `task_manager/mrt-1/PHASE0_MASTER_STRATEGY.md`/
  `CAMPAIGN_COMPLETION_REPORT.md`, mirroring the exact "✅ DONE" annotation
  style already used elsewhere in that same file for other closed items
  (rows 2, 3, and Section C.1).
- **`Top3-Priority-Features.txt`**: this file lives OUTSIDE the git
  repository, under
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\render-graphs\`
  (the source-material "Ideas" folder this whole campaign's own PHASE0
  document was commissioned from) — it is not a tracked, maintained
  project document under `task_manager/`, and no other campaign in this
  repository's history has ever edited a file in that external folder as
  part of closing out a gap it names. Per the task's own explicit
  confirmation text ("RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md
  row 10 explicitly lists... update it... with a pointer to
  task_manager/mrt-1/") — only the in-repository, in-`task_manager/` file was
  named as needing an update. This file was therefore deliberately left
  untouched, noted here rather than silently skipped.

### 3.2 — Final Tier-1 test sweep

- Re-ran every RenderGraph Tier-1 suite
  (`RenderGraphBuilderTests.cpp`/`RenderGraphCompilerTests.cpp`/
  `RenderGraphSnapshotTests.cpp`/`RenderGraphTypesTests.cpp`) against the
  FINAL state of the code — all pass unchanged, confirming no drift occurred
  between PHASE1's original sketch and the final, fully-integrated
  implementation.
- Added ONE new regression-guard test, per Step 3.2's own explicit
  requirement: `RenderGraphCompilerTest.MrtPassAndSeparateNonAttachmentBufferWritePassBothCullAndOrderCorrectly`
  (`tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`) — a graph
  containing BOTH a real 2-attachment MRT pass (`colorAttachments` populated)
  AND a completely separate, unrelated pass declaring an ordinary
  non-attachment `WriteBuffer()`/`ResourceAccess::ComputeShaderWrite` (never
  touching `colorAttachments` at all), confirming both halves independently
  cull/order correctly in the same graph — directly guarding against the
  "`pass.writes` and `pass.colorAttachments` are two separate vectors, so
  writes-order doesn't necessarily match colorAttachments-order" risk
  PHASE1's own `ColorAttachmentDesc` doc comment flagged.
- Confirmed `Pipeline` has no dedicated unit/integration test file
  (`tests/Renderer/` contains no `PipelineTests.cpp`-equivalent) — this is
  the accepted, documented "Tier 2, GPU-dependent, no automated coverage yet"
  gap `AGENTS.md`'s own "Testability & Regression Safety" section already
  names for `Pipeline`/`Buffer`/`RenderTexture`/everything under
  `Renderer/Vulkan/`, not a gap this campaign introduced or is expected to
  close.
- Full result: `GreatTamanaEngineTests.exe --gtest_filter=RenderGraph*` —
  **210/210 passed** (up from PHASE2's own 209, the +1 being this phase's
  new regression-guard test).

### 3.3 — Full build + full regression (this campaign's ONE and ONLY time)

- `cmake --build build` — **succeeded** (`ninja: no work to do` on the final
  run, confirming the tree was already fully, cleanly built after this
  phase's own source changes and the temporary/reverted live-proof edit
  described below).
- `ctest -C Debug --output-on-failure` — **100% tests passed out of 1683**
  (1 pre-existing, environment-gated skip:
  `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`, which
  every other recent campaign's own full-suite run also reports as skipped
  on this machine — not a regression, not caused by this campaign in any
  way). **No failure of any kind was found anywhere in the full suite** — no
  diagnosis/fix/`ask_questions` decision was needed per Step 3.3's own
  contingency plan, since the plan's trigger condition (a real test failure)
  never occurred.

### 3.4 — Final live, running-engine visual proof

Performed against the FINAL, fully-integrated, fully-built binary (the exact
same `build/GreatTamanaEngine.exe` the full `ctest` pass above was run
alongside):

1. **Boot / default-behavior check**: launched `GreatTamanaEngine.exe` via
   `run_app_background`; `GET /activate_tab?name=Game` + `GET /get_game_view`
   returned the engine's completely normal, unmodified sky-background Game
   View — confirming Locked Design Decision 4/the campaign's whole "no
   default behavior change" promise holds in the final, fully-integrated
   build.
2. **Toggling the debug checkbox**: confirmed directly (again, independently
   of PHASE4's own investigation) that no HTTP command-bridge mechanism
   exists anywhere in this engine capable of driving an arbitrary ImGui
   checkbox — `EditorUiCommandBridge` supports exactly one command kind,
   `ActivateTab` (bring a named panel to the front), and `EngineCommandBridge`
   is scoped to ECS-mutating commands, neither of which can flip a boolean
   Editor-context field. This was already PHASE4's own explicit finding, used
   there as "the same technique used to exercise any other checkbox-gated
   Editor debug tool without a mouse" — this phase reused the identical,
   already-established technique: temporarily flipped
   `EditorContext::showGBufferValidationOutput`'s default from `false` to
   `true` (a one-line, clearly-commented, deliberately temporary edit),
   rebuilt `GreatTamanaEngine`, and re-launched.
3. **Visual proof, against the final build**:
   - `GET /list_textures` (Scene tab activated first) lists
     `GBufferAlbedo`/`GBufferNormal`/`GBufferVisualized` as three
     independent 719x447 `R8G8B8A8_UNORM` `texture2d` entries, alongside
     every other real production texture (`GameView`, `SceneView`,
     `GameViewComposited`, all four Atmosphere LUTs, etc.) — the pass is
     correctly, generically discovered by the same debug-texture-registry
     machinery every other pass already uses, with zero special-casing.
   - `GET /get_texture?texture_name=GBufferAlbedo` returned a real red/yellow
     checkerboard PNG.
   - `GET /get_texture?texture_name=GBufferNormal` returned a real, smooth,
     multi-color (blue/purple/pink/orange/green) gradient PNG —
     **immediately, visually, unambiguously distinct from the albedo
     output** — re-confirming this campaign's single most important visual
     proof, this time against the fully-integrated final build rather than
     PHASE4's own in-progress one.
   - `GET /get_texture?texture_name=GBufferVisualized` returned an image
     pixel-identical to `GBufferAlbedo`'s own checkerboard — re-confirming
     the second, compute pass's cross-pass read of one of the two MRT
     outputs round-trips correctly, entirely barrier-synchronized by the
     existing, completely unmodified `RenderGraphBarrierPlanner`.
   - `GET /get_swapchain` (Scene tab active) showed the "Show GBuffer
     Validation (debug)" checkbox checked and the Scene panel's own
     displayed image swapped to the checkerboard "visualized" output, with
     the "Hierarchy"/"Inspector"/"Project" panels all rendering normally
     around it.
   - `GET /activate_tab?name=Game` + `GET /get_game_view` returned the exact
     same, byte-plausible normal sky-background Game View from step 1 —
     **entirely unaffected** by the GBuffer Validation pass running in the
     Scene View, one final time confirming this campaign's "no change to the
     default Game View render output" Non-Goal held all the way through to
     the final, fully-integrated build.
4. **Cleanup**: the app was cleanly terminated via `stop_app_background`.
   `EditorContext::showGBufferValidationOutput`'s default was immediately
   reverted back to `false`, and `cmake --build build` was run one final
   time — succeeded cleanly, confirming the SHIPPED state (the one actually
   committed to git) matches the intended opt-in-only default, with the
   checkbox off by default exactly as every prior phase's own Definition of
   Done requires.

## Definition of Done for the whole campaign — final re-confirmation

Every item in `PHASE0_MASTER_STRATEGY.md`'s own "Definition of Done for the
whole campaign" section, re-confirmed true here against the final build:

- `cmake --build build` succeeds (default configuration, `GTE_ENABLE_EDITOR`
  ON) — **confirmed** (Step 3.3 above).
- `ctest` passes, including every new test this campaign added — **confirmed**,
  1683/1683 (100%), 1 pre-existing environment-gated skip (Step 3.3 above).
- Toggling the Scene panel's "Show GBuffer Validation (debug)" checkbox
  causes a real, 2-3-color-output G-buffer pass to run every frame, each
  output independently visible via `GET /get_texture`/`GET /list_textures`,
  with the Game View's own rendered output completely unaffected the entire
  time — **confirmed** (Step 3.4 above).
- The Render Graph panel shows the new pass with its real N write names; the
  Frame Debugger shows a sane, correctly-grouped leaf for it — **confirmed**
  by PHASE4's own dedicated verification (unchanged since; nothing in
  PHASE5 touched the Frame Debugger or Render Graph panel code).
- Every pre-existing `Renderer::CreatePipeline()` call site (the corrected,
  verified enumeration — `MeshAssetGpuCatalog.cpp`'s `Mesh`/`TexturedMesh`
  pipelines, `PrimitiveGpuCatalog.cpp`'s default/Triangle primitive pipeline)
  compiles and behaves completely unmodified — **confirmed** across PHASE3's
  own compile check, PHASE4's own full build, and this phase's own full
  build/full `ctest` pass.
- `AGENTS.md` documents the new MRT capability; `README.md`'s "Status"
  section has a new bullet — **confirmed** (Step 3.1 above).

## Files changed in this phase

- `AGENTS.md` (new "Multi-Render-Target (MRT) / G-Buffer Support" section)
- `README.md` (new "Status" bullet)
- `task_manager/RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`
  (row 10 updated to "✅ DONE" with a pointer to `task_manager/mrt-1/`)
- `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp` (new
  `MrtPassAndSeparateNonAttachmentBufferWritePassBothCullAndOrderCorrectly`
  regression test)
- `src/Editor/EditorContext.h` (temporarily flipped, then reverted back to
  its shipped `false` default, for the live visual-proof screenshots above —
  the committed state is unchanged from PHASE4's own shipped value)
- `task_manager/mrt-1/CAMPAIGN_COMPLETION_REPORT.md` (this file)

## Final visual-proof evidence (summary)

See Step 3.4 above for the full narrative. In short, against the FINAL,
fully-integrated, fully-built binary: Game View boots and renders identically
whether or not this campaign ever existed; toggling the Scene panel's debug
checkbox makes `GBufferAlbedo` (a real red/yellow checkerboard),
`GBufferNormal` (a real, completely different smooth color gradient), and
`GBufferVisualized` (pixel-identical to `GBufferAlbedo`, proving the
cross-pass compute-copy read) all independently, correctly appear via
`GET /get_texture`/`GET /list_textures`, and the Scene panel's own displayed
image visibly swaps to match — with the Game View's own output completely
unaffected throughout.

## Git

Every source change across all five phases (Phases 1-4's own already-staged
work, plus this phase's documentation/test/report additions) is staged and
committed together in one final commit for this campaign — see the repository
history for the exact commit.
