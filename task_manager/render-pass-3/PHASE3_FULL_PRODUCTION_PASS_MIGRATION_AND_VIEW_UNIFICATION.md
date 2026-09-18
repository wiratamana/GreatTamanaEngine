# PHASE3: Full Production Pass Migration + Game/Scene View Unification

_Child of `PHASE0_MASTER_STRATEGY.md` — read that file first. Also read
`PHASE1_CORE_VOCABULARY_AND_BLACKBOARD.md` and
`PHASE2_GPU_SKINNING_OPAQUE_BLACKBOARD_PROOF.md` — this phase builds
directly on both. Part of the `render-pass-3` campaign._

**This is the heaviest, highest-risk phase in this campaign** (large
surface area: `Application.cpp`'s whole `build` lambda, `RenderPasses.cpp`/
`.h`, `AtmospherePassSequence.cpp`/`.h`). `PHASE0_MASTER_STRATEGY.md`'s own
note about "a phase might carry heavy context" is about THIS phase — the
2nd-iteration double-check pass covering this whole campaign's `.md` files
was specifically instructed to consider giving this one file its own,
dedicated double-check pass before the full-campaign double-check runs.
Whoever actually IMPLEMENTS this phase should also consider splitting its
own real, incremental work into smaller sub-steps/sub-commits (e.g. 3a:
Scene View + Opaque generalization; 3b: Sky Background; 3c: Transparent;
3d: Atmosphere; 3e: Present) rather than one giant commit, even though it
all lives under this one phase document — the Definition of Done below
must be satisfied at the END of this phase, not necessarily inside one
single, atomic step.

## Step 1: The Goal

Every remaining PRODUCTION pass — Scene View's own Opaque+Sky draw,
`"DrawSkyBackground"`, `"RenderTransparent"`, the four/five Atmosphere LUT
passes plus the Aerial Perspective Composite pass, and `"Present"` — moves
onto the `RenderPipeline` provider system PHASE1 built and PHASE2 proved
out. `Application.cpp`'s hand-duplicated `if (gameTarget != nullptr) {
... } if (sceneTarget != nullptr) { ... }` blocks collapse into ONE
generic per-view provider loop, driven by `frame.activeViews` containing
whichever of `RenderViewId::Named("Game")`/`Named("Scene")` is actually
visible this frame. By the end of this phase, `Application.cpp`'s own
`build` lambda for the offscreen regime is dramatically shorter, and a
brand-new `m_presentRenderPipeline` handles the swapchain regime's own
`"Present"` pass.

Per `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 4, this phase
does NOT touch `AddFrameDebuggerReplayPasses()` or
`ComputeBlurValidation.cpp` — both stay on the old, direct
`AddRenderPass()` call style forever.

## Step 2: The Situation

- `AddSceneViewPass()` (`RenderPasses.cpp`) is a near-twin of
  `AddRenderOpaquePass()`+`AddDrawSkyBackgroundPass()` FUSED into one pass
  (it does NOT split Opaque from Sky the way Game View already does — it
  draws both inside one `builder.AddPass("SceneView", ViewScope::SceneView,
  ...)` call, with `recordSkyBackground` invoked BEFORE
  `recordSceneOverlay` inside the same `execute` lambda).
- `Application.cpp`'s `build` lambda (confirmed, lines ~594 onward for the
  Game View block, and a structurally similar block afterward for Scene
  View) independently: resolves an eye world position + view-projection
  matrix for that view, calls `AddAtmosphereViewLutPasses()` for that
  view's own Sky-View LUT + Aerial Perspective volume, builds a
  `recordSkyBackground` callback via `MakeRecordSkyBackgroundCallback()`,
  calls that view's own Opaque/Combined-view pass, and calls
  `AddAtmosphereCompositePass()` for that view.
