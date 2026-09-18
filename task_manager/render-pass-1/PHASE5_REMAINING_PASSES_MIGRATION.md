# PHASE5: Migrate the Remaining Passes (GPU Skinning, Present, Frame Debugger Replay, Compute Blur Validation)

_Child of `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1` through
`PHASE4` already being merged. Part of the `render-pass-1` campaign._

## Step 1: The Goal

Finish the "full migration" scope locked in by PHASE0 (Locked Design
Decision #3): every remaining pass declaration site in the engine that
still calls `builder.AddPass()`/`AddComputePass()` directly is migrated
to `AddRenderPass()`, tagged with the correct `RenderPassCategory`. This
phase covers the four remaining families:

1. GPU Skinning compute dispatch (`AddGpuSkinningPasses()`).
2. The `"Present"` pass (`AddPresentPass()`).
3. The Frame Debugger's own N replay passes
   (`AddFrameDebuggerReplayPasses()`) — **UPDATE (see Step 2/3.3 below):
   this one is ALREADY done by the time this phase starts —
   `PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md`'s own Step 3.3b pulled
   this specific migration forward, because PHASE4's own correctness and
   live verification genuinely depended on it. This phase's own job for
   item 3 is reduced to CONFIRMING it landed correctly, not implementing
   it.**
4. Compute Blur Validation (`src/Editor/ComputeBlurValidation.h/.cpp`,
   an Editor-only debug tool).

This phase changes NO rendering/debugger-visible behavior for (1)/(2)/(4)
beyond the metadata tag itself. For (3), PHASE4 already fixed the real,
pre-existing correctness gap this bullet used to describe as this
phase's own job: the N replay passes are excluded from ever appearing in
the Frame Debugger's own tree (they are a Frame-Debugger-INTERNAL
implementation detail) via the real, generic `RenderPassCategory::Debug`
tag PHASE4 itself now stamps and filters on.

## Step 2: The Situation

- `AddGpuSkinningPasses()` (`src/Application/RenderPasses.cpp`) calls
  `builder.AddComputePass(request.name, setup, execute)` (3-arg form, no
  `ViewScope`) once per GPU-skinning dispatch request.
- `AddPresentPass()` (`RenderPasses.cpp`) calls the 3-arg
  `builder.AddPass("Present", setup, execute)`.
