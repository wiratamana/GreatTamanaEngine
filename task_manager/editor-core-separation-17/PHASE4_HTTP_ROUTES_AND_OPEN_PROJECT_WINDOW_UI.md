# PHASE4 — HTTP Routes + OpenProjectWindow UI + Menu Wiring
## editor-core-separation-17 (On-Engine Project Workflow, BIG-STEP 3 "Open Project")

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.
Depends on: PHASE3 (`OpenProjectAssembly()`/`OpenProjectAssemblyOnMainThread()`/
`ListProjectAssemblies()` must already exist and compile).
Blocks: PHASE5 (live verification needs real, reachable routes/UI to test).

---
## Step 1: The Goal

Make PHASE3's two new capability methods actually reachable: a real
`POST /project_assembly/open_project` HTTP route (calls
`OpenProjectAssembly()` — the network-thread-safe one), a real, read-only
`GET /project_assembly/list_projects` route (calls `ListProjectAssemblies()`),
and a real, floating `OpenProjectWindow` ImGui utility window (calls
`OpenProjectAssemblyOnMainThread()` — the main-thread-only one), reachable
from the Editor's already-existing, currently-disabled "Project > Open
Project..." menu item.

## Step 2: The Situation

`NetworkServer`'s constructor already has a 9th parameter,
`IProjectLifecycleCapability* projectLifecycleCapability` — **zero
constructor signature change needed** for this phase; both new routes
reuse this exact pointer. `NetworkRoutes.cpp`'s existing
`POST /project_assembly/create_project` route
(`src/Network/NetworkServer.cpp`, lines 1334-1361) is the exact template
for `open_project`'s own shape (503 on null capability, then a
success/failure JSON body) — copy its structure, not its business logic.

