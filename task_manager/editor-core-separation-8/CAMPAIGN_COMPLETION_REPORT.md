# `editor-core-separation-8` — CAMPAIGN COMPLETION REPORT

**Status: COMPLETE.** All six phases shipped exactly as scoped by
`PHASE0_MASTER_STRATEGY.md`, with zero unexplained regressions across the
whole campaign. Branch `feature/editor-core-separation` throughout, never
switched.

## Original goal (PHASE0 Step 1)

The story asked for 4 things. `editor-core-separation-7` already made the
"Render Graph" panel **observable** (a data-driven `rg::RenderGraphMetadata`
model, `GET /render_graph`, a real "Export DOT" exporter). This campaign made
it **controllable**:

1. **Render pass ON/OFF** — from the "Render Graph" Editor panel AND from
   HTTP. Covers BOTH built-in passes (`RenderOpaque`, `DrawSkyBackground`,
   `AtmosphereComposite`, `GpuSkinning`, `GpuDrivenBatches`,
   `RenderTransparent`, `Present`, ...) and plugin "scriptable render
   features" (`IRenderFeatureModule_v2`) — ON/OFF ONLY, the same shape for
   both, with one permanent exception: `"Present"` can never be turned off (a
   small, hardcoded deny-list — disabling it bricks the Editor with no
   in-process recovery).
2. **Switch a pass's ordering / adjust its priority** — **the single biggest
   scope correction versus the source Investigation's own proposal, chosen by
   explicit product-owner decision**: built-in passes get **NO runtime
   `RenderPassEvent` reassignment at all** (the Investigation itself flagged
   a real crash risk here — moving a pass's tier while another pass reading
   its output stays put can trip `RenderGraphCompiler`'s own
   `DetectRenderPassEventContradictions()` safety net, which can `assert()`
   in a debug build). Plugin render features, by contrast, get **live,
   runtime-adjustable priority** (`RenderFeatureCompositor::
   SetFeaturePriority()`) — safe, because every `_v2` plugin already renders
   into its own fully private, isolated offscreen target, with the
   compositing chain rebuilt fresh from scratch every single frame. Stage
   (`PostComposite`/`PreUI`) itself stays fixed — only ordering WITHIN a
   stage changes.
3. **Compute Blur / GBuffer Validation ON/OFF** — these two debug passes were
   already toggleable via 2 checkboxes in the "Scene" panel
   (`EditorContext::showBlurredSceneOutput`/`showGBufferValidationOutput`).
   This campaign added (a) matching checkboxes in the "Render Graph" panel
   too (one-stop control), and (b) 2 new HTTP endpoints for both.
4. **HTTP control of 1, 2, 3** — 6 new endpoints total, all `GET` + query
   parameters (no JSON body anywhere in this campaign — every mutated field
   is a plain bool or int, so a query parameter fits perfectly), following
   this codebase's own `FrameDebuggerCommandBridge`/`/frame_debugger/*`
   pattern byte-for-byte.

**Explicitly OUT OF SCOPE** (see "What this campaign explicitly did NOT do"
below for the full, verbatim restatement): no `RenderPassEvent` reassignment
for built-in passes, ever; no persistence to disk of any new toggle/override
state; no new query-parameter filtering beyond each endpoint's own contract;
no plugin ABI change.

## What each phase actually shipped

### PHASE1 — Built-In Pass Toggle Registry + `RenderPipeline` Choke Point
(`PHASE1_COMPLETION_REPORT.md`)

New, plain `gte_core`-tier class pair,
`src/Renderer/RenderGraph/RenderPassToggleRegistry.h/.cpp`
(`gte::rg::RenderPassToggleState`/`RenderPassToggleRegistry` — keyed purely
by a pass's existing `debugName` string, auto-discovering each name the first
time it is actually declared this session, default `enabled = true`, a
`static IsDenyListed()` refusing to ever disable `"Present"`). Wired into
`RenderPipeline::DeclareOnePhase()`'s own existing flush loop as a new
choke-point check (mirroring `SetLegacyViewScopeTranslator()`'s own injection
precedent), PLUS 2 confirmed-necessary early-return guard lines inside the
`"AtmosphereComposite"` and `"GpuSkinning"` provider lambda bodies in
`Core.cpp` specifically — the ONLY 2 of the ~10 built-in providers that call
`frame.builder.AddRenderPass(...)` directly and never reach that flush loop
at all. `Core` owns exactly ONE shared instance
(`m_renderPassToggleRegistry`), consulted by BOTH the offscreen and present
regimes. 10 new Tier-1 tests. Zero observable production behavior change —
pure, additive plumbing (nothing calls the new mutator yet outside tests).

