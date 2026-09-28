# PHASE4 — HTTP Route, EditorContext Fields & the "Project" Menu / NewProjectWindow

Parent: `PHASE0_MASTER_STRATEGY.md` (read first — LDD-PW1, LDD-PW5).

Depends on: PHASE3 (`IProjectLifecycleCapability`/
`EditorProjectLifecycleCapability` must already exist and compile).
Blocks: PHASE5 (the full, live, end-to-end verification exercises both the
HTTP route and the ImGui window this phase adds).

End state of this phase: `POST /project_assembly/create_project?name=<X>`
is a real, live HTTP route; a new "Project" top-level menu bar entry with a
working "New Project..." window exists in the running Editor; both call
the exact same `IProjectLifecycleCapability::CreateNewProjectAssembly()`
method PHASE3 added, per LDD-PW5's "one function, two callers" rule.

---

## STEP 1 — `NetworkServer`'s 9th constructor parameter

`src/Network/NetworkServer.h`, confirmed real, current constructor (lines
156-163) — append a 9th defaulted, non-owning pointer, mirroring every
prior addition's own doc-comment style exactly (copy the 8th argument's
own comment block, lines 146-155, as the template):

```cpp
    // `projectLifecycleCapability` (editor-core-separation-16 campaign, On-
    // Engine Project Workflow plan, BIG-STEP 2) is a NINTH defaulted,
    // non-owning pointer, appended AFTER `hotReloadDebugCapability` so
    // every existing call site keeps compiling unchanged. Non-null in
    // production (EditorHost owns the real EditorProjectLifecycleCapability
    // and passes its address) - `nullptr` means "POST
    // /project_assembly/create_project responds 503 rather than crashing" -
    // the exact same "nullptr degrades gracefully to a 503, never a crash"
    // contract every other bridge above already documents.
    explicit NetworkServer(FrameCaptureBridge* captureBridge = nullptr,
        EngineCommandBridge* commandBridge = nullptr,
        EditorUiCommandBridge* uiCommandBridge = nullptr,
        FrameDebuggerCommandBridge* frameDebuggerCommandBridge = nullptr,
        AssetImportCommandBridge* assetImportCommandBridge = nullptr,
        ILogQueryCapability* logQueryCapability = nullptr,
        RenderGraphControlCommandBridge* renderGraphControlCommandBridge = nullptr,
        IHotReloadDebugCapability* hotReloadDebugCapability = nullptr,
        IProjectLifecycleCapability* projectLifecycleCapability = nullptr);
```

Add `class IProjectLifecycleCapability;` to the forward-declaration block
near the top of the header (confirmed line 62 today has the sibling
`class IHotReloadDebugCapability;` forward-declaration — add the new one
immediately after it), and a new private member,
`IProjectLifecycleCapability* m_projectLifecycleCapability = nullptr;`,
immediately after `m_hotReloadDebugCapability` (confirmed line 259 today).

`src/Network/NetworkServer.cpp`'s constructor definition — extend its own
parameter list and initializer list to match (mechanical), storing the new
pointer into `m_projectLifecycleCapability`.

## STEP 2 — The new route: `POST /project_assembly/create_project`

`src/Network/NetworkServer.cpp`, registered immediately alongside
`/project_assembly/debug/compile_only` (confirmed lines 1313-1330 today —
place the new route directly after it, same file, same section):

```cpp
    server.Post("/project_assembly/create_project",
        [projectLifecycleCapability = m_projectLifecycleCapability](
            const httplib::Request& req, httplib::Response& res) {
        // Deliberately does NOT go through ParseProjectNameQuery() -
        // CreateNewProjectAssembly() itself already validates the name far
        // more strictly (IsValidProjectAssemblyIdentifierName(), PHASE2)
        // than that older, permissive helper ever did; duplicating a
        // weaker check in front of a stronger one adds nothing (this
        // campaign's own PHASE0 doc, Step 6 note).
        const std::string name = req.get_param_value("name");
        if (projectLifecycleCapability == nullptr) {
            res.status = 503;
            res.set_content(
                BuildGenericErrorResponseJson("project lifecycle capability not available"), "application/json");
            return;
        }
        const IProjectLifecycleCapability::CreateProjectOutcome outcome =
            projectLifecycleCapability->CreateNewProjectAssembly(name);
        if (!outcome.success) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
            return;
        }
        nlohmann::json body;
        body["created_source_directory"] = outcome.createdSourceDirectory;
        res.set_content(body.dump(), "application/json");
    });
```

