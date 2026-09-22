# PHASE0 — Master Strategy: Multi-Render-Target (MRT) / G-Buffer Support (`mrt-1`)

This document is the **orchestrator**. It does not itself contain implementation
steps — it defines the goal, the current situation, the locked design
decisions, and the map of child phase documents that carry out the actual code
changes, in order. Every child phase document follows the same three-step
shape (Goal / Situation / Plan) and must be executed in numeric order, since
each phase's code depends on the previous one existing and compiling.

Source material for this whole campaign:
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\render-graphs\MRT-Implementation-HowTo.txt`
(the "how-to"), plus its three companions in the same folder
(`RenderGraph-Bad-Ugly-Missing-Analysis.txt`, `Top3-Priority-Features.txt`,
`RenderGraph-System-Analysis-2026-09-22.txt`). Read the how-to file directly —
every phase below is a direct, expanded, code-grounded elaboration of its six
stages, cross-checked against the ACTUAL current source (not assumed) as of
2026-09-22.

Read this file first. Then execute, in order:

- `PHASE1_SETUP_LAYER_ORDERED_COLOR_ATTACHMENTS.md`
- `PHASE2_EXECUTE_LAYER_MRT_RECORDING.md`
- `PHASE3_PIPELINE_LAYER_N_TARGET_PSO.md`
- `PHASE4_GBUFFER_VALIDATION_PASS_AND_SHADER.md`
- `PHASE5_DOCS_TESTS_AND_FULL_VALIDATION.md`

Always re-read the previous phase's own completion report (each phase's
working agreement, mirrored from every other campaign in this repository —
see `task_manager/logger-1/`, `task_manager/render-pass-1/` — is to write a
short `PHASEn_COMPLETION_REPORT.md` next to this file once that phase's code
compiles) before starting the next one — it may record a decision or a snag
that changes a later phase's exact plan.

**Every implementation phase, and anything it further delegates, must use the
`ask_questions` tool whenever it hits a genuine ambiguity or a design choice
this document doesn't already pin down.** This rule propagates recursively: if
an implementation phase itself delegates a sub-task, that delegation prompt
must repeat this same instruction, verbatim, to whatever it delegates to.

---

## Step 1: The Goal (Where are we going?)

Give GreatTamanaEngine's Render Graph the ability for a single pass to write
**more than one color attachment at once** (Multi-Render-Target / MRT), the
foundational mechanism a real G-buffer/deferred-shading pass needs, with a
concrete, working, end-to-end proof that the mechanism actually works:

1. `RenderGraphBuilder::PassBuilder::WriteColorAttachment()` becomes callable
   **more than once per pass**, appending an ordered color-attachment list
   instead of today's "last write silently overwrites the previous one"
   behavior — attachment index in that list == shader
   `layout(location = N) out`.
2. `RenderGraph::ExecuteCompiledGraph()`'s pass-recording loop records a real
   `vkCmdBeginRendering` with `colorAttachmentCount > 1` for such a pass,
   instead of only ever looking for a single `ColorAttachmentWrite` usage.
3. `Pipeline`/`GpuResourceFactory`/`Renderer::CreatePipeline()` can build a
   real N-color-target PSO (`VkPipelineRenderingCreateInfo::colorAttachmentCount`
   /`pColorAttachmentFormats` + one `VkPipelineColorBlendAttachmentState` per
   target), while **every existing single-target call site keeps compiling
   and behaving completely unmodified** (the only two real
   `Renderer::CreatePipeline()` call sites today — the `Mesh`/`TexturedMesh`
   pipelines and the default/Triangle primitive pipeline; see Locked Design
   Decision 7 below for the full, corrected enumeration — sky background/
   scene grid/mesh preview/GPU skinning/Atmosphere passes do NOT go through
   `Pipeline` at all and are irrelevant here).
4. A first real, working, **additive/opt-in, debug-only** consumer pass
   proves the whole path end-to-end: a small "GBuffer Validation" pass
   writes 2–3 color targets in one draw, and a second pass reads one of
   those targets back and visualizes it — proving both "N targets written in
   one pass" and "a later pass reading one of N targets, cross-pass,
   barrier-synchronized automatically" — with **zero change to what a user
   sees by default** (mirrors the existing `ComputeBlurValidation` debug tool
   exactly: opt-in, Scene-View-only, `RenderPassCategory::Debug`).
5. The Editor's "Render Graph" panel and Frame Debugger correctly show the
   new pass's multiple writes/textures with **no changes needed** to
   `RenderGraphSnapshot.cpp`'s pass-snapshot building or the debug-texture
   registry — this campaign's own Phase 2 (`PHASE2_EXECUTE_LAYER_MRT_RECORDING.md`)
   includes a Tier-1 test that PROVES this claim rather than just asserting
   it from the how-to document.
6. Tier-1 tests (`RenderGraphBuilderTests.cpp`/`RenderGraphCompilerTests.cpp`/
   `RenderGraphSnapshotTests.cpp`) cover a hand-built multi-write pass; a
   final live, running-engine check confirms the real thing works.

This campaign explicitly does **NOT** build a real deferred-shading/lighting
feature, a full PBR G-buffer, or any change to the default rendered Game/Scene
View output — see "Non-Goals" below. It builds the **mechanism**, proven by
the smallest possible real consumer, exactly the same scoping discipline the
compute-shader campaign's `ComputeBlurValidation` box-blur already
established for a single-target compute read/write round trip.

## Step 2: The Situation (Where are we now?)

**Confirmed directly against the current source** (not assumed from the
how-to document alone — every claim below was cross-checked against the real
file before this document was written):

- `RenderGraphBuilder::PassBuilder::WriteColorAttachment()`
  (`src/Renderer/RenderGraph/RenderGraphBuilder.cpp`, line 11) currently does:
  ```cpp
  void RenderGraphBuilder::PassBuilder::WriteColorAttachment(
      TextureHandle handle, const std::optional<std::array<float, 4>>& clearColor)
  {
      m_pass.writes.push_back(ResourceUsage::ForTexture(handle, ResourceAccess::ColorAttachmentWrite));
      if (clearColor.has_value()) {
          m_pass.colorClearValue = clearColor;
      }
  }
  ```
  Calling it twice on the same pass DOES push two `ColorAttachmentWrite`
  entries onto `pass.writes` already (so `RenderGraphCompiler`/culling/
  barrier-planning are already N-ary — see below) — but there is **no
  ordered "this is attachment slot 0, this is slot 1" concept anywhere**, and
  `pass.colorClearValue` is a single `std::optional`, so a second call's clear
  color silently overwrites the first's. `RenderGraphBuilder.h` line
  196-200's own doc comment states outright: "the Phases 1-8 MVP never
  declares more than one color/depth write per pass anyway" — this comment
  must be corrected as part of Phase 1.
- `RenderGraphTypes.h`'s `PassRecord` (line 533) has **no** ordered
  color-attachment list at all — only the generic `reads`/`writes` vectors
  (`std::vector<ResourceUsage>`) and the single `colorClearValue`/
  `depthClearValue` optionals (line 599-600).
- `RenderGraph::ExecuteCompiledGraph()`'s pass loop
  (`src/Renderer/RenderGraph/RenderGraph.cpp`, line 332-354) does a scan that
  explicitly keeps only the **last** `ColorAttachmentWrite` usage it finds
  (`colorHandle = usage.texture; hasColorWrite = true;` inside a plain `for`
  loop with no `break`) — confirmed: today, declaring two color writes on one
  pass silently drops the first one at EXECUTE time, even though the compiler
  already tracked both. Then (line 421-461) exactly ONE
  `VkRenderingAttachmentInfo colorAttachment` is built and
  `renderingInfo.colorAttachmentCount = 1` is hard-coded.
- `Pipeline`'s constructor (`src/Renderer/Pipeline.h` line 103,
  `src/Renderer/Pipeline.cpp` line 29) takes exactly one `VkFormat
  colorFormat`. `Pipeline.cpp` line 193-198 hard-codes
  `renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachmentFormats = &colorFormat;`, and line 143-151
  hard-codes exactly one `VkPipelineColorBlendAttachmentState`/
  `colorBlend.attachmentCount = 1`.
- `GpuResourceFactory::CreatePipeline()` (`GpuResourceFactory.h` line 137,
  `.cpp` line 279) takes a single `VkFormat colorFormat` and forwards it
  straight into `Pipeline`'s constructor. `Renderer::CreatePipeline()`
  (`Renderer.cpp` line ~299) always passes exactly `ColorFormat()` — a single
  value — through this same chain.
- **The genuinely good news, confirmed directly (not just asserted by the
  how-to file):** `RenderGraphCompiler::Compile()` and
  `RenderGraphSnapshot.cpp`'s `BuildPassSnapshot()` both already loop over
  `pass.writes` (a plain `std::vector<ResourceUsage>`) completely generically
  — nothing in either file assumes "at most one write of kind
  `ColorAttachmentWrite`". `RenderGraph::ApplyUsageBarrierIfNeeded()`
  (`RenderGraph.cpp` line 154) is likewise called once per entry of
  `pass.writes` inside a plain loop (line 307-309) with no attachment-count
  assumption anywhere. **This means the ENTIRE dependency-tracking, culling,
  lifetime, barrier-synchronization, and Editor-snapshot machinery already
  works for N writes today, with zero code changes** — the hard-coded "1"
  genuinely only exists in the two places named in items 3-4 above. This is
  the single most important fact this whole campaign leans on; Phase 1
  includes a Tier-1 test that proves it rather than trusting the claim
  blindly.
- **No G-buffer, deferred-shading, or MRT-consuming pass of any kind exists
  anywhere in the engine today** (confirmed via repo-wide search for
  "GBuffer"/"deferred" — the only hits are unrelated, e.g.
  `ProviderTiming::AfterDeferredPasses`, an ordering-tier name with nothing to
  do with deferred *shading*). This campaign is the first to introduce one.
- **`src/Editor/ComputeBlurValidation.h`/`.cpp` is the exact, already-proven
  precedent this campaign's own Phase 4 validation pass must mirror**: a
  small, `GTE_ENABLE_EDITOR`-only class, lazily initialized against a live
  `Renderer&` the first time it's used, owning its own persistent
  `RenderTexture` output(s), declaring itself via
  `RenderGraphBuilder::AddRenderPass(..., rg::ViewScope::SceneView,
  rg::RenderPassCategory::Debug, ...)`, toggled by a small, permanent checkbox
  in the "Scene" panel's toolbar (see `Panels/ScenePanel.cpp`), registered
  with the debug-texture registry automatically (zero extra plumbing, since
  every resolved `PhysicalTexture` is registered generically — see
  `RenderGraph.cpp` line 523-542), and visualized via `ImGui::Image()` against
  its own ImGui descriptor, exactly like the Game/Scene View itself.
- **Attachment cap**: this campaign locks the cap at 8 (see Locked Design
  Decisions below), matching the how-to document's own suggestion and typical
  real-world `VkPhysicalDeviceLimits::maxColorAttachments`.
- **Depth attachment handling is unaffected by this whole campaign.** Every
  pass — including the new G-buffer validation pass — still has AT MOST ONE
  depth/stencil attachment; G-buffer-style passes always share one depth
  target across all of their color outputs, which is exactly what dynamic
  rendering (`vkCmdBeginRendering`) already expects.

## Step 3: The Plan (How do we get there?)

### Locked Design Decisions

These were confirmed with the project owner before writing any phase document
(via `ask_questions`) and MUST NOT be silently changed by a later phase
without updating this file first:

1. **Strategy `.md` files live in `task_manager/mrt-1/`** (a brand-new folder,
   matching this repository's own campaign-naming convention — e.g.
   `atmosphere-scattering-1`, `frame-debugger-1`, `render-pass-1`). The
   originally-given `task_manager/logger-1/` path was confirmed to be a
   copy/paste mistake (that folder already holds an unrelated, finished
   Logger campaign) and is **never** touched by this campaign.
2. **Maximum color attachments per pass: 8** (`RenderGraphTypes.h`, a new
   `constexpr std::uint32_t kMaxColorAttachments = 8;`), enforced by an
   `assert()` in `WriteColorAttachment()` — matches
   `VkPhysicalDeviceLimits::maxColorAttachments`'s typical real-world value
   and the how-to document's own suggestion.
3. **Scope of the first real consumer pass (Phase 4) is deliberately
   MINIMAL**: 2–3 color targets (e.g. `outAlbedo`/`outNormal`, optionally
   `outMotion`), and the "later pass reads it back" half is a **simple
   visualization/copy**, never real deferred-lighting math (no
   diffuse/specular accumulation, no light-loop shader). Building an actual
   usable deferred-lighting feature is real, separate, follow-on work,
   explicitly out of scope here (see Non-Goals).
4. **The new consumer pass is additive/opt-in and debug-only** — it must
   NEVER run by default and must NEVER change the Game View's actual rendered
   output. It is declared with `rg::ViewScope::SceneView` and
   `rg::RenderPassCategory::Debug`, toggled by its own small checkbox in the
   Scene panel's toolbar, mirroring `ComputeBlurValidation` byte-for-byte in
   shape. Its code lives under `src/Editor/` (compiles only when
   `GTE_ENABLE_EDITOR` is ON), exactly like `ComputeBlurValidation`.
5. **No new `RenderPassCategory` enumerator is added this campaign.** The
   validation pass reuses the existing `RenderPassCategory::Debug` value —
   already excluded from the Frame Debugger's primary Game-View tree by the
   `ViewScope::SceneView` filter, with `Debug` as a second, independent
   safety net, exactly like `ComputeBlurValidation`'s own precedent
   (`ComputeBlurValidation.cpp` line 109's own comment). A FUTURE campaign
   that ships a real, production G-buffer pass is free to introduce a
   dedicated `RenderPassCategory::GBuffer` bucket at that time — inventing it
   now, unused, for a debug-only pass would be speculative and is explicitly
   avoided.
6. **The Frame Debugger's "pick which of N outputs to preview" richer UI**
   (how-to Stage 5, item 16(b)) is **out of scope for this campaign**. Only
   the simplest option — the existing whole-frame composite preview, exactly
   as every compute pass already behaves today — is required. This is
   recorded as a Non-Goal below, not silently dropped.
7. **`Pipeline`'s existing single-`VkFormat` constructor signature is
   preserved as a real, callable overload** (never removed, never given a
   behavior change) — the new N-format capability is added as a genuinely
   additive overload/parameter, so `Renderer::CreatePipeline()`'s only two
   real existing call sites today (confirmed via `search_in_dir` for
   `.CreatePipeline(` — `Game/Instantiation/MeshAssetGpuCatalog.cpp`'s
   `Mesh`/`TexturedMesh` pipelines, and `Game/Instantiation/
   PrimitiveGpuCatalog.cpp`'s default/Triangle primitive pipeline) compile
   and link completely unmodified. Same rule for
   `GpuResourceFactory::CreatePipeline()`. **Correction to an earlier draft
   of this document**: sky background/scene grid/mesh preview are NOT
   `Pipeline`/`CreatePipeline()` call sites at all — `AtmosphereSkyBackgroundRenderer`/
   `SceneGridRenderer`/`AssetPreviewMesh` each hand-build their own raw
   `VkPipeline` directly (see each file's own header comment), and GPU
   Skinning / every Atmosphere LUT pass are compute-only work built via the
   unrelated `ComputePipeline`/`CreateComputePipeline()` path — none of them
   are affected by this campaign at all, in either direction, and PHASE3
   must not treat them as a "must still compile" checklist item for
   `Pipeline` itself (see `PHASE3_PIPELINE_LAYER_N_TARGET_PSO.md`'s own
   corrected Definition of Done for the full write-up of this correction).
8. **Only the final phase (`PHASE5`) runs a full build + full `ctest` pass.**
   Phases 1-4 do a fast, targeted incremental compile check only (building
   just the affected target(s), or re-compiling the handful of touched
   translation units) — never a full regression run, to keep iteration fast
   on this machine (per this project's own established working agreement,
   see `task_manager/logger-1/PHASE0_MASTER_STRATEGY.md`'s identical rule).

### Non-Goals (explicitly out of scope for `mrt-1`)

Stated here so no phase document below "helpfully" scope-creeps into these —
each would need its own dedicated, reviewed design pass first:

- **No real deferred-shading/lighting pass.** No light-accumulation shader,
  no shadow sampling, no PBR BRDF evaluation against the G-buffer. The
  "later pass reads the G-buffer back" half of Phase 4 is a simple
  visualization/copy only.
- **No change to the default Game View render output.** Nothing a normal
  (non-debug-toggle) user sees changes at all.
- **No Frame Debugger per-output picker UI** (how-to Stage 5, item 16(b)) —
  future, optional fast-follow work, deliberately deferred.
- **No new `RenderPassCategory` enumerator.**
- **No change to depth/stencil attachment handling** — still exactly one
  optional depth attachment per pass, unchanged.
- **No bindless descriptors, no GPU-driven indirect draw work** (a
  DIFFERENT, already-identified priority pick from the same source analysis
  — see `Top3-Priority-Features.txt` item #1 — explicitly not this
  campaign's concern).
- **No shadow mapping** (`Top3-Priority-Features.txt` item #3 — a separate,
  independent future campaign).
- **No change to `RenderGraphCompiler.cpp` or `RenderGraphSnapshot.cpp`
  production code** — both are proven, via a real Tier-1 test added in
  Phase 1/2, to already work correctly for N writes with zero modification.
  If a phase's own investigation ever finds this claim false for some real
  edge case, STOP, write up the discrepancy in that phase's completion
  report, and use `ask_questions` before changing either file — this would
  be a significant, unplanned scope change.

### Phase Map

| Phase | Deliverable |
|---|---|
| **1** | `RenderGraphTypes.h`/`RenderGraphBuilder.h`/`.cpp` — ordered `PassRecord::colorAttachments` list (index == shader `location`), `WriteColorAttachment()` appends instead of overwriting, per-attachment clear color, 8-attachment cap + assert. Tier-1 tests in `RenderGraphBuilderTests.cpp`/`RenderGraphCompilerTests.cpp`/`RenderGraphSnapshotTests.cpp` proving a hand-built 3-write pass survives culling/lifetime/snapshot with zero changes to `RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp`. |
| **2** | `RenderGraph.cpp`'s `ExecuteCompiledGraph()` pass loop rewritten to build one `VkRenderingAttachmentInfo` per declared color attachment (in order), set a real `colorAttachmentCount`, assert matching extents, set viewport/scissor from attachment 0. `PassContext` unchanged. |
| **3** | `Pipeline.h`/`.cpp` gain a real N-format PSO path (`std::vector<VkFormat>`/small fixed array + count) — one `VkPipelineColorBlendAttachmentState` per target — while the existing single-format constructor keeps working via a trivial one-element-array overload. `GpuResourceFactory::CreatePipeline()`/`Renderer::CreatePipeline()` gain a parallel N-format overload; every existing call site is untouched. |
| **4** | `Shaders/GBufferValidation.frag`/`.vert` (2-3 outputs) + `src/Editor/GBufferValidation.h`/`.cpp` (mirrors `ComputeBlurValidation` exactly: lazy init, persistent `RenderTexture`s, `AddRenderPass(..., SceneView, Debug, ...)`, a second small pass that reads one channel back and copies/visualizes it into its own persistent output), a Scene-panel toolbar checkbox to toggle it, CMake `gte_add_shader()` registration. |
| **5** | `AGENTS.md`/`README.md`/`docs/conventions/` updates describing the new MRT capability and its scope; final Tier-1 test sweep; full `cmake --build build`; full `ctest`; a live, running-engine manual check (toggle the new debug checkbox, confirm via `GET /list_textures`/`GET /get_texture` and a screenshot that N G-buffer channels are independently visible); `CAMPAIGN_COMPLETION_REPORT.md`. |

### Definition of Done for the whole campaign

- `cmake --build build` succeeds (default configuration, `GTE_ENABLE_EDITOR`
  ON — the project's default).
- `ctest` (see the regression command below) passes, including every new
  test file/case this campaign adds.
- Running the built `GreatTamanaEngine.exe`, opening the Editor's "Scene"
  panel, and toggling the new "Show GBuffer Validation (debug)"-style
  checkbox causes a G-buffer pass with 2-3 real color outputs to run every
  frame, each one independently visible via `GET /get_texture`/
  `GET /list_textures` and the Render Graph panel's own texture list, with
  the Game View's own rendered output completely unaffected the entire time.
- The Render Graph panel shows the new pass with its real N write names; the
  Frame Debugger shows a sane, correctly-grouped (Debug-category, excluded
  from the primary tree) leaf for it, exactly like `ComputeBlurValidation`
  today.
- Every pre-existing `Renderer::CreatePipeline()` call site (the only two
  real ones today — `MeshAssetGpuCatalog.cpp`'s `Mesh`/`TexturedMesh`
  pipelines, `PrimitiveGpuCatalog.cpp`'s default/Triangle primitive pipeline
  — see Locked Design Decision 7's own correction above; sky background/
  scene grid/mesh preview/GPU skinning/Atmosphere passes are NOT
  `Pipeline`/`CreatePipeline()` call sites and are irrelevant here) compiles
  and behaves completely unmodified.
- `AGENTS.md` documents the new MRT capability; `README.md`'s "Status"
  section has a new bullet, matching every other feature's own documentation
  precedent in that file.

### Regression / build commands (reference — see each phase for exact use)

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Per this campaign's own working agreement (Locked Design Decision 8): **only
`PHASE5` runs a full build + full `ctest` pass.** Phases 1-4 do a fast,
targeted incremental compile check only.
