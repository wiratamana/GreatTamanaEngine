# PHASE2 — `ScopedUniqueId` Helper + The Exact Reported Bug, Fixed

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first).
**Previous phase:** `PHASE1_ID_CONFLICT_DETECTION_FOUNDATION.md` — read its
own `PHASE1_COMPLETION_REPORT.md` before starting, in case anything drifted
from that phase's plan (e.g. `ImGuiEditorLayer::NewFrame()`'s exact final
shape, or `ImGuiIdConflictGuard`'s exact method signature).

---

## Step 1: The Goal Of This Phase

Two things, in order:

1. Build the ONE public, mandatory RAII helper every ImGui loop in this
   Editor will eventually use: `gte::ScopedUniqueId`. It is a thin,
   `<imgui.h>`-owning wrapper that (a) pushes a per-iteration-unique ImGui ID
   scope derived from a loop index (never from data content alone), and (b)
   reports through to `ImGuiIdConflictGuard::CheckCurrentIdScope()` (built in
   PHASE1) so a genuine future collision is still caught and logged even
   though it should now be structurally impossible.
2. Retrofit `src/Editor/Panels/RenderGraphPanel.cpp`'s three existing ImGui
   loops onto it — this is the literal fix for the bug report this whole
   campaign exists because of. After this phase, hovering either of the two
   Atmosphere pass rows in the "Offscreen Regime (Game View + Scene View)"
   table must NOT show Dear ImGui's red conflict-highlight anymore, while
   the intentional "toggling one row's checkbox also flips the other
   same-named row's checkbox" shared-state behavior must be COMPLETELY
   UNCHANGED (this is app-level business logic this campaign never touches
   — see `PHASE0_MASTER_STRATEGY.md` LDD7).

## Step 2: The Situation Going Into This Phase

`ImGuiIdConflictGuard::Instance().CheckCurrentIdScope(debugContext,
debugKey)` already exists (PHASE1) and already gets a fresh per-frame
tracker reset every frame (`ImGuiEditorLayer::NewFrame()`). Nothing calls
`CheckCurrentIdScope()` yet except (indirectly) nothing — it is dead code
until this phase's new `ScopedUniqueId` calls it.

Re-read `src/Editor/Panels/RenderGraphPanel.cpp` in full before editing —
the exact line numbers quoted below are from the version read while writing
this strategy; they WILL drift slightly once PHASE1's own edits to other
files are in place (`RenderGraphPanel.cpp` itself is untouched by PHASE1, so
its own internal line numbers should not have moved, but always re-read
before editing regardless).

Three call sites in that file need retrofitting:

1. `BuildPassRow()` (currently ~line 49-111) — the checkbox at line 59-60
   (`checkboxId = "##Enabled_" + pass.name`). Called from `BuildPassTable()`
   (currently ~line 152-154) via a plain range-`for` over `regime.passes`
   with NO index available today.
2. `BuildDisabledBuiltInPassesSection()` (currently ~line 340-368) — the
   checkbox at line 357-358 (`checkboxId = "##Enabled_" + state.name`),
   inside a plain range-`for` over a locally-built `disabled` vector, again
   with no index today.
3. `BuildPluginRenderFeaturesSection()` (currently ~line 282-325) — already
   uses `ImGui::PushID(entry.name.c_str())`/`ImGui::PopID()` directly
   (line 292 and 323) inside a plain range-`for` over `entries`, with no
   index today. This one is not the reported bug (no duplicate plugin
   feature name has ever been observed), but it has the exact same LATENT
   shape and this phase's own file is already open for editing — fix it now
   rather than leaving a second copy of the same landmine in the same file
   for PHASE3 to have to come back to.

## Step 3: The Detailed Plan

### 3.1 — Create `src/Editor/ImGuiUniqueId.h`

