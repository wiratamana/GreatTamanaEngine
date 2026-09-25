# PHASE4 — "Render Graph" Panel: The Real UI Controls

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST — especially Step 1's
4 requirements, Step 2.4's asymmetry note, and Locked Product Decisions
#6/#10). Read `PHASE1_COMPLETION_REPORT.md`, `PHASE2_COMPLETION_REPORT.md`,
and `PHASE3_COMPLETION_REPORT.md` FIRST — this phase is the direct
continuation of PHASE3's signature widening: every parameter this phase uses
already compiles and flows correctly by the time this phase starts; this
phase ONLY adds real ImGui code inside `RenderGraphPanel.cpp`. Use
`ask_questions` for any genuine ambiguity — this is the FIRST phase with
real, observable, screenshot-verifiable behavior change in this whole
campaign, so get the UI shape genuinely right rather than guessing.

## Step 1: The Goal

Make the "Render Graph" panel the ONE-STOP place to see and control
everything this campaign is about:

1. Every row in the existing pass tables (`BuildPassTable()`) gains an
   "Enabled" checkbox — unchecking it disables that built-in pass starting
   next frame.
2. A NEW section lists every built-in pass name the registry knows about
   that is CURRENTLY DISABLED (and therefore invisible in the ordinary pass
   tables — see Step 2.4's asymmetry) — each with its own checkbox to turn
   it back on.
3. The existing "Plugin Render Features" section gains an "Enabled" checkbox
   and an editable priority field per entry.
4. Two new checkboxes, "Show Compute Blur (debug)" / "Show GBuffer
   Validation (debug)", bound to the exact same `EditorContext` bools
   `ScenePanel.cpp` already uses — finally using this panel's own previously
   dead `EditorContext& ctx` parameter.

## Step 2: The Situation

Read these exact files IN FULL before writing any code (this phase's own
diff is entirely inside these two):

- `src/Editor/Panels/RenderGraphPanel.h` — confirm the CURRENT signature
  after PHASE3's widening (2 new trailing parameters, both currently
  unused/discarded in the `.cpp`).
- `src/Editor/Panels/RenderGraphPanel.cpp` — the WHOLE file, in particular:
  - `BuildPassRow(const rg::RenderGraphPassMetadata& pass)` — the exact
    current 6-column layout (`Pass`/`Draws`/`Tris`/`GPU Time`/`Reads`/
    `Writes`). This phase adds a 7th column, `Enabled`.
  - `BuildPassTable(const char* tableId, const rg::RenderGraphRegimeMetadata& regime)` —
    the `ImGui::BeginTable(tableId, 6, tableFlags)` call — the column COUNT
    literal must become `7` once the new column is added, and a matching
    `ImGui::TableSetupColumn("Enabled", ...)` call must be added, in the
    SAME position (this phase places it as the FIRST column, before "Pass" —
    read Step 3.1 below for the exact reasoning) — get this exactly right;
    a mismatched column count between `BeginTable()`'s 2nd argument and the
    actual number of `TableSetupColumn()`/`TableSetColumnIndex()` calls is a
    real, silent ImGui bug class this codebase's own comments warn about
    repeatedly (see the file's own `ImGuiTableFlags_NoSavedSettings`
    comments for a related historical bug).
  - `BuildPluginRenderFeaturesSection(const std::vector<RenderFeatureDebugEntry>& entries)` —
    currently a single `ImGui::Text(...)` line per entry, no widget at all.
  - `Build()`'s own top-level call order (Pause checkbox -> GPU-Driven
    Batches -> Plugin Render Features -> Offscreen Regime -> Present Regime
    -> Export). This phase's new Blur/GBuffer checkboxes and the new
    "known-but-disabled built-in passes" section both need a clear, sensible
    placement — see Step 3.4 below for the locked placement.
- `src/Renderer/RenderGraph/RenderPassToggleRegistry.h` (PHASE1) — the WHOLE
  file, in particular `ListAll()`'s exact return shape
  (`std::vector<RenderPassToggleState>`, each with `name`/`enabled`/
  `everDeclaredThisSession`) and `IsEnabled(name)`/`SetEnabled(name, enabled)`'s
  exact signatures.
