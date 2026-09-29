# PHASE0 — MASTER STRATEGY: Fixing the Pink/Magenta Screen Bug in the Render Graph

Campaign folder: `task_manager/editor-core-separation-20/`
Branch: `feature/editor-core-separation`
Orchestrator role: this file is the PARENT of every other file in this folder. Read this file FIRST, always, before opening any `PHASEn_*.md` file.

---

## Step 1: The Goal (Where are we going?)

1. Give the engine a **guaranteed, defined image** in the Game View / Scene View render targets at all times — never undefined GPU memory garbage — regardless of which built-in render passes a developer has toggled on/off via the Editor's "Render Graph" panel or the `GET /render_graph/set_pass_enabled` HTTP endpoint.
2. Make the per-pass "Enabled" checkbox for the Atmosphere LUT/Composite passes **actually do what it says** — today it is a lie for those specific passes (see Step 2 below).
3. Satisfy the user's own explicit acceptance test: **"if atmosphere only enabled, the screen must render the atmosphere"** — i.e. disabling `RenderOpaque`/`RenderTransparent` while keeping the sky/atmosphere passes enabled must show a real, visible sky, not a blank/garbage screen.
4. Do all of this with real, compiled, tested C++ code changes — no "investigation-only" phases, no phases whose only content is "we will not write code yet."

## Step 2: The Situation / The Problem (Where are we now?)

### 2.1 — What the user observed (from their own screenshots)

- The "Render Graph" panel already has a working per-pass "Enabled" checkbox column (`editor-core-separation-8` campaign) and a "Disabled Built-In Passes" section listing every pass currently turned off.
- The user disabled essentially every built-in pass (`RenderOpaque`, `DrawSkyBackground`, `RenderTransparent`, the 5 Atmosphere LUT/composite passes) — **both the Game View and Scene View panels went solid, flat magenta/pink** (RGB `(255, 0, 255)`).
- The user then tried enabling **only** the atmosphere-related passes (keeping `RenderOpaque` disabled) expecting to see the sky/atmosphere render on its own — **still solid pink.**
- The user's own intuition ("resembles a shader compile error") is a reasonable Unity-flavored guess, but this is a raw Vulkan engine — there is no engine-wide "pink error shader" fallback anywhere in this codebase (confirmed by search: no `FF00FF`/`1.0f, 0.0f, 1.0f` hex/float pattern exists anywhere in engine rendering code except one **unrelated, 100% intentional** case — see 2.4 below). The real cause is structural, not a shader compile failure.

### 2.2 — Root Cause #1 (PRIMARY — explains BOTH repro screenshots)

**`DrawSkyBackground` has an undocumented-to-the-toggle-system hard dependency on `RenderOpaque` having already run, THIS SAME FRAME, to clear the view target.**

Concretely, in `src/Core/Core.cpp`'s `RegisterOffscreenRenderPipelineProviders()`:

- The `"RenderOpaque"` provider (~line 551) declares its pass with
  `pass.WriteColorAttachment(viewTarget, kGameClearColor); pass.WriteDepthStencilAttachment(viewTarget, kGameClearDepth);`
  — i.e. it is the **pass that clears** the Game/Scene View render target's color AND depth every frame (`VK_ATTACHMENT_LOAD_OP_CLEAR`).
- The `"DrawSkyBackground"` provider (~line 733) declares its pass with
  `pass.WriteColorAttachment(viewTarget); pass.WriteDepthStencilAttachment(viewTarget);` — **no clear value at all** (`VK_ATTACHMENT_LOAD_OP_LOAD`), by design: it relies on an `EQUAL` depth-test against whatever `RenderOpaque` already wrote, so it only paints the background pixels `RenderOpaque` left empty (the classic "draw sky last, `depth == far-plane`" optimization). This is explicitly documented in `src/Application/RenderPasses.h`'s own doc comment on `AddDrawSkyBackgroundPass()` and confirmed in `AGENTS.md`'s "Render Pass System" section: *"Sky Background relies on an `EQUAL` depth-test against the depth buffer Opaque just wrote."*
