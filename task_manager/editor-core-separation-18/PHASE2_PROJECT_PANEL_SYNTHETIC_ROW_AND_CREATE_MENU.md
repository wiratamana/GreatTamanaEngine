# PHASE2 — ProjectPanel Synthetic "[Active Project]" Row + Create Submenu

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1_*.md` only for the
`AssetScaffoldKind` enum's existence (forward-declared here, never
`#include`-d in full). Does NOT call `CreateAssetScaffold()` directly —
this phase only sets `EditorContext` fields; `PHASE3_*.md`'s own
`CreateAssetWindow` is what actually calls the capability.

## STEP 1 — The Goal

Inside the EXISTING "Project" panel (`src/Editor/Panels/ProjectPanel.cpp`),
add:
1. A new, read-only, always-visible-when-a-project-is-active row, labeled
   exactly `"[Active Project] <Name>"`, showing a flat, one-level listing
   of that project's own `Assets/` folder contents.
2. A right-click context menu on THAT ROW ONLY (never on the pre-existing
   content-asset tree, never on the new row's own child file listing —
   LDD-CA3, `PHASE0_MASTER_STRATEGY.md`) with a "Create" submenu: "Render
   Pass...", "Compute Shader...", "Vertex/Fragment Shader Pair...".
   Selecting one sets two new `EditorContext` fields (open flag + which
   kind) that `PHASE3_*.md`'s `CreateAssetWindow` reads next frame.

The EXISTING content-asset tree/breadcrumb/drag-drop/Delete-Selected/New
Folder behavior of this panel must not change in any observable way.

## STEP 2 — The Situation (exact, current, re-verified code shape)

Re-read `src/Editor/Panels/ProjectPanel.cpp`/`.h` in full before starting —
this phase's own diff touches `Build()`, adds new private methods, and adds
new private members; getting the insertion points right matters more than
anything else in this phase.

Key facts, confirmed by direct reading (this phase's own "Situation"):
- `Build()`'s overall shape: `EnsureRootAndMaybeRescan()` →
  `ImGui::Begin("Project")` → (if `m_rootExists`) two side-by-side
  `BeginChild` blocks (`"ProjectLeftPane"`, `"ProjectRightPane"`), each
  followed immediately by its own `RenderContextMenu(ctx, "...PaneContextMenu")`
  call, inside the SAME `BeginChild`/`EndChild` pair.
- `RenderContextMenu(EditorContext& ctx, const char* popupId)` uses
  `ImGui::BeginPopupContextWindow(popupId)` — a WINDOW-scoped popup (fires
  on a right-click ANYWHERE inside whichever window/child was most recently
  `Begin()`/`BeginChild()`-ed, not tied to a specific item). This is a
  materially different mechanism from `ImGui::BeginPopupContextItem()`
  (item-scoped, fires only when the immediately-preceding widget was
  right-clicked) — **the master-plan source `.txt` file's own STEP 1 code
  sketch assumed `BeginPopupContextItem` was this panel's existing
  convention; it is not. This phase must not silently mix both mechanisms
  without an explicit mutual-exclusion plan (see STEP 3.3 below) — doing so
  risks BOTH popups opening on the same right-click**, since
  `BeginPopupContextWindow`'s default trigger condition (right mouse button
  released while the window is hovered) does not automatically exclude "the
  cursor happened to also be over a specific item with its own
  `BeginPopupContextItem` call" — these are two independent trigger checks,
  each of which can independently decide to call `ImGui::OpenPopup()` on
  the exact same frame.
- `RenderLeftPane(EditorContext& ctx)` renders one root `TreeNodeEx("Project", ...)`
  node, then recurses `RenderLeftPaneFolder()` for every entry in `m_tree`.
  This is the natural place to add a SECOND, sibling top-level tree node
  for `"[Active Project] <Name>"`.
- `EnsureRootAndMaybeRescan()` is the ONE throttled (500ms) place this panel
  already refreshes its own state every frame it's visible — the natural
  place to ALSO refresh a small, new, second piece of state: the active
  project's own one-level `Assets/` file listing.

## STEP 3 — The Plan (exact code)

### 3.1 — `EditorContext.h` additions

Add near `newProjectWindowOpen`/`openProjectWindowOpen` (same family of
fields):

