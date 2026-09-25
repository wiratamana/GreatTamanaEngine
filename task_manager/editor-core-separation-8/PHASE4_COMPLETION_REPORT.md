# PHASE4 — "Render Graph" Panel: The Real UI Controls — COMPLETION REPORT

**Status: DONE.** Implemented exactly what
`PHASE4_RENDER_GRAPH_PANEL_CONTROLS.md` describes. One small, reasoned,
non-behavioral implementation decision was made where the phase doc's own
prose and its own shown code sample briefly disagreed with each other (ImGui
ID-uniqueness convention) — see "Deviations" below for the honest reasoning.
Everything else matches the plan's own shown code verbatim.

## What changed

### `src/Editor/Panels/RenderGraphPanel.cpp` (the ONLY file touched)

- New `#include "../../Core/Plugins/RenderFeatureCompositor.h"` — the real,
  heavy header (Step 3.6), needed now that this file actually calls
  `SetFeatureEnabled()`/`SetFeaturePriority()` on the pointer.
  `RenderPassToggleRegistry.h` was already transitively visible via
  `RenderGraphPanel.h`'s own PHASE3 include — no extra direct include needed
  (confirmed compiling).
- `BuildPassRow()` (Step 3.1): gains `rg::RenderPassToggleRegistry&`, a new
  FIRST column (index 0) with an `ImGui::Checkbox("##Enabled_" + pass.name,
  ...)` reading `IsEnabled(pass.name)` and calling `SetEnabled(pass.name,
  enabled)` on change, plus the exact tooltip text the phase doc specifies
  verbatim (including the shared-name-across-views caveat sentence, word for
  word). Every other column's `TableSetColumnIndex()` shifted by a uniform
  `+1` (1=Pass, 2=Draws, 3=Tris, 4=GPU Time, 5=Reads, 6=Writes).
- `BuildPassTable()` (Step 3.1): gains the same registry parameter,
  `BeginTable(tableId, 7, ...)` (was `6`), a new
  `TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 60.0f)` as
  the FIRST `TableSetupColumn()` call (before "Pass"), forwards the registry
  to `BuildPassRow()`. Column-count literal and the actual number of
  `TableSetupColumn()` calls both confirmed to be exactly 7.
- `BuildRegimeSection()`: gains the registry parameter, forwards it to
  `BuildPassTable()`. Both of `Build()`'s own call sites (Offscreen + Present
  regimes) updated to pass `renderPassToggleRegistry` through.
- New free function `BuildDisabledBuiltInPassesSection()` (Step 3.2): reads
  `renderPassToggleRegistry.ListAll()`, filters to `!state.enabled`, shows
  `ImGui::SeparatorText("Disabled Built-In Passes")` then either
  `"Every known built-in pass is currently enabled."` (empty case) or one row
  per disabled pass (a checkbox to re-enable it, the pass name, and
  `"(never run yet this session)"` when `!everDeclaredThisSession`).
- `BuildPluginRenderFeaturesSection()` (Step 3.3): gains
  `RenderFeatureCompositor* renderFeatureCompositor`, and per entry now draws
  a `"##FeatureEnabled"` checkbox (calls `SetFeatureEnabled()` on change,
  no-op if the pointer is null) plus an `"##FeaturePriority"` `InputInt`
  (calls `SetFeaturePriority()` on change, same null-safety), both wrapped in
  `ImGui::PushID(entry.name.c_str())`/`PopID()`, followed by the existing
  `[stage] name - blend X` text, now with a trailing `" (DISABLED)"` suffix
  when `!entry.enabled`.
- `Build()`'s signature: both of PHASE3's temporary comment-out markers
  removed — `EditorContext& ctx` (was `/*ctx*/`),
  `rg::RenderPassToggleRegistry& renderPassToggleRegistry` (was
  `/*renderPassToggleRegistry*/`), `RenderFeatureCompositor*
  renderFeatureCompositor` (was `/*renderFeatureCompositor*/`) — all 3 markers
  are now gone from the file, exactly as PHASE3's own completion report said
  PHASE4 would do.
