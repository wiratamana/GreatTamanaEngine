# CAMPAIGN COMPLETION REPORT — `editor-core-separation-26`
## "Buffer Roots (KeepBufferOutput) + Blit/Copy Passes (PassKind::Blit)"

Campaign folder: `task_manager/editor-core-separation-26/`
Branch: `feature/editor-core-separation` (unchanged for the whole campaign)
BIG STEP 2 OF 4 of the larger `project-assembly-impl-5` series (BIG STEP 1 —
Core/Editor Separation: PassRecord Debug Metadata Sink — shipped as
`editor-core-separation-25`; BIG STEP 3, Persistent Resource Cache, and
BIG STEP 4, GPU Memory Aliasing, are separate future campaigns).

## Status: CLOSED ✅

## Step 1: The Goal (restated)

Close two small, independent, "real missing feature" gaps confirmed present
in the Render Graph:

- **Gap A — Buffer Roots**: a `BufferHandle` whose only write has no in-frame
  reader could never survive `RenderGraphCompiler::Compile()`'s culling pass,
  even when explicitly requested to be kept alive for a later frame.
- **Gap B — Blit/Copy Passes**: no official, hand-Vulkan-free way existed to
  blit/copy/scale one texture into another via `vkCmdBlitImage`, despite
  `RenderPassDrawKind::Blit` and `PassKind`'s own doc comment already
  anticipating exactly this extension.

## Step 2: The Shape (7 phases)

| Phase | One-line summary | Status |
|---|---|---|
| PHASE1 | Gap A in full: `KeepBufferOutput()`, `finalBufferOutputs`, `ContainsBufferHandle()`, root-scan fix. | DONE — +2 tests |
| PHASE2 | Gap B prerequisite: `PassBuilder::WriteTexture()` gains trailing `isDepthResource` parameter. | DONE — +2 tests |
| PHASE3 | `BlitSpec`, `PassKind::Blit`, `PassRecord::blitCommand`, `ToString(PassKind)` fix, 3 pure helpers. | DONE — +8 tests |
| PHASE4 | Required companion audit: 3 of 5 `FrameDebuggerData.cpp` `PassKind` sites converted to exhaustive dispatch; 2 documented-safe filters left alone. | DONE — +2 tests |
| PHASE5 | `RenderGraphBuilder::AddBlitPass()` — real, official, first-class entry point (pure data only). | DONE — +6 tests |
| PHASE6 | `vkCmdBlitImage2` execution branch, `SupportsBlitSrcDst()`/`VulkanDevice::SupportsDepthBlit()`, permanent `BlitValidation` pass, live color-to-color AND depth-to-depth proof. | DONE — +0 new Tier-1 tests (live/visual phase, per its own plan) |
| PHASE7 (this report) | Full clean build, full `ctest` regression, acceptance-criteria tick-through, campaign closeout. | DONE |

**Total new Tier-1 tests this campaign added: 2 + 2 + 8 + 2 + 6 + 0 = 20.**

## Step 3: Final Verification (fresh evidence gathered THIS phase)

### 3.1 — Full clean build

`cmake --build build --clean-first` from `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`:

- Clean step: `Cleaning... 643 files.`
- **625/625 build steps succeeded, zero errors.** Includes `GreatTamanaEditor.exe`
  and `tests\GreatTamanaEngineTests.exe`, all plugin `.dll`s, all Project
  Assembly `.dll`s, and every shader (including `PluginBlitFullscreen.vert/.frag`
  and every other pre-existing shader — no new shader was needed by this
  campaign, confirmed).
- No `-Wswitch`-class warning, or any warning at all, appeared anywhere in the
  build output — confirmed by reading the full build log. This is the
  concrete proof that PHASE3/PHASE4's own audits correctly kept
  `ToString(PassKind)` and every `FrameDebuggerData.cpp` dispatch genuinely
  exhaustive.
- A follow-up no-op incremental build (`cmake --build build`) confirmed
  `ninja: no work to do.` — the clean build is genuinely, fully up to date.

