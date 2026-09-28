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

## STEP 1 — `NetworkServer`'s 9th constructor parameter (three call sites, not one — read this whole step before touching anything)

Confirmed, by directly reading the real, current
`src/Network/NetworkServer.cpp`, that a new capability pointer added to
this class needs its own new parameter threaded through **three** distinct
places, not just the constructor's own parameter/initializer list — get
this wrong and the code will not compile, or will silently capture a
stale/wrong pointer inside a route lambda:

1. **`src/Network/NetworkServer.h`** — append a 9th defaulted, non-owning
   pointer to the constructor's own declaration (confirmed real, current
   lines 156-163), mirroring every prior addition's own doc-comment style
   exactly (copy the 8th argument's own comment block, lines 146-155, as
   the template):

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
   `namespace gte { class IHotReloadDebugCapability; }` forward-declaration —
   add the new one immediately after it, same `namespace gte { ... }`
   one-liner style), and a new private member,
   `IProjectLifecycleCapability* m_projectLifecycleCapability = nullptr;`,
   immediately after `m_hotReloadDebugCapability` (confirmed line 259
   today, right before the class's closing `};`).

2. **`src/Network/NetworkServer.cpp`'s `NetworkServer::NetworkServer(...)`
   constructor** (confirmed real, current definition/initializer list at
   lines 1410-1421) — extend its own parameter list and initializer list to
   match (mechanical): add the 9th parameter, and
   `, m_projectLifecycleCapability(projectLifecycleCapability)` immediately
   after `, m_hotReloadDebugCapability(hotReloadDebugCapability)` (confirmed
   line 1420).

3. **`RegisterRoutes()` — a SEPARATE, free (non-member) function, NOT a
   `NetworkServer` method** (confirmed real, current signature at
   `NetworkServer.cpp` lines 296-299):

   ```cpp
   void RegisterRoutes(httplib::Server& server, FrameCaptureBridge* captureBridge, EngineCommandBridge* commandBridge,
       EditorUiCommandBridge* uiCommandBridge, FrameDebuggerCommandBridge* frameDebuggerCommandBridge,
       AssetImportCommandBridge* assetImportCommandBridge, ILogQueryCapability* logQueryCapability,
       RenderGraphControlCommandBridge* renderGraphControlCommandBridge, IHotReloadDebugCapability* hotReloadDebugCapability)
   ```

   **This is the one easy-to-miss wiring step**: every route this function
   registers captures one of ITS OWN plain parameters directly in each
   lambda (e.g. `[hotReloadDebugCapability](...)`, confirmed real at lines
   1225/1235/1253/1264/1314/1342) — there is no `this`/no `m_...` member
   accessible inside a free function. STEP 2 below's new route therefore
   needs `RegisterRoutes()` itself to gain a 10th parameter,
   `IProjectLifecycleCapability* projectLifecycleCapability`, appended
   after `hotReloadDebugCapability`, AND the constructor's own call site
   that invokes it (confirmed real, current lines 1427-1428:
   `RegisterRoutes(m_impl->server, m_captureBridge, m_commandBridge, ...,
   m_hotReloadDebugCapability);`) extended with one more trailing argument,
   `m_projectLifecycleCapability`. Skipping this and instead trying to
   capture `m_projectLifecycleCapability` directly from inside
   `RegisterRoutes()`'s own lambda (an easy mistake — it reads exactly like
   a class member name) will not compile: `RegisterRoutes()` is a free
   function with no access to any `NetworkServer` instance at all.

## STEP 2 — The new route: `POST /project_assembly/create_project`

Inside `RegisterRoutes()`'s own body, registered immediately alongside
`/project_assembly/debug/compile_only` (confirmed real, current lines
1313-1330 — place the new route directly after it, same file, same
section, same function):