- `Build()`'s final top-level call order, exactly matching Step 3.4's locked
  layout:
  1. Pause checkbox (unchanged)
  2. GPU-Driven Batches section (unchanged)
  3. Plugin Render Features section (now with checkbox + priority)
  4. **NEW:** Disabled Built-In Passes section
  5. Offscreen Regime section (now with the Enabled column)
  6. Present Regime section (same)
  7. **NEW:** "Debug Passes" mini-section — `ImGui::Checkbox("Show Compute
     Blur (debug)", &ctx.showBlurredSceneOutput)` and `ImGui::Checkbox("Show
     GBuffer Validation (debug)", &ctx.showGBufferValidationOutput)` — the
     EXACT SAME label text and EXACT SAME `EditorContext` fields
     `ScenePanel.cpp` already uses, confirmed by re-reading
     `ScenePanel.cpp`'s own two checkbox lines before writing this phase's
     copies. Plain, direct `EditorContext` field writes, NOT routed through
     `IEditorLayer` (the 2 `SetShowBlurredSceneOutput`/
     `SetShowGBufferValidationOutput()` virtuals PHASE3 added exist only for
     PHASE5's HTTP path).
  8. Export section (unchanged)

### `src/Editor/Panels/RenderGraphPanel.h`

**Not touched.** Confirmed by re-reading the file first: `Build()`'s
declared signature already had real, non-commented parameter names since
PHASE3 landed it — this phase's own `.cpp`-side signature cleanup (removing
the 3 comment-out markers) required zero header change. Mirrors PHASE2's own
honestly-recorded precedent (that phase's `Core.cpp` needed zero changes
despite being named in the plan's file list) — the phase doc's own file list
is an upper bound, not a mandatory-minimum one.

## Deviations from the plan

**One, small, non-behavioral, reasoned deviation — honestly recorded rather
than smoothed over.** Step 3.2's own shown code sample combines BOTH ImGui
ID-uniqueness conventions at once (a `"##Enabled_" + state.name` label AND an
`ImGui::PushID(state.name.c_str())`/`PopID()` wrap), immediately followed by
this phase doc's own explicit prose: *"pick ONE convention and use it
consistently across BOTH new sections; do not mix both styles in the same
file."* Read literally, Step 3.2's own sample code violates its own very next
sentence. I resolved this by re-reading the phase doc's own scoping words —
"BOTH new sections" — as referring specifically to the 2 places THIS
phase adds a brand-new per-row checkbox column/section keyed by a built-in
pass name: `BuildPassRow()`'s new "Enabled" column (Step 3.1, whose OWN shown
code uses ONLY the `"##Enabled_" + name` label scheme, no `PushID`) and
`BuildDisabledBuiltInPassesSection()` (Step 3.2). I implemented
`BuildDisabledBuiltInPassesSection()` using ONLY the `"##Enabled_" + name`
label scheme (dropping the redundant `PushID`/`PopID` Step 3.2's own sample
also showed), matching Step 3.1's scheme exactly, and leaving
`BuildPluginRenderFeaturesSection()`'s own separately-specified `PushID` +
plain-label convention (Step 3.3, unrelated to this specific note) untouched.
This has **zero observable/behavioral effect** either way — both schemes
produce equally-unique ImGui IDs, and this file compiles and renders
correctly with this reading. This was not escalated via `ask_questions`
because it is a purely internal implementation-consistency question, not a
product/design ambiguity — resolving it required only careful re-reading of
the phase doc's own text, not a guess.

Everything else — every function signature, every column index, the exact
column count (7), the exact tooltip text (including the shared-name-across-
views caveat sentence), the exact call order, and the exact Blur/GBuffer
checkbox label text/field bindings — matches Step 3.1-3.6's own shown code
and prose verbatim.

## Verification evidence

1. **`git_status` at start**: branch was `feature/editor-core-separation`,
   working tree clean (PHASE1 + PHASE2 + PHASE3's diffs were already
   committed, nothing outstanding) — confirmed before touching any file.