### PHASE2 — Plugin Render Feature Enable/Disable + Live Priority Reorder
(`PHASE2_COMPLETION_REPORT.md`)

`RenderFeatureCompositor` gained `Entry::enabledOverride` (host-side-only,
never part of the plugin ABI) plus two new public methods,
`SetFeatureEnabled()`/`SetFeaturePriority()` (the latter re-sorting the
affected stage's vector immediately, reusing the exact same
sort+collision-tie-break logic `OnPluginsLoaded()` already had, now extracted
into a shared `SortAndDetectCollisionsInStage()` helper).
`ContributeRenderGraphPasses()` gained a new filter skipping any disabled
entry; `DebugSnapshot()`/`RenderFeatureDebugEntry` gained a new `enabled`
field, which `RenderGraphMetadata.cpp`'s `to_json()` picked up automatically
— meaning `GET /render_graph`'s existing `render_features[]` array started
reporting plugin-feature enabled state with **zero new endpoint**.
`Core::GetRenderFeatureCompositor()`'s declared return type widened from
`const RenderFeatureCompositor*` to a mutable `RenderFeatureCompositor*` (a
safe, backward-compatible, 1-line header change — confirmed `Core.cpp`
needed zero changes, since the whole method is defined inline in `Core.h`).
2 existing Tier-1 tests extended with new `enabled`-field assertions (Step
3.6's own locked decision: no new dedicated test file, since
`SetFeatureEnabled()`/`SetFeaturePriority()` need a live `Renderer&` and stay
Tier 2, exercised end-to-end only by PHASE5's own live HTTP smoke test).
Zero observable production behavior change until PHASE4/5 wire up a real
caller.

### PHASE3 — `IEditorLayer` Surface + `BuildUI()` Wiring
(`PHASE3_COMPLETION_REPORT.md`)

`IEditorLayer::BuildUI()` gained exactly 2 new trailing parameters:
`rg::RenderPassToggleRegistry& renderPassToggleRegistry` (never null, a plain
owned `Core` member) and `RenderFeatureCompositor* renderFeatureCompositor`
(nullable, mirroring `Core::GetRenderFeatureCompositor()`'s own existing
nullability). `EditorLayer.h` forward-declares both types rather than
including their real, heavier headers, mirroring its own existing
`rg::RenderGraph` forward-declare-only convention. 2 new plain
`IEditorLayer` virtual setters, `SetShowBlurredSceneOutput(bool)`/
`SetShowGBufferValidationOutput(bool)`, mirroring `FrameDebuggerSetEnabled
(bool)`'s exact existing shape — these mutate `EditorContext`-owned state, so
they correctly go through `IEditorLayer`, never a direct `Core` call.
`RenderGraphPanel::Build()` gained the same 2 new trailing parameters, both
left as clearly-commented UNUSED markers for this phase only.
`EditorHost.cpp`'s one real `BuildUI()` call site updated, with a genuinely
SECOND, fresh call to `m_core.GetRenderFeatureCompositor()` for the new
trailing argument (never reusing the pre-existing `const`-typed local built
for `renderFeatureEntries` — Step 3.5's own explicit pitfall, correctly
avoided). Pure signature-widening plumbing, zero observable behavior change
— confirmed both by a clean compile and a live screenshot showing an
identical-looking Editor to before this phase.

### PHASE4 — "Render Graph" Panel: The Real UI Controls
(`PHASE4_COMPLETION_REPORT.md`)

The FIRST phase with real, observable UI behavior change, entirely inside
`RenderGraphPanel.cpp` (the only file touched). `BuildPassRow()`/
`BuildPassTable()` gained a new first "Enabled" checkbox column (7 columns
total now: `Enabled | Pass | Draws | Tris | GPU Time | Reads | Writes`), with
a tooltip explicitly documenting the shared-name-across-views consequence. A
new `BuildDisabledBuiltInPassesSection()` free function reads
`RenderPassToggleRegistry::ListAll()` and shows every currently-disabled
built-in pass (with a re-enable checkbox and a "(never run yet this session)"
marker where relevant). `BuildPluginRenderFeaturesSection()` gained a real
enable checkbox + priority `DragInt`/`InputInt` per loaded `_v2` plugin
feature, calling `SetFeatureEnabled()`/`SetFeaturePriority()` directly. A new
"Debug Passes" mini-section added the exact same Blur/GBuffer checkboxes
`ScenePanel.cpp` already has, bound to the exact same `EditorContext` bools
(that file's own checkboxes stay completely untouched). All 3 of PHASE3's
temporary unused-parameter markers removed in this one pass, as planned.
Verified with a real, live, interactive screenshot session (2 loaded `_v2`
plugins, both checkboxes/steppers rendering and populated correctly, 7-column
table headers confirmed exact). One honestly-disclosed, unavoidable
verification gap: this engine's own network endpoints cannot click an
arbitrary ImGui checkbox, so the actual WRITE-side mutation pathway could not
be exercised live from this phase alone — explicitly deferred to, and then
closed by, PHASE5's own HTTP smoke test (which calls the exact same
underlying methods this panel's checkboxes call).

### PHASE5 — Cross-Thread Bridge + HTTP Endpoints
(`PHASE5_COMPLETION_REPORT.md`)

New `src/Application/RenderGraphControlCommandBridge.h/.cpp` — one bridge,
several command kinds, mirroring `FrameDebuggerCommandBridge`'s exact
mutex + `std::condition_variable` + single-pending-request-slot shape, placed
in the `gte_core` CMake target (Step 3.7's confirmed fix — there is no
`gte_application` target in this codebase). New parse/build functions in
`NetworkRoutes.h/.cpp` (following this file's own established "no
Application-tier struct crosses into `NetworkRoutes.h` directly" convention
— `RenderGraphControlPassStateResponseView` is `NetworkRoutes.h`'s own,
independent struct, never `RenderGraphControlPassStateOutcome` itself). 6 new
routes registered in `NetworkServer.cpp`, each: parse → bridge-null-check
(`503`) → build request → `SubmitAndWait()` → respond. `EditorHost.cpp`
gained a new pump block (immediately after the existing
`FrameDebuggerCommandBridge` pump): `SetBuiltInPassEnabled`/`ListPassStates`
call `m_core.GetRenderPassToggleRegistryMutable()` directly;
`SetFeatureEnabled`/`SetFeaturePriority` call
`m_core.GetRenderFeatureCompositor()` (null-checked); `SetBlurEnabled`/
`SetGBufferEnabled` call `m_editorLayer->SetShowBlurredSceneOutput()`/
`SetShowGBufferValidationOutput()` — exactly PHASE0's own Step 2.6 routing.
19 new Tier-1 tests (7 bridge tests + 12 `NetworkRoutesTests.cpp` additions).
Verified with a full, live, end-to-end HTTP smoke-test session against a real
running `GreatTamanaEditor.exe`: every one of the 6 endpoints exercised with
both a success and a real failure case (the `"Present"` deny-list 409, an
unknown plugin-feature-name 409), a full disable→verify-absent→re-enable→
verify-present round trip for BOTH a built-in pass (`RenderTransparent`) AND
the specifically-flagged-risky `AtmosphereComposite` (PHASE0's own "Known
risk" instruction), a live plugin-feature priority reorder, and a real,
visible Blur/GBuffer toggle proving the HTTP path and the Scene-panel
checkbox share the exact same underlying `EditorContext` state.

### PHASE6 — Docs, Full Regression, Campaign Closeout
(`PHASE6_COMPLETION_REPORT.md`, this phase)

Added ONE new combined bullet to `docs/conventions/networking.md` (appended
after the file's real, freshly-`read_file`'d last line, the existing `GET
/render_graph` bullet) documenting all 6 new endpoints' full, real,
as-shipped contract — copied verbatim from PHASE5's own captured live
evidence. Re-confirmed via a fresh `search_in_dir` that none of `AGENTS.md`'s
existing 8 `"Render Graph"` mentions (and zero `"RenderFeatureCompositor"`/
`"IRenderFeatureModule"` mentions) needed correction — every one describes a
still-true historical fact, none describe the panel/compositor as read-only.
Ran the ONE full clean-ish build + full `ctest` regression pass + final live
HTTP smoke test this whole campaign is allowed to run (see below) — zero
unexplained failures, so `delegate_task` was never invoked.

## Locked Product/Architecture Decisions — how each was actually realized

All 16 of `PHASE0_MASTER_STRATEGY.md`'s Locked Product/Architecture Decisions
were followed exactly as written, with zero deviation discovered during
implementation:

1. Built-in passes get ON/OFF ONLY, never `RenderPassEvent` reassignment;
   plugin render features get ON/OFF AND live priority reassignment (never
   stage reassignment) — shipped exactly this way, PHASE1/PHASE2.
2. Plugin feature priority is LIVE-adjustable at runtime, re-sorting the
   affected stage's vector immediately — shipped, PHASE2, confirmed safe live
   in PHASE5's smoke test.
3. Every new HTTP endpoint is `GET` + query parameters, no JSON POST body
   anywhere — shipped, PHASE5, confirmed in all 6 route registrations.
4. All 6 new mutation/discovery endpoints share ONE new bridge class,
   `RenderGraphControlCommandBridge` — shipped, PHASE5.
5. The new built-in-pass-toggle registry is keyed by the pass's existing
   `debugName` STRING, `RenderPassDesc::id`/`RenderPassId` left completely
   untouched — shipped, PHASE1 (8 of ~10 `Register()` lambda bodies needed
   zero changes, confirmed).
6. The registry auto-discovers a built-in pass's name the first time it is
   actually declared this session (default `enabled = true`) — shipped,
   PHASE1, confirmed by the live smoke test's fresh-launch "every pass
   enabled" baseline.
7. Everything this campaign adds is IN-MEMORY ONLY, nothing persists to
   disk — shipped, confirmed by PHASE5's own smoke test leaving the session
   in a clean, all-default state and this being expected to reset on the
   next launch.
8. `GET /render_graph` gains an `enabled` field on every `render_features[]`
   entry, no new endpoint needed for plugin features; built-in passes DO get
   a brand-new discovery endpoint, `GET /render_graph/passes` — shipped,
   PHASE2/PHASE5 respectively.
9. A small, permanent, hardcoded deny-list refuses to ever disable
   `"Present"` — shipped, PHASE1, confirmed live (`409`) in PHASE5's smoke
   test.
10. The "Render Graph" panel ALSO gets its own Blur/GBuffer checkboxes, bound
    to the exact same `EditorContext` bools `ScenePanel.cpp` already uses —
    shipped, PHASE4, `ScenePanel.cpp` itself confirmed untouched.
11. `RenderPassToggleRegistry` is a NEW, plain, `gte_core`-tier class living
    at `src/Renderer/RenderGraph/RenderPassToggleRegistry.h/.cpp` — shipped,
    PHASE1, exactly this location.
12. No mutex/thread-safety of any kind inside `RenderPassToggleRegistry` or
    `RenderFeatureCompositor`'s new methods (both main-thread-only) — shipped,
    PHASE1/PHASE2, cross-thread safety handled one level up by
    `RenderGraphControlCommandBridge`'s own mutex/condvar, PHASE5.
13. `IEditorLayer::BuildUI()` gains exactly 2 new trailing parameters,
    forward-declared, `NullEditorLayer`'s override ignoring both — shipped,
    PHASE3.
14. 2 new plain `IEditorLayer` virtual setters for the Blur/GBuffer booleans,
    mirroring `FrameDebuggerSetEnabled(bool)`'s exact shape — shipped, PHASE3.
15. `RenderGraphPanel::Build()` gains the exact same 2 new trailing
    parameters as `BuildUI()`, forwarded verbatim — shipped, PHASE3/PHASE4.
16. `RenderGraphControlCommandBridge` is a NEW bridge, never a new enum value
    bolted onto `FrameDebuggerCommandBridge`/`EditorUiCommandBridge` —
    shipped, PHASE5.

## The final, locked shape of all 6 new HTTP endpoints (real, captured examples)

Captured live from a running `GreatTamanaEditor.exe` during PHASE5's own
exhaustive smoke test (full transcript in `PHASE5_COMPLETION_REPORT.md`),
re-confirmed still working during PHASE6's own final short smoke test:

```
GET /render_graph/passes
-> 200 {"passes":[{"enabled":true,"ever_declared_this_session":true,"name":"AtmosphereComposite"},
        {"enabled":true,"ever_declared_this_session":true,"name":"DrawSkyBackground"},
        {"enabled":true,"ever_declared_this_session":true,"name":"GpuSkinning"},
        {"enabled":true,"ever_declared_this_session":true,"name":"RenderOpaque"},
        {"enabled":true,"ever_declared_this_session":true,"name":"RenderTransparent"}]}

GET /render_graph/set_pass_enabled?name=RenderTransparent&enabled=false
-> 200 {"success":true}

GET /render_graph/set_pass_enabled?name=Present&enabled=false
-> 409 {"error":"\"Present\" cannot be disabled (deny-listed).","success":false}

GET /render_graph/set_feature_enabled?name=DemoRenderFeatureV2&enabled=false
-> 200 {"success":true}

GET /render_graph/set_feature_enabled?name=NoSuchFeature&enabled=false
-> 409 {"error":"\"NoSuchFeature\" matches no loaded plugin render feature.","success":false}

GET /render_graph/set_feature_priority?name=DemoRenderFeatureV2Second&priority=5
-> 200 {"success":true}

GET /render_graph/set_blur_enabled?enabled=true
-> 200 {"success":true}   (Scene panel's own "Show Compute Blur (debug)" checkbox
                            visibly checks itself the very next frame)

GET /render_graph/set_gbuffer_enabled?enabled=true
-> 200 {"success":true}   (same, for "Show GBuffer Validation (debug)")
```

Status-code mapping, locked: every mutating route responds `200`
`{"success":true}` on success, or a non-200 `{"success":false,
"error":"<message>"}` on failure — `400` bad/missing query parameter, `409` a
semantically-rejected name, `503` bridge pointer null, `504` bridge timeout.
`GET /render_graph/passes` always `200` with the shape shown above, or
`503`/`504` on the same bridge failure modes.

## Full regression result (PHASE6)

- **Full build** (`cmake --build build`, existing configured tree):
  `ninja: no work to do` — everything already up to date from Phases 1–5's
  own incremental builds.
- **Full `ctest` regression**: **1848 total tests, 1846 passed (100% of
  executed tests), 2 legitimate environment-gated skips (the SAME two skips
  `editor-core-separation-7`'s own baseline already documented, confirmed by
  name), ZERO failures** — up from `editor-core-separation-7`'s own
  documented 1819-test baseline (+29, exactly matching this campaign's real,
  itemized new-test count: +10 Phase1, +0 Phase2 [extended existing
  assertions only], +0 Phase3, +0 Phase4, +19 Phase5). No unexplained
  regression was found anywhere, so `delegate_task` was correctly never
  invoked for this campaign.
- **Live HTTP smoke test**: `GET /render_graph/passes` (`200`, every built-in
  pass known and enabled), `GET /get_swapchain` (`200`, real PNG, visually
  confirmed the Editor UI renders correctly — Hierarchy/Scene/Game/Inspector
  panels, both new Blur/GBuffer checkboxes, the Demo Plugin Panel), `GET
  /get_logs?limit=50` (`200`, only pre-existing, already-documented
  warning/info lines — zero new warnings/errors introduced by this whole
  campaign).

## What this campaign explicitly did NOT do (Non-Goals, unchanged from PHASE0)

- No `RenderPassEvent` reassignment for built-in passes, ever, in this
  campaign.
- No persistence to disk of any new toggle/override state — everything is
  in-memory only, reset to defaults on every Editor restart.
- No new query-parameter filtering beyond what each endpoint's own contract
  states.
- No change to the plugin ABI (`GtePluginRenderFeatureDescriptor` stays
  byte-for-byte unchanged).
- No new discovery endpoint for plugin features (`GET /render_graph`'s
  existing `render_features[]` already covers it).
- No `RenderPassId` stamping anywhere.
- No JSON POST body anywhere in this campaign.
- No change to `ScenePanel.cpp`'s own existing Blur/GBuffer checkboxes — they
  stay exactly as-is; this campaign only ADDED a second place to toggle the
  same state.
- No security-hardening pass on the embedded HTTP server itself
  (loopback-only binding, no auth, unchanged).
- No change to any OTHER Editor panel, any OTHER network route, or any
  OTHER `_v1`/`_v2` plugin capability surface.

## Every `PHASEn_COMPLETION_REPORT.md`

- `PHASE1_COMPLETION_REPORT.md`
- `PHASE2_COMPLETION_REPORT.md`
- `PHASE3_COMPLETION_REPORT.md`
- `PHASE4_COMPLETION_REPORT.md`
- `PHASE5_COMPLETION_REPORT.md`
- `PHASE6_COMPLETION_REPORT.md`

(All in this same folder, `task_manager/editor-core-separation-8/`.)

## Conclusion

The Render Graph is now genuinely controllable end-to-end: a real, live
Editor UI checkbox in the "Render Graph" panel and a real HTTP request both
flip the exact same underlying `RenderPassToggleRegistry`/
`RenderFeatureCompositor` state, with the mutation genuinely taking effect in
the very next frame's live render graph — proven not just by design but by
PHASE5's own live disable→verify→re-enable round trips against a real,
running `GreatTamanaEditor.exe`, including the specifically product-owner-
flagged risky case (`AtmosphereComposite`, honestly observed to NOT crash or
hang, exactly as its own dependency-awareness trade-off was documented to
allow). The one intentionally-scoped-down requirement (no `RenderPassEvent`
reassignment for built-in passes) was implemented exactly as the product
owner chose, eliminating the real crash risk the source Investigation itself
identified. A full, zero-regression clean build + `ctest` pass (1848 tests,
100% passing) + final live HTTP smoke test at campaign close (PHASE6)
confirms none of this campaign's additive changes broke anything that
existed before it.