- **Both passes are independently toggleable** via `RenderPassToggleRegistry` (through `RenderPipeline::DeclareOnePhase()`'s per-name check, `RenderPipeline.h` line ~588) — nothing anywhere stops a caller from disabling `RenderOpaque` while leaving `DrawSkyBackground` enabled.
- When `RenderOpaque` is disabled: the view target's color+depth attachment is **never cleared this frame** (Vulkan `LOAD_OP_LOAD` on a resource nothing wrote to yet this frame == whatever bytes were already physically in that image — on first ever use, or after the driver reclaims/re-tags memory, that is **undefined content**, which this specific GPU/driver combination happens to display as a flat, saturated magenta). `DrawSkyBackground`'s `EQUAL`-depth-test fragment shader then compares against garbage depth, which essentially never matches its own freshly-computed depth value → **it draws nothing** → the screen keeps showing that same undefined magenta.
- This exactly explains **both** of the user's repro cases:
  - "Disable everything" → `RenderOpaque` disabled → view target never cleared → magenta.
  - "Enable only atmosphere" (i.e. re-enable `DrawSkyBackground` + the LUT passes, `RenderOpaque` still disabled) → view target STILL never cleared (nothing else clears it) → `DrawSkyBackground`'s `EQUAL` depth test still fails on garbage depth → **still magenta**, even though the atmosphere math itself is completely correct and unaffected.

**This is a genuine, confirmed, structural gap**: the render graph has no notion of "this view target is guaranteed to be cleared exactly once, every frame, no matter which optional passes are toggled." Clearing is currently a side effect bolted onto one specific, independently-disable-able content pass (`RenderOpaque`), which is a footgun.

**A second, load-bearing subtlety the fix itself must not skip.** `RenderGraphCompiler::Compile()` (`src/Renderer/RenderGraph/RenderGraphCompiler.h`'s own doc comment, "Step 2: backward reachability from finalOutputs") keeps a declared pass alive ONLY if its own write is listed in that frame's `finalOutputs`/`finalVolumeTextureOutputs` root set, or if some OTHER kept pass reads (directly or transitively, through a real RAW/WAW dependency edge) whatever it wrote — an imported/externally-owned resource (like the Game/Scene View's own persistent render target) gets **no automatic exemption** from this backward-reachability culling merely because it is externally visible outside the graph. Today, nothing in `Core.cpp` ever adds the Game/Scene View's own raw `TextureHandle` (`viewTarget`, the value `RenderPassViewData::colorTarget` holds) to `frame.finalTextureOutputs` directly — only downstream, DERIVED handles (the Atmosphere LUT outputs, the composited output) are ever added as roots. That means: whenever every pass that would otherwise read `viewTarget` back this same frame (today, in practice, only `"AtmosphereComposite"`) is itself disabled/absent, **nothing keeps alive whichever pass most recently wrote `viewTarget`** — including a brand-new, permanently-enabled `"ClearViewTarget"` pass. The fix therefore must not just ADD a guaranteed-clear pass; that pass's own registration must ALSO mark `viewTarget` itself as a kept root for that frame, or it would be silently culled in exactly the "disable everything" scenario this whole campaign exists to fix (its `execute`/attachment-clear would simply never run, and the view target would stay exactly as undefined as it is today). See `PHASE1_GUARANTEED_VIEW_TARGET_CLEAR.md`'s own Step 3.2 for where this is done, and its own Locked Design Decision reference (LDD-10 below) for the rule this must never regress.

Fix: see `PHASE1_GUARANTEED_VIEW_TARGET_CLEAR.md`.

### 2.3 — Root Cause #2 (SECONDARY — a real, confirmed, but independent bug found during this investigation)

**Five of the Atmosphere render-graph passes never actually consult the toggle registry at all — their "Enabled" checkbox is completely inert.**

- `RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled()` (the ONLY function that ever makes a pass's disabled state take effect) is called from exactly THREE places in the whole codebase:
  1. `RenderPipeline::DeclareOnePhase()` (`RenderPipeline.h` line 588) — the generic, deferred `RenderPassDesc` flush loop. This is what makes `RenderOpaque`/`DrawSkyBackground`/`RenderTransparent`/`GpuDrivenBatches`/`PluginRenderFeatures` genuinely toggleable.
  2. `Core.cpp` line 476, gating the whole `"GpuSkinning"` provider body directly.
  3. `Core.cpp` line 843, gating the whole `"AtmosphereComposite"` provider body directly.
- The five real Atmosphere passes — `AtmosphereTransmittanceLutPass`, `AtmosphereMultiScatteringLutPass`, `AtmosphereSkyViewLutPass` (declared once per view), `AtmosphereAerialPerspectiveVolumePass` (once per view), `AtmosphereAerialPerspectiveCompositePass` (once per view) — are all declared via **direct, immediate `builder.AddRenderPass()` calls inside `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`** (confirmed call sites at lines 247, 345, 485, 627, 794). These calls happen because the `"AtmosphereSharedLut"`/`"AtmosphereViewLut"` providers in `Core.cpp` reach `frame.builder` directly (a deliberate, documented `RenderPipeline.h` mechanism for passes with real cross-call data dependencies — see that file's own `ProviderTiming` doc comment) — but **nobody ever added the toggle-registry consult these direct calls need**, unlike the two `Core.cpp`-level exceptions above.
- Net effect: toggling any of these 5 pass names off via the "Render Graph" panel or `GET /render_graph/set_pass_enabled` **updates the registry's bookkeeping only** — the pass still runs, every frame, unconditionally. The checkbox/HTTP call silently lies.
- Independent, mechanical proof (to be run live in `PHASE1`/`PHASE2`): toggle `AtmosphereTransmittanceLutPass` off via `GET /render_graph/set_pass_enabled?name=AtmosphereTransmittanceLutPass&enabled=false`, then confirm via `GET /render_graph` that its GPU-time/draw-count entry is **still present and non-zero** — unlike `RenderOpaque`, which genuinely disappears from the pass list when disabled the same way.

Fix: see `PHASE2_HONOR_ATMOSPHERE_PASS_TOGGLES.md`.

### 2.4 — A red herring, ruled out, worth recording so nobody re-investigates it

`plugins/demo_render_feature/RenderFeaturePlugin.cpp` and `plugins/demo_render_feature_second/RenderFeaturePlugin.cpp` both call
`builder.AddFullscreenClearPass("DemoRenderFeaturePlugin_Clear", 1.0f, 0.0f, 1.0f, 1.0f)` — **that IS a literal, deliberate magenta clear** (`(1, 0, 1, 1)`), used on purpose as an "unmistakable proof of life" demo color for the Plugin Render Feature system. It is real, but it is **not** the cause of the user's bug: these demo features are gated by the separate, independently-working `RenderFeatureCompositor::SetFeatureEnabled()` mechanism, and the user's own first screenshot shows every one of them correctly `(DISABLED)`. Do not chase this file during PHASE1/PHASE2 — it is unrelated. It is only mentioned here so a future reader who greps for `1.0f, 0.0f, 1.0f` and finds it does not waste time re-investigating a already-ruled-out lead.

### 2.5 — The HTTP live-debugging endpoint the user asked about

The user's instructions said: *"if http request dont have end point to switch on-off render pass, implement it first."* **It already exists and is fully wired**, shipped by the `editor-core-separation-8` campaign:
- `GET /render_graph` — full JSON snapshot (passes, resources, GPU-driven batches, plugin render features). Each pass entry includes an `"is_culled"` field (`src/Renderer/RenderGraph/RenderGraphMetadata.cpp`'s `to_json()`) — a CULLED pass still appears in this list (so a checkbox/toggle state is always visible), but with `"is_culled": true` and zeroed-out draw/GPU-time stats. This is the concrete, load-bearing way both PHASE1 and PHASE2 confirm their own fixes actually took effect at the render-graph-compiler level, not just at the toggle-registry bookkeeping level.
- `GET /render_graph/passes` — read-only list of every known built-in pass's toggle state (`name`, `enabled`, `everDeclaredThisSession`).
- `GET /render_graph/set_pass_enabled?name=<X>&enabled=<true|false>` — the mutation endpoint.
- `GET /render_graph/set_feature_enabled`, `/set_feature_priority`, `/set_blur_enabled`, `/set_gbuffer_enabled` — sibling controls.
- `GET /get_game_view` / `GET /get_swapchain` — live rendered-frame image capture (via `gte_send_request`).
- `GET /get_logs` / `POST /clear_logs` — engine log retrieval (never use raw C/C++ logging — always `GTE_LOG_INFO/WARNING/ERROR`, retrieved via this endpoint).

No new endpoint needs to be implemented. Every phase below uses these existing endpoints for live verification.

## Step 3: The Plan (super-detailed, phase by phase)

| Phase | File | What it does |
|---|---|---|
| 0 | `PHASE0_MASTER_STRATEGY.md` | This file. Orchestrator + root cause record. |
| 1 | `PHASE1_GUARANTEED_VIEW_TARGET_CLEAR.md` | **The primary fix.** Adds a new, deny-listed `"ClearViewTarget"` render pass that unconditionally clears the Game/Scene View target every frame, decoupling the clear from `RenderOpaque`, and marks that view's own target handle as a kept render-graph root so the new pass (and whichever real content passes are enabled) always survives compiler culling. Fixes both of the user's repro screenshots. |
| 2 | `PHASE2_HONOR_ATMOSPHERE_PASS_TOGGLES.md` | **The secondary fix.** Threads `RenderPassToggleRegistry` through the 5 direct-declare Atmosphere passes so their checkbox/HTTP toggle genuinely takes effect, with correct cascading skip-logic through the LUT dependency chain. |
| 3 | `PHASE3_REGRESSION_TESTS_AND_FULL_VERIFICATION.md` | New/updated Tier-1 tests, full clean build, full `ctest` regression run, final live HTTP re-verification of all three original screenshot scenarios, campaign completion report. |

### Locked Design Decisions (do not relitigate these mid-implementation — if you disagree, use `ask_questions` to ask the user BEFORE changing course)

1. **LDD-1**: The new clearing pass is named `"ClearViewTarget"` (one literal name shared by both Game View and Scene View, exactly mirroring how `"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"` already share one literal name across both views — `ViewScope`/`RenderViewId` is what distinguishes the two, never the name).
2. **LDD-2**: `"ClearViewTarget"` is **permanently deny-listed** (`RenderPassToggleRegistry::IsDenyListed()`) — it can never be switched off via the panel or HTTP, exactly like `"Present"` already is. A view that is never cleared has no defined fallback content — this guarantee must be unconditional, not opt-out.
3. **LDD-3**: `RenderOpaque` stops owning the clear (switches from `CLEAR` to `LOAD` on both its color and depth attachment writes) — it no longer needs `kGameClearColor`/`kGameClearDepth` in its own `setup` lambda.
4. **LDD-4**: The 5 Atmosphere passes' fix (PHASE2) uses **cascading, `IsValid()`-based skip logic** (every `TextureHandle`/`VolumeTextureHandle` already has a real `IsValid()` sentinel, `RenderGraphTypes.h`) — never `std::optional<TextureHandle>` wrapping, since the codebase already has a real, working "return an invalid handle as the disabled/degrade-gracefully signal" precedent at `AtmosphereLutRenderer::AddAerialPerspectiveVolumeDebugSlicePass()` (`AtmosphereLutRenderer.cpp` line ~958-963).
5. **LDD-5**: `AtmosphereLutRenderer::AddAerialPerspectiveVolumeDebugSlicePass()` itself (the 6th direct `AddRenderPass()` call site, Frame-Debugger-only volume-slice-preview tooling) is **explicitly out of scope** for PHASE2 — it is a debug-only convenience pass, not part of the main render path the user's bug report is about. Do not touch it unless `ask_questions` confirms the user wants it too.
6. **LDD-6**: The Plugin Render Feature system (`RenderFeatureCompositor`, `demo_render_feature*` plugins, the `_v2`/`_v3` plugin ABI) is **explicitly out of scope** for this whole campaign — see 2.4 above. Its own toggle-registry entries (`DemoRenderFeaturePlugin_Clear`/`DemoRenderFeatureSecondPlugin_Clear`) being similarly inert is a real, but lower-priority, pre-existing cosmetic inconsistency for a possible future campaign — do not fix it here, do not expand scope onto the plugin ABI.
7. **LDD-7**: `src/Application/RenderPasses.cpp`'s free functions `AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()` are **dead code** — confirmed via `search_in_dir` that nothing calls them; the real, live pass declarations are the inline provider lambdas inside `Core.cpp`'s `RegisterOffscreenRenderPipelineProviders()`. PHASE1 only touches `Core.cpp`. Do not waste time "fixing" the dead free functions to match — if you want them deleted for hygiene, ask the user first via `ask_questions`, it is out of scope by default.
8. **LDD-8**: No full build, no full `ctest` regression run until PHASE3. PHASE1/PHASE2 use an **incremental** build (`cmake --build build`) plus a live, HTTP-driven, `gte_send_request`-based smoke test to confirm each phase's own fix before moving on.
9. **LDD-9**: Never use raw C/C++ logging (`printf`/`std::cout`/`fprintf` for new code) — use `GTE_LOG_INFO`/`WARNING`/`ERROR` and retrieve via `GET /get_logs` for any new diagnostic output these phases add.
10. **LDD-10**: `"ClearViewTarget"`'s own provider registration must append that view's `colorTarget` handle (`viewTarget`) to `frame.finalTextureOutputs` itself, every time it runs (i.e. every frame, for every active view, since the pass is permanently deny-listed/always enabled) — this is what guarantees the pass, and the chain of real content passes that write the same resource before/after it, survive `RenderGraphCompiler::Compile()`'s backward-reachability culling even when every OTHER pass that would otherwise read `viewTarget` back (e.g. `AtmosphereComposite`) is disabled/absent that frame. Do not rely on some other, already-existing provider to do this instead — none currently does, which is exactly why `viewTarget` was previously only ever kept alive as a side effect of `AtmosphereComposite` (or another downstream reader) happening to run. This exact "multiple writers to one resource that is itself listed in `finalOutputs`" shape is already a tested, proven-safe pattern in this codebase (see `RenderGraphCompilerTests.cpp`'s `MultipleWritersToSameResourcePreserveWriteAfterWriteOrder`) — this is not a novel mechanism, only a novel call site for it.

### Non-Goals (explicitly out of scope for this whole campaign)

- Rewriting the Atmosphere LUT chain's data-dependency shape (Transmittance → MultiScattering → SkyView/AerialPerspectiveVolume → Composite) — PHASE2 only ADDS a toggle-consult + cascading-skip guard on top of the existing, unchanged chain.
- Any change to `RenderFeatureCompositor`/plugin ABI (LDD-6).
- Any change to the HTTP endpoint surface — it already fully exists (2.5).
- Any change to `RenderPassEvent`'s enum values or `RenderGraphCompiler`'s ordering algorithm — PHASE1 only uses the pre-existing `RenderPassEvent::BeforeEverything` value.

### Success Criteria (how PHASE3 proves this campaign is actually done)

1. Disabling `RenderOpaque` + `DrawSkyBackground` + `RenderTransparent` (+ every Atmosphere pass) via `GET /render_graph/set_pass_enabled` shows a **solid `kGameClearColor` (dark blue-grey, `20/255, 20/255, 30/255`)** in `GET /get_game_view`, never magenta/garbage — AND `GET /render_graph` shows `"ClearViewTarget"` itself with `"is_culled": false` in this exact scenario (proving its own write genuinely survived the compiler's culling, per LDD-10, not merely that the panel/HTTP call reports it as "enabled").
2. Disabling ONLY `RenderOpaque` (leaving `DrawSkyBackground` + the 5 Atmosphere passes enabled) shows a **real, visible rendered sky/atmosphere** in `GET /get_game_view` — the user's own explicit acceptance test.
3. Disabling `AtmosphereTransmittanceLutPass` alone via `GET /render_graph/set_pass_enabled` makes it (and everything downstream of it this frame) **genuinely disappear** from `GET /render_graph`'s pass list / show zero draws — no longer a fake, inert checkbox.
4. Re-enabling every pass returns the engine to its exact prior, fully-rendered baseline visual output (no regression).
5. A full clean build + full `ctest` regression pass with zero new failures.
