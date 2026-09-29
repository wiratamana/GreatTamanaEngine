# PHASE1 — Guaranteed View-Target Clear (the primary pink-screen fix)

Parent: `PHASE0_MASTER_STRATEGY.md` — **read it first**, it contains the full root-cause writeup this phase fixes (Root Cause #1, section 2.2, INCLUDING its "second, load-bearing subtlety" paragraph about `finalTextureOutputs`/culling — do not skip that part, Step 3.2 below implements it).

This is an **implementation task**. Do not use `delegate_task` — you are not allowed to delegate further sub-tasks. If you hit a genuine design ambiguity or disagree with a Locked Design Decision from PHASE0, use `ask_questions` to ask the user directly, in plain, easy English, before proceeding.

---

## Step 1: The Goal

Make the Game View / Scene View render target **always** get a real, defined clear (`kGameClearColor` for color, `kGameClearDepth` for depth) exactly once per frame, per view, **no matter which of `RenderOpaque`/`DrawSkyBackground`/`RenderTransparent` are currently enabled or disabled** via the "Render Graph" panel / `GET /render_graph/set_pass_enabled` — and make sure the new clearing pass itself is never silently dropped by the render graph compiler's own dead-code elimination, in any combination of enabled/disabled passes.

## Step 2: The Situation (recap — full detail lives in PHASE0 section 2.2)

- `Core.cpp`'s `"RenderOpaque"` provider currently owns the ONLY clear of the view target's color+depth attachments this frame (`kGameClearColor`/`kGameClearDepth`).
- `"DrawSkyBackground"` deliberately never clears (relies on an `EQUAL` depth-test against whatever `RenderOpaque` already wrote).
- Both are independently toggleable. Disabling `RenderOpaque` while leaving anything else enabled leaves the view target completely uncleared this frame → undefined GPU memory content → renders as solid magenta on this machine → everything downstream (including a perfectly correct `DrawSkyBackground`) silently fails to show anything, because its `EQUAL` depth test never passes against garbage depth.
- **A second, independent hazard the fix itself must close, not just the missing clear.** `RenderGraphCompiler::Compile()` only keeps a declared pass alive if its own write is reachable (directly, via `finalOutputs`/`finalVolumeTextureOutputs` membership, or transitively, via a real RAW/WAW dependency edge from some OTHER kept pass) — see `RenderGraphCompiler.h`'s own "Step 2: backward reachability from finalOutputs" doc comment. Nothing in `Core.cpp` today ever adds the Game/Scene View's own raw `TextureHandle` (`viewData->colorTarget`, referred to below as `viewTarget`) to `frame.finalTextureOutputs` — only DERIVED handles (Atmosphere LUT outputs, the composited output) are ever added as roots. The only pass that currently reads `viewTarget` back is `"AtmosphereComposite"`. This means: if a user disables `"AtmosphereComposite"` (directly, or because this campaign's own Success Criterion #1 disables literally every pass, atmosphere included), **nothing** reads `viewTarget` this frame, so **nothing keeps alive whichever pass most recently wrote it** — including a brand-new `"ClearViewTarget"` pass added with no other change. The new pass would be declared, then immediately culled by the compiler, and its clear would never actually execute — silently reproducing the exact same magenta bug this phase exists to fix. Step 3.2 below closes this by having `"ClearViewTarget"`'s own provider mark `viewTarget` itself as a kept root, every time it runs.

## Step 3: The Plan

### 3.1 — Locate the exact code

Open `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\src\Core\Core.cpp`. Find `Core::RegisterOffscreenRenderPipelineProviders()`. Inside it, locate (in order):
- The `"AtmosphereSharedLut"` provider registration (near the top of the function).
- The `"GpuSkinning"` provider registration.
- The `"RenderOpaque"` provider registration (~line 551, `m_offscreenRenderPipeline.Register("RenderOpaque", rg::ProviderScope::PerActiveView, ...)`).
- The `"GpuDrivenBatches"` provider registration (has an explicit, load-bearing comment: "MUST stay TEXTUALLY AFTER RenderOpaque's own Register() call and TEXTUALLY BEFORE DrawSkyBackground's own Register() call" — **do not disturb this ordering comment/constraint**, your new code must not violate it).
- The `"DrawSkyBackground"` provider registration (~line 733).

Also open `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\src\Renderer\RenderGraph\RenderPassToggleRegistry.cpp` and `.h` (the `IsDenyListed()` function).

Confirm `Core.cpp` already `#include`s `"../Application/RenderPasses.h"` (it does, line 16) — that header is where `kGameClearColor`/`kGameClearDepth` are defined (`inline constexpr` values, `RenderPasses.h` lines 96/99), so both are already visible inside `Core.cpp` with no new include needed.

### 3.2 — Add the new `"ClearViewTarget"` provider

Insert a brand-new provider registration **immediately before** the `"RenderOpaque"` registration (textual placement doesn't affect execution order — `RenderPassEvent` does, see 3.3 — but placing it first here is the clearest reading order for a future maintainer). Use `ProviderScope::PerActiveView` (exactly like `"RenderOpaque"`) so it fires once for Game View and once for Scene View automatically — you get both views' fix for free from one registration.

```cpp
// editor-core-separation-20 campaign, PHASE1 - "ClearViewTarget" -
// ProviderScope::PerActiveView, BeforeEverything. The ONE guaranteed clear
// of each active view's color+depth render target, decoupled from whether
// "RenderOpaque" itself is enabled this frame - see
// task_manager/editor-core-separation-20/PHASE1_GUARANTEED_VIEW_TARGET_CLEAR.md
// for the full "why" (a confirmed, live bug: disabling "RenderOpaque" via
// the "Render Graph" panel/HTTP used to leave this exact resource
// completely uncleared for the whole frame, since "DrawSkyBackground"
// deliberately never clears either - relying on RenderOpaque's own EQUAL-
// depth-test setup instead). RenderPassEvent::BeforeEverything makes
// RenderGraphCompiler::Compile()'s own effective-order sort
// (render-pass-4 campaign) place this pass before every other pass
// touching the same resource, regardless of provider registration order -
// this is genuinely load-bearing here, not just documentation. Permanently
// denylisted (RenderPassToggleRegistry::IsDenyListed(), updated below) -
// can never be turned off via the panel/HTTP, exactly like "Present": a
// view with no defined clear has no safe fallback content to show.
m_offscreenRenderPipeline.Register("ClearViewTarget", rg::ProviderScope::PerActiveView,
    [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
        const RenderPassViewData* viewData = FindViewData(frame.currentView);
        if (viewData == nullptr) {
            return;
        }

        const rg::TextureHandle viewTarget = viewData->colorTarget;

        // CRITICAL - not decorative. RenderGraphCompiler::Compile() only
        // keeps a declared pass alive if its own write is reachable
        // (directly or transitively) from this frame's finalOutputs/
        // finalVolumeTextureOutputs root set (RenderGraphCompiler.h's own
        // "Step 2: backward reachability from finalOutputs" doc comment) -
        // an imported/externally-owned resource (like this view's own
        // persistent RenderTexture) gets NO automatic exemption from that
        // culling merely because it is externally visible outside the
        // graph. Nothing else in this file ever adds `viewTarget` itself to
        // frame.finalTextureOutputs (only DERIVED handles - the Atmosphere
        // LUT outputs, the composited output - are ever added as roots),
        // and the only pass that currently reads `viewTarget` back is
        // "AtmosphereComposite" - so whenever that pass (or every pass,
        // this campaign's own primary repro case) is disabled/absent this
        // frame, NOTHING keeps this pass's own write alive without the line
        // below. This provider is the one pass guaranteed to run every
        // frame for every active view (deny-listed, PerActiveView), so it
        // is the correct, single place to make this guarantee - every OTHER
        // real writer of viewTarget (RenderOpaque/DrawSkyBackground/
        // RenderTransparent, whichever of them are enabled this frame) then
        // survives culling too, via the ordinary write-after-write
        // dependency chain back to this pass's own write. This exact
        // "multiple writers to one resource that is itself listed in
        // finalOutputs" shape is an already-tested, proven-safe pattern in
        // this codebase - see RenderGraphCompilerTests.cpp's own
        // MultipleWritersToSameResourcePreserveWriteAfterWriteOrder test.
        frame.finalTextureOutputs.push_back(viewTarget);

        rg::RenderPassDesc desc;
        desc.debugName = "ClearViewTarget";
        desc.kind = rg::PassKind::Graphics;
        desc.order = rg::RenderPassEvent::BeforeEverything;
        desc.view = frame.currentView;
        desc.legacyCategory = rg::RenderPassCategory::General;
        desc.setup = [viewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(viewTarget, kGameClearColor);
            pass.WriteDepthStencilAttachment(viewTarget, kGameClearDepth);
        };
        // Deliberately NO desc.execute - RenderGraph::ExecuteCompiledGraph()
        // already tolerates a null execute (`if (pass.execute) { ... }`,
        // RenderGraph.cpp) - this pass's entire job is the load-op CLEAR its
        // attachment writes above request; it issues no draw call of its own.
        out.push_back(std::move(desc));
    });
```

Place this block right above the existing `// "RenderOpaque" - ProviderScope::PerActiveView.` comment/registration.

### 3.3 — Stop `"RenderOpaque"` from clearing

Still inside `Core.cpp`, find the `"RenderOpaque"` provider's `desc.setup` lambda (~line 575):

```cpp
desc.setup = [viewTarget, gpuSkinningBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
    pass.WriteColorAttachment(viewTarget, kGameClearColor);
    pass.WriteDepthStencilAttachment(viewTarget, kGameClearDepth);
    DeclareGpuSkinningReads(pass, gpuSkinningBuffers);
};
```

Change it to drop the clear values (switch from `CLEAR` to `LOAD`, since `"ClearViewTarget"` above now owns the clear):

```cpp
desc.setup = [viewTarget, gpuSkinningBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
    // editor-core-separation-20 campaign, PHASE1 - no clear value here
    // anymore: "ClearViewTarget" (registered above, BeforeEverything, deny-
    // listed) now owns the ONE guaranteed clear of this exact resource,
    // every frame, regardless of whether THIS pass is itself enabled. LOAD
    // is therefore correct and intentional here, mirroring
    // "DrawSkyBackground"'s own pre-existing identical choice against the
    // SAME resource, just below.
    pass.WriteColorAttachment(viewTarget);
    pass.WriteDepthStencilAttachment(viewTarget);
    DeclareGpuSkinningReads(pass, gpuSkinningBuffers);
};
```

Do **not** touch `"DrawSkyBackground"`'s own `setup` lambda — it already has no clear value, and that is now correct for exactly the same reason.

### 3.4 — Deny-list `"ClearViewTarget"`

Open `RenderPassToggleRegistry.cpp`. Find:

```cpp
bool RenderPassToggleRegistry::IsDenyListed(const std::string& name) noexcept
{
    return name == "Present";
}
```

Change to:

```cpp
bool RenderPassToggleRegistry::IsDenyListed(const std::string& name) noexcept
{
    // editor-core-separation-20 campaign, PHASE1 - "ClearViewTarget" added:
    // the ONE guaranteed clear of the Game/Scene View target every frame -
    // disabling it would defeat the entire fix this pass exists for (see
    // task_manager/editor-core-separation-20/PHASE0_MASTER_STRATEGY.md's
    // Root Cause #1) with no in-process recovery, exactly like "Present".
    return name == "Present" || name == "ClearViewTarget";
}
```

Also update `RenderPassToggleRegistry.h`'s `IsDenyListed()` doc comment, which currently says *"currently: `"Present"` only"* — correct it to say `"Present"` and `"ClearViewTarget"`.

### 3.5 — Sanity-check: does anything read `viewTarget` BEFORE `RenderOpaque` that would now see uninitialized-but-un-cleared content, and does `"ClearViewTarget"` itself actually survive compiler culling?

Check `"GpuDrivenBatches"` (the provider registered between `RenderOpaque` and `DrawSkyBackground`) — confirm its own `WriteColorAttachment(viewTarget)`/`WriteDepthStencilAttachment(viewTarget)` calls (no clear value, already `LOAD`) are unaffected — they already assumed `RenderOpaque` cleared first; now `ClearViewTarget` does, transparently, no change needed there.

Check nothing else in `Core.cpp` writes `viewTarget` with `RenderPassEvent::BeforeEverything` or an event ordered before `Opaques` other than the new pass itself — a quick `search_in_dir` for `RenderPassEvent::BeforeEverything` across `src/` should show only your new addition (plus any pre-existing zero uses, confirm the enum value itself was previously unused in production code — it's fine either way, `RenderPassEvent::BeforeEverything = 0` already exists in `RenderGraphTypes.h`, this phase is simply its first real production consumer).

**Culling sanity-check (this is the single most important check in this phase, per PHASE0's own "second, load-bearing subtlety" and this phase's own Step 2/3.2):** confirm your `frame.finalTextureOutputs.push_back(viewTarget);` line landed inside the `"ClearViewTarget"` provider's own lambda, executed unconditionally every time that provider runs (i.e. not gated behind any early-return you might have accidentally added). This is what section 3.7's step 4 below actually proves live, against the running engine — do not skip that step even though it feels like "just another smoke test"; it is the regression test for the exact failure mode this section describes.

### 3.6 — Build (incremental only — no full rebuild)

```
cmake --build build
```
Run from `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`. Fix any compile errors before proceeding. Do **not** run `ctest` yet (PHASE0 LDD-8) — that is PHASE3's job.

### 3.7 — Live verification via the running engine (use `run_app_background` + `gte_send_request`)

1. Launch the built `GreatTamanaEditor.exe` with `run_app_background` (check `build/` for the exact output path — likely `build/GreatTamanaEditor.exe` or a `bin`/`Debug` subfolder; use `browse_dir` to confirm before launching).
2. Wait a moment for the engine + its embedded `NetworkServer` to come up, then confirm liveness: `gte_send_request` with `arguments: "/get_logs"` (should return real log JSON, not a connection error).
3. **Baseline screenshot** (everything enabled, default state): `gte_send_request` with `arguments: "/get_game_view"` — confirm it looks like a normal rendered scene (unchanged from before this phase's change — this proves the refactor introduced no visual regression in the default, fully-enabled case).
4. **Repro test A (disable-everything) — THE test that proves both Root Cause #1 AND the culling hazard (PHASE0 section 2.2's "second subtlety", this phase's own Step 2/3.2) are actually fixed, not just the simpler half**:
   - `GET /render_graph/set_pass_enabled?name=RenderOpaque&enabled=false`
   - `GET /render_graph/set_pass_enabled?name=DrawSkyBackground&enabled=false`
   - `GET /render_graph/set_pass_enabled?name=RenderTransparent&enabled=false`
   - `GET /render_graph/set_pass_enabled?name=AtmosphereComposite&enabled=false` (this is the one existing pass that would otherwise be the ONLY reader of `viewTarget` — disabling it too is what actually exercises the culling hazard; without this step, `AtmosphereComposite` would still be reading `viewTarget` and would mask a broken/missing `frame.finalTextureOutputs.push_back(viewTarget)` line).
   - `GET /get_game_view` — **expected: a solid, flat `kGameClearColor` (dark blue-grey), NOT magenta.** This is the concrete proof Root Cause #1 is fixed.
   - `GET /render_graph` — find the `"ClearViewTarget"` entry in the JSON's pass list and confirm `"is_culled": false` (a culled pass still appears in this list per PHASE0 section 2.5, just with `"is_culled": true` and zeroed stats — so PRESENCE alone is not proof; the field's actual value is). **If this reads `true`, the fix is incomplete** — go back to Step 3.2 and confirm the `frame.finalTextureOutputs.push_back(viewTarget);` line is really there and really runs unconditionally; do not proceed to PHASE2 with this unresolved.
5. **Repro test B (atmosphere-only, the user's own explicit acceptance test)**:
   - Re-enable `DrawSkyBackground` and `AtmosphereComposite`: `GET /render_graph/set_pass_enabled?name=DrawSkyBackground&enabled=true`, `GET /render_graph/set_pass_enabled?name=AtmosphereComposite&enabled=true`.
   - Keep `RenderOpaque`/`RenderTransparent` disabled.
   - `GET /get_game_view` — **expected: a real, visible rendered sky/atmosphere gradient**, not a flat color, not magenta. (Note: at this point in the campaign, this may or may not fully work yet if PHASE2's toggle-consultation fix hasn't landed — but the Atmosphere LUT passes were NEVER actually disabled by the earlier "disable everything" step in test A anyway, per Root Cause #2, so their outputs should already be valid this whole time; this test should already pass after PHASE1 alone. If it does not, capture the exact `GET /render_graph` JSON and `GET /get_logs` output as evidence and flag it via `ask_questions` before assuming PHASE2 will fix it — do not silently defer a real PHASE1 gap into PHASE2.)
6. **Restore state**: re-enable `RenderOpaque`/`RenderTransparent` (`enabled=true`) so the engine is back to its normal, fully-rendering baseline before you stop it.
7. Check `GET /get_logs?level=warning` and `?level=error` for anything new/unexpected introduced by this change (e.g. a validation-layer complaint about attachment load-op ordering). If anything new and relevant appears, diagnose and fix it before moving on — do not leave a new warning/error unexplained.
8. `stop_app_background` the engine process when done (use the PID `run_app_background` returned).

### 3.8 — Report

Write a short `PHASE1_COMPLETION_REPORT.md` in this same folder (`task_manager/editor-core-separation-20/`) describing: the exact diff made, the compile result, and the live verification results (screenshots' visual description, not raw image bytes — describe what `/get_game_view` showed for each of the steps in 3.7, and explicitly state the `"is_culled"` value observed for `"ClearViewTarget"` in step 4). Commit everything (`git_add` + `git_commit`) with a clear message, e.g. `"editor-core-separation-20 PHASE1: guaranteed ClearViewTarget pass, decouple view-target clear from RenderOpaque, mark view target as a kept render-graph root"`.