**Re-verify concretely**: confirm the exact lambda-capture idiom this
file's OTHER routes actually use for their own capability pointer (some
capture the constructor parameter name directly since it's still in scope
at registration time inside this constructor — re-read
`RegisterProjectAssemblyRoutes()`'s/whatever the real, current enclosing
function's name and parameter list is, to match the established idiom
exactly rather than inventing a new one; the snippet above assumes the
member `m_projectLifecycleCapability` is what gets captured, but if this
file's route-registration function is a free function taking the raw
pointer as its own parameter — confirmed likely, since
`hotReloadDebugCapability` appears to be the raw constructor parameter
name captured directly at line 1314 — capture that same parameter instead,
mirroring the immediately-preceding `compile_only` route byte-for-byte).

`#include "../Core/EditorCapabilities.h"` must already be included in this
file (needed for `IHotReloadDebugCapability` today) — `IProjectLifecycleCapability`
is declared in the exact same header, so no new `#include` is needed.

## STEP 3 — `EditorHost.cpp`'s `m_networkServer` initializer

Extend the existing 8-argument construction (confirmed lines 191-193
today) with the 9th argument:

```cpp
    , m_networkServer(&m_captureBridge, &m_commandBridge, &m_uiCommandBridge, &m_frameDebuggerCommandBridge,
          &m_assetImportCommandBridge, &s_editorLogQueryCapability, &m_renderGraphControlCommandBridge,
          &s_editorHotReloadDebugCapability, &s_editorProjectLifecycleCapability)
```

## STEP 4 — `EditorContext.h` new fields

Append immediately after `frameDebuggerWindowOpen` (confirmed line 251
today):

```cpp
    // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 2), PHASE4 - true whenever NewProjectWindow.h's floating
    // window is currently open. Flipped by the new "Project > New
    // Project..." menu item (DockLayout.cpp) and by the window's own
    // titlebar [x] close button (ImGui::Begin()'s p_open parameter keeps
    // both in sync automatically) - mirrors frameDebuggerWindowOpen's own
    // exact convention immediately above.
    bool newProjectWindowOpen = false;

    // Short-lived, colored status feedback shared by every "Project"-menu
    // action this whole 5-file plan adds (New Project this campaign; Open
    // Project/Compile in later campaigns) - mirrors sceneIoStatusMessage/
    // sceneIoStatusIsError/sceneIoStatusSetTime's own exact convention
    // above, just for a different family of actions, so a later campaign's
    // "Open Project" success/failure toast reuses this SAME field rather
    // than inventing a third status-toast mechanism.
    std::string projectWorkflowStatusMessage;
    bool projectWorkflowStatusIsError = false;
    std::chrono::steady_clock::time_point projectWorkflowStatusSetTime;
```

## STEP 5 — `NewProjectWindow` (new class)

`src/Editor/NewProjectWindow.h`:

```cpp
#pragma once

#include "EditorContext.h"

namespace gte {

class IProjectLifecycleCapability;

// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE4. A single, floating, NON-DOCKABLE ImGui utility
// window (mirrors BoneViewerWindow.h's Open()/Build() shape, but with NO
// GPU resources at all - this window only ever manipulates a text buffer
// and calls the SAME capability method the HTTP route calls, LDD-PW5's
// "one function, two callers" rule).
class NewProjectWindow {
public:
    // Clears the text buffer + any previous error message - called the
    // frame ctx.newProjectWindowOpen first flips false->true (mirrors
    // FrameDebuggerPanel's own "the bool IS the open state, Open()-shaped
    // reset happens on the rising edge" precedent).
    void Open();

    // No-op whenever ctx.newProjectWindowOpen is false - safe to call
    // every frame unconditionally.
    void Build(EditorContext& ctx, IProjectLifecycleCapability* capability);

private:
    bool m_wasOpenLastFrame = false;
    char m_nameBuffer[128] = {};
    std::string m_errorMessage; // "" = no error currently shown.
};

} // namespace gte
```