`NewProjectWindow.h/.cpp` (`src/Editor/`) is the exact, real, working
template for `OpenProjectWindow` — same non-dockable
`ImGuiWindowFlags_NoDocking` shape, same `EditorContext`-bool-driven
open/close convention, same "calls the capability directly, writes into
`ctx.projectWorkflowStatusMessage`/`...StatusIsError`/`...StatusSetTime`
on completion" pattern. The one, load-bearing difference: `OpenProjectWindow`
calls `OpenProjectAssemblyOnMainThread()`, **never**
`OpenProjectAssembly()` (PHASE3, Section 2.2's fix) — get this wrong and
the Editor hangs the first time a user opens a compiled project.

`DockLayout.cpp`'s "Project" menu (lines 175-191) already has a disabled,
reserved placeholder:
`if (ImGui::MenuItem("Open Project...", nullptr, false, false)) {}` — this
phase removes the trailing `false` (the disabled flag) and wires the real
handler, mirroring the already-real "New Project..." item immediately
above it.

## Step 3: The Plan

### 3.1 — HTTP routes (`src/Network/NetworkServer.cpp`)

Placed immediately after the existing `/project_assembly/create_project`
route (same section, same style):

```cpp
// editor-core-separation-17 campaign (On-Engine Project Workflow plan,
// BIG-STEP 3), PHASE4 - POST /project_assembly/open_project. Calls
// OpenProjectAssembly() (the network-thread-safe one, NOT
// OpenProjectAssemblyOnMainThread() - see EditorCapabilities.h's own doc
// comments on each for why this distinction is load-bearing).
server.Post("/project_assembly/open_project",
    [projectLifecycleCapability](const httplib::Request& req, httplib::Response& res) {
    const std::string name = req.get_param_value("name");
    if (projectLifecycleCapability == nullptr) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("project lifecycle capability not available"), "application/json");
        return;
    }
    const IProjectLifecycleCapability::OpenProjectOutcome outcome =
        projectLifecycleCapability->OpenProjectAssembly(name);
    if (!outcome.success) {
        res.status = 400;
        res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
        return;
    }
    nlohmann::json body;
    body["status_message"] = outcome.statusMessage;
    body["load_attempted"] = outcome.loadAttempted;
    body["load_succeeded"] = outcome.loadSucceeded;
    res.set_content(body.dump(), "application/json");
});

// GET /project_assembly/list_projects - read-only, any-thread-safe.
server.Get("/project_assembly/list_projects",
    [projectLifecycleCapability](const httplib::Request& /*req*/, httplib::Response& res) {
    if (projectLifecycleCapability == nullptr) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("project lifecycle capability not available"), "application/json");
        return;
    }
    const std::vector<IProjectLifecycleCapability::ProjectListEntry> entries =
        projectLifecycleCapability->ListProjectAssemblies();
    nlohmann::json body = nlohmann::json::array();
    for (const auto& entry : entries) {
        nlohmann::json entryJson;
        entryJson["name"] = entry.name;
        entryJson["tier"] = entry.tierName;
        body.push_back(entryJson);
    }
    res.set_content(body.dump(), "application/json");
});
```

Both deliberately bypass `ParseProjectNameQuery()` for `open_project`
(same reasoning as `create_project`'s own real comment — the capability's
own `IsValidProjectAssemblyIdentifierName()` call is already strictly
correct) — `list_projects` takes no query parameter at all.

No `NetworkServer.h`/constructor change is needed — re-confirm this by
grep'ing for every real call site of `NetworkServer`'s constructor before
declaring this phase done, to make sure nothing was silently assumed.

### 3.2 — `src/Editor/OpenProjectWindow.h`

```cpp
#pragma once

// src/Editor/OpenProjectWindow.h
//
// editor-core-separation-17 campaign (On-Engine Project Workflow plan,
// BIG-STEP 3), PHASE4. The "Open Project" floating utility window -
// mirrors NewProjectWindow.h's own exact shape (non-dockable, one
// EditorContext bool drives open/close, calls the capability directly and
// writes into the shared ctx.projectWorkflowStatus* fields on completion),
// but shows a scrollable, tier-badged list instead of a text box.
//
// CRITICAL: this window calls
// IProjectLifecycleCapability::OpenProjectAssemblyOnMainThread() -
// NEVER OpenProjectAssembly() - see PHASE0_MASTER_STRATEGY.md, Section
// 2.2, and EditorCapabilities.h's own doc comments on each method, for why
// calling the other one from here would deadlock the whole Editor.

// NewProjectWindow.h's own real, current include list #includes
// "EditorContext.h" DIRECTLY (never just a forward declaration) - Build()'s
// own body needs the COMPLETE EditorContext type to read/write its
// `ctx.openProjectWindowOpen`/`ctx.projectWorkflowStatus*` fields, and
// neither NewProjectWindow.cpp nor this window's own .cpp (3.3 below)
// separately #includes EditorContext.h itself - mirror that exact
// precedent here too, or Build()'s body fails to compile with an
// incomplete-type error the moment it touches any `ctx.` field.
#include "EditorContext.h"

#include <string>
#include <vector>

namespace gte {

class IProjectLifecycleCapability;
// Deliberately NO forward-declaration of ProjectValidityTier here - this
// window only ever stores/compares the plain `tierName` STRING each
// ProjectListEntry already carries (see Row below), never the enum
// itself, so pulling in even a forward declaration of it would be a dead,
// unused dependency.

class OpenProjectWindow {
public:
    // Triggers an immediate rescan (calls ListProjectAssemblies()
    // directly, in-process - the exact same data GET
    // /project_assembly/list_projects would return, just read without an
    // HTTP round trip since we are already inside the process).
    void Open(IProjectLifecycleCapability* capability);
    void Build(EditorContext& ctx, IProjectLifecycleCapability* capability);

private:
    struct Row {
        std::string name;
        std::string tierName;
    };
    bool m_wasOpenLastFrame = false;
    std::vector<Row> m_rows;
    int m_selectedIndex = -1;
    std::string m_errorMessage;
};

} // namespace gte
```

### 3.3 — `OpenProjectWindow.cpp`

Mirrors `NewProjectWindow.cpp`'s own `m_wasOpenLastFrame`-driven
"open on the false->true transition" convention exactly:

```cpp
#include "OpenProjectWindow.h"

#include "../Core/EditorCapabilities.h"

#include <imgui.h>

namespace gte {

void OpenProjectWindow::Open(IProjectLifecycleCapability* capability)
{
    m_rows.clear();
    m_selectedIndex = -1;
    m_errorMessage.clear();
    if (capability == nullptr) return;
    for (const auto& entry : capability->ListProjectAssemblies()) {
        m_rows.push_back({ entry.name, entry.tierName });
    }
}

namespace {
ImVec4 ColorForTierName(const std::string& tierName)
{
    if (tierName == "NotAProject") return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);   // gray
    if (tierName == "NotBuildable") return ImVec4(0.9f, 0.8f, 0.2f, 1.0f);  // yellow
    if (tierName == "NotCompiled") return ImVec4(0.95f, 0.55f, 0.15f, 1.0f); // orange
    if (tierName == "Compiled") return ImVec4(0.35f, 0.85f, 0.35f, 1.0f);   // green
    if (tierName == "AlreadyLoaded") return ImVec4(0.35f, 0.65f, 0.95f, 1.0f); // blue
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}
} // namespace

void OpenProjectWindow::Build(EditorContext& ctx, IProjectLifecycleCapability* capability)
{
    if (!ctx.openProjectWindowOpen) {
        m_wasOpenLastFrame = false;
        return;
    }
    if (!m_wasOpenLastFrame) {
        Open(capability);
        m_wasOpenLastFrame = true;
    }

    ImGui::SetNextWindowSize(ImVec2(480.0f, 320.0f), ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Open Project", &ctx.openProjectWindowOpen, flags)) {
        ImGui::BeginChild("OpenProjectRows", ImVec2(0, -40.0f), true);
        for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
            const Row& row = m_rows[i];
            const bool isSelectable = row.tierName != "NotAProject";
            ImGui::PushStyleColor(ImGuiCol_Text, ColorForTierName(row.tierName));
            const std::string label = row.name + "  [" + row.tierName + "]";
            if (ImGui::Selectable(label.c_str(), m_selectedIndex == i,
                    isSelectable ? 0 : ImGuiSelectableFlags_Disabled)) {
                m_selectedIndex = i;
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        if (!m_errorMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_errorMessage.c_str());
        }
        if (ImGui::Button("Cancel")) {
            ctx.openProjectWindowOpen = false;
        }
        ImGui::SameLine();
        const bool canOpen = m_selectedIndex >= 0
            && m_rows[static_cast<std::size_t>(m_selectedIndex)].tierName != "NotAProject";
        ImGui::BeginDisabled(!canOpen);
        if (ImGui::Button("Open") && capability != nullptr) {
            const IProjectLifecycleCapability::OpenProjectOutcome outcome =
                capability->OpenProjectAssemblyOnMainThread(m_rows[static_cast<std::size_t>(m_selectedIndex)].name);
            if (outcome.success) {
                ctx.openProjectWindowOpen = false;
                ctx.projectWorkflowStatusMessage = outcome.statusMessage;
                ctx.projectWorkflowStatusIsError = false;
                ctx.projectWorkflowStatusSetTime = std::chrono::steady_clock::now();
            } else {
                m_errorMessage = outcome.errorMessage;
            }
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}

} // namespace gte
```

(Confirmed, no action needed: `EditorContext.h`'s own real, current include
list already has `#include <chrono>` directly, and this window's own header
(3.2 above) now `#include`s `EditorContext.h` too - `std::chrono::steady_clock::now()`
is transitively available here with no extra include, exactly like
`NewProjectWindow.cpp` already relies on today.)

### 3.4 — `EditorContext.h` addition

Immediately after `newProjectWindowOpen` (line 260), mirroring its own
exact doc-comment convention:

```cpp
// editor-core-separation-17 campaign (On-Engine Project Workflow plan,
// BIG-STEP 3), PHASE4 - true whenever OpenProjectWindow.h's floating
// window is currently open. Same open/close convention as
// newProjectWindowOpen immediately above.
bool openProjectWindowOpen = false;
```

### 3.5 — `ImGuiEditorLayer.cpp` wiring

Mirrors `m_newProjectWindow.Build(m_ctx, m_projectLifecycleCapability);`
(line 534) — add immediately after it:

```cpp
m_openProjectWindow.Build(m_ctx, m_projectLifecycleCapability);
```

Add `#include "OpenProjectWindow.h"` (next to the existing
`#include "NewProjectWindow.h"`, line 13) and a new member,
`OpenProjectWindow m_openProjectWindow;` (next to
`NewProjectWindow m_newProjectWindow;`, line 1166). No change needed to
`SetProjectLifecycleCapability()` itself — `m_projectLifecycleCapability`
is already stored and already the right type.

### 3.6 — `DockLayout.cpp` menu wiring

Replace the existing disabled placeholder (line 187):

```cpp
if (ImGui::MenuItem("Open Project...", nullptr, false, false)) {}
```

with:

```cpp
if (ImGui::MenuItem("Open Project...")) {
    ctx.openProjectWindowOpen = true;
}
```

Update the comment block immediately above (lines 179-186) to drop "Open
Project" from the "reserved, disabled placeholders" list — only "Compile"
(BIG-STEP 5) remains reserved/disabled after this phase.

### 3.7 — Definition of done

- [ ] Both new HTTP routes compile and are registered.
- [ ] `OpenProjectWindow.h/.cpp` compile, are wired into
      `ImGuiEditorLayer.cpp`, and the "Project > Open Project..." menu item
      is enabled and opens the window.
- [ ] Fast incremental compile check of `GreatTamanaEditor` succeeds.
- [ ] A quick, real, running-instance smoke check (via
      `run_app_background` + `gte_send_request`): `GET
      /project_assembly/list_projects` returns a real JSON array
      containing `"ProjectAssemblyProbe"`; a screenshot
      (`GET /get_swapchain` or `/get_game_view`, whichever this session's
      `gte_send_request` convention actually returns for a UI screenshot -
      confirm which endpoint actually renders the Editor's own ImGui UI,
      not just the game viewport, before relying on it) shows the "Project"
      menu with "Open Project..." now enabled (not grayed out).
- [ ] `git_add` + `git_commit` + `PHASE4_COMPLETION_REPORT.md`, explicitly
      noting whether a live screenshot of the OPENED `OpenProjectWindow`
      itself was possible or hit the same "no HTTP route opens an on-demand
      floating window" gap `editor-core-separation-16`'s own
      `CAMPAIGN_COMPLETION_REPORT.md` already flagged (if so, that is an
      accepted, disclosed gap, not a blocker — review the window's own code
      line-by-line against this file's sample instead).

### 3.8 — Non-goals for this phase specifically

- Does NOT run the full regression suite yet (PHASE5).
- Does NOT touch BIG-STEP 5's "Compile" menu item.
