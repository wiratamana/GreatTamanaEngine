# ImGui Widget ID Uniqueness

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

## The problem

Dear ImGui builds a widget's ID from the ID stack plus the label. Two widgets
with the same ID in one frame silently share state. Dear ImGui's own safety net
(`io.ConfigDebugHighlightIdConflicts`, default `true`) then draws a red rectangle
around both widgets and shows "Programmer error: N visible items with conflicting
ID!" on hover. It looks like a rendering bug and is slow to diagnose.

Pass names are not unique per view. The Atmosphere feature declares
`"AtmosphereSkyViewLutPass"` and `"AtmosphereAerialPerspectiveVolumePass"` once
for the Game view and once for the Scene view, under the same literal names
(`src/Features/Atmosphere/AtmosphereFeature.cpp`, `ViewLuts()`). The toggle
registry is keyed by pass name, so one checkbox controls both. Any UI list that
builds an ID from a pass name alone can therefore see the same name twice in one
frame. Never assume a data string is unique.

## The system

### Layer 1: `gte::ScopedUniqueId`

`src/Editor/ImGuiUniqueId.h/.cpp` is the way to enter a per-iteration ID scope for
any widget built inside a loop over runtime data. Construct it with the loop's own
iteration index, a context string, and an optional debug key. It pushes the index
(this gives the uniqueness), then pushes the debug key (only for readability in
Dear ImGui's Item Picker and in log messages). It pops both in its destructor.
It is a scope guard: not copyable, not movable.

```cpp
// GOOD: the index differs for every iteration, whatever the data contains.
for (std::size_t i = 0; i < items.size(); ++i) {
    gte::ScopedUniqueId idScope(static_cast<int>(i), "MyPanel::BuildRow", items[i].name.c_str());
    ImGui::Checkbox("##Enabled", &items[i].enabled);
}

// BAD: two rows with the same name collide.
ImGui::PushID(items[i].name.c_str());
ImGui::Checkbox(("##Enabled_" + items[i].name).c_str(), &items[i].enabled);
ImGui::PopID();
```

### Layer 2: conflict detection

- `src/Editor/ImGuiIdConflictTracker.h/.cpp`: a small class with no ImGui or
  logger dependency. It works on plain `std::uint32_t` IDs (`Reset()`,
  `RegisterAndCheckConflict()`, `DistinctIdCount()`).
- `src/Editor/ImGuiIdConflictGuard.h/.cpp`: a process-global, main-thread-only
  singleton on top of it.
  - `BeginFrame()` runs once per UI frame from `ImGuiEditorLayer::NewFrame()`,
    before any panel builds a widget. It resets the per-frame tracker. It does
    not reset the set of ongoing conflicts.
  - `CheckCurrentIdScope(debugContext, debugKey)` runs in the `ScopedUniqueId`
    constructor. It reads the current ID with `ImGui::GetID("")` and checks it
    against this frame's tracker.

A conflict is logged with `GTE_LOG_ERROR("ImGuiIdConflict", ...)`, visible in the
Editor "Log" panel and at `GET /get_logs?category=ImGuiIdConflict`. It is logged
once when an ID starts to conflict. While the same conflict continues, the log
stays silent. When the conflict has cleared for a frame and then comes back, it
is logged again. The check never asserts, throws or crashes.

### Layer 3: Dear ImGui's own highlight

`io.ConfigDebugHighlightIdConflicts` stays at its default (`true`). It is the last
line of defense for code that does not use `ScopedUniqueId`.

## The rule

Any ImGui widget built inside a loop over runtime data MUST enter its ID scope with
`gte::ScopedUniqueId`, keyed by the loop's own iteration index. Do not build a
widget ID from a name, label or path alone.

## Where it is used

`ScopedUniqueId` scopes exist in:

| File | Function(s) |
|---|---|
| `Panels/ProjectPanel.cpp` | `RenderLeftPaneFolder()`, `RenderBreadcrumb()`, `RenderRightPaneEntry()` |
| `Panels/HierarchyPanel.cpp` | `RenderEntityNode()` |
| `Panels/InspectorPanel.cpp` | Verlet chain and joint loops |
| `Panels/RenderFeaturesPanel.cpp` | `BuildRenderFeaturesSection()` |
| `BoneViewerWindow.cpp` | `RenderBoneTreeNode()`, `RenderFlatPartRow()`, `RenderVerletChainNode()` |

`Panels/RenderGraphPanel.cpp` still calls `ImGui::PushID` with a pass name in
`DrawPassTreeRow()` (one row per toggle-registry entry) and in
`BuildGraphCanvas()` (one button per graph node). Those two sites do not use
`ScopedUniqueId`. New code there must.
