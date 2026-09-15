# PHASE4 — Left-hand event tree pane + draggable splitter

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1 (`FrameDebuggerSnapshot`/`FrameDebuggerEventNode`),
PHASE3 (the `m_enabled`-gated content area exists to build into).
Touches: `src/Editor/Panels/FrameDebuggerPanel.h/.cpp` only.

## Step 1: The Goal

Build the window's left-hand pane: a draggable-width child region
containing the event tree. This campaign, it always shows exactly one
line — **"No frame captured yet."** — because
`BuildPlaceholderFrameDebuggerSnapshot()` always returns zero root nodes
(Locked Design Decision #2). Alongside that, write the **fully real**
recursive tree-row renderer (`RenderEventNode()`) that WOULD draw a real
hierarchy if `snapshot.rootNodes` were ever non-empty — unreachable in
practice this campaign, but ready for a future campaign to exercise for
free the moment it starts returning real data.

## Step 2: The Situation

- The exact draggable-splitter pattern to copy verbatim (adapted to this
  panel's own names) is `src/Editor/Panels/ProjectPanel.cpp`, lines
  ~381–410 (already confirmed present in this codebase during strategy
  research): a `constexpr float kSplitterWidth = 6.0f;` file-local
  constant, a persisted `float m_leftPaneWidth` member (clamped every
  frame against `ImGui::GetContentRegionAvail().x` so it can never grow
  wider than the window or leave the right pane unusably thin), then:
  ```cpp
  ImGui::BeginChild("...LeftPane", ImVec2(m_leftPaneWidth, paneAreaHeight), true);
  /* left content */
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::Button("##...Splitter", ImVec2(kSplitterWidth, paneAreaHeight));
  if (ImGui::IsItemActive()) { m_leftPaneWidth += ImGui::GetIO().MouseDelta.x; }
  if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }
  ImGui::SameLine();
  ImGui::BeginChild("...RightPane", ImVec2(0.0f, paneAreaHeight), true);
  /* right content */
  ImGui::EndChild();
  ```
- The exact recursive indented-tree-row rendering pattern to mirror is
  `src/Editor/BoneViewerWindow.cpp`'s `RenderBoneTreeNode()` (declared in
  `BoneViewerWindow.h`) — a leaf uses `ImGui::Selectable`-style behavior,
  a node with children uses `ImGui::TreeNodeEx`. This phase's version is
  simpler (no search filter, no multi-select, no double-click camera
  recentering) — just: group nodes are expandable tree headers, leaf
  nodes are selectable rows that set `m_selectedEventIndex` on click.
- `FrameDebuggerEventNode` (PHASE1) already carries everything needed:
  `name`, `isDrawCall`, `eventIndex`, `children`.

## Step 3: The Plan

### 3.1 `FrameDebuggerPanel.h` — new members/methods

```cpp
private:
    ...
    void BuildEventTreePane(const FrameDebuggerSnapshot& snapshot);
    void RenderEventNode(const FrameDebuggerEventNode& node);

    // Persisted (across frames) pixel width of the left-hand event tree
    // pane, adjusted live by dragging the splitter - mirrors
    // ProjectPanel::m_leftPaneWidth's own convention exactly (see
    // Panels/ProjectPanel.cpp).
    float m_leftPaneWidth = 320.0f;

    // The currently-selected leaf event's global index (matches
    // FrameDebuggerEventNode::eventIndex), or -1 if nothing is selected.
    // Set by RenderEventNode()'s own click handling below; read by
    // BuildEventDetailsSection() (PHASE6) via FindEventDetailsByIndex().
    // Always -1 in practice this campaign (there is never a leaf node to
    // click, since the placeholder snapshot's rootNodes is always
    // empty), but written for real so PHASE6 needs no further plumbing.
    int m_selectedEventIndex = -1;
```

### 3.2 `FrameDebuggerPanel.cpp` — splitter + tree pane

```cpp
namespace {
constexpr float kSplitterWidth = 6.0f;
} // namespace

void FrameDebuggerPanel::RenderEventNode(const FrameDebuggerEventNode& node)
{
    if (!node.isDrawCall) {
        // A group node (e.g. "Drawing", "Render.OpaqueGeometry") - an
        // expandable tree header; default-open so a shallow real
        // hierarchy is legible without extra clicking, mirroring the
        // reference screenshot's own mostly-expanded look.
        const bool open = ImGui::TreeNodeEx(node.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (open) {
            for (const FrameDebuggerEventNode& child : node.children) {
                RenderEventNode(child);
            }
            ImGui::TreePop();
        }
        return;
    }

    // A leaf draw-call row - a single selectable line, highlighted when
    // it matches m_selectedEventIndex; clicking it selects it (feeding
    // PHASE6's BuildEventDetailsSection() via FindEventDetailsByIndex()).
    const bool isSelected = (node.eventIndex == m_selectedEventIndex);
    if (ImGui::Selectable(node.name.c_str(), isSelected)) {
        m_selectedEventIndex = node.eventIndex;
    }
}

void FrameDebuggerPanel::BuildEventTreePane(const FrameDebuggerSnapshot& snapshot)
{
    if (snapshot.rootNodes.empty()) {
        // Always this branch this campaign - see PHASE0_MASTER_STRATEGY.md's
        // Locked Design Decision #2: zero fake/mock rows, ever.
        ImGui::TextDisabled("No frame captured yet.");
        return;
    }

    // Unreachable in practice this campaign (rootNodes is always
    // empty), but fully correct - ready for a future real-capture
    // campaign to exercise for free.
    for (const FrameDebuggerEventNode& root : snapshot.rootNodes) {
        RenderEventNode(root);
    }
}
```

### 3.3 `FrameDebuggerPanel::Build()` — wire the splitter in

Replace PHASE3's `"(event tree + inspector - added in a later phase..."`
placeholder line (inside the `else` branch, when `m_enabled` is true)
with:

```cpp
    } else {
        const FrameDebuggerSnapshot snapshot = BuildPlaceholderFrameDebuggerSnapshot();

        const float totalAvailWidth = ImGui::GetContentRegionAvail().x;
        const float paneAreaHeight = ImGui::GetContentRegionAvail().y;
        const float maxLeftWidth = std::max(120.0f, totalAvailWidth - 200.0f - kSplitterWidth);
        m_leftPaneWidth = std::clamp(m_leftPaneWidth, 120.0f, maxLeftWidth);

        ImGui::BeginChild("FrameDebuggerEventTree", ImVec2(m_leftPaneWidth, paneAreaHeight), true);
        BuildEventTreePane(snapshot);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::Button("##FrameDebuggerSplitter", ImVec2(kSplitterWidth, paneAreaHeight));
        if (ImGui::IsItemActive()) {
            m_leftPaneWidth += ImGui::GetIO().MouseDelta.x;
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        ImGui::SameLine();

        ImGui::BeginChild("FrameDebuggerInspector", ImVec2(0.0f, paneAreaHeight), true);
        // PHASE5/PHASE6 fill this in.
        ImGui::TextDisabled("(inspector pane - added in a later phase of this campaign)");
        ImGui::EndChild();
    }
```

`<algorithm>` (for `std::clamp`) needs adding to `FrameDebuggerPanel.cpp`'s
includes if not already transitively available.

### 3.4 Compile check

`cmake --build build --target GreatTamanaEngine`, then a live smoke test
confirming: with "Enable" checked, the body now shows a left pane reading
"No frame captured yet." and a draggable splitter (cursor changes to a
horizontal resize icon while hovering/dragging the thin bar) that
actually resizes the left pane, plus a right pane still showing PHASE5's
placeholder line.

### 3.5 File-change inventory (this phase only)

Modified only: `src/Editor/Panels/FrameDebuggerPanel.h`,
`src/Editor/Panels/FrameDebuggerPanel.cpp`.

Write `PHASE4_COMPLETION_REPORT.md` into `task_manager/frame-debugger-2/`
when done, then `git_add`/`git_commit`.