- `AddFrameDebuggerReplayPasses()` (`RenderPasses.cpp`, Editor-only body)
  **is ALREADY migrated by `PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md`
  itself (its own Step 3.3b), not left for this phase.** PHASE4's own
  pre-check found (by directly reading `Application.cpp`) that these N
  replay passes are declared strictly between the view passes and the
  Aerial Perspective Composite pass — i.e. structurally INSIDE PHASE4's
  own "view region" walk (its Step 3.3) — so PHASE4's own live-HTTP
  verification would have shown spurious extra tree leaves for them on
  its very first run unless they were ALREADY tagged
  `RenderPassCategory::Debug`, with a matching `category != Debug`
  exclusion guard, by the time PHASE4 lands. Rather than leave this as a
  conditional, easy-to-miss cross-phase aside ("if it does not yet exist,
  add it now" — the ambiguity this exact paragraph used to contain),
  PHASE4 pulls this one, narrow migration forward into its own Step 3.3b:
  `builder.AddPass(passName, rg::ViewScope::GameView, setup, execute)` is
  replaced with `builder.AddRenderPass(passName, rg::PassKind::Graphics,
  rg::ViewScope::GameView, rg::RenderPassCategory::Debug, setup,
  execute)` THERE, not here. **This phase (PHASE5) therefore has NOTHING
  left to do for this call site** beyond confirming it (3.3 below is now
  a confirmation step, not an implementation step) — if you are executing
  PHASE5 and find this call site is somehow still plain `AddPass()` (i.e.
  PHASE4 was not actually completed as its own file now specifies), that
  is a real regression in PHASE4's own delivery, not a gap for PHASE5 to
  silently patch over — flag it explicitly and fix it via PHASE4's own
  scope/attribution, noting the discrepancy in this phase's completion
  report.
- `src/Editor/ComputeBlurValidation.h/.cpp`'s own `AddPass()` method
  (mirrors the shape of `AtmosphereLutRenderer`'s methods) calls
  `builder.AddComputePass(...)` directly — this is a Scene-View-only
  debug validation tool (`ViewScope::SceneView`), already excluded from
  the Game-View-only Frame Debugger tree by the existing `viewScope !=
  ViewScope::SceneView` filter — tag it `RenderPassCategory::Debug`
  anyway, for consistency/future-proofing (if this filter's own scope
  ever changes, category-based exclusion becomes a second, independent
  safety net).

## Step 3: The Plan

### 3.1 — GPU Skinning

In `AddGpuSkinningPasses()`, replace `builder.AddComputePass(request.name,
setup, execute)` with `builder.AddRenderPass(request.name,
rg::PassKind::Compute, rg::ViewScope::Shared,
rg::RenderPassCategory::GpuSkinning, setup, execute)`. (`ViewScope::Shared`
is what the OLD 3-arg `AddComputePass` call already defaulted to — keep
it explicit here since `AddRenderPass`'s own 4-argument
`viewScope`/`category` overload requires stating it.)

### 3.2 — Present

In `AddPresentPass()`, replace `builder.AddPass("Present", setup,
execute)` with `builder.AddRenderPass("Present", rg::PassKind::Graphics,
rg::ViewScope::Shared, rg::RenderPassCategory::General, setup, execute)`.
No behavior change — `"Present"` is already correctly excluded from the
Frame Debugger's own Game-View-only tree (it's a different `RenderGraph::
Execute()` call entirely — the PIPELINED swapchain regime, never the
offscreen one the Frame Debugger's snapshot is built from).

### 3.3 — Frame Debugger Replay Passes (CONFIRMATION ONLY — already implemented by PHASE4)

**Already done by `PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md`'s own Step
3.3b** (see Step 2's updated bullet above) — `AddFrameDebuggerReplayPasses()`
already calls `builder.AddRenderPass(passName, rg::PassKind::Graphics,
rg::ViewScope::GameView, rg::RenderPassCategory::Debug, setup, execute)` by
the time this phase starts, and PHASE4's own generic "view region" walk
already excludes `category == RenderPassCategory::Debug` passes (its Step
3.3). This phase's job here is purely to CONFIRM both of those are true
(part of 3.5's grep audit below) — if either is missing, that is a PHASE4
regression to fix/attribute there, not something to silently redo here.

### 3.4 — Compute Blur Validation

In `ComputeBlurValidation`'s own pass-declaration method, replace its
`builder.AddComputePass(...)` call with `builder.AddRenderPass(name,
rg::PassKind::Compute, rg::ViewScope::SceneView,
rg::RenderPassCategory::Debug, setup, execute)` (keep whatever `name`
literal it already uses, e.g. `"ComputeBlurValidation"` — do not rename
it, this phase is metadata-only).

### 3.5 — Final grep audit for the whole engine

Run `search_in_dir` (recursive, `filter: *.cpp`) for the literal
substrings `.AddPass(` and `.AddComputePass(` across the WHOLE `src/`
tree. After this phase, the only remaining hits should be:

(a) inside `RenderGraphBuilder.cpp`/`.h` itself (the low-level primitives
`AddRenderPass()` is built on — these must stay, see PHASE1's own
"What We Will NOT Do");

(b) inside test files that intentionally exercise the low-level primitives
directly (`tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`);

(c) **`AddSceneViewPass()`'s own `"SceneView"` pass declaration**
(`src/Application/RenderPasses.cpp`, `builder.AddPass("SceneView",
rg::ViewScope::SceneView, ...)`) — this is a DELIBERATE, PERMANENT
exception, not a gap this phase (or any later one) is meant to close.
`PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md`'s own Step 3.3
already locked this in ("`AddSceneViewPass()`... is explicitly OUT OF
SCOPE for this split... Leave `AddSceneViewPass()` completely untouched by
this phase") and no phase in this campaign ever migrates it — the Scene
View is not part of the Frame Debugger's own Game-View-only scope
(`docs/conventions/frame-debugger.md`'s long-standing rule) and this whole
campaign never had a reason to touch it. Expect this ONE surviving
production `builder.AddPass(` hit forever after this phase — do NOT
"fix" it by migrating `AddSceneViewPass()` onto `AddRenderPass()`; that
would be a real, unrequested scope expansion, not a completion of this
phase's own job;

(d) **`ImGuiEditorLayer.cpp`'s `m_blurValidation.AddPass(builder, renderer,
sceneViewHandle, m_sceneView.Sampler(), sceneExtent)` call** — a FALSE-
POSITIVE grep hit. This is a call to `ComputeBlurValidation::AddPass()`'s
own, unrelated CLASS METHOD (mirroring `AtmosphereLutRenderer`'s own
`AddXxxLutPass()` method-naming convention — see that class's own header
comment), not a raw `RenderGraphBuilder::AddPass()`/`AddComputePass()`
call itself — the substring `.AddPass(` simply also matches this
unrelated method name. Only `ComputeBlurValidation::AddPass()`'s OWN
internal `builder.AddComputePass(...)` call (already migrated by 3.4
above) is this phase's actual concern; do not attempt to rename or
"migrate" this call SITE itself, there is nothing to migrate here.

Every OTHER production (non-test, non-`RenderGraphBuilder`, non-(c)/(d)
above) call site should now go through `AddRenderPass()`. If you find a
remaining production hit this phase's own plan (including the (c)/(d)
carve-outs above) didn't already name, that's a real gap this document
missed — migrate it too, and note it explicitly in the completion report
(do not silently skip it, and do not silently expand scope beyond fixing
it either — flag it).

## Definition of Done

- Every call site named in Step 3 above now goes through
  `AddRenderPass()`, correctly tagged.
- The `search_in_dir` audit in 3.5 confirms no remaining production
  direct `AddPass()`/`AddComputePass()` call sites outside
  `RenderGraphBuilder` itself and its own tests, EXCEPT the two known,
  permanent carve-outs from 3.5(c)/(d) (`AddSceneViewPass()`'s own
  `"SceneView"` pass declaration, and the `ImGuiEditorLayer.cpp`
  `m_blurValidation.AddPass(...)` false-positive grep hit) — both expected
  and NOT bugs.
- The Frame Debugger's tree (re-verify with the same
  `run_app_background`/`gte_send_request` HTTP flow as PHASE4) still
  shows NO leaves at all for the N Frame Debugger replay passes or for
  `ComputeBlurValidation` (when enabled) — confirming the `Debug`
  category's exclusion guard works.
- Incremental compile succeeds; completion report + git commit as usual.

## What We Will NOT Do

- Do NOT change GPU Skinning/Present/Compute Blur Validation's actual
  rendering behavior, resource declarations, or execution order — this
  phase is a pure metadata/chokepoint migration for these three.
- Do NOT attempt to make the Frame Debugger replay passes or Compute
  Blur Validation visible in any tree anywhere — they are deliberately,
  permanently invisible debug-internal machinery, not a feature this
  campaign is trying to expose.