```cpp
// editor-core-separation-18 campaign (On-Engine Project Workflow plan,
// BIG-STEP 4) - forward declaration only, NOT a full #include of
// Core/EditorCapabilities.h (that header pulls in Logging.h/<filesystem>/
// etc., and EditorContext.h is included by nearly every panel in this
// codebase - see PHASE0_MASTER_STRATEGY.md's own risk register). Legal: a
// plain C++11 scoped enum with the implicit default `int` underlying type
// is used identically on both this forward declaration and the real
// definition (Core/EditorCapabilities.h) - the two declarations are
// therefore compatible, confirmed by the C++ standard's own rule that an
// opaque-enum-declaration and its later definition must agree on the
// (here, both-implicit, both-int) underlying type.
enum class AssetScaffoldKind;
```

```cpp
// editor-core-separation-18 campaign (On-Engine Project Workflow plan,
// BIG-STEP 4), PHASE2 - true whenever CreateAssetWindow.h's floating
// window (PHASE3) is currently open. Set by ProjectPanel's own new
// "[Active Project] <Name>" row's Create submenu (Panels/ProjectPanel.cpp);
// read/cleared the same open/close way as newProjectWindowOpen/
// openProjectWindowOpen above.
bool createAssetWindowOpen = false;

// Which AssetScaffoldKind CreateAssetWindow should scaffold - only
// meaningful while createAssetWindowOpen is true; written at the same time
// as createAssetWindowOpen, immediately before it's set true, never read
// beforehand.
AssetScaffoldKind createAssetWindowPendingKind = static_cast<AssetScaffoldKind>(0); // RenderPass - see .cpp for why a literal 0 is used here instead of the enumerator name.
```

Note on the last line: `EditorContext.h` cannot write
`AssetScaffoldKind::RenderPass` as a default member initializer while only
FORWARD-declaring the enum (an incomplete type has no visible enumerators
yet) — `static_cast<AssetScaffoldKind>(0)` is the standard, correct way to
default-initialize an opaque forward-declared scoped enum to its first
enumerator's underlying value, PROVIDED `RenderPass` really is declared
first with value `0` in `Core/EditorCapabilities.h` (confirmed by PHASE1's
own `enum class AssetScaffoldKind { RenderPass, ComputeShader, ShaderPair
};` — `RenderPass` is listed first, so its implicit value is `0`). Add a
one-line comment at THIS exact spot cross-referencing
`Core/EditorCapabilities.h`'s own enum ordering, so a future reordering of
that enum is not silently forgotten here.

### 3.2 — `ProjectPanel.h` additions (private section)

```cpp
void RenderActiveProjectAssetsRow(EditorContext& ctx);
void RescanActiveProjectAssetsIfNeeded();

// One-level (non-recursive), display-only listing of the active project's
// own Assets/ folder - LDD-CA3 (PHASE0_MASTER_STRATEGY.md): these entries
// are NEVER independently selectable/navigable/drag-droppable, unlike
// m_tree above. Refreshed on the SAME throttle as m_tree (see
// EnsureRootAndMaybeRescan()).
std::vector<std::string> m_activeProjectAssetFileNames;
std::string m_activeProjectNameLastScanned; // "" if none - detects a DIFFERENT project becoming active between rescans, forcing an immediate refresh rather than waiting a full throttle interval.
```

### 3.3 — `ProjectPanel.cpp` — the mutual-exclusion-safe implementation

```cpp
#include "../ActiveProjectAssemblyState.h"
#include "../../Core/EditorCapabilities.h" // AssetScaffoldKind's REAL definition - only .cpp needs this, never the header (see EditorContext.h's own forward-declare comment).
```

```cpp
void ProjectPanel::RescanActiveProjectAssetsIfNeeded()
{
    const ActiveProjectAssemblyInfo active = ActiveProjectAssemblyState::Instance().GetActive();
    if (!active.hasActiveProject) {
        m_activeProjectAssetFileNames.clear();
        m_activeProjectNameLastScanned.clear();
        return;
    }
    // Force an immediate rescan (ignoring the normal 500ms throttle this
    // method is itself called under from EnsureRootAndMaybeRescan()) the
    // instant a DIFFERENT project becomes active - otherwise a fresh
    // Open/Create could show the PREVIOUS project's stale file listing for
    // up to one throttle interval.
    m_activeProjectAssetFileNames.clear();
    std::error_code iterationError;
    for (const auto& entry : std::filesystem::directory_iterator(active.assetsDirectory, iterationError)) {
        if (entry.is_regular_file()) {
            m_activeProjectAssetFileNames.push_back(PathToUtf8(entry.path().filename()));
        }
    }
    std::sort(m_activeProjectAssetFileNames.begin(), m_activeProjectAssetFileNames.end());
    m_activeProjectNameLastScanned = active.name;
}
```