### 3.2 — Full `ctest` regression

`cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`:

- **2110 tests total, 100% of executed tests passing, 25 legitimate,
  environment-gated skips** (the exact same 25 skip cases as
  `editor-core-separation-25`'s own baseline — `OpenProjectEndpointEndToEndTest`/
  `PmxLoaderRealModelSmokeTest`/`RegisterProjectRenderFeatureApiTest` (7)/
  `RenderFeatureCompositorProjectFeatureTest` (9)/`ProjectAssemblyHostTest` (3)/
  `ProjectAssemblyRegistrationLedgerTest` (4)/`CoreHeadlessConstructionTest` —
  all pre-existing, environment-specific, none newly introduced).
- **Zero unexplained delta**: `2110 - 2090 (editor-core-separation-25 baseline)
  = +20`, matching this campaign's own recomputed new-test total exactly
  (Step 2 above). Skip count is byte-identical (25 = 25).
- Total wall time: 192.19 seconds.

### 3.3 — Combined Acceptance Criteria tick-through (source document, lines 511-536)

- [x] **Buffer-root Tier-1 test passes.** Re-ran
  `ctest -C Debug -R RenderGraphCompilerTest` fresh this phase: **36/36
  passed**, explicitly confirming both PHASE1 tests by name —
  `BufferOnlyWriteSurvivesCullingOnlyWhenExplicitlyKeptAsOutput` and
  `BufferWriteNeverKeptIsCulledEvenWithNoTextureFinalOutputsAtAll`, both
  `Passed`.
- [x] **Blit Tier-1 test passes, including `isDepthResource` propagation AND
  `drawKind == RenderPassDrawKind::Blit`.** Re-ran
  `ctest -C Debug -R RenderGraphBuilderTest` fresh this phase: **51/51
  passed**, explicitly confirming all 6 PHASE5 `AddBlitPass` tests by name,
  including `AddBlitPassWithDstIsDepthTrueProducesDepthFlaggedWrite`,
  `AddBlitPassWithSrcIsDepthTrueProducesDepthFlaggedRead`, and
  `AddBlitPassCallsSinkWithBlitDrawKindAndSuppliedCategoryAndTags`.
- [x] **A Tier-1 test confirms `srcIsDepth`/`dstIsDepth` resolves to
  `VK_FILTER_NEAREST` regardless of stored `filter`, AND a second confirms a
  fully-color spec resolves to `filter` verbatim.** Re-ran
  `ctest -C Debug -R "RenderGraphResolveEffectiveBlitFilterTest|RenderGraphResolveBlitRegionTest|RenderGraphIsValidBlitRegionTest"`
  fresh this phase: **8/8 passed**, explicitly confirming
  `ReturnsSpecFilterWhenNeitherSideIsDepth`, `ForcesNearestWhenSrcIsDepth`, and
  `ForcesNearestWhenDstIsDepth` by name.
- [x] **Blit live/visual proof passes for color-to-color.** Re-gathered THIS
  PHASE, not cited from PHASE6: `run_app_background`'d the freshly, fully
  rebuilt `GreatTamanaEditor.exe`, `GET /get_texture?texture_name=BlitValidationOutput`
  returned a real 1024x1024 PNG, `load_image`'d and visually confirmed a
  solid, correct, magenta/pink (`{0.85, 0.15, 0.55, 1.0}`) fill — the same
  color `BlitValidationSourceFill` (512x512) writes, correctly scaled up by
  the blit with zero artifact. `GET /get_logs?min_level=Error` returned
  `count: 0` throughout. `stop_app_background`'d cleanly afterward.