```cpp
#pragma once

namespace gte {

// THE mandated way to enter a per-iteration ImGui ID scope for any widget
// built inside a loop over a list/table of runtime data - see AGENTS.md's
// "ImGui Widget ID Uniqueness" section and
// task_manager/editor-core-separation-10/PHASE0_MASTER_STRATEGY.md for the
// full story (the Render Graph Panel duplicate-checkbox bug this exists to
// permanently prevent a recurrence of, engine-wide).
//
// `index` MUST be the current loop iteration's own distinct integer
// position (e.g. a plain `for (std::size_t i = 0; ...; ++i)` counter, or
// any other value the CALLER can guarantee is distinct across all
// iterations of the SAME loop, in the SAME frame) - this is what makes
// widget-ID uniqueness completely INDEPENDENT of whether `debugKey` happens
// to repeat across iterations. Relying on a name/label/data-derived string
// alone for uniqueness is exactly the mistake this class exists to make
// structurally impossible to repeat - "this name is always unique" is
// NEVER a safe assumption to build ImGui ID uniqueness on top of.
//
// `debugContext` (e.g. "RenderGraphPanel::BuildPassRow") and `debugKey`
// (e.g. a pass name, may be empty/omitted) are used ONLY for readability -
// Dear ImGui's own Item Picker, and this class's own conflict-log message
// (ImGuiIdConflictGuard) if a collision is ever still somehow detected -
// NEVER relied upon for uniqueness by themselves. `debugContext` must be a
// string literal (or otherwise outlive the call) - it is not copied.
class ScopedUniqueId {
public:
    explicit ScopedUniqueId(int index, const char* debugContext, const char* debugKey = "");
    ~ScopedUniqueId();

    ScopedUniqueId(const ScopedUniqueId&) = delete;
    ScopedUniqueId& operator=(const ScopedUniqueId&) = delete;
    ScopedUniqueId(ScopedUniqueId&&) = delete;
    ScopedUniqueId& operator=(ScopedUniqueId&&) = delete;

private:
    bool m_pushedDebugKeyScope;
};

} // namespace gte
```

### 3.2 — Create `src/Editor/ImGuiUniqueId.cpp`

```cpp
#include "ImGuiUniqueId.h"

#include "ImGuiIdConflictGuard.h"

#include <imgui.h>

namespace gte {

ScopedUniqueId::ScopedUniqueId(int index, const char* debugContext, const char* debugKey)
    : m_pushedDebugKeyScope(debugKey != nullptr && debugKey[0] != '\0')
{
    // The index-based push is the ONLY thing this class relies on for
    // actual uniqueness - a loop iteration index is always distinct across
    // the SAME loop's iterations in the SAME frame, by construction,
    // regardless of what any data string contains.
    ImGui::PushID(index);

    // Purely cosmetic/for-debuggability nested scope - makes the composed
    // ID stack more legible under Dear ImGui's own Item Picker, and gives
    // ImGuiIdConflictGuard a real key string to put in its log message if a
    // conflict is ever still detected. Never relied upon for uniqueness -
    // the index push above already guarantees that on its own.
    if (m_pushedDebugKeyScope) {
        ImGui::PushID(debugKey);
    }

    ImGuiIdConflictGuard::Instance().CheckCurrentIdScope(debugContext, debugKey);
}

ScopedUniqueId::~ScopedUniqueId()
{
    if (m_pushedDebugKeyScope) {
        ImGui::PopID();
    }
    ImGui::PopID();
}

} // namespace gte
```

### 3.3 — Register the two new files in `CMakeLists.txt` (root)

Right after the `src/Editor/ImGuiIdConflictGuard.h`/`.cpp` pair PHASE1 just
added:

```cmake
    # task_manager/editor-core-separation-10 campaign, PHASE2 - the ONE
    # mandated public helper for entering a per-iteration ImGui ID scope -
    # see AGENTS.md's "ImGui Widget ID Uniqueness" section.
    src/Editor/ImGuiUniqueId.h
    src/Editor/ImGuiUniqueId.cpp
```

### 3.4 — Retrofit `BuildPassRow()`/`BuildPassTable()`

