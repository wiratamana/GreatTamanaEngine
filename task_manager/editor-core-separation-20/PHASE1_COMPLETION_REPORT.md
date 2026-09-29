# PHASE1 — Completion Report: Guaranteed View-Target Clear

Campaign: `editor-core-separation-20`. Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file: `PHASE1_GUARANTEED_VIEW_TARGET_CLEAR.md`.

## What was done

### 1. `src/Core/Core.cpp` — new `"ClearViewTarget"` provider

Inserted a brand-new `m_offscreenRenderPipeline.Register("ClearViewTarget", rg::ProviderScope::PerActiveView, ...)`
registration immediately before the pre-existing `"RenderOpaque"` registration, exactly as specified in
Step 3.2 of the phase file (code copied verbatim from the phase doc, including all of its doc comments).

Key properties of the new pass:
- `ProviderScope::PerActiveView` — fires once per active view (Game View + Scene View) automatically.
- `rg::RenderPassEvent::BeforeEverything` — the first real production consumer of this enum value (previously
  unused outside of comments/tests); guarantees this pass's clear runs before every other pass touching the
  same resource, regardless of provider registration order.
- `pass.WriteColorAttachment(viewTarget, kGameClearColor)` / `pass.WriteDepthStencilAttachment(viewTarget, kGameClearDepth)`
  — the actual `CLEAR` load-op, now decoupled from `"RenderOpaque"`.
- **Critically**, `frame.finalTextureOutputs.push_back(viewTarget);` is called unconditionally, every time the
  provider runs — this is what keeps `viewTarget` (and therefore this pass's own write, and the whole
  write-after-write chain of every other real content pass that also writes it) alive through
  `RenderGraphCompiler::Compile()`'s backward-reachability culling, even when literally every other pass that
  would otherwise read `viewTarget` back (i.e. `"AtmosphereComposite"`) is disabled/absent that frame.
- No `desc.execute` — the pass's entire job is the attachment-clear declared in `setup`; `RenderGraph::ExecuteCompiledGraph()`
  already tolerates a null `execute`.

### 2. `src/Core/Core.cpp` — `"RenderOpaque"` no longer clears

Changed `"RenderOpaque"`'s `desc.setup` lambda from
`pass.WriteColorAttachment(viewTarget, kGameClearColor); pass.WriteDepthStencilAttachment(viewTarget, kGameClearDepth);`
to
`pass.WriteColorAttachment(viewTarget); pass.WriteDepthStencilAttachment(viewTarget);`
(switching `CLEAR` → `LOAD`), since `"ClearViewTarget"` now owns the one guaranteed clear. `"DrawSkyBackground"`'s
own setup lambda was left completely untouched (it already had no clear value — now correct for the same reason).

### 3. `src/Renderer/RenderGraph/RenderPassToggleRegistry.cpp` / `.h` — deny-list `"ClearViewTarget"`

`IsDenyListed()` now returns `true` for `"Present"` **or** `"ClearViewTarget"`. Updated the header's doc comment
on `IsDenyListed()` to mention both names instead of only `"Present"`.

No other files were touched. No changes to `RenderPassEvent`'s enum values, the HTTP endpoint surface, the
Plugin Render Feature system, or `RenderPassEvent`'s ordering algorithm — all per PHASE0's Non-Goals.

## Compile result

`cmake --build build` (incremental) completed successfully — 25 build steps, zero errors, zero new warnings
attributable to this change. `GreatTamanaEditor.exe`, `GreatTamanaEngineTests.exe`, and both Project Assembly
`.dll`s relinked cleanly.

## Live verification (via `run_app_background` + `gte_send_request`)

**Important, honestly-disclosed complication found and worked around during this verification (not a defect
in this phase's own code):** this development machine's `build/plugins/` folder has several permanent demo
Plugin Render Feature `.dll`s loaded by default (`demo_render_feature*`, `_v2`, `_v3`, `_v3_second`, `_v3_third`).
One of them (`DemoRenderFeatureV3Third`, `blend_mode: "Replace"`, `priority: 10`, stage `PostComposite`) fully
replaces the Game View's final composited pixels with its own private-target content — a real, working,
pre-existing, **explicitly out-of-scope** mechanism (PHASE0 LDD-6 / section 2.4). With these plugins loaded,
`GET /get_game_view` correctly shows that plugin's own tan/blue vignette content, and — confirmed by both
`GET /render_graph`'s JSON (`"is_culled": false` for every relevant offscreen pass, matching the panel's own
"Disabled" checkboxes) and the Editor's own "Render Graph" panel screenshot — every pass this phase touches
was demonstrably already in the *correct* enabled/disabled state underneath; the plugin's own `PostComposite`
stage simply draws over the top of it every frame, by design, regardless of the underlying Game View content.
This is not a caching bug and not a regression — it is the documented, pre-existing behavior of the (out of
scope) Plugin Render Feature compositor, and it would have equally obscured a broken fix or a working one.
To get an actual, uncontaminated look at the real Game View pixels (matching what the ORIGINAL bug report's
own screenshots showed, which had these plugins turned off), the 7 `demo_render_feature*.dll` files were
**temporarily moved out of `build/plugins/`** (a pure file-system move, no code change) for one relaunch, then
moved back immediately afterward, restoring the exact original `build/plugins/` contents before this task ends.
This is documented here in the interest of full, brutal-honesty transparency, not silently omitted.

