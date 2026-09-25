# PHASE3 — Editor-Wide Retrofit Sweep — COMPLETION REPORT

**Campaign:** `editor-core-separation-10` — "ImGui Widget ID Uniqueness"
**Phase:** PHASE3 of 4
**Status:** DONE

---

## What was done

`gte::ScopedUniqueId` (PHASE2) needed no changes — this phase is a pure,
mechanical retrofit of every REMAINING `ImGui::PushID(...)` call site
inventoried in `PHASE0_MASTER_STRATEGY.md` section 2.4, exactly as specified
by `PHASE3_EDITOR_WIDE_RETROFIT_SWEEP.md`, with no deviation from the phase's
own code listings.

### Edited files (four, as planned — no new files this phase)

- **`src/Editor/Panels/ProjectPanel.h`** — two method declarations gained a
  new leading index parameter: `RenderLeftPaneFolder(EditorContext&, int
  siblingIndex, const ProjectEntry&)` and `RenderRightPaneEntry(EditorContext&,
  int entryIndex, const ProjectEntry&)`.
- **`src/Editor/Panels/ProjectPanel.cpp`** — added `#include
  "../ImGuiUniqueId.h"`, retrofitted THREE call sites:
  1. `RenderLeftPaneFolder()` — was `ImGui::PushID(entry.relativePath.c_str())`
     alone; now `ScopedUniqueId idScope(siblingIndex,
     "ProjectPanel::RenderLeftPaneFolder", entry.relativePath.c_str())`, fed by
     both its own recursive loop over `entry.children` and
     `RenderLeftPane()`'s top-level loop over `m_tree`, both converted from a
     plain range-`for` to an index-`for`.
  2. `RenderBreadcrumb()` — was `ImGui::PushID(segmentIndex++)`; now wrapped in
     an explicit-braces `{ ScopedUniqueId idScope(segmentIndex++,
     "ProjectPanel::RenderBreadcrumb", segment.c_str()); ... }` block per the
     phase doc's own explicit-braces guidance (so the scope pops before the
     `while` loop's next iteration, matching every other retrofit's shape).
  3. `RenderRightPaneEntry()` — was `ImGui::PushID(entry.relativePath.c_str())`
     alone; now `ScopedUniqueId idScope(entryIndex,
     "ProjectPanel::RenderRightPaneEntry", entry.relativePath.c_str())`, fed by
     `RenderRightPane()`'s loop over `*children`, converted to an index-`for`.
  - Every manual `ImGui::PopID()` at the end of each retrofitted function was
    removed (destructor handles it) and replaced with an explanatory comment.
  - No other line in any of these three functions (drag-and-drop, tooltip,
    selection logic) was touched.