Change `BuildPassRow()`'s signature to take the row index, add the include,
and simplify the checkbox ID back to a literal (uniqueness now comes from
`ScopedUniqueId`'s `PushID(rowIndex)`, not from string concatenation):

```cpp
#include "../ImGuiUniqueId.h"
```

```cpp
void BuildPassRow(int rowIndex, const rg::RenderGraphPassMetadata& pass, rg::RenderPassToggleRegistry& renderPassToggleRegistry)
{
    // task_manager/editor-core-separation-10 campaign, PHASE2 - THE fix for
    // the reported bug: this row's ImGui ID scope is now keyed by its own
    // loop index (always distinct per row, per frame, by construction),
    // never by pass.name alone - so two rows sharing the exact same
    // pass.name (e.g. "AtmosphereSkyViewLutPass" appearing once for Game
    // View and once for Scene View - a real, permanent, INTENTIONAL fact
    // about this table, see PHASE0_MASTER_STRATEGY.md section 2.2) no
    // longer collide as ImGui widgets, even though they intentionally keep
    // sharing the exact same renderPassToggleRegistry STATE (pass.name is
    // still the toggle registry's own lookup key below - that business
    // logic is completely unchanged).
    ScopedUniqueId idScope(rowIndex, "RenderGraphPanel::BuildPassRow", pass.name.c_str());

    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    bool enabled = renderPassToggleRegistry.IsEnabled(pass.name);
    // Uniqueness now comes entirely from the ScopedUniqueId scope above -
    // this literal, un-suffixed label is intentional and correct.
    if (ImGui::Checkbox("##Enabled", &enabled)) {
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

    ImGui::TableSetColumnIndex(1);
    // ... (everything else in this function is COMPLETELY UNCHANGED - do
    // not touch the remaining column-building code, only the top of the
    // function as shown above)
}
```

Keep every remaining line of the function (columns 1 through 6, the
tooltip text) byte-for-byte identical to what is already there — only the
function signature, the new `ScopedUniqueId` line, and the checkbox ID
literal change.

Update the caller, `BuildPassTable()`, to supply an index:

```cpp
        for (std::size_t i = 0; i < regime.passes.size(); ++i) {
            BuildPassRow(static_cast<int>(i), regime.passes[i], renderPassToggleRegistry);
        }
```

(replacing the existing `for (const rg::RenderGraphPassMetadata& pass :
regime.passes) { BuildPassRow(pass, renderPassToggleRegistry); }`).

### 3.5 — Retrofit `BuildDisabledBuiltInPassesSection()`

Same pattern — add an index to the loop, wrap in `ScopedUniqueId`, simplify
the checkbox ID:

```cpp
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
    for (std::size_t i = 0; i < disabled.size(); ++i) {
        const rg::RenderPassToggleState& state = disabled[i];
        // task_manager/editor-core-separation-10 campaign, PHASE2 - same
        // fix as BuildPassRow() above: index-scoped, not name-scoped.
        ScopedUniqueId idScope(static_cast<int>(i), "RenderGraphPanel::BuildDisabledBuiltInPassesSection", state.name.c_str());

        bool enabled = false; // always false here by construction (this loop only ever sees disabled entries).
        if (ImGui::Checkbox("##Enabled", &enabled)) {
            renderPassToggleRegistry.SetEnabled(state.name, enabled);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(state.name.c_str());
        if (!state.everDeclaredThisSession) {
            ImGui::SameLine();
            ImGui::TextDisabled("(never run yet this session)");
        }
    }
}
```

### 3.6 — Retrofit `BuildPluginRenderFeaturesSection()`

Replace the manual `ImGui::PushID(entry.name.c_str())`/`ImGui::PopID()` pair
with `ScopedUniqueId`, adding an index to the loop:

```cpp
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const RenderFeatureDebugEntry& entry = entries[i];
        // task_manager/editor-core-separation-10 campaign, PHASE2 - was a
        // bare ImGui::PushID(entry.name.c_str())/PopID() pair with the same
        // LATENT "two entries could share a name" shape as the reported
        // bug, even though no real collision has ever been observed here.
        ScopedUniqueId idScope(static_cast<int>(i), "RenderGraphPanel::BuildPluginRenderFeaturesSection", entry.name.c_str());

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

        if (entry.isV3) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[v3]");
        }
        // No manual ImGui::PopID() anymore - ScopedUniqueId's destructor
        // handles it when idScope goes out of scope at the end of this
        // loop body.
    }
```

Remove the now-unused manual `ImGui::PushID(entry.name.c_str());` and
`ImGui::PopID();` lines entirely — do not leave both the old and new
mechanism in place at once.

### 3.7 — Update this file's own stale comments

`BuildDisabledBuiltInPassesSection()`'s existing comment block (currently
~lines 327-339) explicitly says: *"Uses the exact same '##Enabled_' + name
checkbox-ID scheme BuildPassRow() above already uses (rather than
ImGui::PushID()/PopID() per row)... pick ONE convention... do not mix both
styles in the same file"*. This comment is now WRONG (both call sites now
use `ScopedUniqueId`, not string-concatenated labels) — replace it with a
short, accurate comment pointing at this campaign instead, e.g.: *"Uses
ScopedUniqueId (see ImGuiUniqueId.h) exactly like BuildPassRow() above -
task_manager/editor-core-separation-10 campaign, PHASE2."* Stale comments
that actively describe removed behavior are worse than no comment at all —
do not leave them.

