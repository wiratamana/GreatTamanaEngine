# PHASE2 — `ScopedUniqueId` Helper + The Exact Reported Bug, Fixed — COMPLETION REPORT

**Campaign:** `editor-core-separation-10` — "ImGui Widget ID Uniqueness"
**Phase:** PHASE2 of 4
**Status:** DONE

---

## What was done

Built the ONE public, mandatory RAII helper (`gte::ScopedUniqueId`) and
retrofitted all three existing ImGui loops in
`src/Editor/Panels/RenderGraphPanel.cpp`, exactly as specified by
`PHASE2_SCOPED_UNIQUE_ID_HELPER_AND_RENDERGRAPHPANEL_FIX.md`, with no
deviation from the phase's own code listings.

### New files

- `src/Editor/ImGuiUniqueId.h` — public `gte::ScopedUniqueId` RAII class.
  `explicit ScopedUniqueId(int index, const char* debugContext, const char*
  debugKey = "")`. Non-copyable, non-movable.
- `src/Editor/ImGuiUniqueId.cpp` — implementation: `PushID(index)` (the ONLY
  thing uniqueness relies on) + an optional cosmetic nested
  `PushID(debugKey)` scope, then
  `ImGuiIdConflictGuard::Instance().CheckCurrentIdScope(debugContext,
  debugKey)`. Destructor pops in reverse order.

### Edited files

- `CMakeLists.txt` (root) — added the 2 new `gte_editor` source files right
  after `src/Editor/ImGuiIdConflictGuard.cpp` (line 997 as of this phase's
  start, matching PHASE1's own reported insertion point exactly).
- `src/Editor/Panels/RenderGraphPanel.cpp` — added
  `#include "../ImGuiUniqueId.h"`, and retrofitted all three call sites the
  phase doc inventoried:
  1. **`BuildPassRow()`/`BuildPassTable()`** — `BuildPassRow()` now takes a
     new leading `int rowIndex` parameter, opens
     `ScopedUniqueId idScope(rowIndex, "RenderGraphPanel::BuildPassRow",
     pass.name.c_str())` as its first statement, and the checkbox ID
     collapsed from the old `"##Enabled_" + pass.name` string-concatenation
     back down to the literal `"##Enabled"` (uniqueness now comes entirely
     from the `ScopedUniqueId` scope). `BuildPassTable()`'s caller loop
     changed from a plain range-`for` over `regime.passes` to an
     index-`for` (`for (std::size_t i = 0; i < regime.passes.size(); ++i)`)
     passing `static_cast<int>(i)`.
  2. **`BuildDisabledBuiltInPassesSection()`** — same pattern: the `for
     (const auto& state : disabled)` range loop became an index loop,
     wrapping each iteration in `ScopedUniqueId idScope(static_cast<int>(i),
     "RenderGraphPanel::BuildDisabledBuiltInPassesSection",
     state.name.c_str())`, and the checkbox ID collapsed from
     `"##Enabled_" + state.name` to the literal `"##Enabled"`.
  3. **`BuildPluginRenderFeaturesSection()`** — the manual
     `ImGui::PushID(entry.name.c_str())` / `ImGui::PopID()` pair was
     replaced entirely: the range loop became an index loop, each iteration
     opens `ScopedUniqueId idScope(static_cast<int>(i),
     "RenderGraphPanel::BuildPluginRenderFeaturesSection",
     entry.name.c_str())` and the old manual `PopID()` call was removed
     (the destructor handles it).
  - The stale `BuildDisabledBuiltInPassesSection()` comment block claiming
    it "Uses the exact same `##Enabled_` + name checkbox-ID scheme
    `BuildPassRow()` above already uses (rather than
    `ImGui::PushID()`/`PopID()` per row)... pick ONE convention... do not
    mix both styles in the same file" was replaced with an accurate one
    ("Uses `ScopedUniqueId` (see `ImGuiUniqueId.h`) exactly like
    `BuildPassRow()` above - `task_manager/editor-core-separation-10`
    campaign, PHASE2.").
  - Every other line in every retrofitted function (all remaining table
    columns, tooltip text, priority/enabled logic, `[v3]` label,
    `BuildResourceTable`, `BuildGpuDrivenBatchesSection`,
    `RenderGraphPanel::Build()`) is byte-for-byte unchanged.

No deviation from `PHASE1_COMPLETION_REPORT.md`'s handoff notes was needed —
`ImGuiIdConflictGuard::CheckCurrentIdScope(const char*, const char*)`'s
signature matched `ImGuiUniqueId.cpp`'s call exactly, no adjustment required.

## Verification performed

1. **Configure**: re-ran `cmake -S . -B build` (root `CMakeLists.txt` file
   list changed) — succeeded, only the same pre-existing/unrelated warnings
   already seen in PHASE1 (MinGW static-CRT plugin-linkage no-ops, KTX
   git-describe fallback), nothing related to this phase's own files.
2. **Incremental build (`GreatTamanaEditor`)**:
   `cmake --build build --target GreatTamanaEditor` — recompiled exactly the
   expected 2 translation units (`ImGuiUniqueId.cpp`,
   `RenderGraphPanel.cpp`), relinked `gte_editor.a` and
   `GreatTamanaEditor.exe`. Zero warnings, zero errors.
3. **Incremental build (`GreatTamanaEngineTests`)**: also rebuilt cleanly
   (no test file changed this phase, so this confirms nothing else in the
   tree references the retrofitted signatures in an incompatible way) —
   link-only step, zero errors.
4. **`search_in_dir` sweep #1**: `"##Enabled_"` under `src/Editor/` — **zero
   matches** (both old string-concatenated checkbox IDs are gone).
5. **`search_in_dir` sweep #2**: `ImGui::PushID(entry.name` under
   `src/Editor/` — **zero real call-site matches** (the only hit is inside
   this file's own explanatory comment describing what the old code used to
   do, not a live call).
6. **Live HTTP-driven proof**:
   - Launched `build\GreatTamanaEditor.exe` via `run_app_background` (PID
     18352).
   - `GET /list_tabs` confirmed the exact tab name is `"Render Graph"`
     (with a literal space, URL-encoded as `%20` in the query string).
   - `GET /activate_tab?name=Render%20Graph` → `{"activated_tab":"Render
     Graph","success":true}`.
   - `GET /get_swapchain` → a real 246 KB PNG screenshot showing the
     Editor's docked layout with the "Render Graph" tab active at the
     bottom, its Pause row, "GPU-Driven Batches", and "Plugin Render
     Features" sections rendering correctly and interactively (checkboxes,
     +/- priority steppers, `[v3]` badges all visible). **Honest,
     documented limitation**: the panel's own "Offscreen Regime (Game View
     + Scene View)" pass table (where the two duplicate Atmosphere pass
     rows live) sits further down inside this panel's own scrollable
     region than this fixed-resolution screenshot's visible viewport
     reaches — there is no HTTP endpoint to scroll a panel or resize/
     maximize the docked layout, so this screenshot alone cannot visually
     re-confirm the duplicate-name rows by eye. This is the exact same
     class of disclosed limitation the phase doc itself anticipated for
     "simulate a mouse hover" — extended here to "scroll a panel" for the
     same underlying reason (no such HTTP command exists yet).
   - Let the app run for ~3 real seconds (`run_shell timeout /t 3`) so
     `ImGuiIdConflictGuard`'s per-frame logic executed many times against
     the real, live "Render Graph" panel (including its real duplicate-name
     Offscreen Regime table, which was actively being built every frame
     regardless of scroll position/visibility — `ImGui::TableNextRow()`/
     `ScopedUniqueId` execute for every row whether or not that row is
     currently scrolled into view).
   - `GET /get_logs?category=ImGuiIdConflict` → `{"count":0,"entries":[],
     "latest_id":31,"logging_enabled":true}` — **EMPTY**, proving no
     conflict was detected against the real, live, duplicate-named
     Atmosphere pass data during this session.
   - `GET /get_logs?min_level=Warning` → 18 entries, all pre-existing/
     unrelated (plugin CRT-linkage notices, render-feature priority
     tie-breaks, GPU-timing-slot-budget exhaustion for demo plugin passes)
     — **zero** `ImGuiIdConflict` entries among them.
   - `stop_app_background(pid: 18352)` closed the process cleanly.

