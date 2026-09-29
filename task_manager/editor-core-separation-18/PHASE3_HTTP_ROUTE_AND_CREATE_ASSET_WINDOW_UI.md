# PHASE3 — IEditorLayer Wiring + CreateAssetWindow + HTTP Route

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1_*.md`'s
`IAssetScaffoldingCapability`/`CreateAssetScaffold()` and `PHASE2_*.md`'s
`EditorContext::createAssetWindowOpen`/`createAssetWindowPendingKind`
fields.

## STEP 1 — The Goal

1. `IEditorLayer` gains `SetAssetScaffoldingCapability(IAssetScaffoldingCapability*)`,
   wired exactly once from `EditorHost`'s constructor, exactly mirroring
   `SetProjectLifecycleCapability`'s own already-proven shape.
2. A new floating window, `src/Editor/CreateAssetWindow.h/.cpp`, mirrors
   `NewProjectWindow` — a single textbox + Create/Cancel buttons — but its
   title and the outcome's `reminderMessage` handling are kind-aware.
3. `POST /project_assembly/create_asset` exists in `NetworkServer.cpp`,
   calling the EXACT SAME `CreateAssetScaffold()` method the ImGui window
   calls (LDD-PW5, restated one final time by this last UI-wiring phase of
   the campaign).

## STEP 2 — The Situation (exact citations)

- `src/Editor/EditorLayer.h` line 720:
  `virtual void SetProjectLifecycleCapability(IProjectLifecycleCapability* capability) = 0;`
  — this phase adds a sibling pure virtual immediately after it.
- `src/Editor/NullEditorLayer.cpp` line 101:
  `void SetProjectLifecycleCapability(IProjectLifecycleCapability* /*capability*/) override { }`
  — this phase adds the matching no-op override.
- `src/Editor/ImGuiEditorLayer.cpp`:
  - Line 947: `void SetProjectLifecycleCapability(IProjectLifecycleCapability* capability) override { m_projectLifecycleCapability = capability; }`
  - Line 1183: `IProjectLifecycleCapability* m_projectLifecycleCapability = nullptr;`
  - Lines 535/540: `m_newProjectWindow.Build(m_ctx, m_projectLifecycleCapability);` /
    `m_openProjectWindow.Build(m_ctx, m_projectLifecycleCapability);` — both
    called from the SAME per-frame `BuildUI()`-adjacent block. This phase's
    new `m_createAssetWindow.Build(m_ctx, m_assetScaffoldingCapability);`
    call goes immediately after these two, same block.
  - Line 34: `#include "Panels/ProjectPanel.h"` — this phase adds
    `#include "CreateAssetWindow.h"` alongside the existing
    `#include "NewProjectWindow.h"`/`#include "OpenProjectWindow.h"` (confirm
    those two exact include lines first — they are not shown in this
    citation list, re-grep before editing).
- `src/Editor/EditorHost.cpp`:
  - Line 141: `EditorProjectLifecycleCapability s_editorProjectLifecycleCapability;`
    — the SAME static object this phase's new capability pointer targets;
    NO new static object is created (`LDD-CA4`, `PHASE0_MASTER_STRATEGY.md`).
  - Lines 205-209: the `RegisterRoutes(...)` call, currently 9 pointer
    arguments, `&s_editorProjectLifecycleCapability` being the ninth. This
    phase adds a TENTH: `&s_editorProjectLifecycleCapability` AGAIN, this
    time implicitly upcast to `IAssetScaffoldingCapability*` (legal,
    ordinary C++ — the compiler resolves which base sub-object's pointer
    value to take based on the PARAMETER's declared type at the call site,
    since the full concrete type `EditorProjectLifecycleCapability` is
    visible here).
  - Line 236: `m_editorLayer->SetProjectLifecycleCapability(&s_editorProjectLifecycleCapability);`
    — this phase adds `m_editorLayer->SetAssetScaffoldingCapability(&s_editorProjectLifecycleCapability);`
    immediately after it.