- [x] **Depth-to-depth blit — restated, re-verified live, not silently
  upgraded.** This dev machine's real GPU (Intel(R) Iris(R) Xe Graphics)
  reports `SupportsDepthBlit()==true` (confirmed live during PHASE6), so the
  real, permanent `"BlitValidationDepthBlit"` pass genuinely runs every
  frame. Re-fetched THIS phase (fresh rebuild, fresh process):
  `GET /get_texture?texture_name=BlitValidationOutput&channel=depth` (1024x1024)
  and `GET /get_texture?texture_name=BlitValidationSource&channel=depth`
  (512x512) both returned a real, solid, matching mid-gray PNG — the correct
  grayscale representation of the shared `0.3f` clear-depth value — confirming
  the depth blit genuinely copied/scaled real depth data, not garbage or
  uninitialized memory. **VERIFIED WORKING on real hardware, not merely
  shipped-but-unverified**, for the second time (PHASE6's own live run, and
  this phase's independent fresh re-run against a from-scratch clean build).
- [x] **Every non-exhaustive `PassKind` comparison identified is converted.**
  Re-ran the full grep (`\.kind\s*[=!]=` across all of `src/*.cpp`) ONE FINAL
  TIME, fresh, this phase: found the exact same 4 raw-comparison hits inside
  `src/Editor/FrameDebuggerData.cpp` (line 249 — Site 1, the documented-safe
  filter; line 918 — a comment referencing the new guard, not executable
  code; line 932 — the real Site 3 fix, `pass.kind == rg::PassKind::Graphics`;
  line 999 — Site 4, the other documented-safe filter) as PHASE4's own
  completion report recorded, PLUS one new, EXPECTED hit inside
  `src/Renderer/RenderGraph/RenderGraph.cpp` line 737
  (`pass.kind == PassKind::Blit`) — this is PHASE6's own real
  `vkCmdBlitImage2` execution-branch dispatch, not a hazard: `RenderGraph.cpp`
  is one of the three files (`RenderGraph.cpp`/`RenderGraphCompiler.cpp`/
  `RenderGraphBarrierPlanner.cpp`) the source document's own TR1/TR2 already
  exempt from the audit requirement, since it is the engine's own internal
  dispatch code, never a downstream consumer assuming a closed two-value enum.
  Every other hit across the codebase (`RenderFeatureCompositor.cpp`,
  `FrameDebuggerPanel.cpp`, `RenderGraphCompiler.cpp`) is an unrelated,
  pre-existing `ResourceKind`/other `.kind` field comparison. Confirmed by
  reading the actual current code (not citing PHASE4's report) that exactly 3
  of the 5 original `FrameDebuggerData.cpp` sites are the real
  switch-based exhaustive dispatch and 2 remain documented-safe one-way
  filters, exactly as Locked Decision 2 requires.
- [x] **Full `ctest` regression pass, zero unexplained delta.** Covered by
  3.2 above.

**Every single box in the source document's own "COMBINED ACCEPTANCE
CRITERIA" checklist is ticked, with fresh evidence gathered in this phase.**

## Step 4: Permanent Limitations (honest, restated)

- **Depth-to-depth blit is verified working ONLY on this dev machine's actual
  GPU** (Intel Iris Xe, `SupportsDepthBlit()==true`). The mechanism is
  engine-checked at runtime (`VulkanDevice::SupportsDepthBlit()`, cached once
  at device construction) — on a different GPU/driver that lacks
  `VK_FORMAT_FEATURE_BLIT_SRC_BIT`/`_DST_BIT` for the engine's real depth
  format, `BlitValidation`'s own depth-blit pass simply never declares itself
  (per its own runtime-gated `if (renderer.SupportsDepthBlit())` check), and
  the debug-assert inside `RenderGraph::ExecuteCompiledGraph()` would catch
  any future accidental unconditional-declare regression in a debug build.
  This is the honest, permanent shape of the feature — not a "someday" gap.
- **`FrameDebuggerData.cpp`'s Site 1
  (`FindPostGameViewCompositePassExecutionIndex()`) and Site 4 (the
  Post-GameView compute-discovery loop) were deliberately left as one-way
  Compute-only filters, never converted to a 3-way switch** — per Locked
  Decision 2, confirmed independently twice (PHASE4's own audit, and this
  phase's final fresh re-grep): both are structurally guaranteed safe for
  `PassKind::Blit` (a Blit pass is simply skipped, exactly like a Graphics
  pass already is, and correctly falls through to the "Other Render Passes"
  sweep), and converting them would only add speculative, symmetry-only
  `case PassKind::Blit: break;` arms for a hazard that provably cannot occur
  there.
- **No buffer-to-buffer copy (`PassKind::Copy`)** — explicitly out of scope
  per the source document's own Non-Goal; `BlitSpec` is texture-only.
- **No MSAA source/destination support** — `vkCmdBlitImage2` cannot resolve a
  multisampled image; a future `PassKind::Resolve` would be a separate,
  unrelated item.
- **`RenderGraphBuilder.h`'s generic `AddRenderPass()` template's own
  `if (kind == PassKind::Compute) { ... } else { ... }` branch was found
  (PHASE3) to be structurally guaranteed to never see `PassKind::Blit`**,
  since `AddBlitPass()` is its own, separate, callback-free builder method —
  documented in PHASE3's own completion report, not re-litigated here.

## Step 5: What the NEXT campaign (BIG STEP 3 of 4 — Persistent Resource
Cache) needs to know

- Both `KeepBufferOutput()`/`finalBufferOutputs` (Gap A) and `AddBlitPass()`/
  `BlitSpec`/`PassKind::Blit` (Gap B) are now real, shipped, production
  vocabulary — a Persistent Resource Cache campaign can freely build on top
  of either (e.g. a cache-fill compute pass using `KeepBufferOutput()` to
  survive to a later frame's read; a cache-population blit using
  `AddBlitPass()`).
- Zero changes were made to `RenderGraphBarrierPlanner.cpp` or
  `RenderGraphCompiler.cpp`'s general dependency/culling machinery beyond the
  one, narrow `finalBufferOutputs` root-set addition — both remain exactly as
  fast/simple as `render-pass-6`'s own optimization campaign left them.
- `RenderGraph` now carries a non-owning `Renderer*` member
  (`m_renderer`, PHASE6) — the first time `RenderGraph` has ever needed to
  call back into `Renderer` for a capability query. A future campaign that
  needs another device-capability check from inside `RenderGraph::ExecuteCompiledGraph()`
  can reuse this same member rather than re-threading a new parameter.
- `src/Editor/BlitValidation.h/.cpp` is now a small, permanent, real
  consumer of `AddBlitPass()` — useful as a working, minimal reference
  implementation for any future pass author who wants to see the whole
  pattern (fill -> blit -> depth-blit) end to end.

## Step 6: Delegation and Ambiguity Summary (PHASE1-7)

Every real `ask_questions`/`dispatch_sub_agent` use across the whole campaign,
confirmed by reading each phase's own completion report:

- **PHASE1**: No `ask_questions` needed. One `dispatch_sub_agent` used as an
  independent double-check of the finished diff before writing its
  completion report — reported SUCCESS.
- **PHASE2**: No `ask_questions` needed. One `dispatch_sub_agent` double-check
  — reported SUCCESS.
- **PHASE3**: No `ask_questions` needed (one genuine finding — the
  `RenderGraphBuilder.h` `AddRenderPass()` if/else shape — was resolved by
  direct code inspection, not a question). One `dispatch_sub_agent`
  double-check — reported SUCCESS.
- **PHASE4**: No `ask_questions` needed (the one flagged risk,
  `FindViewRegionPivot()`'s exact semantics, was resolved by direct code
  reading). One `dispatch_sub_agent` double-check — reported SUCCESS.
- **PHASE5**: No `ask_questions` needed. One `dispatch_sub_agent`
  double-check — reported SUCCESS.
- **PHASE6**: **One genuine `ask_questions` use** — whether to extend
  `BlitValidation` with a real third depth-to-depth blit pass beyond the
  phase file's own literal color-only code sample, given this dev machine's
  GPU genuinely supports it. The user chose to add the real pass. One
  `dispatch_sub_agent` double-check of the finished diff — reported SUCCESS.
- **PHASE7 (this phase)**: No genuine ambiguity found beyond what PHASE0 and
  this phase's own file already resolved — the file-naming instruction given
  directly by the top-level task runner (`PHASE7_COMPLETION_REPORT.md`, as
  opposed to this file's own name) was treated as an explicit instruction
  overriding nothing, not a genuine ambiguity requiring `ask_questions` — both
  this file (mirroring every prior campaign's own `CAMPAIGN_COMPLETION_REPORT.md`
  convention) and `PHASE7_COMPLETION_REPORT.md` (the literal top-level
  instruction) are written, so both expectations are satisfied with zero
  conflict. No `dispatch_sub_agent` was used this phase — every verification
  step (build, full regression, targeted re-runs, live HTTP proof) was
  performed directly, with real, fresh, first-hand evidence gathered in this
  same phase, per this phase's own explicit requirement to never merely cite
  an earlier phase's own report without re-verification.
- **`delegate_task` was never used by PHASE1-7** — confirmed by every phase's
  own completion report explicitly stating dispatch_sub_agent (never
  delegate_task) was used for delegation, consistent with PHASE0 Rule 4. No
  process violation found.

## Step 7: Files changed across the whole campaign

- `src/Renderer/RenderGraph/RenderGraphCompiler.cpp` (PHASE1)
- `src/Renderer/RenderGraph/RenderGraphCompiler.h` (PHASE1)
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` (PHASE1, PHASE2, PHASE5)
- `src/Renderer/RenderGraph/RenderGraphBuilder.cpp` (PHASE1, PHASE2, PHASE5)
- `src/Renderer/RenderGraph/RenderGraphTypes.h` (PHASE3)
- `src/Renderer/RenderGraph/RenderGraphTypes.cpp` (PHASE3)
- `src/Editor/FrameDebuggerData.cpp` (PHASE4)
- `src/Renderer/RenderGraph/RenderGraph.h` (PHASE6)
- `src/Renderer/RenderGraph/RenderGraph.cpp` (PHASE6)
- `src/Renderer/Vulkan/FormatCapabilities.h` (PHASE6)
- `src/Renderer/Vulkan/FormatCapabilities.cpp` (PHASE6)
- `src/Renderer/Vulkan/VulkanDevice.h` (PHASE6)
- `src/Renderer/Vulkan/VulkanDevice.cpp` (PHASE6)
- `src/Renderer/Renderer.h` (PHASE6)
- `src/Renderer/Renderer.cpp` (PHASE6)
- `src/Editor/EditorLayer.h` (PHASE6)
- `src/Editor/NullEditorLayer.cpp` (PHASE6)
- `src/Editor/ImGuiEditorLayer.cpp` (PHASE6)
- `src/Editor/BlitValidation.h` (PHASE6, **NEW FILE**)
- `src/Editor/BlitValidation.cpp` (PHASE6, **NEW FILE**)
- `src/Core/Core.cpp` (PHASE6)
- `CMakeLists.txt` (PHASE6 — new source-file entries only)
- `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp` (PHASE1)
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` (PHASE2, PHASE5)
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` (PHASE3)
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (PHASE4)
- `task_manager/editor-core-separation-26/PHASE1_COMPLETION_REPORT.md` through
  `PHASE6_COMPLETION_REPORT.md`, `PHASE7_COMPLETION_REPORT.md`, and this file.

`src/Editor/BlitValidation.h`/`.cpp` are the ONE deliberate exception to this
campaign's own "no new source file" expectation (PHASE0 Step 2/Rule 7),
exactly as anticipated.

## Step 8: Closeout

This campaign (`editor-core-separation-26`) is **CLOSED**. BIG STEP 3 of 4
(Persistent Resource Cache) may begin as its own, later, separate campaign —
nothing further is expected here.