No full build / no full `ctest` regression was run this phase, per this
campaign's own Locked Design Decision #8 (reserved for PHASE4).

## Notes / deviations for PHASE3

- None — every code listing in
  `PHASE2_SCOPED_UNIQUE_ID_HELPER_AND_RENDERGRAPHPANEL_FIX.md` was used
  verbatim. `RenderGraphPanel.cpp`'s own internal structure matched the
  phase doc's expectations exactly (only minor, expected line-number drift
  from the doc's own "currently ~line N" estimates — no structural
  surprise).
- Confirmed working HTTP call shapes for PHASE3/PHASE4 to reuse directly,
  with no rediscovery needed:
  - `GET /list_tabs` — lists every known panel name (includes `"Log"` and
    `"Demo Plugin Panel"` too, beyond the six named in `AGENTS.md`'s
    Networking section — this Editor session has demo plugins loaded).
  - `GET /activate_tab?name=Render%20Graph` — the `name` query parameter
    needs URL-encoding for the embedded space (`%20`); returns
    `{"activated_tab":"...","success":true}`.
  - `GET /get_swapchain` — returns a real PNG image directly.
  - `GET /get_logs?category=ImGuiIdConflict` — the exact filter this whole
    campaign is built around; returns `{"count":N,"entries":[...],
    "latest_id":N,"logging_enabled":true}`.
- **For PHASE4's own final live proof**: if a byte-for-byte visual
  confirmation of the duplicate Atmosphere pass rows themselves (not just
  the absence of a log entry) is ever wanted, note there is currently no
  HTTP mechanism to scroll an ImGui panel or resize/maximize the docked
  layout to bring an off-screen table row into a screenshot's visible
  viewport — this is a real, standing gap in the Networking surface
  (`AGENTS.md`), not something specific to this campaign, and is NOT
  something any phase of this campaign is scoped to fix.
- `ScopedUniqueId` (this phase) is complete, final, and ready for PHASE3 to
  reuse verbatim across every other inventoried call site
  (`ProjectPanel.cpp` x3, `HierarchyPanel.cpp`, `InspectorPanel.cpp` x2,
  `BoneViewerWindow.cpp` x3) with the exact same
  `"<File>::<Function>"`-style `debugContext` + most-meaningful-available
  `debugKey` pattern used here.