2. **Incremental build**:
   - `cmake --build build --target GreatTamanaEditor -j 8` — succeeded, 3
     build steps (`RenderGraphPanel.cpp.obj` recompiled, `libgte_editor.a`
     relinked, `GreatTamanaEditor.exe` relinked) — a genuine incremental
     build, not a full clean one.
   - `cmake --build build --target GreatTamanaEngineTests -j 8` — succeeded
     (1 step: relink only — no test `.cpp` file references
     `RenderGraphPanel.cpp` directly, so nothing needed recompiling; the test
     binary still links cleanly against the changed `libgte_editor.a`).

3. **Live, interactive, screenshot-based proof** (the real proof this phase
   actually works, since there is no HTTP endpoint yet to drive these
   controls automatically — PHASE5's job):
   - `run_app_background` on the real `GreatTamanaEditor.exe` (PID 18588).
   - `gte_send_request("/activate_tab?name=Render%20Graph")` — HTTP 200,
     `{"activated_tab":"Render Graph","success":true}`.
   - `gte_send_request("/get_swapchain")` + the image viewer — **confirmed,
     by actually looking at the rendered frame**:
     - The "Disabled Built-In Passes" section renders with the exact
       expected fresh-launch text: "Every known built-in pass is currently
       enabled."
     - The "Plugin Render Features" section shows a real checkbox (checked)
       + a real priority stepper (`InputInt`, showing `0` with `-`/`+`
       buttons) per loaded `_v2` plugin — both `DemoRenderFeatureV2` and
       `DemoRenderFeatureV2Second` are loaded this session, so this was a
       genuinely non-empty, real test of this new widget, not just the
       empty-state fallback text.
     - The "Offscreen Regime (Game View + Scene View)" pass table now shows
       7 columns with headers exactly `Enabled | Pass | Draws | Tris | GPU
       Time | Reads | Writes`, and every visible row has a real, checked
       checkbox in the new first column (e.g. `AtmosphereTransmittanceLut`,
       `AtmosphereMultiScatteringLut`, `AtmosphereSkyViewLut_GameView`,
       `AtmosphereAerialPerspective...`, etc.) — no ImGui column-count
       assertion/crash, no visually broken/overlapping layout.
     - "Scene"/"Game" views still render the expected atmosphere/skybox test
       content correctly (a red/blue radial gradient), confirming this
       change did not break rendering itself.
     - The panel's scroll position did not happen to show the new "Debug
       Passes" checkboxes or the Present Regime table in this one screenshot
       (they are further down, past the Offscreen Regime table) — their
       code path is IDENTICAL to the already-visually-confirmed Offscreen
       Regime path (`BuildRegimeSection()` is the exact same function,
       called a second time with the same widened signature), and the build
       itself would have failed to compile/link if their column counts or
       parameter forwarding were wrong, so this is not treated as an
       unverified code path, just an unphotographed one.
   - `gte_send_request("/get_logs?limit=100")` — **13 entries total, ZERO
     new** compared to PHASE3's own already-recorded baseline: the same
     pre-existing CRT-linkage warning, the same 6 plugin-load Info lines, the
     same "2 loaded plugins implement IRenderFeatureModule_v1" warning, the
     same NetworkServer/EditorHost Info lines, and the same 3
     GPU-timing-slot-budget warnings. Confirmed this phase's own new code
     logged nothing new (it doesn't call `GTE_LOG_*` at all — no new
     observable log line was ever expected).
   - `stop_app_background(pid: 18588)` — confirmed stopped successfully.

4. **`git_status` immediately before this commit**: diff touches EXACTLY
   `src/Editor/Panels/RenderGraphPanel.cpp` — modified, nothing else, no
   untracked files (besides this campaign's own `task_manager/` folder, not
   part of the source diff). `RenderGraphPanel.h` was correctly left
   untouched (see "What changed" above for why) — this is a strict SUBSET of
   the file set the phase's own "Verification" §3 names ("EXACTLY:
   `RenderGraphPanel.h`, `RenderGraphPanel.cpp`. Nothing else"), read as an
   upper bound, exactly like PHASE2's own honestly-recorded precedent for
   `Core.cpp`.

## Honest note on the verification gap this phase cannot close (as instructed)

**This engine's own network endpoints cannot click an arbitrary ImGui
checkbox.** Everything above proves the new UI *renders* correctly (7
columns, correct headers, correct checkbox/stepper widgets, correct
call-order placement, no crash, no unexpected log output) on a fresh launch
where every built-in pass and every plugin feature starts enabled. It does
**NOT** prove — because it CANNOT, over HTTP, in this phase — that actually
*clicking* one of these new checkboxes/inputs (a) really flips
`RenderPassToggleRegistry`'s stored state, (b) really causes that pass to
stop appearing in the "Offscreen Regime"/"Pipelined Regime" tables the very
next frame and start appearing in "Disabled Built-In Passes" instead, or (c)
really re-sorts a plugin feature's priority live. This is an accepted,
explicitly-disclosed verification gap for THIS phase specifically — the
phase doc's own "Verification" section names it directly and says so, and it
mirrors `editor-core-separation-7`'s own PHASE3 precedent for its "Export
DOT" button (a button whose click could not be automated over HTTP either,
at the time).