- `AtmosphereLutRenderer`'s six `AddXxxLutPass()`/`AddAerialPerspectiveCompositePass()`
  methods (`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`) already take
  a `viewScope` parameter and already call `AddRenderPass()` — these are
  RENDERER-layer functions, not Application-layer ones (see
  `RenderPasses.h`'s own Clean Architecture comment on why): this phase
  does NOT move their bodies into `RenderPipeline` providers directly
  (`RenderPipeline` and everything that constructs a `RenderPassDesc`
  belongs to the Application layer, per this same Clean Architecture rule —
  `Renderer`/`AtmosphereLutRenderer` must never know about `RenderPipeline`/
  `RenderPassDesc` at all). Instead, THIS phase's new Application-layer
  provider wrapper functions (living in `AtmospherePassSequence.cpp`, same
  file as today) call the EXISTING, UNCHANGED `AtmosphereLutRenderer`
  methods DIRECTLY, against a real `RenderGraphBuilder&` reached through the
  frame context (see Step 3.3b below) — NOT from inside a
  `RenderPassDesc.setup`/`.execute` pair, since these existing methods
  themselves call `builder.AddRenderPass()` (sometimes several times, with
  a real data dependency between calls), which cannot be deferred that way.
  Only the OUTER wrapping (a free function that calls
  `builder.AddRenderPass()` directly, vs. a provider registered on
  `RenderPipeline` that does the same, immediately, using a builder handed
  to it via the frame context) changes — see Step 3.3b for the full
  mechanical resolution this requires.
- `AddPresentPass()` (`RenderPasses.cpp`) is declared inside the SEPARATE,
  PIPELINED swapchain `RenderGraph::Execute()` call (a completely different
  `build` lambda, later in `Application.cpp`, for the swapchain regime) —
  `m_offscreenRenderPipeline` must NEVER be reused for this; see PHASE0's
  Locked Design Decision 6.

## Step 3: The Plan

### 3.1 — Per-view data: what a provider needs to know about "the current view"

Add a small, plain struct (e.g. `RenderPassViewData`, living wherever is
cleanest — `AtmospherePassSequence.h` or a new small
`src/Application/RenderPassViewData.h`, Application-layer, NOT
`RenderPipeline.h` itself, since this is Application-specific "what is a
view" knowledge the generic `rg` layer must never carry):

```cpp
struct RenderPassViewData {
    rg::RenderViewId id;
    rg::TextureHandle colorTarget;
    RenderTexture* renderTexture = nullptr; // for Sampler()/DepthSampler()/Extent()/Format() - see AddAtmosphereCompositePass()
    float aspectWidthOverHeight = 1.0f;
    Mat4 viewProjection;
    Vec3 eyeWorldPosition;
    // Scene-View-only, empty/default for Game View:
    std::function<void(VkCommandBuffer, const Mat4&)> recordSceneOverlay;
};
```

`Application.cpp`'s `build` lambda constructs a
`std::vector<RenderPassViewData>` ONCE per frame (0, 1, or 2 entries,
depending on which of `gameTarget`/`sceneTarget` is non-null), and stores
it somewhere `RenderPassFrameContext` can reach — either as a NEW field
directly on `RenderPassFrameContext` (`std::vector<RenderPassViewData>
viewData;` plus a small `const RenderPassViewData* FindViewData(RenderViewId)
const` helper) or as an Application-owned side table a provider's captured
`this` can look up by `frame.currentView` — prefer putting it ON the frame
context itself (simpler, and it is genuinely per-frame data the design
doc's own Section 5 `RenderPassFrameContext` comment already anticipates:
"... camera/target/dt data, all plain, all opaque to the pipeline itself
...").

### 3.2 — Legacy `ViewScope` translation table (the Decision-5 bridge)

Exactly one small, private, Application-layer function
`rg::ViewScope TranslateLegacyViewScope(rg::RenderViewId view) noexcept`:

```cpp
rg::ViewScope TranslateLegacyViewScope(rg::RenderViewId view) noexcept
{
    if (view == rg::RenderViewId::Named("Game")) return rg::ViewScope::GameView;
    if (view == rg::RenderViewId::Named("Scene")) return rg::ViewScope::SceneView;
    return rg::ViewScope::Shared; // Shared() itself, or any future named view with no legacy meaning
}
```

`RenderPipeline::DeclareInto()` (PHASE1) calls this INTERNALLY,
immediately before its own `builder.AddRenderPass(...)` call, so every
migrated pass's underlying `PassRecord::viewScope` is stamped EXACTLY as
if the old direct call had supplied it by hand — this is what keeps
`FrameDebuggerData.cpp`'s `viewScope == SceneView` checks (Step 2 of
`PHASE0_MASTER_STRATEGY.md`) working, unchanged, for every pass this
phase migrates. Since this function is Application-specific knowledge
("Game"/"Scene" are OUR names for views, not the core `rg` layer's), it
must live in `src/Application/` (e.g. a small file alongside
`RenderPassViewData`), and `RenderPipeline::DeclareInto()` (a core `rg`
type) must take it as an INJECTED callable rather than hardcoding it — add
a small, optional `std::function<rg::ViewScope(RenderViewId)>
legacyViewScopeTranslator` member/constructor parameter on `RenderPipeline`
itself (defaulting to "always return `Shared`" if never set), assigned
once when `m_offscreenRenderPipeline`/`m_presentRenderPipeline` are
constructed in `Application`. This keeps `rg::RenderPipeline` itself
free of any "Game"/"Scene" string knowledge, matching this whole
campaign's own Clean Architecture rule.

### 3.3 — Register every remaining offscreen-regime provider

On `m_offscreenRenderPipeline` (already holding `"GpuSkinning"`/
`"RenderOpaque"` from PHASE2), register, in this order (order of
REGISTRATION does not matter — `DeclareInto()`'s own `.order`-based sort
is what actually fixes declaration order — but register them in this
order anyway, for readability, matching today's real call order):

| Provider name | Scope | `RenderPassEvent` | Notes |
|---|---|---|---|
| `"AtmosphereSharedLut"` | `Once` | `BeforeEverything` | Wraps `AddAtmosphereSharedLutPasses()` unchanged; publishes `transmittanceLutHandle`/`multiScatteringLutHandle` onto the blackboard (a new key, e.g. `"Atmosphere.SharedLuts"_passId`) for the per-view Atmosphere provider below to fetch — this REPLACES today's `Application.cpp` local variable capture with a second, real blackboard hand-off, reinforcing the pattern PHASE2 proved. Also appends both handles to `frame.finalTextureOutputs` (PHASE1's own mechanism), replacing today's manual `outputs.push_back(...)` calls. |
| `"AtmosphereViewLut"` | `PerActiveView` | `PreOpaques` | Wraps `AddAtmosphereViewLutPasses()`; reads the shared LUT handles from the blackboard; publishes its OWN per-view results (`skyViewLutHandle`/`aerialPerspectiveVolumeHandle`/`frameUniforms`) onto the blackboard, keyed per-view (e.g. combine the key string with `frame.currentView`'s own debug name, or publish a small `std::unordered_map<RenderViewId, ...>`-shaped value under one fixed key — pick whichever is simplest; document the choice) — the Sky Background/Composite providers below need these same values for the SAME view. |
| `"RenderOpaque"` | `PerActiveView` | `Opaques` | Already exists from PHASE2 — GENERALIZE it now to read `frame.viewData`/`FindViewData(frame.currentView)` instead of a single captured `gameViewTarget`, so it works correctly for BOTH Game and Scene views. **This is the one place Scene View's own opaque draw becomes a REAL, SEPARATE `"RenderOpaque"`-shaped pass for the first time** — see Step 3.4 below for what this means for `"SceneView"`'s old, fused pass. |
| `"DrawSkyBackground"` | `PerActiveView` | `AfterOpaques` | Wraps `AddDrawSkyBackgroundPass()`'s existing body (unchanged clear/no-clear rules — see that function's own doc comment on why it must never clear); fetches this view's own Sky-View LUT + frame uniforms from the blackboard (published by `"AtmosphereViewLut"` above) to build its own `recordSkyBackground` callback via `MakeRecordSkyBackgroundCallback()`, replacing today's manual capture. |
| `"RenderTransparent"` | `PerActiveView` | `Transparents` | Wraps `AddRenderTransparentPass()`'s existing (always-empty-today) body unchanged. |
| `"AtmosphereComposite"` | `PerActiveView` | `AfterTransparents` | Wraps `AddAtmosphereCompositePass()`; fetches this view's own Aerial Perspective volume handle + frame uniforms from the blackboard; appends its own output `TextureHandle` to `frame.finalTextureOutputs` and calls `builder.KeepVolumeTextureOutput(...)` for the volume handle from directly inside its OWN `setup` lambda (this is a `PassBuilder`-adjacent, `RenderGraphBuilder&`-level call — resolve exactly like PHASE2's own `ImportBuffer()` wrinkle: prefer resolving this OUTSIDE the provider, at the point `Application::Run()` already has a real `RenderGraphBuilder& b`, if at all possible). |

### 3.3b — The Atmosphere-wrapping providers need REAL builder access, not
just a `RenderPassDesc`

**A real, load-bearing mechanical gap the table above glosses over —
resolve this explicitly, do not skip it.** `RenderPassProvider` (PHASE1)
has the signature `(const RenderPassFrameContext&,
std::vector<RenderPassDesc>&) -> void` — it never receives a
`rg::RenderGraphBuilder&` at all, by design (a provider is only supposed
to APPEND data; `RenderPipeline::DeclareInto()`'s own FINAL, sorted loop is
the only thing that ever calls `builder.AddRenderPass()`). That model fits
`"GpuSkinning"`/`"RenderOpaque"` (PHASE2) because each produced
`RenderPassDesc` maps to exactly ONE real pass, with its `setup`/`.execute`
bodies only ever needing the `PassBuilder&`/`PassContext&` PHASE1 already
threads through. It does NOT fit `AddAtmosphereSharedLutPasses()`/
`AddAtmosphereViewLutPasses()`/`AddAtmosphereCompositePass()`: each of
these free functions (and the `AtmosphereLutRenderer` methods they call —
`AddTransmittanceLutPass()`, `AddMultiScatteringLutPass()`,
`AddSkyViewLutPass()`, `AddAerialPerspectiveVolumePass()`,
`AddAerialPerspectiveCompositePass()`) calls `builder.ImportTexture()`/
`builder.AddRenderPass()`/`builder.KeepVolumeTextureOutput()` directly,
sometimes MORE THAN ONCE per call, against a REAL, live
`RenderGraphBuilder&`, with a genuine data dependency between successive
calls (`AddMultiScatteringLutPass()` needs `AddTransmittanceLutPass()`'s
own just-returned `TextureHandle` as an argument, and cannot run before
it). None of that can be deferred into a single `RenderPassDesc.setup`/
`.execute` pair the way the table above implies — there is no
`RenderGraphBuilder&` anywhere in scope by the time a provider only has
`PassBuilder&`/`PassContext&` to work with, and by the time
`DeclareInto()`'s own final loop DOES have the real `builder`, the
provider has already returned.

**Resolution**: add ONE small, additive field directly to
`RenderPassFrameContext` (PHASE1's own struct) — `rg::RenderGraphBuilder&
builder;` — set once by `Application::Run()` to the exact same `b` its
`build` lambda already receives, immediately before calling
`DeclareInto()` (mirrors the precedent PHASE1 already set adding
`finalTextureOutputs`/`finalVolumeTextureOutputs` beyond the design doc's
own Section 5 shape). The THREE Atmosphere-sequence providers
(`"AtmosphereSharedLut"`/`"AtmosphereViewLut"`/`"AtmosphereComposite"`) are
therefore a deliberate, documented EXCEPTION to the "providers only append
data" rule: their bodies call `AddAtmosphereSharedLutPasses(frame.builder,
...)`/`AddAtmosphereViewLutPasses(frame.builder, ...)`/
`AddAtmosphereCompositePass(frame.builder, ...)` DIRECTLY, declaring their
real passes immediately (exactly as `Application.cpp` does today), and
publish their resulting handles onto the blackboard for later providers to
fetch — they contribute ZERO deferred `RenderPassDesc` entries to `out` for
the actual LUT/composite work itself (only `"RenderOpaque"`/
`"DrawSkyBackground"`/`"RenderTransparent"` — the ones with no such
inter-call data dependency — actually use the deferred `RenderPassDesc`
mechanism). Document this exception loudly in the completion report: it
means `RenderPipeline` is not quite as generic as
`GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`'s own Section 3 diagram implies
once a wrapped legacy function is itself a multi-pass declaration entry
point rather than a single setup/execute body — a real, worthwhile
limitation to flag for whoever reads this campaign's own completion report
later, not something to silently paper over.

One second-order consequence worth stating explicitly: because these three
providers call `builder.AddRenderPass()` immediately, DURING
`DeclareInto()`'s own provider-invocation loop, their passes land in the
underlying `m_passes` vector BEFORE any of the deferred, `RenderPassDesc`-
sorted passes (`RenderOpaque` etc.), regardless of `RenderPassEvent` value
— `RenderPassEvent` only ever sorts the DEFERRED half of the collected
list. This happens to match today's real, intended order (Atmosphere
before Opaque) but is a real, structural quirk of mixing immediate-declare
and deferred-declare providers in one `RenderPipeline` — call this out in
the completion report too, since a future reader could otherwise assume
`RenderPassEvent` alone fully explains declaration order (real ordering
correctness still ultimately rests on the compiler's own dependency
analysis, per `RenderGraphCompiler.cpp`'s own "declaration order is one
valid topological order" reasoning — worth re-confirming this explicitly
for the Sky-Background-before-ground-grid requirement in Step 3.4 below,
not just assuming it).

### 3.4 — Scene View's own pass genuinely SPLITS into Opaque + Sky for the
first time

This is the one real, user-approved (Locked Design Decision 1), BREAKING
structural change this phase makes: `AddSceneViewPass()`'s old, single,
fused pass (draw everything, THEN sky, THEN the Editor's own ground-grid
overlay, all inside one `vkCmdBeginRendering` bracket) is REPLACED by
Scene View going through the exact SAME `"RenderOpaque"`/
`"DrawSkyBackground"`/`"RenderTransparent"` providers Game View already
uses — each one now genuinely declares a SEPARATE `PassRecord` per view
(same pass NAME, e.g. both views' Opaque draw is literally named
`"RenderOpaque"` — this is fine and already how `ViewScope`-duplicated
passes work today, e.g. `AtmosphereSkyViewLutPass` already gets declared
twice with the same literal name, once per view, per `RenderGraphTypes.h`'s
own `ViewScope` doc comment). The ONE thing Scene View needs that Game View
does not — `recordSceneOverlay` (the Editor's ground-grid overlay,
`IEditorLayer::RenderSceneGrid()`) — is invoked from a NEW, small,
Scene-View-only tail step: either (a) fold it into `"RenderTransparent"`'s
own provider body, gated on `frame.currentView == RenderViewId::Named("Scene")`
(simplest — `"RenderTransparent"` already always runs per-view and is
already a no-op for Game View today in terms of real transparent
geometry), or (b) add ONE more small provider,
`"SceneViewOverlay"`(`PerActiveView`, `RenderPassEvent::AfterTransparents`,
a genuine no-op when `frame.currentView != Named("Scene")`). Pick whichever
reads more clearly once the real code is in front of you; document the
choice. Either way, the actual ORDERING requirement from
`AddSceneViewPass()`'s own existing doc comment (sky background BEFORE the
grid overlay, so the grid's own alpha blend composites correctly) MUST
still hold — verify this explicitly with a live Scene View screenshot
(`gte_send_request`) showing the ground grid still renders correctly
against the sky.

`AddSceneViewPass()` itself (`RenderPasses.cpp`/`.h`) can be DELETED once
nothing calls it — confirm via `search_in_dir` for `AddSceneViewPass`
across the whole repo (including `tests/`) before deleting; if a test
directly exercises it, migrate/delete that test too, noting it in this
phase's completion report (this is one of the very few places this
campaign deletes an existing function, since, unlike the OLD enums, this
one really has no remaining caller once this phase lands — it is not
protected by any Locked Design Decision the way `ViewScope`/
`RenderPassCategory` are).

### 3.5 — `m_presentRenderPipeline` and `"Present"`

Add a SECOND, separate `rg::RenderPipeline m_presentRenderPipeline;`
member on `Application`, with exactly one provider, `"Present"`
(`Once`, `RenderPassEvent::Opaques` — irrelevant, since it is the only
pass this pipeline ever declares), wrapping `AddPresentPass()`'s existing
body unchanged (including its `directGameRenderAspect` fallback branch —
this fallback path's own existing "never pass a real
`frameDebuggerCapture`" rule, `RenderPasses.h`'s own doc comment, is
UNCHANGED by this phase). Driven from the SEPARATE, PIPELINED swapchain
`Execute()` call's own `build` lambda, with its own, separate
`RenderPassFrameContext`/`RenderPassBlackboard` (never shared with the
offscreen regime's own blackboard/frame context).

**Correction to an inaccurate assumption about today's code — verify this
directly against `Application.cpp` before writing the provider body**:
today's real, current present-regime `build` lambda does NOT keep one
`gpuSkinningBuffers` variable alive across both `Execute()` calls. Each
regime's own `build` lambda declares its OWN, separately-scoped local of
that name: the offscreen lambda's copy comes from its own unconditional
`AddGpuSkinningPasses(b, m_game, m_renderer)` call; the present lambda's
copy is a SECOND, INDEPENDENT call —
`needsDirectGameRender ? AddGpuSkinningPasses(b, m_game, m_renderer) :
std::vector<rg::BufferHandle>{}` — populated ONLY in the direct-render
fallback branch (both Game/Scene panels hidden that frame), and empty
every other frame. This is not an arbitrary style choice:
`AddGpuSkinningPasses()`'s own doc comment (`RenderPasses.h`) states
plainly that "a compute pass declared into a DIFFERENT `Execute()` call
could never be ordered against [it] by the compiler at all — each
`Execute()` call compiles/executes its own, completely independent graph."
A `BufferHandle` minted inside the offscreen regime's own
`CompiledGraphInput` is meaningless (its index/generation belongs to a
completely different, separately-compiled graph's own tables) inside the
swapchain regime's own graph — reusing/capturing it across the two
`Execute()` calls, as an earlier draft of this section suggested, would
silently reintroduce exactly that class of correctness bug, not "keep
already-proven behavior" as it claimed.

The correct migration therefore preserves today's real mutual-exclusivity
rule: the `"Present"` provider must itself decide (via a plain bool
threaded into its own frame context, or captured the same way
`needsDirectGameRender` is already computed today) whether it is
responsible for a direct Game render this frame, and if so, call
`AddGpuSkinningPasses(builder, ...)` freshly, itself, against the
swapchain regime's OWN builder — exactly mirroring what `Application.cpp`
already does today — never fetching or reusing any handle the offscreen
regime's `"GpuSkinning"` provider published to ITS OWN, separate
blackboard this same frame. Since this means `"Present"`'s own provider
needs to call `AddGpuSkinningPasses()`/`AddPresentPass()` directly (both
of which themselves call `builder.AddRenderPass()`), it has the exact same
`RenderGraphBuilder&`-access requirement as the Atmosphere-wrapping
providers in Step 3.3b above — resolve it the same way (a `builder` field
on this pipeline's own `RenderPassFrameContext`, set from the swapchain
regime's own `build` lambda's real `b`).

### 3.6 — `Application.cpp`'s `build` lambda: the final shape

After this phase, the offscreen regime's `build` lambda should read
roughly:

```cpp
m_renderGraph.Execute(offscreenCmd, rg::ExecuteTimingMode::SynchronousImmediateReadback,
    [&](rg::RenderGraphBuilder& b) {
        rg::RenderPassBlackboard blackboard;
        blackboard.BeginFrame();
        rg::RenderPassFrameContext frame{ /* ... */ blackboard };
        if (gameTarget != nullptr) { frame.activeViews.push_back(rg::RenderViewId::Named("Game"));
            frame.viewData.push_back(BuildGameViewData(...)); }
        if (sceneTarget != nullptr) { frame.activeViews.push_back(rg::RenderViewId::Named("Scene"));
            frame.viewData.push_back(BuildSceneViewData(...)); }

        m_offscreenRenderPipeline.DeclareInto(b, frame);
#ifndef NDEBUG
        blackboard.ReportUnusedPublishesIfAny();
#endif
        return frame.finalTextureOutputs; // plus any VolumeTexture KeepVolumeTextureOutput() calls already made inside providers' own setup lambdas
    });
```

This should be DRAMATICALLY shorter than today's hand-duplicated
`if (gameTarget) { ...40+ lines... } if (sceneTarget) { ...40+ lines... }`
pair — if the resulting lambda is NOT meaningfully shorter/simpler than
before, something about the migration went sideways (e.g. per-view logic
leaking back out into `Application.cpp` instead of living inside each
provider) — treat that as a signal to reconsider the split, not something
to just accept.

### 3.7 — Preserving `AddFrameDebuggerReplayPasses()`'s existing,
unmigrated call site

**A real gap in the `build`-lambda sketch above — Locked Design Decision 4
requires this call site to keep working, unmigrated, and Step 3.6's
simplified lambda does not show how.** Today, `AddFrameDebuggerReplayPasses()`
is called directly from `Application.cpp`, still inside the
`if (gameTarget != nullptr)` block, AFTER `AddRenderOpaquePass()`/
`AddDrawSkyBackgroundPass()`/`AddRenderTransparentPass()` are declared,
using THREE values that only exist as ordinary locals in today's code
because the whole sequence is still hand-written inline: `gpuSkinningBuffers`
(from `AddGpuSkinningPasses()`), `recordGameSkyBackground` (built via
`MakeRecordSkyBackgroundCallback()`, itself fed by the Game View's own
`AddAtmosphereViewLutPasses()` result), and `objectCount`/`aspect`/
`*gameTarget`/`*frameDebuggerCapture` (all still ordinary locals after this
phase too). Once `"GpuSkinning"`/`"AtmosphereViewLut"`/`"RenderOpaque"`/
`"DrawSkyBackground"` move behind `RenderPipeline` providers, the first two
of those three values are produced DEEP INSIDE provider closures with no
stated path back out to `Application.cpp` — Step 3.6's sketch simply drops
the whole call, which would silently regress the Frame Debugger's
replay-step feature for Game View.

**Resolution**: `"GpuSkinning"` already publishes its buffer vector onto
the blackboard (PHASE2) — keep doing so. Additionally, `"AtmosphereViewLut"`
must ALSO publish its Game-View sky-background callback (or the raw
ingredients `MakeRecordSkyBackgroundCallback()` needs — whichever is
simpler to key correctly per-view) onto the blackboard, per view. Because
`blackboard`/`frame` are both still ordinary locals owned by
`Application::Run()`'s own `build` lambda (constructed just before calling
`m_offscreenRenderPipeline.DeclareInto(b, frame)`), they are STILL IN SCOPE,
unchanged, immediately after `DeclareInto()` returns and before
`ReportUnusedPublishesIfAny()`/the lambda's own `return` — this is exactly
where `Application.cpp` should `Fetch()` the Game View's own published
buffers/callback back out and call `AddFrameDebuggerReplayPasses()` exactly
as it does today, still as a direct, unmigrated call (Locked Design
Decision 4). This also naturally satisfies the existing ordering
requirement (replay passes must be declared AFTER the real
`"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"` passes) for
free, since `DeclareInto()`'s own final, sorted loop — the thing that
actually calls `builder.AddRenderPass()` for those three — has already
fully returned by the time `Application.cpp` reaches this point. Verify
this specific path live (`gte_send_request`, Frame Debugger replay-step
capture on Game View) — do not assume it still works purely from a
successful compile.

## Definition of Done

- `Application.cpp`'s offscreen `build` lambda no longer contains two
  separate, hand-duplicated `if (gameTarget != nullptr)`/
  `if (sceneTarget != nullptr)` blocks each independently calling
  Atmosphere/Opaque/Sky/Transparent/Composite — both views are handled by
  ONE generic loop inside `RenderPipeline::DeclareInto()`.
- `AddSceneViewPass()` is deleted (or confirmed to have zero remaining
  callers if kept for some other reason — document explicitly either way).
- Scene View now genuinely produces separate `"RenderOpaque"`/
  `"DrawSkyBackground"`/`"RenderTransparent"` passes tagged
  `ViewScope::SceneView` (verify via a live capture/inspection, e.g.
  `GET /frame_debugger/capture` — note the Frame Debugger tree itself is
  Game-View-only per its own existing, unchanged design, so this is
  verified via `RenderGraphSnapshot`/direct engine inspection or a
  temporary debug log, not the Frame Debugger UI itself).
- `m_presentRenderPipeline` exists, with `"Present"` as its one provider,
  driven from the swapchain regime's own `Execute()` call.
- Every provider correctly stamps `legacyCategory`/`view`→`ViewScope`
  exactly as the OLD direct calls used to, confirmed by: (a) a direct
  `RenderGraphSnapshot` field comparison for at least `"RenderOpaque"`,
  `"DrawSkyBackground"`, and one Atmosphere LUT pass, OR (b) the Frame
  Debugger's Game-View tree shape (`GET /frame_debugger/capture` +
  `/get_swapchain`) being visually IDENTICAL to before this phase.
- A live, HTTP-driven check confirms: Game View renders correctly; Scene
  View renders correctly (mesh geometry + sky + ground grid, correctly
  ordered); Present (a release-style direct-render fallback, if
  reachable/testable in this build configuration) still works.
- An incremental compile of `gte_core` succeeds.
- `AddFrameDebuggerReplayPasses()` and `ComputeBlurValidation.cpp` are
  BYTE-FOR-BYTE UNCHANGED by this phase (confirm via `git diff` showing
  zero changes to either file).
- The Atmosphere-wrapping providers (`"AtmosphereSharedLut"`/
  `"AtmosphereViewLut"`/`"AtmosphereComposite"`) are confirmed to declare
  their real passes via a genuine `rg::RenderGraphBuilder&` reachable from
  `RenderPassFrameContext` (Step 3.3b) - NOT by attempting to defer a
  multi-pass, inter-dependent legacy function through a single
  `RenderPassDesc.setup`/`.execute` pair.
- `AddPresentPass()`'s own `gpuSkinningOutputBuffers` are supplied by a
  FRESH, direct-render-only `AddGpuSkinningPasses()` call made from inside
  the swapchain regime's own `"Present"` provider - confirmed, by code
  reading, to never reuse/capture a `BufferHandle` produced by the
  offscreen regime's own, separately-compiled graph this same frame (Step
  3.5).
- `AddFrameDebuggerReplayPasses()`'s existing call site in
  `Application.cpp` still compiles, is still called (unmigrated, per Locked
  Design Decision 4), and still functions correctly for Game View - verified
  live via a Frame Debugger replay-step capture (Step 3.7).

## What We Will NOT Do

- Do NOT touch `AddFrameDebuggerReplayPasses()` or
  `ComputeBlurValidation.cpp` — Locked Design Decision 4.
- Do NOT touch `FrameDebuggerData.cpp` in this phase — PHASE4's job, and
  ONLY the one named pivot-search fix, nothing else.
- Do NOT delete `ViewScope`/`RenderPassCategory`/`RenderPassDrawKind`, or
  the old `AddRenderPass()` overloads themselves — Locked Design Decision
  5. This phase only ADDS new providers that call INTO those existing,
  unchanged mechanisms.
- Do NOT attempt to make `RenderGraphBuilder`/`RenderPipeline` itself know
  the strings `"Game"`/`"Scene"` — that knowledge belongs entirely in
  `Application`'s own `TranslateLegacyViewScope()` function and the
  `RenderViewId::Named("Game")`/`Named("Scene")` call sites, never inside
  `rg::RenderPipeline`'s own implementation.
- Do NOT run a full build or full `ctest` regression suite in this phase —
  incremental compile + targeted live/HTTP verification only. PHASE5 is
  the only phase that runs the full suite.