Call this from `EnsureRootAndMaybeRescan()`, inside the SAME throttled
block that already refreshes `m_tree`/`m_assetDatabase` (do not add a
second, independent throttle timer — one rescan cadence for the whole
panel is simpler and matches this file's own existing philosophy):

```cpp
void ProjectPanel::EnsureRootAndMaybeRescan()
{
    const auto now = std::chrono::steady_clock::now();
    if (!m_needsRescan && (now - m_lastScanTime) < kRescanInterval) {
        return;
    }
    m_rootExists = EnsureProjectRootExists(m_rootPath);
    m_tree = m_rootExists ? ScanProjectDirectory(m_rootPath) : std::vector<ProjectEntry>{};
    if (m_rootExists) {
        m_assetDatabase.RefreshFromDirectory(m_rootPath);
    } else {
        m_assetDatabase.Clear();
    }
    ReconcileCurrentFolderAfterRescan();

    // editor-core-separation-18 campaign, PHASE2 - added to the SAME
    // throttled rescan; completely independent filesystem root from
    // m_rootPath above (LDD-PW3, PROJECTWORKFLOW_BIGSTEP_01...txt), never
    // touches m_tree/m_rootExists/m_assetDatabase.
    RescanActiveProjectAssetsIfNeeded();

    m_lastScanTime = now;
    m_needsRescan = false;
}
```

**Mutual-exclusion-safe rendering** — the actual row + its own popup,
rendered as a SIBLING to the existing `"Project"` root node inside
`RenderLeftPane()`:

```cpp
void ProjectPanel::RenderActiveProjectAssetsRow(EditorContext& ctx)
{
    if (m_activeProjectNameLastScanned.empty()) {
        return; // No active project right now - the whole row is absent (Definition of Done item 1, PHASE0_MASTER_STRATEGY.md).
    }

    const std::string label = "[Active Project] " + m_activeProjectNameLastScanned;
    ImGui::TreeNodeEx(label.c_str(),
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow);

    // Item-scoped popup, tied to the tree node widget rendered IMMEDIATELY
    // above - fires ONLY when THIS SPECIFIC row was right-clicked, never
    // from elsewhere in the pane.
    //
    // Mechanically re-verified against this repo's own vendored ImGui
    // source (third_party/imgui/imgui.cpp) rather than assumed: unlike a
    // naive reading of the doc comment on BeginPopupContextItem() might
    // suggest, its trailing `return BeginPopupEx(id, ...)` call is
    // UNCONDITIONAL - it does not merely fire on the frame the popup first
    // opens. BeginPopupEx() renders the popup on EVERY frame this exact
    // call site executes while `id` is still on ImGui's own internal
    // open-popup stack, regardless of whether THIS frame's own
    // IsItemHovered()/IsMouseReleased() trigger condition (which only gates
    // the earlier OpenPopupEx() call) is true. Since
    // RenderActiveProjectAssetsRow() itself runs unconditionally every
    // frame the row exists, the `if` branch below is therefore already
    // entered on EVERY frame this popup is open - including while a nested
    // "Create" submenu is open on top of it - not just the single frame it
    // was first triggered. A separate `else if (ImGui::IsPopupOpen(...))`
    // fallback is therefore unreachable dead code and is deliberately NOT
    // used here.
    if (ImGui::BeginPopupContextItem("ProjectAssemblySourceContextMenu")) {
        // Set on every frame this popup renders (see above) - closes the
        // double-popup hazard deterministically for the FULL lifetime of
        // this popup, not just its opening frame.
        m_suppressPaneContextMenuThisFrame = true;
        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Render Pass...")) {
                ctx.createAssetWindowPendingKind = AssetScaffoldKind::RenderPass;
                ctx.createAssetWindowOpen = true;
            }
            if (ImGui::MenuItem("Compute Shader...")) {
                ctx.createAssetWindowPendingKind = AssetScaffoldKind::ComputeShader;
                ctx.createAssetWindowOpen = true;
            }
            if (ImGui::MenuItem("Vertex/Fragment Shader Pair...")) {
                ctx.createAssetWindowPendingKind = AssetScaffoldKind::ShaderPair;
                ctx.createAssetWindowOpen = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::TreePush("ProjectAssemblySourceChildren");
    for (const std::string& fileName : m_activeProjectAssetFileNames) {
        ImGui::BulletText("%s", fileName.c_str()); // display-only (LDD-CA3) - no Selectable/click/drag handling at all.
    }
    ImGui::TreePop();

    ImGui::TreePop(); // matches the outer TreeNodeEx() above (ImGuiTreeNodeFlags_DefaultOpen means this is always "open" and always needs its matching TreePop()).
}
```

**The guard flag + `RenderContextMenu()`'s own new suppression check**:

```cpp
// New private member, ProjectPanel.h - reset to false at the TOP of every
// Build() call, set true (for the REST of this same frame only) the
// instant RenderActiveProjectAssetsRow()'s own popup opens or is already
// open. Deterministic, mechanical mutual exclusion - never relies on
// hoping ImGui's own internal per-widget click arbitration happens to
// agree between an item-scoped and a window-scoped popup check.
bool m_suppressPaneContextMenuThisFrame = false;
```

```cpp
void ProjectPanel::RenderContextMenu(EditorContext& ctx, const char* popupId)
{
    if (m_suppressPaneContextMenuThisFrame) {
        return; // editor-core-separation-18 campaign, PHASE2 - see RenderActiveProjectAssetsRow()'s own comment for why.
    }
    if (ImGui::BeginPopupContextWindow(popupId)) {
        // ... completely unchanged from here down ...
    }
}
```

```cpp
void ProjectPanel::Build(EditorContext& ctx)
{
    EnsureRootAndMaybeRescan();
    m_suppressPaneContextMenuThisFrame = false; // editor-core-separation-18 campaign, PHASE2 - reset once, at the very top, before either pane renders this frame.

    // ... everything else in this method is UNCHANGED ...
}
```

**Wiring the new row into `RenderLeftPane()`** — call it BEFORE the
existing `"Project"` root node (so the active-project row always appears
first, above the ordinary content-asset tree, mirroring how the master-plan
file describes it as its own distinct "top-level" row):

```cpp
void ProjectPanel::RenderLeftPane(EditorContext& ctx)
{
    RenderActiveProjectAssetsRow(ctx); // editor-core-separation-18 campaign, PHASE2 - rendered BEFORE the existing "Project" root node below; a no-op row (returns immediately) when no project is active.

    ImGuiTreeNodeFlags rootFlags = ...; // unchanged from here down
    ...
}
```

New includes needed in `ProjectPanel.cpp`: `<algorithm>` (for
`std::sort`), if not already present.

## STEP 4 — Live verification for THIS phase (screenshot-based, per the
master task's own "make use of the engine's network debugging features"
instruction)