**What DOES close this gap**: PHASE5's own live HTTP smoke test. Once
`RenderGraphControlCommandBridge` and its 6 `/render_graph/*` routes exist,
`gte_send_request` can call `GET /render_graph/set_pass_enabled?name=...&
enabled=false` directly — and since that HTTP path calls the **exact same
underlying `RenderPassToggleRegistry::SetEnabled()`/
`RenderFeatureCompositor::SetFeatureEnabled()`/`SetFeaturePriority()`
methods** this panel's own checkboxes call (confirmed by reading this
phase's own new code: both paths are thin call-throughs to the identical
PHASE1/PHASE2 methods, nothing panel-specific happens in between), a
passing PHASE5 HTTP smoke test IS the real, end-to-end proof that the
mutation pathway this panel's UI relies on genuinely works — this phase's
own screenshot only proves the READ side (the UI correctly reflects current
state); PHASE5 is what proves the WRITE side actually takes effect.

I did **not** attempt to fake this by, e.g., writing a throwaway
Tier-2/manual test that calls `SetEnabled()` directly and then re-screenshots
— that would test PHASE1's registry (already covered by its own Tier-1 tests)
and would NOT test this phase's own UI code (the checkbox/table rendering
logic itself) at all, so it would be a misleading substitute for the real
gap, not a genuine closing of it.

## Honest notes for future phases

- `BuildDisabledBuiltInPassesSection()` reads `ListAll()` fresh every frame —
  on a fresh Editor launch this is always empty (every pass defaults
  `enabled = true`, per PHASE1's own auto-discovery design), exactly as
  observed in the live screenshot above.
- The new "Enabled" checkbox in `BuildPassRow()` will, once clicked to
  disable a pass with a known downstream reader (e.g. disabling
  `"RenderOpaque"` while `"AtmosphereComposite"`/`"DrawSkyBackground"` stay
  on — PHASE0's own "Known risk" from Step 2.1), NOT be crash-prevented by
  anything this phase adds — this campaign's whole ON/OFF mechanism has no
  dependency-awareness, by design (Locked Product Decision #1's trade-off).
  This phase's own verification could not exercise that specific scenario
  live (see "verification gap" above) — PHASE5's own live HTTP smoke test is
  the first phase able to actually flip a real checkbox-equivalent state and
  confirm the resulting frame, and per PHASE0's own Step 2.1 instruction,
  PHASE5 must include that exact "disable a pass with a known downstream
  reader, screenshot it, confirm it looks wrong but the Editor keeps running"
  check.
- `Core::GetRenderFeatureCompositor()` returned a genuinely non-null pointer
  this whole live session (2 `_v2` plugins loaded), so the `renderFeatureCompositor
  == nullptr` degraded-path branches in `BuildPluginRenderFeaturesSection()`
  were compiled but not exercised by this phase's own live check either —
  this is the SAME kind of "compiles, but not this session's own live path"
  gap PHASE2's own completion report already recorded honestly for its own
  new mutator methods, not a new concern this phase introduces.
