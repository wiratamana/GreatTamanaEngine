# PHASE5 — Right-hand inspector pane: frame-level static chrome — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-2/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE5_INSPECTOR_PANE_FRAME_LEVEL_CHROME.md`

## Summary

Implemented PHASE5's own "Step 3: The Plan" exactly as written, verbatim
from the phase document's own code listings, with no deviations. After
this phase, the "Frame Debugger" window's right-hand inspector pane
(previously PHASE4's placeholder line, "(inspector pane - added in a
later phase of this campaign)") is replaced by real, always-visible,
FRAME-LEVEL static chrome — independent of any event selection, exactly
as `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision #2 requires:

- A **RenderTarget** row: the label "RenderTarget" plus
  `snapshot.renderTarget.name` (always `"<No name>"`, from PHASE1's
  `FrameDebuggerRenderTargetInfo` default), and a permanently-disabled
  one-item ("RT 0") combo.
- A **Channels** toggle row: "Channels" label plus five
  `ImGui::SmallButton()`s ("All"/"R"/"G"/"B"/"A") on the same line —
  purely cosmetic, no real channel-isolation concept exists yet (per the
  phase document's own §3.2 comment, verbatim).
- A **Levels** slider: a permanently-disabled `ImGui::SliderFloat`
  (0.0–1.0, no visible label format), matching the reference
  screenshot's own slider look.
- A resolution/format caption (`"0x0  Default"` — always this exact
  string, since `FrameDebuggerRenderTargetInfo` defaults to
  `width=0`/`height=0`/`format="Default"`).
- A bordered **texture-preview placeholder box**
  (`ImGui::BeginChild("FrameDebuggerTexturePreview", ...)`, height
  `max(120.0f, avail.y * 0.5f)`) with a manually-centered "No Texture"
  text label (`ImGui::CalcTextSize` + `ImGui::SetCursorPos` math, exactly
  as the phase document specifies) — there is no real
  `RenderTexture`/`VkDescriptorSet` to sample from this campaign.
- A trailing `ImGui::Separator()`, with a comment noting PHASE6 appends
  `BuildEventDetailsSection()` immediately below it (not implemented this
  phase — reserved for PHASE6, per the task's own explicit scope
  boundary).

### Modified files (only these two, matching the phase document's §3.5
file-change inventory exactly — no new files this phase)

- `src/Editor/Panels/FrameDebuggerPanel.h`:
  - Added one new private method declaration,
    `BuildInspectorPane(const FrameDebuggerSnapshot&)`, alongside the
    existing PHASE3/PHASE4 methods. No new member fields were needed
    (per the phase document's own §3.1 note: the channel row's
    "currently selected channel" is cosmetic-only, with zero real
    selection concept, so no member field was added for it).
- `src/Editor/Panels/FrameDebuggerPanel.cpp`:
  - Added `BuildInspectorPane()`, copied verbatim from the phase
    document's §3.2 listing.
  - Replaced the PHASE4 placeholder body inside `Build()`'s
    `"FrameDebuggerInspector"` child region
    (`ImGui::TextDisabled("(inspector pane - added in a later phase of
    this campaign)");`) with the exact two-line wire-up from the phase
    document's §3.3 listing:
    `BuildInspectorPane(snapshot);` between the existing
    `BeginChild`/`EndChild` pair.

Every line matches the phase document's own code listings verbatim — no
adaptation was needed beyond what the phase document itself already
specified.

## Deviations from the phase document

None. The implementation is a direct, verbatim copy of the phase
document's §3.2/§3.3 code listings, and its own §3.4 smoke-test wording
was followed as closely as this environment allows (see below).

As with PHASE2/PHASE3/PHASE4, actually opening the "Frame Debugger"
window via a real mouse click, checking "Enable", and visually
confirming the exact pixel layout of the new inspector-pane chrome
remains **not feasible** from this environment: the window is
deliberately not part of `EditorPanelCatalog.h` (Locked Design Decision
#6), so `GET /activate_tab` cannot bring it to front, and no HTTP
command endpoint exists anywhere in `src/Network/` capable of flipping
`EditorContext::frameDebuggerWindowOpen` or synthesizing a checkbox
click. This is the same pre-existing, expected limitation already
documented identically in PHASE2/PHASE3/PHASE4's own completion reports,
not a defect introduced by this phase. The new `BuildInspectorPane()`
code is a direct, reviewed, verbatim copy of the phase document's own
listing, using only well-established, already-working ImGui idioms
(`BeginDisabled`/`Combo`/`SmallButton`/`SliderFloat`/`BeginChild`/
`CalcTextSize`/`SetCursorPos`) already used elsewhere in this same file
and across the Editor module, so it is expected to render exactly as the
phase document's own §3.4 description states once triggered by a human
via a real click.

## Compile check (fast, per this phase's own §3.4 instructions — not a
full clean rebuild/regression)

Ran exactly the command PHASE5's own "3.4 Compile check" section
specifies:

```
cmake --build build --target GreatTamanaEngine
```

Result: **succeeded** — compiled the modified
`src/Editor/Panels/FrameDebuggerPanel.cpp` and the transitively-affected
`src/Editor/ImGuiEditorLayer.cpp`, relinked `libgte_core.a`, and relinked
`GreatTamanaEngine.exe` — no warnings or errors from any of the new/
modified code.

### Live runtime smoke test

1. `run_app_background` launched the freshly-built
   `build/GreatTamanaEngine.exe` (PID 29132).
2. `gte_send_request` `GET /get_swapchain` (first request, immediately
   after launch) returned a `200 image/png` frame showing only the menu
   bar and Pause/Step toolbar — the very first rendered frame, before
   the dock layout had finished laying out; a second `GET /get_swapchain`
   moments later returned the fully-rendered default dock layout
   (Hierarchy/Scene/Game/Inspector, then Memory/Profiler/Render Graph/
   Atmosphere/Jobs/Project, all with real scene content — a sky gradient
   in Scene/Game, "Entity 0 (Camera)" in Hierarchy, "TestScene.gtscene"
   in Project) — confirming **no visual regression anywhere** from this
   phase's changes (the Frame Debugger window itself is closed by
   default, `EditorContext::frameDebuggerWindowOpen` starting `false`,
   so nothing new is visible on this screenshot — expected, matching
   PHASE2/PHASE3/PHASE4's own identical observation).
3. `gte_send_request` `GET /list_tabs` — confirmed the response still
   lists exactly the same ten pre-existing panels
   (`Hierarchy`/`Inspector`/`Scene`/`Game`/`Memory`/`Profiler`/
   `Render Graph`/`Jobs`/`Atmosphere`/`Project`) with **no** "Frame
   Debugger" entry — re-confirming Locked Design Decision #6 still holds
   after this phase's changes.
4. `stop_app_background` cleanly terminated the process.

As with PHASE2/PHASE3/PHASE4, actually opening the "Frame Debugger"
window, checking "Enable", and visually inspecting the new inspector
chrome with a real mouse could not be exercised remotely in this
environment (see "Deviations" above) — the code itself is a verbatim,
reviewed copy of the phase document's own listing.

## Next step

PHASE6 (`PHASE6_EVENT_DETAILS_SECTION_AND_PROPERTY_FORMATTING.md`) — the
event-level bottom section (Shader/Pass/Blend/Z-state/Stencil rows, the
Preview/ShaderProperties tab bar, Textures/Vectors/Matrices
subsections). Not started as part of this phase — PHASE5 touched only
`src/Editor/Panels/FrameDebuggerPanel.h/.cpp`, exactly as its own §3.5
file-change inventory specifies.