```cpp
    server.Post("/project_assembly/create_project",
        [projectLifecycleCapability](const httplib::Request& req, httplib::Response& res) {
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

Note the lambda captures `projectLifecycleCapability` — the plain
parameter STEP 1.3 above just added to `RegisterRoutes()`'s own signature —
directly by value, mirroring the immediately-preceding `compile_only`
route's own `[hotReloadDebugCapability](...)` capture byte-for-byte. This
is confirmed to be the one and only capture idiom this file's route table
uses (every route above it does the same, e.g. `[logQueryCapability]`,
`[commandBridge]`).

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
today, right before the struct's closing `};`):

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

## STEP 6 — Threading the capability into the real ImGui UI: `IEditorLayer` gains a new pure-virtual setter (this step was previously left vague — it has now been pinned down exactly, by directly reading the real code)

**Important correction to how this step used to be described**: there is
NO `src/Editor/ImGuiEditorLayer.h` file anywhere in this repository — the
real, ImGui-backed `ImGuiEditorLayer` class is defined ENTIRELY inside
`src/Editor/ImGuiEditorLayer.cpp` (confirmed: `class ImGuiEditorLayer final
: public IEditorLayer` at that file's own line 128; a `search_in_dir` for
"ImGuiEditorLayer.h" across `src/Editor/` finds nothing). `EditorHost` only
ever owns a `std::unique_ptr<IEditorLayer> m_editorLayer` (an ABSTRACT
interface pointer, `src/Editor/EditorLayer.h`) — it can never call a method
that exists only on the concrete `ImGuiEditorLayer` class. Every prior
campaign that needed to hand ImGuiEditorLayer a new piece of state from
EditorHost (e.g. `SetGameViewCompositedTexture()`, `SetShowBlurredSceneOutput()`,
`SetShowGBufferValidationOutput()`) did so the ONLY way that is actually
possible: by adding a new PURE VIRTUAL method to `IEditorLayer` itself,
with a real implementation in `ImGuiEditorLayer.cpp` and a safe no-op
override in `NullEditorLayer.cpp` (confirmed: `SetShowGBufferValidationOutput`
appears in all three of `EditorLayer.h` (pure virtual declaration),
`ImGuiEditorLayer.cpp` (real body, `m_ctx.showGBufferValidationOutput =
enabled;`), and `NullEditorLayer.cpp` (`{ }` no-op) — this IS the
established, load-bearing precedent to copy, not a vague "re-read the
setter list" gesture). This campaign's own `NewProjectWindow` needs exactly
the same treatment, since it draws real ImGui UI and can therefore only
ever be built from inside `ImGuiEditorLayer::BuildUI()`.

1. **`src/Editor/EditorLayer.h`** — add a forward declaration right next to
   the existing `class RenderFeatureCompositor;` forward declaration
   (confirmed real, current, inside `namespace gte { ... }`):
   ```cpp
   // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
   // BIG-STEP 2), PHASE4 - forward-declared only, mirrors
   // "class RenderFeatureCompositor;" immediately above: this header only
   // ever stores/passes a POINTER to it, never dereferences one itself.
   class IProjectLifecycleCapability;
   ```
   Then add a new pure-virtual method to `IEditorLayer`, placed right after
   `SetShowGBufferValidationOutput()`'s own declaration:
   ```cpp
   // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
   // BIG-STEP 2), PHASE4 - hands the real ImGui implementation a live
   // IProjectLifecycleCapability* (Core/EditorCapabilities.h) so its own
   // "New Project..." floating window (NewProjectWindow.h) can call
   // CreateNewProjectAssembly() directly - the exact SAME method
   // POST /project_assembly/create_project calls (LDD-PW5's "one function,
   // two callers" rule). Called exactly ONCE, from EditorHost's own
   // constructor body, immediately after Core::SetEditorLayerHook() -
   // never per-frame, unlike BuildUI()'s own trailing parameters, since
   // this pointer's value never changes for the life of the process
   // (mirrors how m_sceneIOCapability/the hot-reload-debug-capability
   // static are each wired exactly once too). Always a safe no-op for
   // NullEditorLayer (a release build has no "New Project..." window to
   // give a capability to at all).
   virtual void SetProjectLifecycleCapability(IProjectLifecycleCapability* capability) = 0;
   ```

2. **`src/Editor/NullEditorLayer.cpp`** — add the no-op override, mirroring
   `SetShowGBufferValidationOutput`'s own placement/style exactly:
   ```cpp
   void SetProjectLifecycleCapability(IProjectLifecycleCapability* /*capability*/) override { }
   ```

3. **`src/Editor/ImGuiEditorLayer.cpp`**:
   - Add `#include "NewProjectWindow.h"` to this file's own include list
     (alongside the other `Panels/*.h`/Editor-only includes near the top).
   - Add the real override, placed immediately after the existing
     `SetShowGBufferValidationOutput(bool enabled) override { ... }` line
     (confirmed real, current line 925), mirroring that line's own
     one-line style:
     ```cpp
     // editor-core-separation-16 campaign (On-Engine Project Workflow
     // plan, BIG-STEP 2), PHASE4 - see IEditorLayer::
     // SetProjectLifecycleCapability()'s own doc comment for the full
     // contract. Stored, never called from here - m_newProjectWindow's
     // own Build() call (below, inside BuildUI()) is what actually
     // invokes it.
     void SetProjectLifecycleCapability(IProjectLifecycleCapability* capability) override
     {
         m_projectLifecycleCapability = capability;
     }
     ```
   - Add a new private member, mirroring `m_boneViewer`'s own placement
     precedent (confirmed real, current line 1127 — the Bone Viewer's own
     on-demand floating window, right before `EditorContext m_ctx;`):
     ```cpp
     // editor-core-separation-16 campaign (On-Engine Project Workflow
     // plan, BIG-STEP 2), PHASE4 - the "Create New Project" floating
     // utility window (NewProjectWindow.h), opened on demand via
     // "Project > New Project..." (DockLayout.cpp). No GPU resources, so
     // unlike m_boneViewer above it needs no explicit Reset() in this
     // class's own destructor. NOT inside the "#if GTE_ENABLE_PROJECT_PANEL"
     // block above - this window is independent of the (content-asset)
     // "Project" panel and must exist in every build.
     NewProjectWindow m_newProjectWindow;
     // Non-owning - see IEditorLayer::SetProjectLifecycleCapability()'s
     // own doc comment for the lifetime contract (EditorHost's own
     // s_editorProjectLifecycleCapability static outlives this object).
     IProjectLifecycleCapability* m_projectLifecycleCapability = nullptr;
     ```
   - Call `m_newProjectWindow.Build(m_ctx, m_projectLifecycleCapability);`
     from inside `BuildUI()`, immediately after
     `BuildDockspaceAndMenuBar(m_ctx, game, renderer);` (confirmed real,
     current line 526 — the very first line of `BuildUI()`'s own body).
     Placed there, not inside the `#if GTE_ENABLE_PROJECT_PANEL` block
     further down (confirmed real, current lines 686-694, which gates the
     UNRELATED content-asset "Project" panel/Bone Viewer) — "New Project..."
     must be available in every real Editor build regardless of that
     switch.

4. **`src/Editor/EditorHost.cpp`** — call the new setter exactly once, in
   the constructor body, immediately after the existing
   `m_core.SetEditorLayerHook(m_editorLayer.get());` line (confirmed real,
   current line 211 — the same place `m_editorLayer` first becomes usable
   this constructor), and BEFORE PHASE3's own
   `ActiveProjectAssemblyState::Instance().SetProjectAssemblyHost(...)` line:
   ```cpp
   // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
   // BIG-STEP 2), PHASE4 - gives the real ImGui implementation
   // (ImGuiEditorLayer) a live IProjectLifecycleCapability* so its "New
   // Project..." window can call CreateNewProjectAssembly() directly. Safe
   // here: s_editorProjectLifecycleCapability (PHASE3's own namespace-scope
   // static) already exists via ordinary static initialization, strictly
   // before this constructor body ever runs.
   m_editorLayer->SetProjectLifecycleCapability(&s_editorProjectLifecycleCapability);
   ```

## STEP 7 — `DockLayout.cpp`'s new "Project" top-level menu

Add a THIRD top-level menu, immediately after "Window" (confirmed real,
current lines 162-174), inside the same `if (ImGui::BeginMenuBar())` block
(confirmed real, current line 140 — this whole function's own signature,
`void BuildDockspaceAndMenuBar(EditorContext& ctx, Game& game, Renderer&
renderer)`, needs NO new parameter for this step: the menu item only ever
flips `ctx.newProjectWindowOpen`, a plain bool this function already has
access to via its existing `ctx` parameter — it never needs to see
`IProjectLifecycleCapability*` itself, since STEP 6 already threads that
straight into `NewProjectWindow::Build()` separately):

```cpp
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("New Project...")) {
                ctx.newProjectWindowOpen = true;
            }
            // editor-core-separation-16 campaign - reserved, disabled
            // placeholders for the "Open Project" (BIG-STEP 3) and
            // "Compile" (BIG-STEP 5) campaigns, named exactly as
            // PROJECTWORKFLOW_BIGSTEP_01...txt's own LDD-PW1 requires (all
            // three items share ONE menu) - never wired to any real action
            // by THIS campaign. The trailing `false` disables the item
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
`sceneIoStatusMessage` block (confirmed real, current lines 262-271) —
copy that block's own exact shape, substituting the field names:

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

- [ ] `NetworkServer`'s 9th constructor parameter exists; `RegisterRoutes()`'s
      own free-function signature AND its one call site (inside
      `NetworkServer`'s constructor) were BOTH also extended (STEP 1.3) —
      not just the constructor's own parameter/initializer list; every
      pre-existing call site (including every test file constructing a
      `NetworkServer` with fewer arguments) still compiles unchanged.
- [ ] `POST /project_assembly/create_project` is registered and reachable.
- [ ] `EditorContext.h`'s 4 new fields exist and compile.
- [ ] `IEditorLayer` gained the new `SetProjectLifecycleCapability()`
      pure-virtual method; `NullEditorLayer.cpp` has a compiling no-op
      override; `ImGuiEditorLayer.cpp` has a real override that stores the
      pointer AND actually calls `m_newProjectWindow.Build(m_ctx,
      m_projectLifecycleCapability)` from `BuildUI()`; `EditorHost.cpp`
      calls the new setter exactly once, wiring
      `&s_editorProjectLifecycleCapability` through. The whole engine
      still compiles with BOTH `IEditorLayer` implementations linked in
      (this repo always links both, unconditionally - see AGENTS.md,
      "`gte_core`/`gte_editor` Library Separation").
- [ ] `NewProjectWindow` exists and compiles.
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