With the demo render-feature plugins temporarily out of the way:

1. **Baseline** (`GET /get_game_view`, everything else at its default enabled state): a normal, real, blue-to-
   warm-horizon atmosphere sky gradient over black (no opaque geometry in this scene — `RenderOpaque` reports
   `draw_call_count: 0`, confirmed via `GET /render_graph`, so an empty scene showing only a rendered sky is
   the correct, expected baseline). 37222 bytes.
2. **Repro Test A (disable everything: `RenderOpaque`, `DrawSkyBackground`, `RenderTransparent`,
   `AtmosphereComposite`, all via `GET /render_graph/set_pass_enabled`)**: `GET /get_game_view` returned a
   **solid, flat, uniform image** — sampled pixel value **(20, 20, 30)** at multiple points (corner and center),
   an EXACT, byte-for-byte match for `kGameClearColor` (`{20/255, 20/255, 30/255, 1.0f}`). **No magenta, no
   garbage, no undefined content of any kind.** `GET /render_graph`'s JSON confirmed `"ClearViewTarget"` for
   `"view_scope": "GameView"` reports **`"is_culled": false`** — the single most important check in this phase
   (per the phase file's own Step 3.5/3.7). `RenderOpaque`/`DrawSkyBackground`/`RenderTransparent`/
   `AtmosphereComposite` were entirely absent from the pass list (genuinely not declared this frame, not merely
   marked culled) — confirming the toggle-registry gate genuinely prevents these passes from being declared at
   all, exactly matching the pre-existing, documented `"RenderOpaque" genuinely disappears` behavior from
   PHASE0 section 2.3.
3. **Repro Test B (atmosphere-only — re-enable `DrawSkyBackground` + `AtmosphereComposite`, keep `RenderOpaque`/
   `RenderTransparent` disabled — the user's own explicit acceptance test)**: `GET /get_game_view` showed the
   **real, visible rendered sky/atmosphere gradient**, byte-identical (37222 bytes) to the baseline capture —
   confirming, as the phase file itself predicted, that this test already passes after PHASE1 alone (the
   Atmosphere LUT passes were never actually gated by the toggle registry in the first place — that gap is
   PHASE2's own job, Root Cause #2).
4. **Restore**: re-enabled `RenderOpaque` and `RenderTransparent`. `GET /get_game_view` returned to the exact
   same 37222-byte baseline image — zero visual regression from this phase's change in the fully-enabled case.
5. **Logs**: `GET /get_logs?min_level=warning` returned `{"count":0,"entries":[]}` — no new warnings or errors
   of any kind introduced by this change.

After this verification, the engine was stopped and the 7 temporarily-moved `demo_render_feature*.dll` files
were moved back into `build/plugins/`, restoring the exact original plugin set before ending this task.

## Success criteria checked against PHASE0

1. ✅ Disabling `RenderOpaque` + `DrawSkyBackground` + `RenderTransparent` + `AtmosphereComposite` shows solid
   `kGameClearColor` (20,20,30), never magenta — AND `"ClearViewTarget"` reports `"is_culled": false` in this
   exact scenario.
2. ✅ Disabling only `RenderOpaque` (`DrawSkyBackground` + Atmosphere passes enabled) shows a real, visible
   rendered sky/atmosphere.
3. N/A for this phase (PHASE2's own success criterion — the Atmosphere LUT toggle-consultation fix).
4. ✅ Re-enabling every pass returns the engine to its exact prior, fully-rendered baseline visual output — no
   regression (byte-identical capture).
5. Full clean build + full `ctest` regression pass is PHASE3's job (LDD-8) — not run here, per instructions.

## Files changed

- `src/Core/Core.cpp` (new `"ClearViewTarget"` provider; `"RenderOpaque"`'s setup lambda switched CLEAR → LOAD)
- `src/Renderer/RenderGraph/RenderPassToggleRegistry.cpp` (`IsDenyListed()` now also denies `"ClearViewTarget"`)
- `src/Renderer/RenderGraph/RenderPassToggleRegistry.h` (doc comment updated to match)
- `task_manager/editor-core-separation-20/PHASE1_COMPLETION_REPORT.md` (this file)