1. Build, launch `GreatTamanaEditor.exe` via `run_app_background`.
2. `gte_send_request("/get_swapchain")` with NO active project set — the
   "Project" panel must show its ordinary tree with NO
   "[Active Project]" row anywhere.
3. `POST /project_assembly/create_project?name=<Scratch>` (the EXISTING
   BIG-STEP 2 route — no code from this phase needed to exercise it) to get
   a real active project without needing PHASE3's own UI yet.
4. `gte_send_request("/get_swapchain")` again — confirm the new
   `"[Active Project] <Scratch>"` row is now visible, showing
   `<Scratch>Game.cpp` as its one bulleted child (the scaffold BIG-STEP 2
   already writes).
5. Right-click that row live is not screenshot-drivable over HTTP (the same
   pre-authorized, already-twice-disclosed gap `editor-core-separation-16`/
   `-17` each already hit for their own floating windows — restate it here
   rather than re-discovering it) — substitute a direct source-code read
   confirming the "Create" submenu's three `MenuItem` calls compile and are
   reachable, plus confirm (via a second HTTP screenshot) that the ordinary
   "New Folder"/"Delete Selected" menu on the PRE-EXISTING tree still opens
   normally by right-clicking there instead (this at least confirms the new
   `m_suppressPaneContextMenuThisFrame` guard doesn't accidentally suppress
   the OLD menu everywhere, only during the new popup's own open window —
   this can be checked by right-clicking a normal folder row and confirming
   "New Folder" still fires via a follow-up `POST` folder-creation-adjacent
   action, e.g. observing a resulting file-count change through
   `gte_send_request` against a subsequent panel screenshot, since there is
   no direct "click here" HTTP primitive).
6. Clean up the scratch project folder afterward (delete
   `Projects/<Scratch>/`, do NOT leave it committed).

## STEP 5 — Fast compile check

`cmake --build build --target GreatTamanaEditor` (or the actual target
name for the full Editor executable — confirm via root `CMakeLists.txt`).
No new tests are added in this phase (pure UI wiring) — confirm the
existing `ProjectPanelDataTests.cpp` (a DIFFERENT file, `tests/Editor/`,
testing `ProjectPanelData.h`'s free functions, not `ProjectPanel` the class
itself) still compiles and passes unmodified, since this phase does not
touch `ProjectPanelData.h` at all.
