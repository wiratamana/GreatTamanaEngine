# ImGui widget ID uniqueness
> How to build unique widget IDs inside loops in editor panels. For anyone who writes ImGui code.
> Verified against code: 2026-10-30

## TL;DR
- ImGui makes a widget ID from the ID stack plus the label. Two widgets with the same ID share state.
- Data strings are not unique. Two render passes can have the same name. Never build an ID from a name alone.
- In a loop over runtime data, enter the scope with `gte::ScopedUniqueId`, keyed by the loop index.
- A conflict is logged once as `ImGuiIdConflict`. ImGui also draws a red box on the widgets.

## Rules
1. A widget built in a loop over runtime data MUST use `gte::ScopedUniqueId(index, context, debugKey)`.
2. `index` must differ for every iteration of that loop in one frame.
3. `debugContext` must be a string literal. `debugKey` is only for logs and the ImGui Item Picker.
4. NEVER use `ImGui::PushID(name)` alone for a row built from data.
5. Keep `io.ConfigDebugHighlightIdConflicts` at its default (`true`). It is the last safety net.

## Do and Don't
```cpp
// Do: the index is unique, whatever the names are.
for (std::size_t i = 0; i < items.size(); ++i) {
    gte::ScopedUniqueId idScope(static_cast<int>(i), "MyPanel::BuildRow", items[i].name.c_str());
    ImGui::Checkbox("##Enabled", &items[i].enabled);
}

// Don't: two rows with the same name collide.
ImGui::PushID(items[i].name.c_str());
ImGui::Checkbox(("##Enabled_" + items[i].name).c_str(), &items[i].enabled);
ImGui::PopID();
```

## How it works
| Layer | File | Role |
|---|---|---|
| `ScopedUniqueId` | `src/Editor/ImGuiUniqueId.h` | Pushes `index`, then `debugKey`. Pops both in the destructor. Not copyable. |
| `ImGuiIdConflictTracker` | `src/Editor/ImGuiIdConflictTracker.h` | Plain class. Checks `uint32_t` IDs. No ImGui, no logger. |
| `ImGuiIdConflictGuard` | `src/Editor/ImGuiIdConflictGuard.h` | Main thread singleton. Resets per frame in `ImGuiEditorLayer::NewFrame()`. |
| ImGui highlight | Dear ImGui | Red box and "conflicting ID" hover text. |

- The guard logs `GTE_LOG_ERROR("ImGuiIdConflict", ...)` when an ID starts to conflict. It stays silent while the conflict continues.
- It logs again if the conflict clears for a frame and then returns. It never asserts or throws.
- Find conflicts with `GET /get_logs?category=ImGuiIdConflict` or the Log panel.

## Checks
- `tests/Editor/ImGuiIdConflictTrackerTests.cpp` covers the tracker. The guard needs a live ImGui context, so it has no test.

## Gotchas
- Pass names are not unique per view. Atmosphere declares `AtmosphereSkyViewLutPass` once for each view.
- The pass toggle registry is keyed by name, so one checkbox controls both views. A UI list can still see the name twice.
- Two sites still call `ImGui::PushID` with a name: `DrawPassTreeRow` and `BuildGraphCanvas` in `Panels/RenderGraphPanel.cpp`. New code there must use `ScopedUniqueId`.
- A fixed number is fine as `index` for separate sections (for example `ClusterViewerWindow::Toolbar`), as long as it differs inside one window.

## Related
- [Logging](../debugging/logging.md)
- [Editor](../architecture/editor.md)
- [Contributing](contributing.md)