- **RESOLVED (was flagged "unconfirmed" in an earlier draft of this file) —
  `RegisterRoutes()` genuinely IS a flat pointer-parameter free function,
  and the constructor genuinely IS a flat pointer-parameter list too, with
  NO intermediate struct anywhere.** Mechanically confirmed by directly
  reading the current source, THREE exact places to edit, all in lock-step:
  1. `src/Network/NetworkServer.h` line 68:
     `namespace gte { class IProjectLifecycleCapability; }` — add a sibling
     forward declaration, `namespace gte { class IAssetScaffoldingCapability; }`,
     immediately after it (before `namespace gte::Network {` at line 70).
     Then add a TENTH constructor parameter,
     `IAssetScaffoldingCapability* assetScaffoldingCapability = nullptr`,
     after `hotReloadDebugCapability`/before the closing `)` of the
     constructor declaration (lines 171-179), and a new member field,
     `IAssetScaffoldingCapability* m_assetScaffoldingCapability = nullptr;`,
     immediately after `m_projectLifecycleCapability` (line 280, right
     before the class's closing `};`).
  2. `src/Network/NetworkServer.cpp` lines 296-300 —
     `void RegisterRoutes(httplib::Server& server, FrameCaptureBridge* captureBridge, ...,
     IProjectLifecycleCapability* projectLifecycleCapability)` — add the
     tenth parameter, `IAssetScaffoldingCapability* assetScaffoldingCapability`,
     at the end of this SAME flat list (no struct involved anywhere).
  3. `src/Network/NetworkServer.cpp` lines 1492-1515 — the
     `NetworkServer::NetworkServer(...)` constructor: its OWN parameter
     list (lines 1492-1496) gets the same tenth parameter, its member
     init list (lines 1497-1506) gets a matching
     `, m_assetScaffoldingCapability(assetScaffoldingCapability)` line, and
     its own `RegisterRoutes(...)` call (lines 1513-1515) gets
     `m_assetScaffoldingCapability` appended as the tenth argument.
  `#include "../Core/EditorCapabilities.h"` is ALREADY present in
  `NetworkServer.cpp` (confirmed, line 14 — added for `ILogQueryCapability`)
  — no new include is needed there; `IAssetScaffoldingCapability` becomes
  visible automatically the moment PHASE1 adds it to that same header.
- **RESOLVED — `RegisterRoutes()`'s real body genuinely lives inside
  `NetworkServer.cpp` itself** (confirmed, line 296) — `NetworkRoutes.h/.cpp`
  is a SEPARATE pair of files holding only shared HELPER functions
  (`BuildGenericErrorResponseJson()` and similar), never route
  registrations themselves. Paste the new route directly into
  `NetworkServer.cpp`, immediately after the existing
  `/project_assembly/create_project` route (lines 1337-1361) — no
  ambiguity remains about which file to edit.
- `src/Network/NetworkServer.cpp` lines 1337-1361 — the EXISTING
  `POST /project_assembly/create_project` route is the closest, most
  recent precedent to copy the shape of (nullable-capability 503 check,
  400 on `!outcome.success`, else build a small `nlohmann::json` body).

## STEP 3 — The Plan (exact code)

### 3.1 — `EditorLayer.h`

```cpp
// editor-core-separation-18 campaign (On-Engine Project Workflow plan,
// BIG-STEP 4), PHASE3 - hands the real ImGui implementation a live
// IAssetScaffoldingCapability* (Core/EditorCapabilities.h) so its own
// "Create -> Render Pass/Compute Shader/Shader Pair" floating window
// (CreateAssetWindow.h) can call CreateAssetScaffold() directly - the exact
// SAME method POST /project_assembly/create_asset calls (LDD-PW5's "one
// function, two callers" rule, mirroring SetProjectLifecycleCapability's
// own precedent immediately above/below). Called exactly ONCE, from
// EditorHost's own constructor body. Always a safe no-op for
// NullEditorLayer.
virtual void SetAssetScaffoldingCapability(IAssetScaffoldingCapability* capability) = 0;
```

**RESOLVED (was a hedged guess in an earlier draft of this file) —
`EditorLayer.h` does NOT `#include "../Core/EditorCapabilities.h"`
anywhere** (mechanically confirmed by grepping the whole file — the only
hit for "EditorCapabilities" is this doc comment's own text). It only
FORWARD-DECLARES `IProjectLifecycleCapability`, at line 88:
`class IProjectLifecycleCapability;`, immediately before this header's own
`IEditorLayer` class declaration begins. This phase must add a SIBLING
forward declaration, `class IAssetScaffoldingCapability;`, immediately
after that line 88 (a plain forward declaration is sufficient — this
header only ever stores/passes a POINTER to it, exactly like
`IProjectLifecycleCapability`/`RenderFeatureCompositor` immediately above
it, never dereferencing one itself). `NullEditorLayer.cpp` and
`ImGuiEditorLayer.cpp` both need NO new include either — each already gets
the forward declaration transitively via `#include "EditorLayer.h"`, and
neither one ever dereferences the pointer directly (only
`CreateAssetWindow.cpp`, which DOES dereference it, already plans its own
`#include "../Core/EditorCapabilities.h"` in 3.5 below — mirrors
`NewProjectWindow.cpp`'s own identical precedent).

### 3.2 — `NullEditorLayer.cpp`

```cpp
void SetAssetScaffoldingCapability(IAssetScaffoldingCapability* /*capability*/) override { }
```

### 3.3 — `ImGuiEditorLayer.cpp`

```cpp
void SetAssetScaffoldingCapability(IAssetScaffoldingCapability* capability) override
{
    m_assetScaffoldingCapability = capability;
}
```
```cpp
IAssetScaffoldingCapability* m_assetScaffoldingCapability = nullptr; // Non-owning - see IEditorLayer::SetAssetScaffoldingCapability()'s own doc comment.
```
```cpp
m_createAssetWindow.Build(m_ctx, m_assetScaffoldingCapability);
```
```cpp
#include "CreateAssetWindow.h"
```
```cpp
CreateAssetWindow m_createAssetWindow; // same "member object, Build() called every frame" shape as m_newProjectWindow/m_openProjectWindow immediately above/below it.
```

### 3.4 — `src/Editor/CreateAssetWindow.h` (new file, mirrors
`NewProjectWindow.h` exactly)

```cpp
#pragma once

#include "EditorContext.h"

namespace gte {

class IAssetScaffoldingCapability;

// editor-core-separation-18 campaign (On-Engine Project Workflow plan,
// BIG-STEP 4). A single, floating, NON-DOCKABLE ImGui utility window
// (mirrors NewProjectWindow.h's Open()/Build() shape exactly), parameterized
// by whichever AssetScaffoldKind the Project panel's own "Create" submenu
// most recently opened it with (ctx.createAssetWindowPendingKind).
class CreateAssetWindow {
public:
    void Open();
    void Build(EditorContext& ctx, IAssetScaffoldingCapability* capability);

private:
    bool m_wasOpenLastFrame = false;
    char m_nameBuffer[128] = {};
    std::string m_errorMessage;
};

} // namespace gte
```

### 3.5 — `src/Editor/CreateAssetWindow.cpp` (new file)

```cpp
#include "CreateAssetWindow.h"

#include "../Core/EditorCapabilities.h"

#include <imgui.h>

#include <cstring>

namespace gte {

namespace {
const char* TitleForKind(AssetScaffoldKind kind)
{
    switch (kind) {
    case AssetScaffoldKind::RenderPass: return "Create New Render Pass";
    case AssetScaffoldKind::ComputeShader: return "Create New Compute Shader";
    case AssetScaffoldKind::ShaderPair: return "Create New Vertex/Fragment Shader Pair";
    }
    return "Create New Asset"; // unreachable - silences a "not all control paths return a value" warning.
}
} // namespace

void CreateAssetWindow::Open()
{
    std::memset(m_nameBuffer, 0, sizeof(m_nameBuffer));
    m_errorMessage.clear();
}

void CreateAssetWindow::Build(EditorContext& ctx, IAssetScaffoldingCapability* capability)
{
    if (!ctx.createAssetWindowOpen) {
        m_wasOpenLastFrame = false;
        return;
    }
    if (!m_wasOpenLastFrame) {
        Open();
        m_wasOpenLastFrame = true;
    }

    const AssetScaffoldKind kind = ctx.createAssetWindowPendingKind;

    ImGui::SetNextWindowSize(ImVec2(460.0f, 160.0f), ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin(TitleForKind(kind), &ctx.createAssetWindowOpen, flags)) {
        ImGui::InputText("Name", m_nameBuffer, sizeof(m_nameBuffer));
        if (!m_errorMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_errorMessage.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button("Cancel")) {
            ctx.createAssetWindowOpen = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Create") && capability != nullptr) {
            const IAssetScaffoldingCapability::ScaffoldOutcome outcome =
                capability->CreateAssetScaffold(kind, m_nameBuffer);
            if (outcome.success) {
                ctx.createAssetWindowOpen = false;
                // The reminder is the single most important thing the user
                // needs to read after this action (PHASE0_MASTER_STRATEGY.md's
                // own restated Step 2 caveat) - surfaced prominently via the
                // SAME shared status-toast field every "Project" menu action
                // already uses, not just a plain "created" message.
                std::string message = "Created " + std::to_string(outcome.createdFiles.size()) + " file(s).";
                if (!outcome.reminderMessage.empty()) {
                    message += " " + outcome.reminderMessage;
                }
                ctx.projectWorkflowStatusMessage = message;
                ctx.projectWorkflowStatusIsError = false;
                ctx.projectWorkflowStatusSetTime = std::chrono::steady_clock::now();
            } else {
                m_errorMessage = outcome.errorMessage; // window stays open, user can fix and retry.
            }
        }
    }
    ImGui::End();
}

} // namespace gte
```

### 3.6 — `NetworkServer.cpp` route (placed immediately after
`/project_assembly/create_project`, same file/section/style)

```cpp
// editor-core-separation-18 campaign (On-Engine Project Workflow plan,
// BIG-STEP 4), PHASE3 - POST /project_assembly/create_asset. Calls the
// EXACT SAME CreateAssetScaffold() method CreateAssetWindow's own "Create"
// button calls (LDD-PW5).
server.Post("/project_assembly/create_asset",
    [assetScaffoldingCapability](const httplib::Request& req, httplib::Response& res) {
    const std::string kindParam = req.get_param_value("kind"); // "render_pass" | "compute_shader" | "shader_pair"
    const std::string name = req.get_param_value("name");
    AssetScaffoldKind kind;
    if (kindParam == "render_pass") {
        kind = AssetScaffoldKind::RenderPass;
    } else if (kindParam == "compute_shader") {
        kind = AssetScaffoldKind::ComputeShader;
    } else if (kindParam == "shader_pair") {
        kind = AssetScaffoldKind::ShaderPair;
    } else {
        res.status = 400;
        res.set_content(
            BuildGenericErrorResponseJson("'kind' must be render_pass, compute_shader, or shader_pair"),
            "application/json");
        return;
    }
    if (assetScaffoldingCapability == nullptr) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("asset scaffolding capability not available"), "application/json");
        return;
    }
    const IAssetScaffoldingCapability::ScaffoldOutcome outcome =
        assetScaffoldingCapability->CreateAssetScaffold(kind, name);
    if (!outcome.success) {
        res.status = 400;
        res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
        return;
    }
    nlohmann::json body;
    body["created_files"] = outcome.createdFiles;
    body["reminder_message"] = outcome.reminderMessage;
    res.set_content(body.dump(), "application/json");
});
```

(A small local `if/else if` chain is used above instead of a separate
`TryParseAssetScaffoldKind()` free helper the master-plan file sketched —
three string comparisons inline is not worth a whole new named helper
function for a single call site; add a dedicated helper ONLY if a second
call site for this same parsing appears later. CONFIRMED, not merely
assumed: `RegisterRoutes()`'s real body lives entirely inside
`NetworkServer.cpp` (line 296) — `NetworkRoutes.h/.cpp` is a separate pair
of files holding only shared response-building HELPER functions
(`BuildGenericErrorResponseJson()` and similar), never a route
registration itself. Paste this snippet directly into `NetworkServer.cpp`.)

### 3.7 — Threading the tenth pointer through

CONFIRMED (see STEP 2's own now-resolved citation above): this is a flat
pointer parameter list in all three places, no intermediate struct anywhere.
Add `IAssetScaffoldingCapability* assetScaffoldingCapability = nullptr` as
the tenth parameter of `NetworkServer`'s constructor (`NetworkServer.h`
lines 171-179) and of the free-function `RegisterRoutes()`
(`NetworkServer.cpp` lines 296-300), add `m_assetScaffoldingCapability` as a
new member field (`NetworkServer.h`, immediately after
`m_projectLifecycleCapability`, line 280) initialized from it in the
constructor's member-init list, and append it as the tenth argument to the
constructor's own internal `RegisterRoutes(...)` call
(`NetworkServer.cpp` lines 1513-1515). From `EditorHost.cpp`'s own
`RegisterRoutes(...)`-style constructor CALL SITE (lines 205-209), pass
`&s_editorProjectLifecycleCapability` again, as the tenth argument (legal,
ordinary C++ — see STEP 2's own reasoning on why passing the same object's
address twice, once per base sub-object pointer type, is fine).

## STEP 4 — Live verification for THIS phase

1. Build, launch `GreatTamanaEditor.exe`.
2. `POST /project_assembly/create_project?name=<Scratch2>`.
3. `POST /project_assembly/create_asset?kind=render_pass&name=Foo` — expect
   `200`, `created_files: ["FooRenderPass.cpp"]`, a non-empty
   `reminder_message`.
4. Confirm `Projects/<Scratch2>/Assets/FooRenderPass.cpp` exists on disk
   with the exact PHASE1 template content, `<Name>`/`__NAME__` correctly
   substituted to `Foo` everywhere.
5. Repeat step 3 with the SAME name — expect `400`, zero new files written,
   confirm via a fresh directory listing that only the original file is
   still there.
6. Repeat step 3 with `name=foo` (different case) — ALSO expect `400`
   (case-insensitive collision).
7. `gte_send_request("/get_swapchain")` — confirm the Editor's menu
   bar/panels still render correctly (a real screenshot), and that the
   "Project" panel's `"[Active Project] <Scratch2>"` row (PHASE2) now shows
   BOTH `Scratch2Game.cpp` AND `FooRenderPass.cpp` as bulleted children.
8. Clean up `Projects/<Scratch2>/` afterward.
9. GET the engine's own internal log via `gte_send_request("/get_logs")`
   and confirm the `GTE_LOG_INFO("ProjectLifecycle", "CreateAssetScaffold(...)")`
   line from PHASE1 appears — this is the campaign's own required use of
   the engine's internal logging system for debugging, per the master
   task's own instruction to never use raw C/C++ logging.

## STEP 5 — Fast compile check

`cmake --build build --target GreatTamanaEditor`. If a separate test
binary target also compiles `EditorLayer.h`/`NullEditorLayer.cpp` (likely,
since `NullEditorLayer` exists specifically for a non-Editor/test build
configuration), build that target too:
`cmake --build build --target GreatTamanaEngineTests` (confirm the real
target name from `tests/CMakeLists.txt` first). Do not run the full `ctest`
suite yet — that is `PHASE4_*.md`'s own job.