- **`src/Editor/Panels/HierarchyPanel.cpp`** — added `#include
  "../ImGuiUniqueId.h"`, retrofitted `RenderEntityNode()`'s
  `ImGui::PushID(static_cast<int>(entity.index))` into `ScopedUniqueId
  idScope(static_cast<int>(entity.index), "HierarchyPanel::RenderEntityNode")`
  (no `debugKey` argument — matches the phase doc's own reasoning: `label` is
  computed a few lines later and reordering the function just to feed it in
  wasn't worth the churn). Manual `ImGui::PopID()` removed.
- **`src/Editor/Panels/InspectorPanel.cpp`** — added `#include
  "../ImGuiUniqueId.h"`, retrofitted the nested Verlet chain/joint loop: outer
  `chainIndex` loop now opens `ScopedUniqueId chainIdScope(...)`, inner
  `jointIndex` loop opens a DISTINCTLY-NAMED `ScopedUniqueId jointIdScope(...)`
  (per the phase doc's explicit warning against reusing `idScope` for both
  nested scopes). Both manual `ImGui::PopID()` calls removed.
- **`src/Editor/BoneViewerWindow.cpp`** — added `#include "ImGuiUniqueId.h"`
  (same-directory include, no `../`), retrofitted all three call sites:
  `RenderBoneTreeNode()` (`ScopedUniqueId idScope(boneIndex,
  "BoneViewerWindow::RenderBoneTreeNode", bone.name.c_str())`),
  `RenderFlatPartRow()` (`ScopedUniqueId idScope(index,
  "BoneViewerWindow::RenderFlatPartRow", name.c_str())`), and
  `RenderVerletChainNode()` (`ScopedUniqueId idScope(chainIndex,
  "BoneViewerWindow::RenderVerletChainNode")` — no `debugKey`, matching the
  phase doc's own note that no cheap string is available before `header` is
  built). All three manual `ImGui::PopID()` calls removed.

No `CMakeLists.txt`/`tests/CMakeLists.txt` changes were needed this phase —
no new files were created (`ScopedUniqueId` already registered by PHASE2).

## The "grep gate" (this phase's own acceptance criterion) — verbatim output

```
search_in_dir path="...\src\Editor" content="ImGui::PushID" filter="*.cpp"

Found 6 match(es) in 4 file(s) for "ImGui::PushID" (filter: *.cpp):

ImGuiUniqueId.cpp
[15] ImGui::PushID(index);
[23] ImGui::PushID(debugKey);

Panels\HierarchyPanel.cpp
[72] // ImGui::PushID(static_cast<int>(entity.index)) alone. entity.index is

Panels\ProjectPanel.cpp
[137] // ImGui::PushID(entry.relativePath.c_str()) alone. A relative
[235] // ImGui::PushID(entry.relativePath.c_str()) alone.

Panels\RenderGraphPanel.cpp
[305] // bare ImGui::PushID(entry.name.c_str())/PopID() pair with the same
```

```
search_in_dir path="...\src\Editor" content="ImGui::PopID" filter="*.cpp"

Found 11 match(es) in 6 file(s) for "ImGui::PopID" (filter: *.cpp):

BoneViewerWindow.cpp
[632] // No manual ImGui::PopID(); anymore.
[686] // No manual ImGui::PopID(); anymore.
[751] // No manual ImGui::PopID(); anymore.

ImGuiUniqueId.cpp
[32] ImGui::PopID();
[34] ImGui::PopID();

Panels\HierarchyPanel.cpp
[184] // No manual ImGui::PopID(); anymore - ScopedUniqueId's destructor handles it.

Panels\InspectorPanel.cpp
[773] // No manual ImGui::PopID(); anymore.
[804] // No manual ImGui::PopID(); anymore.

Panels\ProjectPanel.cpp
[168] // No manual ImGui::PopID() anymore - ScopedUniqueId's destructor
[279] // No manual ImGui::PopID() anymore - ScopedUniqueId's destructor handles it.

Panels\RenderGraphPanel.cpp
[338] // No manual ImGui::PopID() anymore - ScopedUniqueId's destructor
```

**Result: PASS.** Every real (non-comment) call to `ImGui::PushID`/
`ImGui::PopID` anywhere under `src/Editor/` lives ONLY inside
`ImGuiUniqueId.cpp` itself (PHASE2's own implementation, which legitimately
calls the raw functions so `ScopedUniqueId` can wrap them). Every other match
is a stale-code-removed explanatory comment (either pre-existing from
PHASE2's own retrofit of `RenderGraphPanel.cpp`, or newly added by this
phase) — no live call site anywhere else.

A final sanity re-check of PHASE2's own gate also still passes:

```
search_in_dir path="...\src\Editor" content="##Enabled_" filter="*.cpp"
No matches found.
```

## Verification performed

1. **Incremental build (`GreatTamanaEditor`)**:
   `cmake --build build --target GreatTamanaEditor` — recompiled exactly the
   5 expected translation units (`ProjectPanel.cpp`, `HierarchyPanel.cpp`,
   `InspectorPanel.cpp`, `BoneViewerWindow.cpp`, plus `ImGuiEditorLayer.cpp`
   — relinked as a dependency of the same static library), relinked
   `gte_editor.a` and `GreatTamanaEditor.exe`. Zero warnings, zero errors.
2. **Incremental build (`GreatTamanaEngineTests`)** — link-only step (no test
   file touched this phase), zero errors, confirming nothing else in the tree
   references any of the four retrofitted signatures in an incompatible way.
3. **Live, HTTP-driven smoke test** (per this campaign's own operating
   rules):
   - Launched `build\GreatTamanaEditor.exe` via `run_app_background`
     (PID 19380).
   - `GET /list_tabs` confirmed the known tab set (`Hierarchy`, `Inspector`,
     `Project`, ... — unchanged from PHASE2's own discovery).
   - `POST /instantiate_primitive` (payload `{"shape":"...","name":"..."}` —
     both fields are required; an earlier attempt with only `shape` returned
     `400 missing or invalid required field: name`, corrected immediately)
     spawned three primitives (`TestCube1`, `TestSphere1`, `TestCapsule1`) so
     "Hierarchy"'s retrofitted `RenderEntityNode()` had real rows beyond just
     the default camera entity to render this session.
   - `POST /clear_logs` reset the log buffer, then `GET
     /activate_tab?name=Hierarchy` brought "Hierarchy" to the front.
   - `GET /get_swapchain` → a real PNG screenshot showing "Hierarchy"
     correctly listing `Entity 0 (Camera)`, `TestCube1`, `TestSphere1`,
     `TestCapsule1` (all four rows render distinctly, no visual corruption),
     "Inspector" showing "No entity selected." (untouched, expected — no
     entity was selected this session), and "Project" showing its own
     (currently empty) tree — all three retrofitted panels rendering
     correctly.
   - `GET /activate_tab?name=Render%20Graph` re-confirmed PHASE2's own fix is
     still intact (no regression from this phase's changes elsewhere in the
     same `gte_editor` library).
   - Let the app run a few real seconds so `ImGuiIdConflictGuard`'s per-frame
     logic executed many times against the real, live retrofitted panels.
   - `GET /get_logs?category=ImGuiIdConflict` → `{"count":0,"entries":[],
     "latest_id":31,"logging_enabled":true}` — **EMPTY**.
   - `GET /get_logs?min_level=Warning` → also **EMPTY** (`count:0`) — this
     session had zero warnings/errors of any kind, not just zero
     `ImGuiIdConflict` entries.
   - `stop_app_background(pid: 19380)` closed the process cleanly.
   - **Honest, disclosed limitation** (same class as PHASE2's own): there is
     still no HTTP endpoint to simulate a mouse click/hover, so this smoke
     test could not click a Hierarchy row to also visually exercise
     "Inspector" with real component data, or open "Bone Viewer" (which has
     no listed tab name — it's a floating window opened by selecting a
     rigged mesh entity in the Editor, not through `/activate_tab`). The
     absence of any `ImGuiIdConflict`/warning log entry during a real live
     session against all three retrofitted, HTTP-reachable panels, combined
     with the "grep gate" proving the mechanical retrofit is complete and
     uniform, is the authoritative proof for this phase.

No full build / no full `ctest` regression was run this phase, per this
campaign's own Locked Design Decision #8 (reserved for PHASE4).

## Notes / deviations for PHASE4

- None — every code listing in `PHASE3_EDITOR_WIDE_RETROFIT_SWEEP.md` was
  used verbatim. All four files' internal structure matched the phase doc's
  expectations exactly (the line numbers had NOT drifted at all from the
  phase doc's own estimates, since none of these four files were touched by
  PHASE1/PHASE2, exactly as that phase doc itself predicted).
- `POST /instantiate_primitive` requires BOTH a `"shape"` and a `"name"`
  field in its JSON payload (discovered during this phase's own live smoke
  test) — useful for PHASE4's own final live proof if it also wants to spawn
  test content.
- After this phase, `ScopedUniqueId` is the ONLY way any ImGui code anywhere
  under `src/Editor/` enters a per-iteration ID scope — code, not just
  intent, now matches Locked Design Decision #1 (LDD1, whole-Editor scope).
  PHASE4's job is to make this binding on all FUTURE code too
  (`docs/conventions/imgui-id-uniqueness.md` + an `AGENTS.md` section), then
  prove the whole campaign with one full clean build and one full `ctest`
  regression pass.