### 3.8 — Verify (incremental compile check + live proof — no full
build/ctest this phase)

1. Incremental build. Fix any compile error (a very likely one: forgetting
   to update BOTH the function signature AND every call site of
   `BuildPassRow()` — there is exactly one caller, `BuildPassTable()`).
2. `search_in_dir` for `"##Enabled_"` under `src/Editor/` — must now return
   ZERO matches (both old string-concatenated checkbox IDs are gone).
3. `search_in_dir` for `ImGui::PushID(entry.name` under
   `src/Editor/Panels/RenderGraphPanel.cpp` — must now return ZERO matches
   (the old manual PushID in `BuildPluginRenderFeaturesSection()` is gone).
4. **Live HTTP-driven proof** (per this campaign's own operating rules —
   use the engine's real debugging network features, do not just trust the
   compile):
   - `run_app_background` the built Editor executable.
   - `gte_send_request` `GET /activate_tab?name=Render%20Graph` (or
     whatever the exact panel name/query parameter turns out to be — check
     `GET /list_tabs` first if unsure) to bring the "Render Graph" panel to
     the front.
   - `gte_send_request` `GET /get_swapchain` (or `/get_game_view`) to
     capture a screenshot. Visually confirm the "Offscreen Regime (Game
     View + Scene View)" table still renders, and — importantly — still
     legitimately shows the SAME duplicate Atmosphere pass names as before
     this campaign (that duplication is permanent and intentional, LDD7 —
     do not "fix" it if you see it).
   - Let the app run for at least a few real seconds/frames (so
     `ImGuiIdConflictGuard`'s per-frame logic has actually executed many
     times against this real table), then `gte_send_request`
     `GET /get_logs?category=ImGuiIdConflict` and confirm the result is
     EMPTY — proving no conflict was detected against real, live,
     duplicate-named data.
   - `stop_app_background` the process when done.
   - Honest limitation to note in the completion report: there is currently
     no HTTP endpoint to simulate a mouse hover, so this live proof cannot
     directly reproduce "hover and see if Dear ImGui's own red highlight
     still appears" end-to-end over the network. The absence of any
     `ImGuiIdConflict` log line during a real live session against the
     real duplicate-name table, combined with PHASE1's own deterministic
     Tier-1 test proving the underlying tracker logic is correct, is the
     authoritative proof for this phase — this is a real, disclosed
     limitation, not something to paper over.

### 3.9 — Write `PHASE2_COMPLETION_REPORT.md`

Cover: the exact final diff for all three retrofitted functions, the
screenshot evidence (describe what was seen), the `GET /get_logs` result,
and anything PHASE3 needs to know (e.g. the exact working query parameters
for `/activate_tab` and `/get_swapchain`, so PHASE3/PHASE4 don't have to
rediscover them). Then `git_add` + `git_commit`.

## Step 4: Handoff To PHASE3

PHASE3 repeats this EXACT same retrofit pattern (add a loop index if one
doesn't already exist, wrap in `ScopedUniqueId` with a
`"<File>::<Function>"`-style `debugContext` string and the most meaningful
available string as `debugKey`) across every remaining call site inventoried
in `PHASE0_MASTER_STRATEGY.md` section 2.4. Nothing new needs to be built —
`ScopedUniqueId` (this phase) is already complete and final.