- `src/Core/Plugins/RenderFeatureCompositor.h` (PHASE2) — the WHOLE file,
  in particular `SetFeatureEnabled(name, enabled)`/
  `SetFeaturePriority(name, priority)`'s exact signatures. This phase's own
  `.cpp` needs a REAL `#include "../../Core/Plugins/RenderFeatureCompositor.h"`
  now (the `.h` only forward-declares it — see PHASE3's own Locked
  Architecture Decision #13).
- `src/Editor/Panels/ScenePanel.cpp` — re-read the EXACT existing
  `ImGui::Checkbox("Show Compute Blur (debug)", &ctx.showBlurredSceneOutput);`/
  `ImGui::Checkbox("Show GBuffer Validation (debug)", &ctx.showGBufferValidationOutput);`
  lines one more time — this phase's own 2 new checkboxes in
  `RenderGraphPanel.cpp` use the EXACT SAME label text (so a screenshot of
  either panel reads identically) and the EXACT SAME `EditorContext` fields,
  never a copy/duplicate field.

## Step 3: The Plan

### Step 3.1 — The "Enabled" column in `BuildPassRow()`/`BuildPassTable()`

Placed as the FIRST column (before "Pass") — reasoning: an unchecked box
sitting immediately to the LEFT of a pass's name reads naturally as "this
row's own on/off switch", matching how a file manager/task list typically
places its own checkbox column first, and keeps every other existing column
index shifted by a UNIFORM `+1` rather than inserted awkwardly in the
middle.

`BuildPassRow()`'s new signature gains the registry:

```cpp
void BuildPassRow(const rg::RenderGraphPassMetadata& pass, rg::RenderPassToggleRegistry& renderPassToggleRegistry)
{
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    bool enabled = renderPassToggleRegistry.IsEnabled(pass.name);
    // ImGui::Checkbox's own label must be unique across the WHOLE panel (ImGui
    // IDs are label-derived by default) - "##Enabled_<passName>" mirrors this
    // file's own existing "RgPasses##" + idSuffix ImGui-ID-uniqueness
    // convention (BuildRegimeSection()).
    const std::string checkboxId = std::string("##Enabled_") + pass.name;
    if (ImGui::Checkbox(checkboxId.c_str(), &enabled)) {
        renderPassToggleRegistry.SetEnabled(pass.name, enabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Unchecking this disables \"%s\" starting next frame - it will stop appearing in this "
                           "table entirely once disabled (see the \"Disabled Built-In Passes\" section below). "
                           "If this same name appears in BOTH the Offscreen Regime table (Game View + Scene View "
                           "share it), toggling it here affects EVERY row with this exact name at once - there is "
                           "no per-view control (see PHASE0_MASTER_STRATEGY.md's Step 2.1).",
            pass.name.c_str());
    }

    ImGui::TableSetColumnIndex(1); // was 0
    ... (every existing column's index below simply shifts by +1 - "Pass" at
    1, "Draws" at 2, "Tris" at 3, "GPU Time" at 4, "Reads" at 5, "Writes" at
    6) ...
}
```

`BuildPassTable()`'s changes:
```cpp
if (ImGui::BeginTable(tableId, 7, tableFlags)) {   // was 6
    ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Draws", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableSetupColumn("Tris", ImGuiTableColumnFlags_WidthFixed, 65.0f);
    ImGui::TableSetupColumn("GPU Time", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    ImGui::TableSetupColumn("Reads");
    ImGui::TableSetupColumn("Writes");
    ImGui::TableHeadersRow();

    for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
        BuildPassRow(pass, renderPassToggleRegistry);
    }

    ImGui::EndTable();
}
```

`BuildPassTable()`'s own signature gains the registry parameter too, and its
2 existing call sites inside `BuildRegimeSection()` forward it through
(`BuildRegimeSection()`'s own signature ALSO gains the registry parameter,
forwarded from `Build()`'s own 2 existing call sites for the Offscreen and
Present regimes).

**A disabled pass row is a real, valid, non-culled-looking row while it is
STILL declared this exact frame** (the checkbox click takes effect starting
NEXT frame, per Step 1's own contract) — no special "about to be disabled"
visual treatment is needed; the row simply vanishes from this table entirely
the frame after, and starts appearing in Step 3.2's new section instead.

### Step 3.2 — New section: "Disabled Built-In Passes"

A brand-new free function, `BuildDisabledBuiltInPassesSection()`, placed
right after `BuildPluginRenderFeaturesSection()`'s own call in `Build()`
(see Step 3.4's placement rationale):

```cpp
// editor-core-separation-8 campaign, PHASE4 - the ONLY place a built-in
// pass the caller has switched OFF is still visible at all (see
// PHASE0_MASTER_STRATEGY.md's Step 2.4 - a disabled pass leaves ZERO trace
// in rg::RenderGraphMetadata, since it is never declared into the graph at
// all). Reads renderPassToggleRegistry.ListAll() directly - the registry
// itself, not the metadata, is this section's own source of truth.
void BuildDisabledBuiltInPassesSection(rg::RenderPassToggleRegistry& renderPassToggleRegistry)
{
    const std::vector<rg::RenderPassToggleState> allStates = renderPassToggleRegistry.ListAll();
    std::vector<rg::RenderPassToggleState> disabled;
    for (const rg::RenderPassToggleState& state : allStates) {
        if (!state.enabled) {
            disabled.push_back(state);
        }
    }

    ImGui::SeparatorText("Disabled Built-In Passes");
    if (disabled.empty()) {
        ImGui::TextDisabled("Every known built-in pass is currently enabled.");
        return;
    }
    for (const rg::RenderPassToggleState& state : disabled) {
        bool enabled = false; // always false here by construction (this loop only ever sees disabled entries).
        const std::string checkboxId = "##Enabled_" + state.name;
        ImGui::PushID(state.name.c_str());
        if (ImGui::Checkbox(checkboxId.c_str(), &enabled)) {
            renderPassToggleRegistry.SetEnabled(state.name, enabled);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(state.name.c_str());
        if (!state.everDeclaredThisSession) {
            ImGui::SameLine();
            ImGui::TextDisabled("(never run yet this session)");
        }
        ImGui::PopID();
    }
}
```

(`ImGui::PushID`/`PopID` per row, rather than baking the name into the
checkbox's own label string, is an equally valid alternative to the
`"##Enabled_" + name` scheme used in Step 3.1 — pick ONE convention and use
it consistently across BOTH new sections; do not mix both styles in the
same file.)

### Step 3.3 — `BuildPluginRenderFeaturesSection()`'s new checkbox + priority field

```cpp
void BuildPluginRenderFeaturesSection(
    const std::vector<RenderFeatureDebugEntry>& entries, RenderFeatureCompositor* renderFeatureCompositor)
{
    ImGui::SeparatorText("Plugin Render Features");
    if (entries.empty()) {
        ImGui::TextDisabled("No loaded plugin implements IRenderFeatureModule_v2 this session.");
        return;
    }

    for (const RenderFeatureDebugEntry& entry : entries) {
        ImGui::PushID(entry.name.c_str());

        bool enabled = entry.enabled;
        if (ImGui::Checkbox("##FeatureEnabled", &enabled) && renderFeatureCompositor != nullptr) {
            renderFeatureCompositor->SetFeatureEnabled(entry.name, enabled);
        }
        ImGui::SameLine();

        int priority = entry.priority;
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("##FeaturePriority", &priority) && renderFeatureCompositor != nullptr) {
            renderFeatureCompositor->SetFeaturePriority(entry.name, priority);
        }
        ImGui::SameLine();

        ImGui::Text("[%s] %s - blend %s%s", entry.stage.c_str(), entry.name.c_str(), entry.blendMode.c_str(),
            entry.enabled ? "" : " (DISABLED)");

        ImGui::PopID();
    }
}
```

(`renderFeatureCompositor == nullptr` — e.g. a `GTE_ENABLE_EDITOR`-adjacent
degraded build, or genuinely zero loaded plugins — means the checkbox/input
still RENDER, but any edit is silently a no-op, mirroring this whole
campaign's own "null bridge/pointer degrades gracefully, never crashes"
discipline used everywhere else. Do NOT `ImGui::BeginDisabled()` them purely
because the pointer happens to be null on ONE particular frame — a
transient null is not expected in practice once a compositor exists, so this
is purely defensive, not a real, reachable, steady-state UI mode worth a
special disabled-look.)

`Build()`'s own existing call, `BuildPluginRenderFeaturesSection(metadata.renderFeatures);`,
gains the new argument: `BuildPluginRenderFeaturesSection(metadata.renderFeatures, renderFeatureCompositor);`.

### Step 3.4 — Blur/GBuffer checkboxes + final call-order placement

`Build()`'s FULL new top-level call order (this phase's own final,
locked layout — read it against the CURRENT file's own order before
editing, to confirm exactly which existing lines move/stay):

```
1. Pause checkbox (unchanged)
2. GPU-Driven Batches section (unchanged)
3. Plugin Render Features section (Step 3.3's new checkbox/priority added)
4. NEW: "Disabled Built-In Passes" section (Step 3.2) - placed HERE,
   immediately after Plugin Render Features and BEFORE the two regime
   sections, so a caller sees "what's currently OFF" before scrolling past
   the (often much longer) live pass/resource tables - mirrors this file's
   own existing placement rationale for GPU-Driven Batches/Plugin Render
   Features ("this panel's own newest, most immediately actionable live
   signal, shown early").
5. Offscreen Regime section (BuildPassTable() now needs
   renderPassToggleRegistry forwarded through)
6. Present Regime section (same)
7. NEW: "Debug Passes" mini-section - exactly 2 checkboxes:
   ImGui::SeparatorText("Debug Passes");
   ImGui::Checkbox("Show Compute Blur (debug)", &ctx.showBlurredSceneOutput);
   ImGui::Checkbox("Show GBuffer Validation (debug)", &ctx.showGBufferValidationOutput);
   - placed AFTER both regime sections and BEFORE "Export" (a natural final
     "debug toggles" grouping, right before the panel's own closing Export
     button) - this FINALLY uses ctx (removing the `/*ctx*/` comment-out from
     Build()'s own parameter, this phase's job per PHASE3's own note).
8. Export section (unchanged)
```

**These 2 checkboxes are PLAIN, direct `EditorContext` field writes — NOT
routed through `IEditorLayer`.** This is intentional and mirrors
`ScenePanel.cpp`'s own existing checkboxes exactly (both are ordinary,
synchronous, main-thread-only ImGui widgets mutating shared, by-reference
`EditorContext` state) — the 2 new `IEditorLayer::SetShowBlurredSceneOutput()`/
`SetShowGBufferValidationOutput()` methods PHASE3 added exist ONLY for the
HTTP path (PHASE5), never for this panel's own hand-drawn checkboxes.

### Step 3.5 — Final signature cleanup

Remove BOTH of `Build()`'s now-genuinely-used parameter comment-outs:
`EditorContext& /*ctx*/` becomes `EditorContext& ctx`, and PHASE3's own 2
temporary markers (`rg::RenderPassToggleRegistry& /*renderPassToggleRegistry*/`,
`RenderFeatureCompositor* /*renderFeatureCompositor*/`) become plain, real,
named parameters.

### Step 3.6 — `#include` additions to `RenderGraphPanel.cpp`

- `#include "../../Core/Plugins/RenderFeatureCompositor.h"` (the real,
  heavy header — needed now that this file actually calls
  `SetFeatureEnabled()`/`SetFeaturePriority()` on the pointer).
- Confirm `RenderPassToggleRegistry.h` is reachable (already `#include`d, or
  reachable transitively via `RenderGraphPanel.h`'s own PHASE3 addition —
  add a direct include here too if not already visible, rather than relying
  on an indirect one).

### Verification (the highest-scrutiny phase in this campaign)

1. Incremental build: `cmake --build build`.
2. **Live, interactive, screenshot-based proof** — the real proof this
   phase actually works, since there is no HTTP endpoint yet to drive these
   controls automatically:
   - `run_app_background` the real `GreatTamanaEditor.exe`.
   - `gte_send_request("/activate_tab?name=Render%20Graph")` then
     `gte_send_request("/get_swapchain")` + `load_image` — confirm the new
     "Enabled" checkbox column, the new "Disabled Built-In Passes" section
     (should read "every known built-in pass is currently enabled" on a
     fresh launch), the new per-plugin-feature checkbox/priority field (if
     any `_v2` plugin is loaded this session — otherwise confirm the
     existing "No loaded plugin..." message still renders), and the 2 new
     "Debug Passes" checkboxes all render correctly, with no ImGui column-
     count assertion/crash and no visually broken layout.
   - This engine's own network endpoints cannot click an arbitrary ImGui
     checkbox — this phase cannot automate "click the checkbox, confirm the
     pass disappears next frame" purely over HTTP. **This is an accepted,
     explicitly-disclosed verification gap for THIS phase specifically**
     (mirroring `editor-core-separation-7`'s own PHASE3 precedent for its
     "Export DOT" button) — PHASE5's own live HTTP smoke test IS what
     actually proves the underlying `SetEnabled()`/`SetFeatureEnabled()`/
     `SetFeaturePriority()` mutation pathway genuinely works end-to-end
     (since the HTTP path calls the EXACT SAME underlying methods this
     panel's checkboxes call) — record this explicitly in
     `PHASE4_COMPLETION_REPORT.md` rather than silently glossing over it.
   - `gte_send_request("/get_logs?limit=50")` — confirm no new
     warning/error text was logged by this phase's own changes.
   - `stop_app_background` afterward.
3. `git_status` — confirm the diff touches EXACTLY: `RenderGraphPanel.h`,
   `RenderGraphPanel.cpp`. Nothing else (all the plumbing this phase relies
   on already landed in PHASE1–PHASE3).

### What this phase does NOT do

- Does not add any HTTP endpoint (PHASE5's job).
- Does not change `ScenePanel.cpp`'s own existing Blur/GBuffer checkboxes.
- Does not run a full build or full `ctest` pass (Workflow Rule 1 — Phase 6
  only).

### Completion

Write `PHASE4_COMPLETION_REPORT.md` (the real screenshot evidence described
above, the exact final `Build()` call-order, and an HONEST note about the
"cannot click an ImGui checkbox over HTTP" verification gap and how PHASE5
closes it), then `git_add` + `git_commit`.