`src/Editor/NewProjectWindow.cpp`:

```cpp
#include "NewProjectWindow.h"

#include "../Core/EditorCapabilities.h"

#include <imgui.h>

#include <cstring>

namespace gte {

void NewProjectWindow::Open()
{
    std::memset(m_nameBuffer, 0, sizeof(m_nameBuffer));
    m_errorMessage.clear();
}

void NewProjectWindow::Build(EditorContext& ctx, IProjectLifecycleCapability* capability)
{
    if (!ctx.newProjectWindowOpen) {
        m_wasOpenLastFrame = false;
        return;
    }
    if (!m_wasOpenLastFrame) {
        Open();
        m_wasOpenLastFrame = true;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 160.0f), ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Create New Project", &ctx.newProjectWindowOpen, flags)) {
        ImGui::InputText("Project Name", m_nameBuffer, sizeof(m_nameBuffer));
        if (!m_errorMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_errorMessage.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button("Cancel")) {
            ctx.newProjectWindowOpen = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Create") && capability != nullptr) {
            const IProjectLifecycleCapability::CreateProjectOutcome outcome =
                capability->CreateNewProjectAssembly(m_nameBuffer);
            if (outcome.success) {
                ctx.newProjectWindowOpen = false;
                ctx.projectWorkflowStatusMessage = "Created project at " + outcome.createdSourceDirectory;
                ctx.projectWorkflowStatusIsError = false;
                ctx.projectWorkflowStatusSetTime = std::chrono::steady_clock::now();
            } else {
                // Window stays open, user can fix and retry - never
                // auto-closes on failure (this campaign's own explicit
                // Definition of Done item).
                m_errorMessage = outcome.errorMessage;
            }
        }
    }
    ImGui::End();
}

} // namespace gte
```

`ImGuiWindowFlags_NoDocking` confirmed real, already used identically at
`DockLayout.cpp` line 119 (a different, unrelated window there — same
flag, same effect anywhere it's passed to `ImGui::Begin()`).

Register both new files in root `CMakeLists.txt`'s `gte_editor` source
list.

## STEP 6 — `ImGuiEditorLayer.cpp` wiring

Add a member, mirroring `m_boneViewer`'s own placement precedent (near the
other on-demand floating windows, confirmed around line 1127 today):

```cpp
    // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 2), PHASE4 - the "Create New Project" floating utility
    // window (NewProjectWindow.h), opened on demand via "Project > New
    // Project..." (DockLayout.cpp). No GPU resources, so unlike
    // m_boneViewer above it needs no explicit Reset() in this class's own
    // destructor.
    NewProjectWindow m_newProjectWindow;
```

Call `m_newProjectWindow.Build(m_ctx, m_projectLifecycleCapability)` once
per frame from `BuildUI()`, alongside the other on-demand window
`Build()` calls (find the exact call site pattern
`m_boneViewer`/`FrameDebuggerPanel` use inside `BuildUI()` and mirror it —
`ImGuiEditorLayer` needs a live `IProjectLifecycleCapability*` of its own
to pass through: add a small setter,
`SetProjectLifecycleCapability(IProjectLifecycleCapability*)`, called once
from `EditorHost`'s constructor immediately after
`m_editorLayer` is constructed — mirrors how `IEditorLayer`/`EditorContext`
already get wired into `ImGuiEditorLayer` elsewhere; re-read
`ImGuiEditorLayer.h`'s own existing setter list before adding a new one, to
match its established naming/placement convention exactly).

## STEP 7 — `DockLayout.cpp`'s new "Project" top-level menu

Add a THIRD top-level menu, immediately after "Window" (confirmed line
162-174 today), inside the same `if (ImGui::BeginMenuBar())` block:

```cpp
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("New Project...")) {
                ctx.newProjectWindowOpen = true;
            }
            // editor-core-separation-16 campaign - reserved, disabled
            // placeholders for the "Open Project" (BIG-STEP 3) and
            // "Compile" (BIG-STEP 5) campaigns, named exactly as
            // PROJECTWORKFLOW_BIGSTEP_01...txt's own LDD-PW1 requires (all
            // three items share ONE menu) - never wired to any action by
            // THIS campaign. The trailing `false` disables the item
            // (ImGui::MenuItem's 4th parameter); remove it, and add the
            // real handler, only when that later campaign actually lands.
            if (ImGui::MenuItem("Open Project...", nullptr, false, false)) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Compile", nullptr, false, false)) {}
            ImGui::EndMenu();
        }
```

Add the SAME shared status-toast rendering this campaign's new
`projectWorkflowStatusMessage` needs, immediately after the existing
`sceneIoStatusMessage` block (confirmed lines 262-271 today) — copy that
block's own exact shape, substituting the field names:

```cpp
    if (!ctx.projectWorkflowStatusMessage.empty()) {
        constexpr std::chrono::milliseconds kProjectWorkflowStatusLifetime{ 4000 };
        if (std::chrono::steady_clock::now() - ctx.projectWorkflowStatusSetTime < kProjectWorkflowStatusLifetime) {
            const ImVec4 color = ctx.projectWorkflowStatusIsError ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                                                   : ImVec4(0.6f, 0.85f, 0.6f, 1.0f);
            ImGui::TextColored(color, "%s", ctx.projectWorkflowStatusMessage.c_str());
        } else {
            ctx.projectWorkflowStatusMessage.clear();
        }
    }
```

## STEP 8 — Verification (incremental build + one live HTTP round trip only)

1. Incremental build succeeds, zero new warnings.
2. `run_app_background` the Editor.
3. `gte_send_request` a `POST
   /project_assembly/create_project?name=EcsPhase4SmokeTest` — confirm a
   200 response with `created_source_directory`, and the real 3-file
   scaffold on disk.
4. `gte_send_request` the SAME request again — confirm 400, "already
   exists", and nothing changed on disk.
5. `gte_send_request` `POST
   /project_assembly/create_project?name=CON` (and `?name=` empty) —
   confirm both 400, rejected before any write.
6. Take a screenshot (`gte_send_request` against `/get_swapchain` or
   `/get_game_view`, whichever this engine's real capture endpoint is) of
   the Editor with "Project > New Project..." clicked, to visually confirm
   the window renders and is genuinely non-dockable.
7. `stop_app_background`; delete `Projects/EcsPhase4SmokeTest/` before
   committing (never leave a scratch project folder committed).

## Definition of Done — this phase only

- [ ] `NetworkServer`'s 9th constructor parameter exists; every pre-existing
      call site (including every test file constructing a `NetworkServer`
      with fewer arguments) still compiles unchanged.
- [ ] `POST /project_assembly/create_project` is registered and reachable.
- [ ] `EditorContext.h`'s 4 new fields exist and compile.
- [ ] `NewProjectWindow` exists, compiles, is wired into
      `ImGuiEditorLayer`'s per-frame `Build()` loop.
- [ ] `DockLayout.cpp`'s new "Project" menu exists with exactly the 3 items
      LDD-PW1 specifies (2 of them deliberately disabled placeholders).
- [ ] All 5 live verification checks in STEP 8 were actually performed and
      passed.
- [ ] No scratch project folder remains in the final commit.

## What this phase does NOT do

- Does NOT implement "Open Project..." or "Compile" for real — both
  remain disabled, reserved placeholders (Non-Goal, PHASE0).
- Does NOT add any new Tier-1 unit test file — this phase's own
  correctness is proven live (STEP 8) plus PHASE5's later, fuller live
  verification; a full HTTP end-to-end test file
  (`tests/Network/CreateProjectEndpointEndToEndTests.cpp`, mirroring
  `ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`'s own exact fixture
  shape) is RECOMMENDED but left to PHASE5 to add alongside its own full
  regression pass, since writing it requires the same real
  `EditorProjectLifecycleCapability`/temp-directory-shaped fixture design
  PHASE5 already has to set up for its own end-to-end proof — doing it
  once, there, avoids duplicating that fixture-design effort across two
  phases.
